browserChannel = require('browserchannel').server

connect = require 'connect'

sharedb = require './client'

webserver = connect(
  #  connect.logger()
  connect.static "#{__dirname}"
)

nextId = 1000

opts = {webserver}
webserver.use browserChannel opts, (client) ->
  console.log 'BC session established'

  server = sharedb()

  client.on 'close', ->
    console.log 'client closed'
    server.close()
  server.on 'close', ->
    console.log 'server closed'
    client.close()
  server.on 'error', (e) ->
    console.log "server died: #{e}"
    client.stop ->
      console.log 'xxxx'
      client.close()

  clientDoc = null
  client.on 'message', (data) ->
    if data.doc
      clientDoc = data.doc
    else
      data.doc = clientDoc

    switch
      when data.auth isnt undefined
        # Auth (hello)
        console.log 'auth'
        server.auth()
        client.send auth:"#{nextId++}"
      when data.open is true
        # Open!
        opts =
          type:data.type,
          create:data.create,
          snapshot:data.snapshot is null
        opts.type = 'text' if opts.type is 'text2'
        server.open data.doc, opts
      when data.open is false
        server.close data.doc
      when data.op isnt undefined
        # Submit op
        server.sendOp data.doc, data.v, data.op
      else
        console.log data

  server.on 'open', (err, docName, data) ->
    return client.send doc:docName, open:false, error:err if err
    {v, type, snapshot, ctime, mtime, create} = data
    client.send doc:docName, open:true, v:v, snapshot:snapshot, create:create, meta:{ctime, mtime}

  server.on 'op', (err, docName, v, op) ->
    console.log 'retransmitting op', doc:docName, v:v-1, op:op
    # It doesn't really make sense to get an error here.
    throw new Error(err) if err

    client.send doc:docName, v:v-1, op:op, meta:{} # Meta should contain source:

  server.on 'op applied', (err, docName, v) ->
    return client.send doc:docName, v:null, error:err if err

    # The version in ShareJS is the version at which the op was submitted.
    # In contrast, ShareDB acknowledges using the new server version. Hence v-1.
    client.send doc:docName, v:v-1
   


webserver.listen 7000
console.log 'Listening on http://localhost:7000/'

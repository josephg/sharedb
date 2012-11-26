net = require 'net'

connect = (port, host, cb) ->
  if typeof host is 'function'
    cb = host
    host = 'localhost'

  port ||= 8766

  client = net.connect port, host, ->
    c =
      open: (docName, callback) ->
        console.log "trying to open #{docName}"

        b = new Buffer 4 + 1 + docName.length + 1
        b.writeUInt32LE b.length - 4, 0 # packet length
        console.log b.length - 4
        b.writeUInt8 3 | 0x80, 4 # open + set doc name
        b.write docName, 5 # doc name
        b.writeInt8 0, 5 + docName.length # write the trailing \0.
        
        client.write b

    cb null, c

  client.on 'error', (e) ->
    cb e

s = connect null, (error, c) ->
  return console.error "Error: '#{error}'" if error
  c.open 'hi', (e) ->
    console.log 'opened!'


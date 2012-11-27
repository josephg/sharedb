net = require 'net'
buffer = (require './binary').writeBuffer()

# Small tweaks - reset should skip the packet length.
buffer.r = (type) ->
  buffer.reset()
  buffer.uint32 0
  buffer.uint8 type if type?

buffer.flush = ->
  data = buffer.data()
  data.writeUInt32LE data.length - 4, 0
  data

connect = (port, host, cb) ->
  if typeof host is 'function'
    cb = host
    host = 'localhost'

  port ||= 8766

  client = net.connect port, host, ->
    c =
      open: (docName, callback) ->
        console.log "trying to open #{docName}"
        
        buffer.r 3 | 0x80
        buffer.zstring docName

        client.write buffer.flush()

      sendOp: (docName, version, op, callback) ->
        console.log 'submitting op'

        buffer.r 1 | 0x80
        buffer.zstring docName

        buffer.uint32 version
        buffer.uint16 op.length
        for o in op
          if typeof o is 'number'
            buffer.uint8 1
            buffer.uint32 o
          else if typeof o is 'string'
            buffer.uint8 3
            buffer.zstring o
          else if o.d?
            buffer.uint8 4
            buffer.uint32 o.d
          else
            throw new Error "Invalid op component: #{o}"

        client.write buffer.flush()

    cb null, c

  client.on 'data', (msg) ->
    console.log "msg: '#{msg}'"

  client.on 'error', (e) ->
    cb e

s = connect null, (error, c) ->
  return console.error "Error: '#{error}'" if error
  c.open 'hi', (e) ->
    console.log 'opened!'

  c.sendOp 'hi', 2, [2, '-internet-']


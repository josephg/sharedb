net = require 'net'
binary = require './binary'
buffer = binary.writeBuffer()
{EventEmitter} = require 'events'

# Mirrors src/net.h
MSG_OP = 1
MSG_CURSOR = 2

MSG_OPEN = 3
MSG_CLOSE = 4
MSG_GET_OPS = 5

MSG_OP_APPLIED = 6

MSG_FLAG_ERROR = 0x40
MSG_FLAG_HAS_DOC_NAME = 0x80

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

  c = new EventEmitter
  client = net.connect port, host, ->
    c.open = (docName, callback) ->
      console.log "trying to open #{docName}"
      
      buffer.r 3 | MSG_FLAG_HAS_DOC_NAME
      buffer.zstring docName

      client.write buffer.flush()

      listener = (error, openedDoc) ->
        if openedDoc isnt docName
          c.once 'open', listener
        else
          callback error

      c.once 'open', listener

    c.sendOp = (docName, version, op, callback) ->
      console.log 'submitting op'

      buffer.r 1 | MSG_FLAG_HAS_DOC_NAME
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

  sDocName = null
  cDocName = null

  client.on 'data', require('./buffer') (b) ->
    packet = binary.read b
    type = packet.uint8()
    console.log "type: #{type}"

    if type & MSG_FLAG_HAS_DOC_NAME
      # Doc name incoming!
      sDocName = packet.zstring()
      type &= ~MSG_FLAG_HAS_DOC_NAME

    error = if type & MSG_FLAG_ERROR
      type &= ~MSG_FLAG_ERROR
      true
    else
      false

    switch type
      when MSG_OPEN
        e = packet.zstring() if error
        c.emit 'open', e, sDocName
      when MSG_OP_APPLIED
        if error
          e = packet.zstring()
        else
          v = packet.uint32()
        c.emit 'op applied', e, v

  client.on 'error', (e) ->
    cb e

s = connect null, (error, c) ->
  return console.error "Error: '#{error}'" if error
  c.open 'hi2', (e) ->
    return console.log "error opening document: #{e}" if e
    console.log 'opened!'

  c.sendOp 'hi', 2, [2, '-internet-']




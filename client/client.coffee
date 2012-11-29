net = require 'net'
binary = require './binary'
buffer = binary.writeBuffer()
{EventEmitter} = require 'events'

# Mirrors src/net.h
MSG_HELLO = 1
MSG_OP = 2
MSG_CURSOR = 3

MSG_OPEN = 4
MSG_CLOSE = 5
MSG_GET_OPS = 6

MSG_OP_APPLIED = 7

MSG_FLAG_SNAPSHOT = 0x10
MSG_FLAG_CREATE = 0x20

MSG_FLAG_ERROR = 0x40
MSG_FLAG_HAS_DOC_NAME = 0x80

PROTOCOL_VERSION = 0

buffer.flush = ->
  data = buffer.data()
  data.writeUInt32LE data.length - 4, 0
  data

connect = (port, host, cb) ->
  if typeof host is 'function'
    cb = host
    host = 'localhost'

  port ||= 8766

  sDocName = null
  cDocName = null

  preparePacket = (type, docName) ->
    buffer.reset()
    # Skip the packet length part of the packet for now. We'll fill it in later.
    buffer.uint32 0
 
    if docName and docName isnt cDocName
      cDocName = docName
      buffer.uint8 type | MSG_FLAG_HAS_DOC_NAME
      buffer.zstring docName
    else
      buffer.uint8 type

  c = new EventEmitter
  client = net.connect port, host, ->
    c.open = (docName, callback) ->
      console.log "trying to open #{docName}"

      preparePacket MSG_OPEN, docName
      client.write buffer.flush()

      listener = (error, openedDoc) ->
        if openedDoc isnt docName
          c.once 'open', listener
        else
          callback error

      c.once 'open', listener

    c.sendOp = (docName, version, op, callback) ->
      console.log 'submitting op', op

      preparePacket MSG_OP, docName
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

  # Send a hello message immediately.
  preparePacket MSG_HELLO
  buffer.uint8 PROTOCOL_VERSION
  client.write buffer.flush()

  client.on 'data', require('./buffer') (b) ->
    packet = binary.read b
    type = packet.uint8()

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
      when MSG_HELLO
        v = packet.uint8()
        if v isnt PROTOCOL_VERSION
          throw new Error "Incorrect protocol version - got #{v} expected #{PROTOCOL_VERSION}"
        console.log 'hello message'
        c.emit 'hello'
      when MSG_OPEN
        e = packet.zstring() if error
        c.emit 'open', e, sDocName
      when MSG_OP_APPLIED
        if error
          e = packet.zstring()
        else
          v = packet.uint32()
        c.emit 'op applied', e, sDocName, v
      else
        console.log "Unhandled type #{type}"

  client.on 'error', (e) ->
    cb e

s = connect null, (error, c) ->
  return console.error "Error: '#{error}'" if error
  c.open 'hi', (e) ->
    return console.log "error opening document: #{e}" if e
    console.log "opened 'hi'"

  c.sendOp 'hi', 2, [2, '-internet-']

  c.on 'op applied', (e, docName, v) ->
    return console.error "Could not apply op: #{e}" if e
    console.log "op applied on #{docName} -> version #{v}"




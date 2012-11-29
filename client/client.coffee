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

MSG_OP_ACK = 7

MSG_FLAG_SNAPSHOT = 0x10
MSG_FLAG_CREATE = 0x20

MSG_FLAG_ERROR = 0x40
MSG_FLAG_HAS_DOC_NAME = 0x80

PROTOCOL_VERSION = 0

OP_COMPONENT_SKIP = 1
OP_COMPONENT_INSERT = 3
OP_COMPONENT_DELETE = 4

buffer.flush = ->
  data = buffer.data()
  data.writeUInt32LE data.length - 4, 0
  data

readSnapshot = (packet, type) ->
  throw new Error "Don't know how to read snapshots of type #{type}" unless type is 'text'
  packet.zstring()

readOp = (packet, type) ->
  throw new Error "Don't know how to read ops of type #{type}" unless type is 'text'

  n = packet.uint16()
  for c in [0...n]
    type = packet.uint8()
    switch type
      when OP_COMPONENT_SKIP
        packet.uint32()
      when OP_COMPONENT_INSERT
        packet.zstring()
      when OP_COMPONENT_DELETE
        {d:packet.uint32()}
      else
        throw new Error 'Invalid op component type'

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
    c.open = (docName, type, callback) ->
      [type, callback] = [null, type] if typeof type is 'function'
      console.log "trying to open #{docName}, type: #{type}"

      flags = MSG_FLAG_SNAPSHOT | if type then MSG_FLAG_CREATE else 0
      preparePacket MSG_OPEN | flags, docName
      buffer.zstring if type then type else ''
      buffer.uint32 0xffffffff # uint32_max for the version
      client.write buffer.flush()

      listener = (error, openedDoc, v, type, snapshot) ->
        if openedDoc isnt docName
          c.once 'open', listener
        else
          callback error, v, type, snapshot

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

    c.close = (docName, callback) ->
      preparePacket MSG_CLOSE, docName
      client.write buffer.flush()

      listener = (error, doc) ->
        if doc isnt docName
          c.once 'close', listener
        else
          callback error
      c.once 'close', listener if callback


    cb null, c

  # Send a hello message immediately.
  preparePacket MSG_HELLO
  buffer.uint8 PROTOCOL_VERSION
  client.write buffer.flush()

  client.on 'data', require('./buffer') (b) ->
    packet = binary.read b
    type = packet.uint8()
    flags = type & 0xf0
    type &= 0xf

    # Doc name incoming!
    sDocName = packet.zstring() if flags & MSG_FLAG_HAS_DOC_NAME

    error = if flags & MSG_FLAG_ERROR then packet.zstring()

    switch type
      when MSG_HELLO
        v = packet.uint8()
        if v isnt PROTOCOL_VERSION
          throw new Error "Incorrect protocol version - got #{v} expected #{PROTOCOL_VERSION}"
        console.log 'hello message'
        c.emit 'hello'

      when MSG_OPEN
        return c.emit 'open', error, sDocName if error

        v = packet.uint32()
        if flags & MSG_FLAG_SNAPSHOT
          type = packet.zstring()
          snapshot = readSnapshot(packet, type)
        c.emit 'open', null, sDocName, v, type, snapshot

      when MSG_CLOSE
        c.emit 'close', error, sDocName

      when MSG_OP
        if error
          throw new Error 'Error receiving op? What does that even?'

        v = packet.uint32()
        op = readOp packet, 'text'
        c.emit 'op', null, sDocName, v, op

      when MSG_OP_ACK
        return c.emit 'op applied', error, sDocName if error

        v = packet.uint32()
        c.emit 'op applied', null, sDocName, v

      else
        console.log "Unhandled type #{type}"

  client.on 'error', (e) ->
    cb e

s = connect null, (error, c) ->
  return console.error "Error: '#{error}'" if error
  docName = 'hi2'
  c.open docName, 'text', (e, v, type, snapshot) ->
    return console.log "error opening document: #{e}" if e
    console.log "opened #{docName} at version #{v} type #{type} snapshot '#{snapshot}'"

    c.sendOp docName, v, ["#{v} "]

    c.close docName

  c.on 'op applied', (e, docName, v) ->
    return console.error "Could not apply op: #{e}" if e
    console.log "op applied on #{docName} -> version #{v}"

  c.on 'op', (e, docName, v, op) ->
    console.log "Got an op on #{docName}. Now at v#{v}"
    console.log op



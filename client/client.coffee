net = require 'net'
binary = require './binary'
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

  c = new EventEmitter
  client = net.connect port, host

  preparePacket = (type, docName) ->
    buffer = binary.writeBuffer()
    # Skip the packet length part of the packet for now. We'll fill it in later.
    buffer.uint32 0
 
    if docName and docName isnt cDocName
      cDocName = docName
      buffer.uint8 type | MSG_FLAG_HAS_DOC_NAME
      buffer.zstring docName
    else
      buffer.uint8 type
    buffer

  writePacket = (buffer) ->
    data = buffer.data()
    data.writeUInt32LE data.length - 4, 0
    client.write data

  client.on 'connect', ->
    c.emit 'connect'
    cb? null, c
    cb = null

  client.on 'error', (e) ->
    c.emit 'error'
    cb? e
    cb = null

  closed = false
  c.close = ->
    # This doesn't guarantee that we'll stop getting messages.
    closed = true
    client.end()

  # This should be sent immediately.
  c.auth = ->
    p = preparePacket MSG_HELLO
    p.uint8 PROTOCOL_VERSION
    writePacket p
    console.log 'said hi'

  # opts can contain type (string), snapshot (bool) and create (bool)
  c.open = (docName, opts, callback) ->
    [opts, callback] = [{}, opts] if typeof opts is 'function'

    console.log "trying to open #{docName}, type: #{opts.type}"

    flags = 0
    flags |= MSG_FLAG_SNAPSHOT if opts.snapshot
    flags |= MSG_FLAG_CREATE if opts.create

    p = preparePacket MSG_OPEN | flags, docName
    p.zstring opts.type || ''
    p.uint32 0xffffffff # uint32_max for the version
    writePacket p

    listener = (error, openedDoc, data) ->
      if openedDoc isnt docName
        c.once 'open', listener
      else
        callback error, data

    c.once 'open', listener if callback

  c.sendOp = (docName, version, op, callback) ->
    console.log 'submitting op', op

    p = preparePacket MSG_OP, docName
    p.uint32 version
    p.uint16 op.length
    for o in op
      if typeof o is 'number'
        p.uint8 1
        p.uint32 o
      else if typeof o is 'string'
        p.uint8 3
        p.zstring o
      else if o.d?
        p.uint8 4
        p.uint32 o.d
      else
        throw new Error "Invalid op component: #{o}"

    writePacket p

  c.close = (docName, callback) ->
    writePacket preparePacket MSG_CLOSE, docName

    listener = (error, doc) ->
      if doc isnt docName
        c.once 'close', listener
      else
        callback error
    c.once 'close', listener if callback

  client.on 'data', require('./buffer') (b) ->
    return if closed # Don't want to emit any data after close() is called
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

        data =
          create: !!(flags & MSG_FLAG_CREATE)
          v: packet.uint32()

        if flags & MSG_FLAG_SNAPSHOT
          data.type = packet.zstring()
          data.snapshot = readSnapshot packet, data.type

        c.emit 'open', null, sDocName, data

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

  c

module.exports = connect


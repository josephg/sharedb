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

MSG_FLAG_CURSOR_SET = 0x10
MSG_FLAG_CURSOR_REMOVE = 0x20
MSG_FLAG_CURSOR_REPLACE_ALL = 0x30

MSG_FLAG_ERROR = 0x40
MSG_FLAG_HAS_DOC_NAME = 0x80


OPEN_FLAG_SNAPSHOT = 1
OPEN_FLAG_CREATE = 2
OPEN_FLAG_TRACK_CURSORS = 4

PROTOCOL_VERSION = 0

OP_COMPONENT_SKIP = 1
OP_COMPONENT_INSERT = 3
OP_COMPONENT_DELETE = 4

readSnapshot = (packet, type) ->
  throw new Error "Don't know how to read snapshots of type #{type}" unless type is 'text'
  packet.zstring()

readOp = (packet, type) ->
  throw new Error "Don't know how to read ops of type #{type}" unless type is 'text'

  op = []
  while (type = packet.uint8())
    switch type
      when OP_COMPONENT_SKIP
        op.push packet.uint32()
      when OP_COMPONENT_INSERT
        op.push packet.zstring()
      when OP_COMPONENT_DELETE
        op.push {d:packet.uint32()}
      else
        throw new Error 'Invalid op component type'

  op

readCursor = (packet, type) ->
  throw new Error "Don't know how to read cursors of type #{type}" unless type is 'text'
  start = packet.uint32()
  end = packet.uint32()

  if start == end then start else [start, end]

connect = (port, host, cb) ->
  if typeof host is 'function'
    cb = host
    host = 'localhost'

  port ||= 8766

  sDocName = null
  cDocName = null

  c = new EventEmitter
  client = net.connect port, host
  id = null

  # Send magic bytes before anything else.
  do ->
    buffer = binary.writeBuffer()
    buffer.string 'WAVE'
    client.write buffer.data()

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
    c.emit 'error', e
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

    open_flags = 0
    open_flags |= OPEN_FLAG_SNAPSHOT if opts.snapshot
    open_flags |= OPEN_FLAG_CREATE if opts.create

    # These are on by default.
    open_flags |= OPEN_FLAG_TRACK_CURSORS unless opts.trackCursors is false

    p = preparePacket MSG_OPEN, docName
    p.uint8 open_flags
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
    p.uint8 0

    writePacket p

  c.close = (docName, callback) ->
    writePacket preparePacket MSG_CLOSE, docName

    listener = (error, doc) ->
      if doc isnt docName
        c.once 'close', listener
      else
        callback error
    c.once 'close', listener if callback

  c.setCursor = (docName, v, cursor) ->
    p = preparePacket MSG_CURSOR | MSG_FLAG_CURSOR_SET, docName
    p.uint32 v
    if typeof cursor is 'number'
      p.uint32 cursor
      p.uint32 cursor
    else
      p.uint32 cursor[0]
      p.uint32 cursor[1]
    writePacket p

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
        id = packet.uint32()
        c.emit 'hello'
        c.emit 'has id', id

      when MSG_OPEN
        return c.emit 'open', error, sDocName if error

        openFlags = packet.uint8()

        data =
          create: !!(openFlags & OPEN_FLAG_CREATE)
          v: packet.uint32()

        if openFlags & OPEN_FLAG_SNAPSHOT
          data.type = packet.zstring()
          data.ctime = packet.uint64()
          data.mtime = packet.uint64()
          data.snapshot = readSnapshot packet, data.type

        c.emit 'open', null, sDocName, data

      when MSG_CLOSE
        c.emit 'close', error, sDocName

      when MSG_OP
        if error
          throw new Error 'Error receiving op? What does that even?'

        v = packet.uint32()
        clientId = packet.uint32()
        op = readOp packet, 'text'
        c.emit 'op', null, sDocName, v, op, clientId

      when MSG_OP_ACK
        return c.emit 'op applied', error, sDocName if error

        v = packet.uint32()
        c.emit 'op applied', null, sDocName, v

      when MSG_CURSOR
        break if error # For now, we'll just swallow cursor set errors.

        switch flags
          when MSG_FLAG_CURSOR_SET
            cid = packet.uint32()
            data = {}
            data[cid] = readCursor packet, 'text'
            c.emit 'cursor', null, sDocName, data

          when MSG_FLAG_CURSOR_REMOVE
            cid = packet.uint32()
            data = {}
            data[cid] = null
            c.emit 'cursor', null, sDocName, data

          when MSG_FLAG_CURSOR_REPLACE_ALL
            data = {}
            while (cid = packet.uint32()) != 0
              # THIS WONT WORK FOR ANYTHING BUT THE TEXT TYPE!
              data[cid] = readCursor packet, 'text'
            c.emit 'cursor', null, sDocName, data

      else
        console.log "Unhandled type #{type}"
  c

module.exports = connect


exports.read = (buffer) ->
  pos = 0

  p = (size) ->
    oldPos = pos
    pos += size
    oldPos

  int32: -> buffer.readInt32LE (p 4)
  int16: -> buffer.readInt16LE (p 2)
  int8:  -> buffer.readInt8    (p 1)

  uint64: ->
    low = buffer.readUInt32LE (p 4)
    high = buffer.readUInt32LE (p 4)
    # Can't use bitwise operations here because javascript's bitwise ops
    # truncate to 32bit integers. Otherwise this is low | high<<32
    low + high * Math.pow(2, 32)
  uint32: -> buffer.readUInt32LE (p 4)
  uint16: -> buffer.readUInt16LE (p 2)
  uint8:  -> buffer.readUInt8    (p 1)

  float64: -> buffer.readDoubleLE (p 8)
  float32: -> buffer.readFloatLE  (p 4)

  skip: (x) -> pos += x

  zstring: ->
    # Wow, rewriting strlen in JS.
    end = pos
    end++ while buffer.readInt8 end
    
    # End points to the '\0' character.
    str = buffer.toString 'utf8', pos, end
    pos = end + 1
    str

  bytesLeft: -> buffer.length - pos


exports.writeBuffer = ->
  size = 64*1024
  buffer = new Buffer size
  pos = 0

  p = (s) ->
    oldPos = pos
    pos += s

    while pos >= size
      size *= 2
      console.log "resizing to #{size}"
      newBuffer = new Buffer size
      buffer.copy newBuffer, 0, 0, oldPos
      buffer = newBuffer

    oldPos

  string = (value) ->
    ofs = p Buffer.byteLength value
    buffer.write value, ofs
    @

  data: -> buffer.slice 0, pos
  reset: -> pos = 0

  int32: (value) -> ofs = p 4; buffer.writeInt32LE value, ofs; @
  int16: (value) -> ofs = p 2; buffer.writeInt16LE value, ofs; @
  int8:  (value) -> ofs = p 1; buffer.writeInt8    value, ofs; @

  uint32: (value) -> ofs = p 4; buffer.writeUInt32LE value, ofs; @
  uint16: (value) -> ofs = p 2; buffer.writeUInt16LE value, ofs; @
  uint8:  (value) -> ofs = p 1; buffer.writeUInt8    value, ofs; @

  float64: (value) -> ofs = p 8; buffer.writeDoubleLE value, ofs; @
  float32: (value) -> ofs = p 4; buffer.writeFloatLE  value, ofs; @

  string: string
  zstring: (value) ->
    string(value)
    ofs = p 1
    buffer.fill 0, ofs, ofs + 1
    @


  pos: -> pos


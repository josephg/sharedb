read = (buffer, offset, len) ->
  # This code needs to be adjusted to use nodejs buffers instead of array buffers.
  throw new Error 'Not implemented'

  if len?
    view = new DataView(buffer, offset, len)
  else if offset?
    view = new DataView(buffer, offset)
  else
    view = new DataView(buffer)
  pos = 0

  p = (size) ->
    oldPos = pos
    pos += size
    oldPos

  int32: -> view.getInt32 (p 4), true
  int16: -> view.getInt16 (p 2), true
  int8:  -> view.getInt8  (p 1)

  uint32: -> view.getUint32 (p 4), true
  uint16: -> view.getUint16 (p 2), true
  uint8:  -> view.getUint8  (p 1)

  float64: -> view.getFloat64 (p 8), true
  float32: -> view.getFloat32 (p 4), true

  skip: (x) -> pos += x

  bytestring: (len) ->
    s = new Array(len)
    for i in [0...len]
      c = @uint8()
      unless c # Skip '\0'
        @skip len - i - 1
        return s.join ''

      s[i] = String.fromCharCode c
    s.join ''

  bytesLeft: -> view.byteLength - pos


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


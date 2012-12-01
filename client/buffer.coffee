# A buffer is something you can write into, and then read packets back out from.
#
# So, you write everything that comes over the wire, and it'll tell you when there's
# data available.
#
# The packetHandler you pass in is your function to get called with the ready packet.

module.exports = (packetHandler) ->
  # List of pending Buffer objects with juicy data in them
  buffers = []
  # Offset into the first buffer
  offset = 0
  # Length of the packet we're currently trying to read
  packetLength = -1
  
  # Temporary buffer used to read in the length
  lenBuf = new Buffer 4

  # For sharedb, the connection starts with 4 magic bytes ('WAVE'). We should expect these first.
  seenMagic = false
  
  readBytes = (dest, num = dest.length) ->
    # Read num bytes from the pending buffers into dest (another buffer)
    # There have to be enough bytes ready to read.
    p = 0
    while true
      read = buffers[0].copy dest, p, offset
      p += read
      offset += read
      if offset is buffers[0].length
        offset = 0
        buffers.shift()

      return dest if p == num

  (data) ->
    buffers.push data

    # Figure out how many bytes we have waiting to read
    bytes = -offset
    bytes += b.length for b in buffers

    while true
      if packetLength is -1
        if bytes >= 4
          readBytes lenBuf
          if !seenMagic
            throw new Error 'Invalid magic' unless lenBuf.toString() is 'WAVE'
            seenMagic = true
          else
            packetLength = lenBuf.readUInt32LE(0)

          bytes -= 4
        else
          break
      else
        if bytes >= packetLength
          packet = new Buffer packetLength
          readBytes packet
          bytes -= packetLength
          packetLength = -1

          packetHandler packet
        else
          break

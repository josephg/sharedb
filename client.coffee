net = require 'net'

client = net.connect 8766, ->
  console.log 'connected!'

  b = new Buffer 5
  b.writeUInt32LE 1, 0
  b.writeUInt8 2, 4
  client.write b

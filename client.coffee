net = require 'net'

client = net.connect 8766, ->
  console.log 'connected!'

  b = new Buffer 8
  b.writeUInt32LE b.length - 4, 0
  b.writeUInt8 3 | 0x80, 4
  b.write 'hi', 5

  client.write b
  #setTimeout (-> client.write b.slice 0, 20), 1000
  #setTimeout (-> client.write b.slice 20), 2000

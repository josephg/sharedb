net = require 'net'

client = net.connect 8766, ->
  console.log 'connected!'

  b = new Buffer 100
  b.writeUInt32LE b.length - 4, 0
  b.writeUInt8 2, 4

  client.write b
  setTimeout (-> client.write b.slice 0, 20), 1000
  setTimeout (-> client.write b.slice 20), 2000

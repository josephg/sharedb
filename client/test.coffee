# This is a simple test of the binary writer code.

b = (require './binary').writeBuffer()

console.log (b.int32(10))
console.log (b.int32(10))
console.log (b.int32(10))
console.log (b.int32(10))
console.log (b.int32(10))
console.log (b.string("hi"))
console.log (b.zstring("hi"))
console.log b.data()

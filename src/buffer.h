// This is basically the same as a string, except it isn't null terminated and its got heaps
// of different write methods.
//
// This is used by the net code to build outgoing messages.

#ifndef share_buffer_h
#define share_buffer_h

#include <stddef.h>
#include <stdint.h>
#include "dstr.h"
#include <rope.h>

typedef struct buffer_t {
  char *bytes;
  uint32_t length;
  uint32_t capacity;
  uint32_t pos;
} buffer;

void buf_init(buffer *b);
void buf_free(buffer *b);
static inline void buf_seek(buffer *b, uint32_t pos) {
  b->pos = pos;
}

// Reset a buffer to being in the 'empty' state.
static inline void buf_reset(buffer *b) {
  b->pos = b->length = 0;
}

#pragma mark Writing functions

void *_buf_insert_pos(buffer *b, size_t length);

#define XX *(typeof(value) *)_buf_insert_pos(b, sizeof(value)) = value;
// Write the various numeric types. These functions all write using the native byte order, which is
// (delightfully) little endian basically everywhere now.
// TODO: Make this code endian-aware.
static inline void buf_uint64(buffer *b, uint64_t value) { XX }
static inline void buf_uint32(buffer *b, uint32_t value) { XX }
static inline void buf_uint16(buffer *b, uint16_t value) { XX }
static inline void buf_uint8 (buffer *b, uint8_t  value) { XX }

static inline void buf_int64(buffer *b, int64_t value) { XX }
static inline void buf_int32(buffer *b, int32_t value) { XX }
static inline void buf_int16(buffer *b, int16_t value) { XX }
static inline void buf_int8 (buffer *b, int8_t  value) { XX }

static inline void buf_float64(buffer *b, double value) { XX }
static inline void buf_float32(buffer *b, float value) { XX }
#undef XX

// Write raw bytes
void buf_bytes(buffer *b, void *bytes, size_t num);

// Write a null-terminated string
void buf_zstring_len(buffer *b, const char *str, size_t strlen);
void buf_zstring(buffer *b, const char *str);

static inline void buf_zstring_dstr(buffer *b, dstr str) {
  buf_zstring_len(b, str, dstr_len(str));
}

static inline void buf_zstring_rope(buffer *b, rope *rope) {
  rope_write_cstr(rope, _buf_insert_pos(b, rope_byte_count(rope)));
}

#pragma mark Reading functions

void *_buf_read_pos(buffer *b, size_t len);
#define DEF_READ(type) \
static inline type##_t buf_read_##type(buffer *b, bool *err) { \
  void *ptr = _buf_read_pos(b, sizeof(type##_t)); \
  return ptr == NULL ? *err=true,(type##_t)-1 : *(type##_t *)ptr; \
}

DEF_READ(uint64)
DEF_READ(uint32)
DEF_READ(uint16)
DEF_READ(uint8)

DEF_READ(int64)
DEF_READ(int32)
DEF_READ(int16)
DEF_READ(int8)

#undef DEF_READ

// Returns null on error
dstr buf_read_zstring(buffer *b);

#endif

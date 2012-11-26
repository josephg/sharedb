// This is basically the same as a string, except it isn't null terminated and its got heaps
// of different write methods.
//
// This is used by the net code to build outgoing messages.

#ifndef share_buffer_h
#define share_buffer_h

#include <stddef.h>
#include <stdint.h>
#include "sds.h"

typedef struct {
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

void *_buf_insert_pos(buffer *b, size_t length);

#define XX *(typeof(value) *)_buf_insert_pos(b, sizeof(value)) = value;
static inline void buf_uint32(buffer *b, uint32_t value) { XX }
static inline void buf_uint16(buffer *b, uint16_t value) { XX }
static inline void buf_uint8 (buffer *b, uint8_t  value) { XX }

static inline void buf_int32(buffer *b, int32_t value) { XX }
static inline void buf_int16(buffer *b, int16_t value) { XX }
static inline void buf_int8 (buffer *b, int8_t  value) { XX }

static inline void buf_float64(buffer *b, double value) { XX }
static inline void buf_float32(buffer *b, float value) { XX }
#undef XX

void buf_zstring(buffer *b, sds str);

#endif
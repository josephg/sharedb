
#include <stdlib.h>
#include <string.h>
#include "buffer.h"

void buf_init(buffer *b) {
  // Is this a good size? Who knows. Benchmark me maybe?
  b->capacity = 1024;
  b->bytes = malloc(b->capacity);
  b->pos = b->length = 0;
}

void buf_free(buffer *b) {
  free(b->bytes);
  b->capacity = 0;
}

// Ensures there's enough space to write size bytes and returns a pointer into the buffer
// at the start of the region.
void *_buf_insert_pos(buffer *b, size_t length) {
  size_t old_pos = b->pos;
  b->pos += length;
  if (b->pos > b->capacity) {
    do  {
      b->capacity *= 2;
    } while (b->pos > b->capacity);
    
    b->bytes = realloc(b->bytes, b->capacity);
  }

  if (b->pos > b->length) b->length = b->pos;
  return &b->bytes[old_pos];
}

void buf_zstring(buffer *b, const char *str, size_t len) {
  void *start = _buf_insert_pos(b, len + 1);
  memcpy(start, str, len);
  ((char *)start)[len] = '\0';
}

static void buf_write_fn(void *bytes, size_t num, void *user) {
  buffer *b = (buffer *)user;
  void *start = _buf_insert_pos(b, num);
  memcpy(start, bytes, num);
}

void buf_op(buffer *b, ot_type *type, ot_op *op) {
  type->write_op(op, buf_write_fn, b);
}

void buf_doc(buffer *b, ot_type *type, void *snapshot) {
  type->write_doc(snapshot, buf_write_fn, b);
}

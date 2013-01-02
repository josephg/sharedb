#ifndef share_ot_h
#define share_ot_h

#include <sys/types.h>
#include <stdbool.h>
#include <stddef.h>
#include "text.h"

struct buffer_t;

typedef union ot_op_t {
  text_op text;
} ot_op;

typedef union ot_cursor_t {
  text_cursor text;
} ot_cursor;

typedef void (*write_fn)(void *bytes, size_t num, void *user);

struct ot_type_t {
  char *name;
  size_t op_size; // size in bytes of the op type
  
  // size in bytes of the cursor type. Must be stable between 32bit / 64bit platforms.
  size_t cursor_size;
  
  // Create a document
  void *(*create)();
  // Destroy the document
  void (*free)(void *doc);
  
  // Read an op. Returns false success, true on failure.
  bool (*read_op)(ot_op *result, struct buffer_t *buf);
  // Write an op.
  void (*write_op)(ot_op *op, struct buffer_t *buf);
  
  // Read doc fn.
  
  // Write a document to a byte stream.
  void (*write_doc)(void *doc, struct buffer_t *buf);
  
  int (*check)(void *doc, const ot_op *op);
  void (*apply)(void *doc, ot_op *op);
  
  // Compose should be here.
  void (*transform)(ot_op *result, ot_op *op1, ot_op *op2, bool is_left);
  
  ot_cursor (*read_cursor)(struct buffer_t *buf, bool *err);
  void (*write_cursor)(ot_cursor cursor, struct buffer_t *buf);
  
  // Transform a cursor by an operation. is_own_cursor is set if the op was sent by the cursor's
  // owner. This is so we can make sure your own typing always pushes your cursor.
  void (*transform_cursor)(ot_cursor *cursor, ot_op *op, bool is_own_cursor);
};

typedef const struct ot_type_t ot_type;

extern ot_type text_composable;

ot_type *ot_type_with_name(const char *name);

#endif

#include "ot.h"

#include <rope.h>
#include "text.h"

static void *create_tc() {
  return rope_new();
}

static void free_tc(void *doc) {
  rope_free((rope *)doc);
}

static ssize_t read_op_tc(ot_op *result_out, void *data, size_t length) {
  return text_op_from_bytes(&result_out->text, data, length);
}

void write_op_tc(ot_op *op, write_fn write, void *user) {
  text_op_to_bytes(&op->text, write, user);
}

// Text docs are written as a big NULL-terminated string.
void write_doc_tc(void *doc, write_fn write, void *user) {
  ROPE_FOREACH((rope *)doc, n) {
    write(rope_node_data(n), rope_node_num_bytes(n), user);
  }
  // And a NULL terminator.
  uint8_t zero = '\0';
  write(&zero, 1, user);
}

void write_cursor_tc(ot_cursor cursor, write_fn write, void *user) {
  write(&cursor.pos, sizeof(uint32_t), user);
}

static int check_tc(void *doc, const ot_op *op) {
  return text_op_check((rope *)doc, &op->text);
}

static void apply_tc(void *doc, ot_op *op) {
  text_op_apply((rope *)doc, &op->text);
}

static void transform_tc(ot_op *result_out, ot_op *op1, ot_op *op2, bool isLeft) {
  result_out->text = text_op_transform(&op1->text, &op2->text, isLeft);
}

/*
 typedef struct {
 void *(*create)();
 void (*free)(void *doc);
 
 void (*apply)(void *doc, void *op);
 
 void (*transform)(void *op1, void *op2, bool isLeft);
 } ot_type;
 */
const ot_type text_composable = {
  "text",
  sizeof(text_op),
  sizeof(uint32_t), // Cursor size (cursors are ints)
  
  create_tc, // Create
  free_tc,
  
  read_op_tc,
  write_op_tc,
  
  write_doc_tc,
  
  write_cursor_tc,
  
  check_tc,
  apply_tc,
  
  // COMPOSE!
  transform_tc
};

ot_type *ot_type_with_name(const char *name) {
  if (strcmp(name, "text") == 0) {
    return &text_composable;
  } else {
    return NULL;
  }
}

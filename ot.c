#include "ot.h"

#include <rope.h>
#include <text-composable.h>

static void *create_tc() {
  return rope_new();
}

static void free_tc(void *doc) {
  rope_free((rope *)doc);
}

static ssize_t read_tc(ot_op *result_out, void *data, size_t length) {
  return text_op_from_bytes(&result_out->text, data, length);
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
  
  create_tc, // Create
  free_tc,
  read_tc,
  
  check_tc,
  apply_tc,
  
  // COMPOSE!
  transform_tc
};
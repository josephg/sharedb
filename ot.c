#include "ot.h"

#include <rope.h>
#include <text-composable.h>

static void *create_tc() {
  return rope_new();
}

static void free_tc(void *doc) {
  rope_free((rope *)doc);
}

static int check_tc(void *doc, void *op) {
  return text_op_check((rope *)doc, (text_op *)op);
}

static void apply_tc(void *doc, void *op) {
  text_op_apply((rope *)doc, (text_op *)op);
}

static void transform_tc(ot_op *result_out, void *op1, void *op2, bool isLeft) {
  result_out->text = text_op_transform((text_op *)op1, (text_op *)op2, isLeft);
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
  
  check_tc,
  apply_tc,
  
  // COMPOSE!
  transform_tc
};
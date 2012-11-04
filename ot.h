#ifndef share_ot_h
#define share_ot_h

#include <stdbool.h>
#include <stddef.h>
#include "text-composable.h"

typedef union {
  text_op text;
} ot_op;

struct _ot_type {
  char *name;
  size_t op_size; // Expected size in bytes of an op
  
  void *(*create)();
  void (*free)(void *doc);
  
  int (*check)(void *doc, void *op);
  void (*apply)(void *doc, void *op);
  
  // COMPOSE!
  void (*transform)(ot_op *result, void *op1, void *op2, bool isLeft);
};

typedef const struct _ot_type ot_type;

extern ot_type text_composable;

#endif

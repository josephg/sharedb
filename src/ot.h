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
  // Read an op from binary bytes. Returns bytes read on success, negative errcode on failure.
  ssize_t (*read)(ot_op *result, void *data, size_t length);
  
  int (*check)(void *doc, const ot_op *op);
  void (*apply)(void *doc, ot_op *op);
  
  // COMPOSE!
  void (*transform)(ot_op *result, ot_op *op1, ot_op *op2, bool isLeft);
};

typedef const struct _ot_type ot_type;

extern ot_type text_composable;

#endif

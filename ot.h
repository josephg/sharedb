#ifndef share_ot_h
#define share_ot_h

#include <stdbool.h>
#include <stddef.h>

struct _ot_type {
  char *name;
  
  void *(*create)();
  void (*free)(void *doc);
  
  int (*check)(void *doc, void *op);
  void (*apply)(void *doc, void *op);
  
  // COMPOSE!
  void (*transform)(void *op1, void *op2, bool isLeft);
};

typedef const struct _ot_type ot_type;

extern ot_type text_composable;

#endif

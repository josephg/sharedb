// This is an awesome little string structure based on redis's simple dynamic strings.
// The OT library's string structure is faster, but unfortunately its not compatible with redis's
// dictionary structure.

#ifndef _string_h_
#define _string_h_

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

struct string_t {
  uint_fast32_t len;
  uint_fast16_t refcount;
  char bytes[];
};

typedef char *dstr;

#define DSTR_BASE(s) (struct string_t *)((s) - offsetof(struct string_t, bytes))

dstr dstr_new2(const char *data, size_t len);

static inline dstr dstr_new(char *data) {
  return dstr_new2(data, strlen(data));
}

static inline dstr dstr_retain(dstr s) {
  struct string_t *string = DSTR_BASE(s);
  string->refcount++;
  return s;
}
void dstr_release(dstr s);

static inline size_t dstr_len(const dstr s) {
  struct string_t *string = DSTR_BASE(s);
  return string->len;
}

bool dstr_eq(dstr s1, dstr s2);

#endif

#include "dstr.h"
#include <stdlib.h>
#include <assert.h>

dstr dstr_new2(const char *data, size_t len) {
  struct string_t *string = malloc(sizeof(struct string_t) + len + 1);
  assert(len < UINT_FAST16_MAX);
  string->len = (uint_fast16_t)len;
  string->refcount = 1;
  memcpy(string->bytes, data, len);
  string->bytes[len] = '\0';
  return string->bytes;
}

void dstr_release(dstr s) {
  struct string_t *string = DSTR_BASE(s);
  if (--string->refcount == 0) {
    free(string);
  }
}

bool dstr_eq(dstr s1, dstr s2) {
  if (s1 == s2) {
    return true;
  }
  
  if (s1 == NULL || s2 == NULL) {
    return false;
  }
  
  size_t len = dstr_len(s1);
  if (dstr_len(s2) != len) {
    return false;
  }
  
  return memcmp(s1, s2, len) == 0;
}
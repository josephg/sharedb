#include <stdio.h>
#include <stdlib.h>
#include "uv.h"
#include "dstr.h"
#include "db.h"
#include "ot.h"
#include "rope.h"
#include "text.h"
#include "net.h"

#include "buffer.h"

void print_doc(ot_document *doc) {
  printf("Version: %zd, type: %s\n", doc->version, doc->type->name);
  if (doc->type == &text_composable) {
    unsigned char *str = rope_create_cstr((rope *)doc->snapshot);
    printf("Contents: '%s'\n", str);
    free(str);
  }
}

extern dictType dbDictType;
int main(int argc, const char *argv[]) {
  database db;
  db_init(&db);
  
  uv_loop_t *loop = uv_default_loop();
  net_listen(&db, loop, 8766);
  
  printf("Listening on port 8766");
  
  db_free(&db);
}


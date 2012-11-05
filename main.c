#include <stdio.h>
#include <stdlib.h>
#include "uv.h"
#include "sds.h"
#include "db.h"
#include "ot.h"
#include "rope.h"
#include "text-composable.h"
#include "net.h"

void print_doc(char *error, void *user, size_t version, const ot_type *type, void *data) {
  if (error) {
    printf("Error: %s\n", error);
  } else {
    printf("Version: %zd, type: %s\n", version, type->name);
    if (type == &text_composable) {
      unsigned char *str = rope_createcstr((rope *)data, NULL);
      printf("Contents: '%s'\n", str);
      free(str);
    }
  }
}

extern dictType dbDictType;
int main(int argc, const char *argv[]) {
  database db;
  db_init(&db);
  
  sds docName = sdsnew("hi");
  db_create(&db, docName, &text_composable, NULL, NULL);
  db_get(&db, docName, NULL, print_doc);
  
  text_op op = text_op_insert(0, (uint8_t *)"hi there");
  db_apply_op(&db, docName, 0, &op, sizeof(text_op), NULL, NULL);
  db_apply_op(&db, docName, 0, &op, sizeof(text_op), NULL, NULL);

  db_get(&db, docName, NULL, print_doc);
  sdsfree(docName);
  
  uv_loop_t *loop = uv_default_loop();
  net_listen(&db, loop, 8766);
  
  
  db_free(&db);
}


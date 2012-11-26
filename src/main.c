#include <stdio.h>
#include <stdlib.h>
#include "uv.h"
#include "sds.h"
#include "db.h"
#include "ot.h"
#include "rope.h"
#include "text-composable.h"
#include "net.h"

void print_doc(ot_document *doc) {
  printf("Version: %zd, type: %s\n", doc->version, doc->type->name);
  if (doc->type == &text_composable) {
    unsigned char *str = rope_createcstr((rope *)doc->snapshot, NULL);
    printf("Contents: '%s'\n", str);
    free(str);
  }
}

extern dictType dbDictType;
int main(int argc, const char *argv[]) {
  database db;
  db_init(&db);
  
  sds docName = sdsnew("hi");
  db_create(&db, docName, &text_composable, NULL, NULL);
  db_get_b(&db, docName, ^(char *error, ot_document *doc) {
    if (error) {
      printf("Error getting document: %s\n", error);
      return;
    }
    print_doc(doc);
    
    text_op op = text_op_insert(0, (uint8_t *)"hi there");
    db_apply_op(&db, doc, 0, (ot_op *)&op, NULL, NULL);
    db_apply_op(&db, doc, 0, (ot_op *)&op, NULL, NULL);
    print_doc(doc);
  });
  
  sdsfree(docName);
  
  uv_loop_t *loop = uv_default_loop();
  net_listen(&db, loop, 8766);
  
  db_free(&db);
}


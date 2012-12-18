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
#include <assert.h>
#include <sys/time.h>

void print_doc(ot_document *doc) {
  printf("Version: %zd, type: %s\n", doc->version, doc->type->name);
  if (doc->type == &text_composable) {
    unsigned char *str = rope_create_cstr((rope *)doc->snapshot);
    printf("Contents: '%s'\n", str);
    free(str);
  }
}

//uint64_t get_time_ms() {
//  struct timeval time;
//  assert(gettimeofday(&time, NULL) == 0);
//  return time.tv_sec * 1000 + time.tv_usec / 1000;
//}

void timer_cb(uv_timer_t* handle, int status) {
  printf("%lld\n", uv_now(handle->loop));
  printf("%lld\n", uv_hrtime());
  printf("%ld\n", time(NULL));
//  printf("%lld\n", get_time_ms());
}

extern dictType dbDictType;
int main(int argc, const char *argv[]) {
  database db;
  db_init(&db);
  
  uv_loop_t *loop = uv_default_loop();
  uv_tcp_t server = {};
  net_listen(&server, &db, loop, 8766);
  
  uv_timer_t t;
  uv_timer_init(loop, &t);
//  uv_timer_start(&t, timer_cb, 0, 1000);
  
  printf("Listening on port 8766\n");
  
  uv_set_process_title("shared");
  uv_run(loop);
  
  db_free(&db);
}


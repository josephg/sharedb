#include <stdio.h>
#include <stdlib.h>
#include "uv.h"
#include "sds.h"
#include "db.h"
#include "ot.h"
#include "rope.h"

struct client {
  
  uv_stream_t *socket;
};

uv_buf_t alloc_buffer(uv_handle_t *handle, size_t suggested_size) {
  return uv_buf_init((char *)malloc(suggested_size), (unsigned int)suggested_size);
}

void close_handle(uv_handle_t *handle) {
  free(handle);
}

void got_data(uv_stream_t* stream, ssize_t nread, uv_buf_t buf) {
  if (nread < 0) {
    // An error occurred on the stream. Punt 'em.
    uv_close((uv_handle_t *)stream, close_handle);
  } else if (nread > 0) {
    fprintf(stdout, "%s", buf.base);
  }
  
  if (buf.base) {
    free(buf.base);
  }
}

void got_connection(uv_stream_t* server, int status) {
  printf("got connection\n");
  
  if (status < 0) {
    fprintf(stderr, "Error with connection..?");
    return;
  }
  
  uv_tcp_t *client = calloc(sizeof(uv_tcp_t), 1);
  uv_tcp_init(uv_default_loop(), client);

  if (uv_accept(server, (uv_stream_t *)client)) {
    uv_close((uv_handle_t *)client, NULL);
    free(client);
    return;
  }
  
  uv_read_start((uv_stream_t *)client, alloc_buffer, got_data);
}

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
  
  sdsfree(docName);
  
  uv_loop_t *loop = uv_default_loop();

  uv_tcp_t server = {};
  uv_tcp_init(loop, &server);
  
//  struct sockaddr_in addr = uv_ip4_addr("0.0.0.0", 8766);
//  uv_tcp_bind(&server, addr);
  struct sockaddr_in6 addr6 = uv_ip6_addr("::", 8766);
  uv_tcp_bind6(&server, addr6);
  
  uv_listen((uv_stream_t *)&server, 128, got_connection);
  
  uv_set_process_title("share");
  uv_run(loop);
  
  db_free(&db);
}


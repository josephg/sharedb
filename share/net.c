
#include "uv.h"
#include <stdlib.h>
#include <stdio.h>


struct client {
  
  uv_stream_t *socket;
};

static uv_buf_t alloc_buffer(uv_handle_t *handle, size_t suggested_size) {
  return uv_buf_init((char *)malloc(suggested_size), (unsigned int)suggested_size);
}

static void close_handle(uv_handle_t *handle) {
  free(handle);
}

static void got_data(uv_stream_t* stream, ssize_t nread, uv_buf_t buf) {
  if (nread < 0) {
    // An error occurred on the stream. Punt 'em.
    printf("Closing stream\n");
    uv_close((uv_handle_t *)stream, close_handle);
  } else if (nread > 0) {
    fprintf(stdout, "%s", buf.base);
  }
  
  if (buf.base) {
    free(buf.base);
  }
}

static void got_connection(uv_stream_t* server, int status) {
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


static uv_tcp_t server = {};

void net_listen(uv_loop_t *loop, int port) {
  uv_tcp_init(loop, &server);
  
  //  struct sockaddr_in addr = uv_ip4_addr("0.0.0.0", 8766);
  //  uv_tcp_bind(&server, addr);
  struct sockaddr_in6 addr6 = uv_ip6_addr("::", 8766);
  uv_tcp_bind6(&server, addr6);
  
  uv_listen((uv_stream_t *)&server, 128, got_connection);
  
  uv_set_process_title("share");
}

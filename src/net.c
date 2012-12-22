#include "net.h"
#include "db.h"
#include "uv.h"
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#include "protocol.h"

static const char *MAGIC_BYTES = "WAVE";
static const int MAGIC_BYTES_LEN = 4;

open_pair *open_pair_alloc() {
  // Object pool me
  return malloc(sizeof(open_pair));
}

void open_pair_free(open_pair *pair) {
  // Change me to use an object pool.
  free(pair);
}

write_req *write_req_alloc() {
  // Should be using an object pool!!
  write_req *req = malloc(sizeof(write_req));
  buf_init(&req->buffer);
  
  buf_reset(&req->buffer);
  // & seek past the length field. We'll fill that in later.
  buf_seek(&req->buffer, 4);
  return req;
}

void write_req_free(write_req *req) {
  // mmm how about that object pool...
  buf_free(&req->buffer);
  free(req);
}

void close_pair(open_pair *pair) {
  if (pair->prev_client) {
    pair->prev_client->next_client = pair->next_client;
  } else {
    // The node is the doc's head node
    assert(pair == pair->doc->open_pair_head);
    pair->doc->open_pair_head = pair->next_client;
  }
  if (pair->next_client) {
    pair->next_client->prev_client = pair->prev_client;
  }
  // & probably refresh the document's reaping timeout.
  open_pair_free(pair);
}

void client_release(client *client) {
  client->retain_count--;
  if (client->retain_count == 0) {
    assert(!uv_is_active((uv_handle_t *)client));
    
    // Close all the documents the client still has open
    for (open_pair *o = client->open_docs_head; o;) {
      broadcast_remove_cursor(o);
      
      open_pair *prev = o;
      o = o->next;
      close_pair(prev);
    }
    
    if (client->client_doc_name) {
      dstr_release(client->client_doc_name);
    }
    if (client->server_doc_name) {
      dstr_release(client->server_doc_name);
    }
    
    free(client->packet);
    free(client);
  }
}

#define MAX(x, y) ((x) > (y) ? (x) : (y))

static uv_buf_t alloc_buffer(uv_handle_t *handle, size_t suggested_size) {
  // I'm cheating, and returning where I want the packet (or packet length) to be stored.

  client *c = (client *)handle;
  if (c->offset < 4) {
    // Read in the packet length
    return uv_buf_init((char *)&c->packet_length + c->offset, 4 - c->offset);
  } else {
    // Reading in the packet itself
    size_t new_size = MAX(suggested_size, c->packet_length);
    
    if (new_size > 1000000) {
      fprintf(stderr, "Warning: trying to allocate %zd\n", new_size);
    }
    // We might need to resize the packet...
    if (c->packet_capacity < new_size) {
      c->packet = realloc(c->packet, new_size);
      c->packet_capacity = new_size;
    }
    
    return uv_buf_init(c->packet + c->offset - 4, c->packet_length - c->offset + 4);
  }
}

static void magic_bytes_write_cb(uv_write_t *write, int status) {
  free(write);
}

static void write_magic_bytes(client *c) {
  uv_buf_t buf = uv_buf_init((char *)MAGIC_BYTES, MAGIC_BYTES_LEN);
  uv_write_t *write = malloc(sizeof(uv_write_t));
  uv_write(write, (uv_stream_t *)&c->socket, &buf, 1, magic_bytes_write_cb);
}

// This is called after uv closes a client's handle.
static void close_handle_cb(uv_handle_t *handle) {
  client *c = (client *)handle;
  client_release(c);
}

// libuv callback for incoming network data.
static void got_data(uv_stream_t* stream, ssize_t nread, uv_buf_t uv_buf) {
  if (nread < 0) {
    // An error occurred on the stream. Punt 'em.
    printf("Closing stream\n");
    uv_err_t err = uv_last_error(uv_default_loop());
    if (err.code != UV_EOF) {
      fprintf(stderr, "uv error %s: %s\n", uv_err_name(err), uv_strerror(err));
    }
    uv_close((uv_handle_t *)stream, close_handle_cb);
  } else if (nread > 0) {
    // We have new data. Add the buffer to the client's linked list of buffers and check to see
    // if we can parse a packet.
    client *c = (client *)stream;
    c->offset += nread;
    
    if (c->offset == 4) {
      // Finished reading in the packet length.
      
      if (!c->seen_magic_bytes) {
        // The length field should contain the magic bytes.
        if (c->packet_length != *(int *)MAGIC_BYTES) {
          fprintf(stderr, "Did not get magic bytes from client\n");
          uv_close((uv_handle_t *)c, close_handle_cb);
        } else {
          c->seen_magic_bytes = true;
          c->offset = 0;
          write_magic_bytes(c);
        }
      }
      // Big endian???
      //c->packet_length = ntohl(c->packet_length);
    }
    
    if (c->offset > 4) {
      assert(c->offset - 4 <= c->packet_length);
    }
    
    //printf("Read in %zd\n", nread);
    
    if (c->offset == c->packet_length + 4) {
      // Read a packet.
      if (handle_packet(c)) {
        // Invalid packet.
        fprintf(stderr, "Closing connection due to invalid packet\n");
        uv_close((uv_handle_t *)c, close_handle_cb);
        return;
      }
      c->offset = 0;
    }
  }
}

static void write_cb(uv_write_t *write, int status) {
  // Honestly, I don't care if there was an error writing to the client. If there was an error,
  // the client has probably disconnected by now anyway.
  write_req *req = (write_req *)((void *)write - offsetof(write_req, write));
  write_req_free(req);
}

void client_write(client *c, write_req *req) {
  // First go back to the start of the packet and fill in the length.
  buf_seek(&req->buffer, 0);
  // The length field doesn't include itself. Hence the - 4.
  buf_uint32(&req->buffer, (uint32_t)req->buffer.length - 4);
  
  uv_buf_t buf = uv_buf_init(req->buffer.bytes, req->buffer.length);
  uv_write(&req->write, (uv_stream_t *)&c->socket, &buf, 1, write_cb);
}

// Callback for libuv's uv_listen(server, ...) method.
static void got_connection(uv_stream_t* server, int status) {
  printf("got connection\n");
  
  if (status < 0) {
    fprintf(stderr, "Error with connection..?");
    return;
  }
  
  client *c = calloc(sizeof(client), 1);
  uv_tcp_init(uv_default_loop(), &c->socket);
  c->db = (database *)server->data;
  c->retain_count = 1;

  // uv_accept is guaranteed to succeed here.
  assert(uv_accept(server, (uv_stream_t *)&c->socket) == 0);
  
  uv_read_start((uv_stream_t *)&c->socket, alloc_buffer, got_data);
}

void net_listen(uv_tcp_t *server, database *db, uv_loop_t *loop, int port) {
  server->data = db;
  uv_tcp_init(loop, server);
  
  //  struct sockaddr_in addr = uv_ip4_addr("0.0.0.0", 8766);
  //  uv_tcp_bind(&server, addr);
  struct sockaddr_in6 addr6 = uv_ip6_addr("::", 8766);
  uv_tcp_bind6(server, addr6);
  
  uv_listen((uv_stream_t *)server, 128, got_connection);
}

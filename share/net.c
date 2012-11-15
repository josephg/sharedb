#include "db.h"
#include "uv.h"
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

enum message_type {
  // Common
  MSG_OP, MSG_CURSOR,
  
  // Client -> server
  MSG_OPEN, MSG_CLOSE, MSG_GET_OPS,
  
  // Server -> client
  MSG_OP_APPLIED,
};

#pragma pack(1)

typedef struct {
  uint32_t length;
  uint8_t type;
  uint8_t data[];
} message;

#pragma pack()

typedef struct client_s {
  uv_tcp_t socket;
  
  // Stuff for framing network messages
  
  // A buffer for the incoming packet
  uint32_t offset; // Where we're up to in the current packet (including the length)

  uint32_t packet_length; // Size of the currently incoming packet.
  char *packet; // The buffer has
  size_t packet_capacity;
  
  database *db;
  
  struct client_s *next_client;
} client;

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

static void close_handle(uv_handle_t *handle) {
  client *c = (client *)handle;
  free(c->packet);
  free(c);
}

static bool read_bytes(void *dest, void *src, void *end, size_t length) {
  if (length + src >= end) {
    return true;
  }
  
  memcpy(dest, src, length);
  return false;
}

// Handle a pending packet. There must be a packet's worth of buffers waiting in c.
static bool handle_packet(client *c) {
  // I don't know if this is the best way to do this. It would be nice to avoid extra memcpys,
  // although one large memcpy is cheaper than lots of small ones.
  bool error = false;
  
  char *data = c->packet;
  // A pointer just past the end of the packet.
  char *end = c->packet + c->packet_length;
  
  if (data == end) return true;
  unsigned char type = *(data++);
  
  switch (type) {
    case MSG_OPEN:
      printf("Open!\n");
      
      break;
      
    default:
      // Invalid data.
      fprintf(stderr, "Invalid packet - unexpected type %d\n", type);
      error = true;
  }
  
  if (data != end) {
    fprintf(stderr, "%ld trailing bytes\n", end - data);
//    read_bytes(c, NULL, c->num_bytes - expected_remaining_bytes);
//    return true;
  }
  
  return error;
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
    uv_close((uv_handle_t *)stream, close_handle);
  } else if (nread > 0) {
    // We have new data. Add the buffer to the client's linked list of buffers and check to see
    // if we can parse a packet.
    client *c = (client *)stream;
    c->offset += nread;
    
    if (c->offset == 4) {
      // Finished reading in the packet length.
      
      // Big endian???
      //c->packet_length = ntohl(c->packet_length);
    }
    
    if (c->offset > 4) {
      assert(c->offset - 4 <= c->packet_length);
    }
    
    printf("Read in %zd\n", nread);
    
    if (c->offset == c->packet_length + 4) {
      // Read a packet.
      if (handle_packet(c)) {
        // Invalid packet.
        fprintf(stderr, "Closing connection due to invalid packet\n");
        uv_close((uv_handle_t *)c, close_handle);
        return;
      }
      c->offset = 0;
    }
  }
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
  
  if (uv_accept(server, (uv_stream_t *)&c->socket)) {
    uv_close((uv_handle_t *)&c->socket, NULL);
    free(c);
    return;
  }
  
  uv_read_start((uv_stream_t *)&c->socket, alloc_buffer, got_data);
}


void net_listen(database *db, uv_loop_t *loop, int port) {
  uv_tcp_t server = {};
  server.data = db;
  uv_tcp_init(loop, &server);
  
  //  struct sockaddr_in addr = uv_ip4_addr("0.0.0.0", 8766);
  //  uv_tcp_bind(&server, addr);
  struct sockaddr_in6 addr6 = uv_ip6_addr("::", 8766);
  uv_tcp_bind6(&server, addr6);
  
  uv_listen((uv_stream_t *)&server, 128, got_connection);
  
  uv_set_process_title("shared");
  
  uv_run(loop);
}

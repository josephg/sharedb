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


typedef struct buffer_s {
  struct buffer_s *next;
  size_t capacity;
  size_t used;
  char bytes[];
} buffer;

typedef struct client_s {
  uv_tcp_t socket;
  
  // Stuff for framing network messages
  buffer *first_buffer;
  buffer *last_buffer;
  
  int32_t offset; // Byte offset into the first buffer
  int32_t num_bytes; // Number of pending bytes
  
  size_t packet_length; // Size of the next incoming packet
  
  database *db;
  
  struct client_s *next_client;
} client;

static buffer *buffer_pool = NULL;

static void pool_free(buffer *buf) {
  buf->next = buffer_pool;
  buffer_pool = buf;
}

static uv_buf_t alloc_buffer(uv_handle_t *handle, size_t suggested_size) {
  // The buffer is preceeded by a next pointer.
  assert(suggested_size > sizeof(buffer));
  
  buffer *b;
  if (buffer_pool) {
    b = buffer_pool;
    buffer_pool = b->next;
  } else {
    b = (buffer *)malloc(suggested_size);
    b->next = 0;
    b->capacity = suggested_size;
    b->used = 0;
  }
  return uv_buf_init(b->bytes, (unsigned int)suggested_size - sizeof(buffer));
}

static void close_handle(uv_handle_t *handle) {
  client *c = (client *)handle;

  // Recycle the buffers.
  if (c->last_buffer) {
    c->last_buffer->next = buffer_pool;
    buffer_pool = c->first_buffer;
  }
  
  free(c);
}

static bool read_bytes(client *c, void *dest, size_t length) {
  if (c->num_bytes < length) return true;
  
  c->num_bytes -= length;
  while (length) {
    buffer *buf = c->first_buffer;
    
    assert(buf);
    assert(buf->used > c->offset);
    
    if (buf->used - c->offset <= length) {
      // Not enough bytes in the current buffer.
      size_t s = buf->used - c->offset;
      memcpy(dest, &buf->bytes[c->offset], s);
      dest += s;
      c->offset = 0;
      c->first_buffer = buf->next;
      if (c->first_buffer == NULL) {
        c->last_buffer = NULL;
      }
      pool_free(buf);
      length -= s;
    } else {
      memcpy(dest, &buf->bytes[c->offset], length);
      c->offset += length;
      length = 0;
    }
  }
  return false;
}

static bool handle_packet(client *c) {
  uint8_t packet_type;
  size_t expected_remaining_bytes = c->num_bytes - c->packet_length;
  
  if (read_bytes(c, &packet_type, 1)) return true;
  
  switch (packet_type) {
    case MSG_OPEN:
      printf("Open!\n");
      break;
      
    default:
      // Invalid data.
      fprintf(stderr, "Invalid packet - unexpected type %d\n", packet_type);
      return true;
  }
  
  if (c->num_bytes != expected_remaining_bytes) {
    fprintf(stderr, "Invalid packet - %ld trailing bytes\n",
            c->num_bytes - expected_remaining_bytes);
    return true;
  }
  
  return false;
}

static void got_data(uv_stream_t* stream, ssize_t nread, uv_buf_t uv_buf) {
  if (nread < 0) {
    // An error occurred on the stream. Punt 'em.
    printf("Closing stream\n");
    uv_err_t err = uv_last_error(uv_default_loop());
    if (err.code != UV_EOF) {
      fprintf(stderr, "uv error %s: %s\n", uv_err_name(err), uv_strerror(err));
    }
    uv_close((uv_handle_t *)stream, close_handle);
    
    if (uv_buf.base) {
      pool_free((buffer *)(uv_buf.base - sizeof(buffer)));
    }
  } else if (nread > 0) {
    // We have new data.
    buffer *buf = (buffer *)(uv_buf.base - sizeof(buffer));
    client *c = (client *)stream;
    buf->used = nread;
    
    if (c->first_buffer == NULL) {
      c->first_buffer = c->last_buffer = buf;
    } else {
      c->last_buffer->next = buf;
      c->last_buffer = buf;
    }
    
    c->num_bytes += nread;
    
    while(true) {
      if(c->packet_length == 0) {
        if (c->num_bytes < 4) {
          break;
        }
        
        // The bytes are already little endian.
        read_bytes(c, &c->packet_length, 4);
        //printf("read header %d\n", client->packetLength);
        
        if (c->packet_length == 0) {
          // Invalid packet.
          fprintf(stderr, "Invalid packet - packet length is 0\n");
          uv_close((uv_handle_t *)c, close_handle);
          return;
        }
      } else {
        if (c->num_bytes < c->packet_length) {
          break;
        }
        
        // Read a packet.
        if (handle_packet(c)) {
          // Invalid packet.
          fprintf(stderr, "Closing connection due to invalid packet\n");
          uv_close((uv_handle_t *)c, close_handle);
          return;
        }
        c->packet_length = 0;
      }
    }
  }
}

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
  
  buffer *next;
  for (buffer *iter = buffer_pool; iter; iter = next) {
    next = iter->next;
    free(iter);
  }
  buffer_pool = NULL;
}

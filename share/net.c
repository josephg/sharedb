#include "net.h"
#include "db.h"
#include "uv.h"
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

enum message_type {
  // Common
  MSG_OP = 1,
  MSG_CURSOR = 2,
  
  // Client -> server
  MSG_OPEN = 3,
  MSG_CLOSE = 4,
  MSG_GET_OPS = 5,
  
  // Server -> client
  MSG_OP_APPLIED = 6,
  
  MSG_FLAG_HAS_DOC_NAME = 0x80
};

typedef struct client_t {
  uv_tcp_t socket;
  
  // Stuff for framing network messages
  
  // A buffer for the incoming packet
  uint32_t offset; // Where we're up to in the current packet (including the length)

  uint32_t packet_length; // Size of the currently incoming packet.
  char *packet; // The buffer has
  size_t packet_capacity;
  
  // The name of the most recently accessed document. This is cached, so subsequent requests on
  // the same document don't need the docname resent.
  // It starts off as NULL.
  sds doc_name;
  
  database *db;
  
  struct client_t *next_client;

  open_pair *open_docs_head;

  // The retain count starts at 1, which means the client is connected. The client is freed when
  // the retain count gets to 0.
  int retain_count;
} client;

static open_pair *open_pair_alloc() {
  return malloc(sizeof(open_pair *));
}

static void open_pair_free(open_pair *pair) {
  // Change me to use an object pool.
  free(pair);
}

static inline void client_retain(client *client) {
  client->retain_count++;
}

static void close_pair(open_pair *pair) {
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

static void client_release(client *client) {
  client->retain_count--;
  if (client->retain_count == 0) {
    assert(!uv_is_active((uv_handle_t *)client));
    
    // Close all the documents the client still has open
    for (open_pair *o = client->open_docs_head; o;) {
      open_pair *prev = o;
      o = o->next;
      close_pair(prev);
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

// This is called after uv closes a client's handle.
static void close_handle_cb(uv_handle_t *handle) {
  client *c = (client *)handle;
  client_release(c);
}

static bool read_bytes(void *dest, void *src, void *end, size_t length) {
  if (length + src >= end) {
    return true;
  }
  
  memcpy(dest, src, length);
  return false;
}

static sds read_string(char **src, char *end) {
  char *data = *src;
  size_t len = strnlen(data, end - data);
  if (len == end - data) {
    fprintf(stderr, "Not enough bytes left for the string!\n");
    return NULL;
  } else {
    sds str = sdsnewlen(data, len);
    *src += len + 1; // Also skip the \0
    return str;
  }
}

// It might make more sense to put this function into db.h.
static void open_doc(client *client, sds doc_name, void (^callback)(char *error)) {
  printf("Open '%s'\n", doc_name);
  client_retain(client);
  
  db_get_b(client->db, doc_name, ^(char *error, ot_document *doc) {
    // doc_name may have been freed by this point.
    if (error) {
      if (callback) callback(error);
      client_release(client);
      return;
    }
    
    // First make sure the doc isn't already open.
    for (open_pair *o = client->open_docs_head; o; o = o->next) {
      if (o->doc == doc) {
        if (callback) callback("Doc is already open");
        client_release(client);
        return;
      }
    }
    
    // The document isn't open. Open it.
    open_pair *pair = open_pair_alloc();
    pair->client = client;
    pair->doc = doc;
    pair->next = client->open_docs_head;
    client->open_docs_head = pair;
    
    pair->next_client = doc->open_pair_head;
    pair->prev_client = NULL;
    doc->open_pair_head = pair;
    
    if (callback) callback(NULL);
    client_release(client);
  });
}

#define READ_INT32(dest) if(end - data < 4) return false; dest = *(int *)data; data += 4

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
  
  if (type & MSG_FLAG_HAS_DOC_NAME) {
    if (c->doc_name) {
      //sdsclear(c->doc_name);
      // ... and sdscat. But I'm lazy for now.
      
      sdsfree(c->doc_name);
    }
    c->doc_name = read_string(&data, end);
    if (c->doc_name == NULL) {
      return true;
    }
  }
  
  type &= 0x7f;
  
  if (c->doc_name == NULL) {
    fprintf(stderr, "Doc name not known\n");
    return true;
  }
  
  switch (type) {
    case MSG_OPEN:
      open_doc(c, c->doc_name, ^(char *error) {
        printf("Open callback - error: '%s'\n", error);
        
        // ... Reply to the client.
        
      });
      break;
      
    case MSG_OP:
      // Packet contents:
      // - Version
      // - Op. Op data is specific to the OT type.
      // Not implemented: dupIfSource
      if (c->doc_name == NULL) return true;
      uint32_t version;
      READ_INT32(version);
      //ot_op op;
      
      
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
    uv_close((uv_handle_t *)stream, close_handle_cb);
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
        uv_close((uv_handle_t *)c, close_handle_cb);
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
  c->retain_count = 1;

  // uv_accept is guaranteed to succeed here.
  assert(uv_accept(server, (uv_stream_t *)&c->socket) == 0);
  
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

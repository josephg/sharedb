
#ifndef share_net_h
#define share_net_h

#include <stdint.h>
#include "uv.h"
#include "dstr.h"
#include "buffer.h"
#include "ot.h"

struct client_t;
struct uv_loop_s;
struct database_t;
struct ot_document_t;

// This 2d linked list is used to keep track of the M-N relationship between clients and open
// documents. Doing it this way means that a client opening & closing a document takes O(n)
// time. (n = number of docs open by that client).
//
// Submitting an op to a document also takes O(m) time, m = number of clients with the document
// open.
typedef struct open_pair_t {
  struct client_t *client;
  struct ot_document_t *doc;
  
  bool tracks_cursors;
  bool has_cursor;
  ot_cursor cursor;
  
  // The next open_doc for this client
  struct open_pair_t *next;
  
  struct open_pair_t *prev_client;
  struct open_pair_t *next_client;
  
} open_pair;

open_pair *open_pair_alloc();
void open_pair_free(open_pair *pair);

// Unlink the pair from the open list and free it.
void close_pair(open_pair *pair);

typedef struct client_t {
  uv_tcp_t socket;
  
  // Stuff for framing network messages
  
  // A buffer for the incoming packet
  uint32_t offset; // Where we're up to in the current packet (including the length)
  
  uint32_t packet_length; // Size of the currently incoming packet.
  char *packet; // The incoming packet itself.
  size_t packet_capacity;
  
  // The name of the most recently accessed document. This is cached, so subsequent requests on
  // the same document don't need the docname resent.
  // It starts off as NULL.
  dstr client_doc_name;
  dstr server_doc_name;
  
  struct database_t *db;
  
  struct client_t *next_client;
  
  open_pair *open_docs_head;
  
  // The retain count starts at 1, which means the client is connected. The client is freed when
  // the retain count gets to 0.
  int retain_count;
  
  // Before clients can do anything, they must first send magic bytes to the server, and then
  // send a hello message, which authenticates them. Not actually implemented yet.
  bool seen_magic_bytes;
  bool said_hello;
  
  // Client's id. The id is unique for the lifetime of the server.
  uint32_t cid;
} client;

// Main entrypoint for the net code. Opens the network socket for incoming connections.
void net_listen(uv_tcp_t *server, struct database_t *db, uv_loop_t *loop, int port);

static inline void client_retain(client *client) {
  client->retain_count++;
}
void client_release(client *client);

// This contains an outgoing packet.
typedef struct {
  buffer buffer;
  uv_write_t write;
} write_req;

write_req *write_req_alloc();
void write_req_free(write_req *req);
//write_req *write_req_clone(write_req *orig);

// Write the write request out to a client. This consumes the request.
void client_write(client *c, write_req *req);

// Notify a client that an op has been sent on a document it has open.
void client_notify_op(client *c, struct ot_document_t *doc, ot_op *op);
#endif

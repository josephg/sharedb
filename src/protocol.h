#ifndef share_protocol_h
#define share_protocol_h

#include <stdint.h>

// There's no real problem #including a bunch of stuff here, but I appreciate my fast compiles.


enum message_type {
  // Common
  MSG_HELLO = 1,
  MSG_OP = 2,
  MSG_CURSOR = 3,
  
  // Client -> server
  MSG_OPEN = 4,
  MSG_CLOSE = 5,
  MSG_GET_OPS = 6,
  
  // Server -> client
  MSG_OP_ACK = 7,
  
  // For cursor messages, we use these couple bits to set what kind of cursor data op it is.
  MSG_CURSOR_SET = 0x10,
  MSG_CURSOR_REMOVE = 0x20,
  MSG_CURSOR_REPLACE_ALL = 0x30,
  
  // S->C this means that instead of containing actual data, this packet has a string error message.
  MSG_FLAG_ERROR = 0x40,
  
  // This packet replaces the currently active docname.
  MSG_FLAG_HAS_DOC_NAME = 0x80,
};

enum open_flags {
  // C->S this means you need a fresh copy of the snapshot.
  // S->C the packet contains a document snapshot.
  OPEN_FLAG_SNAPSHOT = 0x1,
  
  // C->S this means the document to open should be created if it doesn't already exist.
  // S->C this means the document being opened is brand new.
  OPEN_FLAG_CREATE = 0x2,
  
  // C->S this indiciates the client wants to know about all user cursors in the document.
  OPEN_FLAG_TRACK_CURSORS = 0x4,
};


struct client_t;
struct ot_document_t;
struct open_pair_t;
union ot_op_t;

// Handle the pending packet waiting in the client.
bool handle_packet(struct client_t *c);

// Notify the clients that an op has been sent on a document it has open.
void broadcast_op_to_clients(struct ot_document_t *doc, struct client_t *source,
                             uint32_t version, union ot_op_t *op);

void broadcast_remove_cursor(struct open_pair_t *pair);

#endif

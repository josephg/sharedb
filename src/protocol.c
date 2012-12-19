// This file contains all the actual network protocol code. It parses packets coming
// on the wire, decodes them and fires off the appropriate requests to the database.

#include <stdio.h>
#include <stdbool.h>
#include <assert.h>
#include "protocol.h"
#include "net.h"
#include "db.h"
#include "ot.h"

// The next client id. TODO: Replace me with GUIDs or something.
// Its dangerous when an id is reused on the same document (which could
// happen once the server has persistance).
static int next_id = 1000;

static const uint8_t PROTOCOL_VERSION = 0;

static void open_internal(client *client, ot_document *doc, uint32_t version, uint8_t open_flags,
                          void (^callback)(char *error, ot_document *doc, uint8_t open_flags)) {
  // Make sure the doc isn't already open.
  for (open_pair *o = client->open_docs_head; o; o = o->next) {
    if (o->doc == doc) {
      if (callback) callback("Doc is already open", NULL, 0);
      return;
    }
  }
  
  if (version < doc->version) {
    if (callback) callback("Getting historical ops in open not implemented yet", NULL, 0);
    return;
  }
  
  // The document isn't open. Open it.
  open_pair *pair = open_pair_alloc();
  pair->client = client;
  pair->doc = doc;
  pair->tracks_cursors = open_flags & OPEN_FLAG_TRACK_CURSORS;
  pair->has_cursor = open_flags & OPEN_FLAG_HAS_CURSOR;
  pair->cursor = (ot_cursor){};
  
  pair->next = client->open_docs_head;
  client->open_docs_head = pair;
  
  pair->next_client = doc->open_pair_head;
  if (pair->next_client) {
    pair->next_client->prev_client = pair;
  }
  pair->prev_client = NULL;
  doc->open_pair_head = pair;
  
  if (callback) callback(NULL, doc, open_flags);
}

// It might make more sense to put this function into db.h.
static void handle_open(client *client, dstr doc_name, dstr doc_type,
                        uint32_t version, uint8_t open_flags,
                        void (^callback)(char *error, ot_document *doc, uint8_t flags)) {
  printf("Open '%s'\n", doc_name);
  
  db_get_b(client->db, doc_name, ^(char *error, ot_document *doc) {
    if (open_flags & OPEN_FLAG_CREATE && error && strcmp(error, "Doc does not exist") == 0) {
      // Create it.
      ot_type *type = ot_type_with_name(doc_type);
      if (type == NULL) {
        if (callback) callback("Unknown type", NULL, 0);
        return;
      }
      
      db_create_b(client->db, doc_name, type, ^(char *error, ot_document *doc) {
        if (error) {
          if (callback) callback(error, NULL, 0);
          return;
        }
        
        open_internal(client, doc, doc->version, open_flags, callback);
      });
      return;
    } else if (error) {
      if (callback) callback(error, NULL, 0);
      return;
    }
    
    // Check the requested version is valid
    if (version != UINT32_MAX && version > doc->version) {
      if (callback) callback("Invalid version", NULL, 0);
      return;
    }
    
    if (version != UINT32_MAX && open_flags & OPEN_FLAG_SNAPSHOT) {
      if (callback) callback("Cannot fetch historical snapshots", NULL, 0);
      return;
    }
    
    if (doc_type != dstr_empty && strcmp(doc_type, doc->type->name) != 0) {
      // The document isn't the requested type.
      if (callback) callback("Type mismatch", NULL, 0);
      return;
    }
    
    open_internal(client, doc,
                  version == UINT32_MAX ? doc->version : version,
                  open_flags & ~OPEN_FLAG_CREATE,
                  callback);
  });
}

//void print_doc(ot_document *doc);

// Handle a MSG_OP packet.
// Not implemented: dupIfSource
static void handle_op(client *client, uint32_t version, char *op_data, size_t op_data_size,
      void (^callback)(char *error, uint32_t new_version)) {
  // Inside the block below, the packet data won't be valid anymore. I can't parse it now because
  // I don't know what the data type is until the block (below), by which time the packet itself
  // might have been rewritten with new data over the wire. I'll copy it to a stack-local buffer
  // This won't work with VSC - but thats a bridge to cross when we get to it.
  char op_buffer[op_data_size];
  memcpy(op_buffer, op_data, op_data_size);
  
  // I don't know why I need to do this, or if there are dragons involved.
  char *b = op_buffer;
  
  db_get_b(client->db, client->client_doc_name, ^(char *error, ot_document *doc) {
    if (error) {
      if (callback) callback(error, UINT32_MAX);
      return;
    }
    ot_op op;
    ssize_t bytes_read = doc->type->read_op(&op, b, op_data_size);
    if (bytes_read < 0) {
      // Error parsing the op.
      if (callback) callback("Error parsing op", UINT32_MAX);
      return;
    }
    
    // The callback parameters match, so we can just chain 'em.
    db_apply_op_b(client->db, client, doc, version, &op, callback);
  });
}

static char *handle_close(client *client, dstr doc_name) {
  open_pair *prev = NULL;
  for (open_pair *o = client->open_docs_head; o; o = o->next) {
    if (dstr_eq(o->doc->name, doc_name)) {
      if (prev) {
        prev->next = o->next;
      } else {
        client->open_docs_head = o->next;
      }
      close_pair(o);
      return NULL;
    }
    prev = o;
  }

  return "Doc is not open";
}

static bool read_bytes(void *dest, void *src, void *end, size_t length) {
  if (length + src >= end) {
    return true;
  }
  
  memcpy(dest, src, length);
  return false;
}

static dstr read_string(char **src, char *end) {
  char *data = *src;
  size_t len = strnlen(data, end - data);
  if (len == end - data) {
    fprintf(stderr, "Not enough bytes left for the string!\n");
    return NULL;
  } else {
    dstr str = dstr_new2(data, len);
    *src += len + 1; // Also skip the \0
    return str;
  }
}

#define READ_INT32(dest) if(end - data < 4) return true; dest = *(int *)data; data += 4

// This creates and returns a write request that must be sent to client_write before
// the main loop resumes.
static write_req *req_for_immediate_writing_to(client *c, uint8_t type,
                                               dstr doc_name, char *error) {
  write_req *req = write_req_alloc();
  
  // If the type already has the has_doc_name flag, something's gone wrong somewhere.
  assert(!(type & (MSG_FLAG_HAS_DOC_NAME | MSG_FLAG_ERROR)));
  
  if (error) type |= MSG_FLAG_ERROR;
  
  // The buffer has already skipped the length bytes (write_req_alloc takes care of that). Now
  // I need to (maybe!) write the doc_name.
  // Rules:
  //  no if doc_name is NULL
  //  yes if c->server_doc_name is NULL
  //  no if the c->server_doc_name is equal to doc_name
  //  yes otherwise.
  if (doc_name != NULL && (c->server_doc_name == NULL || !dstr_eq(doc_name, c->server_doc_name))) {
    if (c->server_doc_name) {
      dstr_release(c->server_doc_name);
    }
    c->server_doc_name = dstr_retain(doc_name);

    buf_uint8(&req->buffer, type | MSG_FLAG_HAS_DOC_NAME);
    buf_zstring_dstr(&req->buffer, doc_name);
  } else {
    buf_uint8(&req->buffer, type);
  }
  
  if (error) buf_zstring(&req->buffer, error);

  return req;
}

// Handle a pending packet. There must be a packet's worth of buffers waiting in c.
// Returning true here indicates an unrecoverable error and the client socket is terminated.
bool handle_packet(client *c) {
  // I don't know if this is the best way to do this. It would be nice to avoid extra memcpys,
  // although one large memcpy is cheaper than lots of small ones.
  
  char *data = c->packet;
  // A pointer just past the end of the packet.
  char *end = c->packet + c->packet_length;
  
  if (data == end) return true;
  uint8_t type = *(data++);
  
  uint8_t flags = type & 0xf0;
  type &= 0xf;

  if (flags & MSG_FLAG_HAS_DOC_NAME) {
    // It'd be nice to do all this without additional allocations, but I'm not really sure thats
    // possible.
    if (c->client_doc_name) {
      dstr_release(c->client_doc_name);
    }
    c->client_doc_name = read_string(&data, end);
    if (c->client_doc_name == NULL) {
      return true;
    }
  }
  
  if (type != MSG_HELLO) {
    if (c->said_hello == false) {
      fprintf(stderr, "How rude - client didn't say hi\n");
      return true;
    }

    if (c->client_doc_name == NULL) {
      fprintf(stderr, "Doc name not known\n");
      return true;
    }
  }

  switch (type) {
    case MSG_HELLO: {
      if (c->said_hello) {
        // Repeated hellos = error.
        return true;
      }
      unsigned char p_version = *(data++);
      if (p_version != PROTOCOL_VERSION) {
        // The protocol version is currently 1.
        fprintf(stderr, "Wrong protocol version - was %d expected %d\n",
                p_version, PROTOCOL_VERSION);
        return true;
      }
      printf("Oh hi\n");
      c->said_hello = true;
      
      write_req *req = req_for_immediate_writing_to(c, type, NULL, NULL);
      buf_uint8(&req->buffer, PROTOCOL_VERSION);
      c->cid = next_id++;
      buf_uint32(&req->buffer, c->cid);
      client_write(c, req);
      break;
    }
    case MSG_OPEN: {
      dstr doc_name = dstr_retain(c->client_doc_name);
      if (data == end) return true;
      uint8_t open_flags = *(data++);
      dstr doc_type = read_string(&data, end); // an empty string if the type isn't specified.
      if (doc_type == NULL) return true;
      uint32_t version;
      READ_INT32(version); // UINT32_MAX means 'use the current version'.
      client_retain(c);
      handle_open(c, doc_name, doc_type, version, open_flags,
                  ^(char *error, ot_document *doc, uint8_t open_flags) {
        write_req *req = req_for_immediate_writing_to(c, MSG_OPEN, doc_name, error);
        dstr_release(doc_name);
        dstr_release(doc_type);
        write_req *cursor_req = NULL;
        if (!error) {
          buf_uint8(&req->buffer, open_flags);
          buf_uint32(&req->buffer, doc->version);
          if (open_flags & OPEN_FLAG_SNAPSHOT) {
            buf_zstring(&req->buffer, doc->type->name);
            buf_uint64(&req->buffer, doc->ctime);
            buf_uint64(&req->buffer, doc->mtime);
            doc->type->write_doc(doc->snapshot, &req->buffer);
            // Also need to add cursors.
          }
          
          // If I want to make the packet system more complex, I could just append the cursor
          // data on the end of the open response.
          if (open_flags & OPEN_FLAG_TRACK_CURSORS) {
            cursor_req = req_for_immediate_writing_to(c, MSG_CURSOR | MSG_CURSOR_REPLACE_ALL,
                                                      doc_name, NULL);
            buffer *b = &cursor_req->buffer;
            for (open_pair *pair = doc->open_pair_head; pair; pair = pair->next_client) {
              if (pair->client != c && pair->has_cursor) { // && within timeout?
                buf_uint32(b, pair->client->cid);
                // & client name
                doc->type->write_cursor(pair->cursor, b);
              }
            }
            buf_uint32(b, 0);
          }
        }
        client_write(c, req);
        if (cursor_req) {
          client_write(c, cursor_req);
        }
        client_release(c);
      });
      break;
    }
    case MSG_OP: {
      uint32_t version;
      READ_INT32(version);
      size_t op_size = end - data;
      dstr doc_name = dstr_retain(c->client_doc_name);
      handle_op(c, version, data, op_size, ^(char *error, uint32_t applied_at) {
        write_req *req = req_for_immediate_writing_to(c, MSG_OP_ACK, doc_name, error);
        dstr_release(doc_name);
        if (!error) {
          buf_uint32(&req->buffer, applied_at);
        }
        client_write(c, req);
        
//        db_get_b(c->db, doc_name, ^(char *error, ot_document *doc) {
//          if (doc) print_doc(doc);
//        });
        
      });
      data = end;
      break;
    }
    case MSG_CLOSE: {
      // I'm using the callback style here, but handle_close is always synchronous anyway.
      char *error = handle_close(c, c->client_doc_name);
      write_req *req = req_for_immediate_writing_to(c, MSG_CLOSE, c->client_doc_name, error);
      client_write(c, req);
      break;
    }
    case MSG_CURSOR: {
      
    }
      
    default:
      // Invalid data.
      fprintf(stderr, "Invalid packet - unexpected type %d\n", type);
      return true;
  }
  
  if (data != end) {
    fprintf(stderr, "%ld trailing bytes on packet type %d\n", end - data, type);
    //    read_bytes(c, NULL, c->num_bytes - expected_remaining_bytes);
    //    return true;
  }
  
  return false;
}

void broadcast_op_to_clients(ot_document *doc, client *source, uint32_t version, ot_op *op) {
  // TODO: Speed this up by only serializing the op once, and copy the serialized bytes to each
  // subsequent client.
  for (open_pair *pair = doc->open_pair_head; pair; pair = pair->next_client) {
    if (pair->client != source) {
      write_req *req = req_for_immediate_writing_to(pair->client, MSG_OP, doc->name, NULL);
      buf_uint32(&req->buffer, version);
      doc->type->write_op(op, &req->buffer);
      client_write(pair->client, req);
    }
  }
}

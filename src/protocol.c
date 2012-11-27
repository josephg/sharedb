// This file contains all the actual network protocol code. It parses packets coming
// on the wire, decodes them and fires off the appropriate requests to the database.

#include <stdio.h>
#include <stdbool.h>
#include <assert.h>
#include "net.h"
#include "db.h"

static const uint8_t PROTOCOL_VERSION = 0;

// It might make more sense to put this function into db.h.
static void handle_open(client *client, dstr doc_name, void (^callback)(char *error)) {
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
    if (pair->next_client) {
      pair->next_client->prev_client = pair;
    }
    pair->prev_client = NULL;
    doc->open_pair_head = pair;
    
    if (callback) callback(NULL);
    client_release(client);
  });
}

void print_doc(ot_document *doc);

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
    db_apply_op_b(client->db, doc, version, &op, callback);
  });
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
static write_req *req_for_immediate_writing_to(client *c, uint8_t type, dstr doc_name) {
  write_req *req = write_req_alloc();
  
  // If the type already has the has_doc_name flag, something's gone wrong somewhere.
  assert(!(type & MSG_FLAG_HAS_DOC_NAME));
  
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

  return req;
}

// Handle a pending packet. There must be a packet's worth of buffers waiting in c.
bool handle_packet(client *c) {
  // I don't know if this is the best way to do this. It would be nice to avoid extra memcpys,
  // although one large memcpy is cheaper than lots of small ones.
  bool error = false;
  
  char *data = c->packet;
  // A pointer just past the end of the packet.
  char *end = c->packet + c->packet_length;
  
  if (data == end) return true;
  unsigned char type = *(data++);
  
  if (type & MSG_FLAG_HAS_DOC_NAME) {
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
  
  type &= 0x7f;
  
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
        fprintf(stderr, "Wrong protocol version - was %d expected %d\n", p_version, PROTOCOL_VERSION);
        return true;
      }
      printf("Oh hi\n");
      c->said_hello = true;
      
      write_req *req = req_for_immediate_writing_to(c, type, NULL);
      buf_uint8(&req->buffer, PROTOCOL_VERSION);
      client_write(c, req);
      break;
    }
    case MSG_OPEN: {
      dstr doc_name = dstr_retain(c->client_doc_name);
      handle_open(c, doc_name, ^(char *error) {
        uint8_t type = MSG_OPEN | (error ? MSG_FLAG_ERROR : 0);
        write_req *req = req_for_immediate_writing_to(c, type, doc_name);
        dstr_release(doc_name);
        if (error) {
          buf_zstring(&req->buffer, error, strlen(error));
        }
        client_write(c, req);
      });
      break;
    }
    case MSG_OP: {
      uint32_t version;
      READ_INT32(version);
      size_t op_size = end - data;
      dstr doc_name = dstr_retain(c->client_doc_name);
      handle_op(c, version, data, op_size, ^(char *error, uint32_t new_version) {
        uint8_t type = MSG_OP_APPLIED | (error ? MSG_FLAG_ERROR : 0);
        write_req *req = req_for_immediate_writing_to(c, type, doc_name);
        
        db_get_b(c->db, doc_name, ^(char *error, ot_document *doc) {
          if (doc) print_doc(doc);
        });
        
        dstr_release(doc_name);
        if (error) {
          buf_zstring(&req->buffer, error, strlen(error));
        } else {
          buf_uint32(&req->buffer, new_version);
        }
        client_write(c, req);
      });
      data = end;
      break;
    }
      
    default:
      // Invalid data.
      fprintf(stderr, "Invalid packet - unexpected type %d\n", type);
      error = true;
  }
  
  if (data != end) {
    fprintf(stderr, "%ld trailing bytes on packet type %d\n", end - data, type);
    //    read_bytes(c, NULL, c->num_bytes - expected_remaining_bytes);
    //    return true;
  }
  
  return error;
}

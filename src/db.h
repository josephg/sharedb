#ifndef share_model_h
#define share_model_h

#include "ot.h"
#include "dict.h"
#include "dstr.h"
#include "net.h"

struct client_t;

typedef void (*db_load_content_cb)(void *content, void *ctx);

typedef struct database_t {
  // Optional function which initializes document's content based on its document name.
  // If this function returns NULL, a new document is created if opt_auto_create is set.
  void (*opt_load_initial_content)(char *doc_name, ot_type *type, void *ctx, db_load_content_cb);
  
//  bool opt_auto_create;

  // Dict from doc name (string) -> ot_document.
  dict *docs;
  
  
} database;


typedef struct ot_document_t {
  const ot_type *type;
  
  void *snapshot;
  uint32_t version;
  
  size_t op_cache_capacity;
  void *op_cache;
  
  int retain_count;
  
  open_pair *open_pair_head;
  
  // op cache
  // metadata
  
  // listeners?
  // reaping timer
  
  // size_t committed_version;
  
  // bool snapshot_write_lock;
  
  // void *db_meta;
  
} ot_document;


database *db_init(database *db);
database *db_new();
void db_free(database *db);

void doc_retain(ot_document *doc);
void doc_release(ot_document *doc);

// Create a document. The document is only created if it doesn't already exist. The document
// is returned through the callback. Retain the document if you want to keep it.
//
// If the document already exists, it _may_ be passed through the callback. (It is right now, but
// this behaviour may change in future versions).
//
// Error is null if the create operation was successful.
// Valid errors:
//  Forbidden
//  Document already exists
//  Invalid document name
typedef void (*db_create_cb)(char *error, ot_document *doc, void *user);

// Create a new document with the specified name and type.
// Calls callback when complete.
// TODO: use opt_load_initial_content.
void db_create(database *, const dstr doc_name, ot_type *, void *user, db_create_cb callback);


// Valid errors:
//  Document does not exist
//  Forbidden
typedef void (*db_delete_cb)(char *error, void *user);

// Removes a document from the database
void db_delete(database *db, const dstr doc_name, void *user, db_delete_cb callback);


// Valid errors:
//  Forbidden
//  Document does not exist
typedef void (*db_get_cb)(char *error, void *user, ot_document *doc);

// Get the specified document. Returned via callback. Retain the document if you need it beyond the
// call stack.
void db_get(database *db, const dstr doc_name, void *user, db_get_cb callback);

#ifdef __BLOCKS__
typedef void (^db_get_bcb)(char *error, ot_document *doc);
// Version of db_get which uses blocks.
void db_get_b(database *db, const dstr doc_name, db_get_bcb callback);
#endif

// Valid errors:
//  Forbidden
//  Document does not exist
//  Op already submitted
//  Op at future version
//  Op too old
//  Op invalid
typedef void (*db_apply_cb)(char *error, void *user, uint32_t new_version);

// Apply an operation to the database
void db_apply_op(const database *db, ot_document *doc, uint32_t version, const ot_op *op,
                 void *user, db_apply_cb callback);

#ifdef __BLOCKS__
typedef void (^db_apply_bcb)(char *error, uint32_t new_version);
// Version of db_get which uses blocks.
void db_apply_op_b(const database *db, ot_document *doc, uint32_t version, const ot_op *op,
                   db_apply_bcb callback);
#endif

/*
// Valid errors:
//  Forbidden
//  Document does not exist
//  Versions invalid
typedef void (*db_get_ops_cb)(char *error, void *user);

void db_get_ops(database *db, const dstr doc_name, void *dest, size_t vstart, size_t vnum,
                void *user, db_get_ops_cb callback);
*/

#endif

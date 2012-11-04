#ifndef share_model_h
#define share_model_h

#include "ot.h"
#include "dict.h"
#include "sds.h"

typedef void (*db_load_content_cb)(void *content, void *ctx);

typedef struct {
  // Optional function which initializes document's content based on its document name.
  // If this function returns NULL, a new document is created if opt_auto_create is set.
  void (*opt_load_initial_content)(char *doc_name, ot_type *type, void *ctx, db_load_content_cb);
  
//  bool opt_auto_create;

  // Dict from doc name (string) -> ot_document.
  dict *docs;
  
  
} database;


typedef struct {
  const ot_type *type;
  
  void *snapshot;
  size_t version;
  
  void *op_cache;
  size_t op_cache_capacity;
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

// Error is null if the create operation was successful.
// Valid errors:
//  Forbidden
//  Document already exists
//  Invalid document name
typedef void (*db_create_cb)(char *error, void *user);

// Create a new document with the specified name and type.
// Calls callback when complete.
// TODO: use opt_load_initial_content.
void db_create(database *db, const sds doc_name, ot_type *type, void *user, db_create_cb callback);


// Valid errors:
//  Document does not exist
//  Forbidden
typedef void (*db_delete_cb)(char *error, void *user);

// Removes a document from the database
void db_delete(database *db, const sds doc_name, void *user, db_delete_cb callback);


// Valid errors:
//  Forbidden
//  Document does not exist
typedef void (*db_get_cb)(char *error, void *user, size_t version, ot_type *type, void *data);

// Get the specified document. Returned via callback.
void db_get(database *db, const sds doc_name, void *user, db_get_cb callback);


// Valid errors:
//  Forbidden
//  Document does not exist
//  Op already submitted
//  Op at future version
//  Op too old
//  Op invalid
typedef void (*db_apply_cb)(char *error, void *user, size_t newVersion);

// Apply an operation to the database
void db_apply_op(database *db, const sds doc_name, size_t version, void *op, size_t op_length,
                 void *user, db_apply_cb callback);

#endif

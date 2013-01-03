#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#ifdef __BLOCKS__
#include <Block.h>
#endif
#include <sys/time.h>

#include "db.h"
#include "protocol.h"

/*
static ot_document doc_add(database *db, char *name, ot_type *type, void *initial_data) {
  
}
*/

static unsigned int dict_dstr_hash(const void *key) {
  return dictGenHashFunction((unsigned char*)key, (int)dstr_len((dstr)key));
}

static void *dict_dstr_dup(void *data, const void *key) {
  return (void *)dstr_retain((dstr)key);
}

static int dict_dstr_key_compare(void *data, const void *key1, const void *key2) {
  return dstr_eq((dstr)key1, (dstr)key2);
}

static void dict_dstr_key_destructor(void *data, void *val) {
  dstr_release((dstr)val);
}

static void dict_val_destructor(void *data, void *obj) {
  ot_document *doc = (ot_document *)obj;
  doc->type->free(doc->snapshot);
  free(doc);
}

/* Db->document, keys are dstr strings, vals are ot_documents. */
dictType db_dict_type = {
  dict_dstr_hash,                /* hash function */
  dict_dstr_dup,                       /* key dup */
  NULL,                       /* val dup */
  dict_dstr_key_compare,          /* key compare */
  dict_dstr_key_destructor,          /* key destructor */
  dict_val_destructor               /* val destructor */
};

database *db_init(database *db) {
  db->opt_load_initial_content = NULL;
  db->docs = dictCreate(&db_dict_type, NULL);
  
  return db;
}

database *db_new() {
  return db_init(malloc(sizeof(database)));
}

void db_free(database *db) {
  dictRelease(db->docs);
  db->docs = NULL;
//  free(db);
}

void doc_retain(ot_document *doc) {
  doc->retain_count++;
}

void doc_release(ot_document *doc) {
  assert(doc->retain_count);
  doc->retain_count--;
  
  if (doc->retain_count == 0) {
    // TODO: Set a timeout to reap the document.
  }
}

// Weird place for this function, but for now its only used here.
static uint64_t time_ms() {
  struct timeval time;
  assert(gettimeofday(&time, NULL) == 0);
  return time.tv_sec * 1000 + time.tv_usec / 1000;
}

void db_create(database *db, dstr doc_name, ot_type *type,
    void *user, db_create_cb callback) {
  assert(db);
  assert(doc_name);
  assert(type);
  dictEntry *entry = dictFind(db->docs, doc_name);
  if (entry) {
    if (callback) callback("Document already exists", dictGetVal(entry), user);
  } else {
    // Create it.
    ot_document *doc = malloc(sizeof(ot_document));
    doc->name = dstr_retain(doc_name);
    doc->type = type;
    doc->snapshot = type->create(),
    doc->version = 0;
    doc->op_cache_capacity = 100;
    doc->op_cache = malloc(type->op_size * doc->op_cache_capacity);
    doc->retain_count = 0;
    doc->open_pair_head = NULL;
    doc->ctime = doc->mtime = time_ms();
    
    dictAdd(db->docs, (void *)doc_name, doc);
    if (callback) callback(NULL, doc, user);
  }
}

#ifdef __BLOCKS__
static void _create_bcb(char *error, ot_document *doc, void *user) {
  db_create_bcb cb = (db_create_bcb)user;
  cb(error, doc);
  Block_release(cb);
}

void db_create_b(database *db, const dstr doc_name, ot_type *type, db_create_bcb callback) {
  db_create(db, doc_name, type, (void *)Block_copy(callback), _create_bcb);
}
#endif

void db_delete(database *db, const dstr doc_name, void *user, db_delete_cb callback) {
  assert(db);
  assert(doc_name);
  int status = dictDelete(db->docs, doc_name);
  if (callback) callback(status == DICT_ERR ? "Doc does not exist" : NULL, user);
}

typedef float V __attribute__(( vector_size(16) ));

void db_get(database *db, const dstr doc_name, void *user, db_get_cb callback) {
  assert(db);
  assert(doc_name);
  assert(callback);
  
  dictEntry *entry = dictFind(db->docs, doc_name);
  if (!entry) {
    callback("Doc does not exist", user, NULL);
    return;
  }
  
  ot_document *doc = dictGetVal(entry);
  callback(NULL, user, doc);
}

#ifdef __BLOCKS__
static void _get_bcb(char *error, void *user, ot_document *doc) {
  db_get_bcb cb = (db_get_bcb)user;
  cb(error, doc);
  Block_release(cb);
}

void db_get_b(database *db, const dstr doc_name, db_get_bcb callback) {
  db_get(db, doc_name, (void *)Block_copy(callback), _get_bcb);
}
#endif

static void add_op_to_cache(ot_document *doc, ot_op *op) {
  size_t op_size = doc->type->op_size;
  if (doc->op_cache_capacity == doc->version) {
    doc->op_cache_capacity *= 2;
    doc->op_cache = realloc(doc->op_cache, op_size * doc->op_cache_capacity);
  }
  memcpy(doc->op_cache + op_size * doc->version, op, op_size);
}

void db_apply_op(const database *db, client *source,
                 ot_document *doc, uint32_t version, const ot_op *op,
   void *user, db_apply_cb callback) {
  assert(db);
  assert(doc);
  assert(op);
  
  if (version > doc->version) {
    if (callback) callback("Op at future version", user, 0);
    return;
  }
  
  // First, transform the op to be current.
  // TODO: Consider avoiding this copy if the op is current.
  ot_op op_local = *op;
  while (version < doc->version) {
    ot_op *other = (ot_op *)(doc->op_cache + doc->type->op_size * version);
    doc->type->transform(&op_local, &op_local, other, true);
    version++;
  }
  
  if (doc->type->check_op(doc->snapshot, &op_local)) {
    if (callback) callback("Op invalid", user, 0);
    return;
  }
  
  // Then apply it.
  doc->type->apply(doc->snapshot, &op_local);
  
  add_op_to_cache(doc, &op_local);
  
  doc->version++;
  
  doc->mtime = time_ms();
  
  // Update all the document cursors based on the applied op
  for (open_pair *pair = doc->open_pair_head; pair; pair = pair->next_client) {
    if (pair->has_cursor) {
      doc->type->transform_cursor(&pair->cursor, &op_local, pair->client == source);
    }
  }
  
  if (callback) callback(NULL, user, doc->version);
  
  // And now we need to notify our listeners.
  broadcast_op_to_clients(doc, source, doc->version, &op_local);
}

#ifdef __BLOCKS__
static void _apply_op_b_cb(char *error, void *user, uint32_t new_version) {
  db_apply_bcb cb = (db_apply_bcb)user;
  cb(error, new_version);
  Block_release(cb);
}

void db_apply_op_b(const database *db, client *source,
                   ot_document *doc, uint32_t version, const ot_op *op,
                   db_apply_bcb callback) {
  db_apply_op(db, source, doc, version, op, (void *)Block_copy(callback), _apply_op_b_cb);
}
#endif

void db_transform_cursor(ot_document *doc, ot_cursor *cursor, uint32_t version) {
  assert(doc);
  assert(cursor);
  assert(version <= doc->version);
  
  while (version < doc->version) {
    ot_op *op = (ot_op *)(doc->op_cache + doc->type->op_size * version);
    // This method gets called when a user moves their cursor around.
    doc->type->transform_cursor(cursor, op, false);
  }
}
#include "db.h"
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#ifdef __BLOCKS__
#include <Block.h>
#endif

/*
static ot_document doc_add(database *db, char *name, ot_type *type, void *initial_data) {
  
}
*/

static unsigned int dict_sds_hash(const void *key) {
  return dictGenHashFunction((unsigned char*)key, (int)sdslen((char*)key));
}

static void *dict_sds_dup(void *data, const void *key) {
  return sdsdup((const sds)key);
}

static int dict_sds_key_compare(void *data, const void *key1, const void *key2) {
  size_t l1 = sdslen((sds)key1);
  size_t l2 = sdslen((sds)key2);
  if (l1 != l2) return 0;
  return memcmp(key1, key2, l1) == 0;
}

static void dict_sds_key_destructor(void *data, void *val) {
  sdsfree(val);
}

static void dict_val_destructor(void *data, void *obj) {
  ot_document *doc = (ot_document *)obj;
  doc->type->free(doc->snapshot);
  free(doc);
}

/* Db->document, keys are sds strings, vals are Redis objects. */
dictType db_dict_type = {
  dict_sds_hash,                /* hash function */
  dict_sds_dup,                       /* key dup */
  NULL,                       /* val dup */
  dict_sds_key_compare,          /* key compare */
  dict_sds_key_destructor,          /* key destructor */
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
    // ... Set a timeout to reap the document.
  }
}

void db_create(database *db, const char *doc_name, ot_type *type,
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
    doc->type = type;
    doc->snapshot = type->create(),
    doc->version = 0;
    doc->op_cache_capacity = 100;
    doc->op_cache = malloc(type->op_size * doc->op_cache_capacity);
    doc->retain_count = 0;
    doc->open_pair_head = NULL;
    
    dictAdd(db->docs, (void *)doc_name, doc);
    if (callback) callback(NULL, doc, user);
  }
}

void db_delete(database *db, const sds doc_name, void *user, db_delete_cb callback) {
  assert(db);
  assert(doc_name);
  int status = dictDelete(db->docs, doc_name);
  if (callback) callback(status == DICT_ERR ? "Doc does not exist" : NULL, user);
}

typedef float V __attribute__(( vector_size(16) ));

void db_get(database *db, const sds doc_name, void *user, db_get_cb callback) {
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
static void _db_get_b_cb(char *error, void *user, ot_document *doc) {
  db_get_bcb cb = (db_get_bcb)user;
  cb(error, doc);
  Block_release(cb);
}

void db_get_b(database *db, const sds doc_name, db_get_bcb callback) {
  db_get(db, doc_name, (void *)Block_copy(callback), _db_get_b_cb);
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

void db_apply_op(const database *db, ot_document *doc, size_t version, const ot_op *op,
   void *user, db_apply_cb callback) {
  assert(db);
  assert(doc);
  assert(op);
  
  if (version > doc->version) {
    if (callback) callback("Op at future version", user, 0);
    return;
  }
  
  // First, transform the op to be current.
  ot_op op_local = *op;
  while (version < doc->version) {
    ot_op *other = (ot_op *)(doc->op_cache + doc->type->op_size * version);
    doc->type->transform(&op_local, &op_local, other, true);
    version++;
  }
  
  if (doc->type->check(doc->snapshot, op)) {
    if (callback) callback("Op invalid", user, 0);
    return;
  }
  
  // Then apply it.
  doc->type->apply(doc->snapshot, &op_local);
  
  add_op_to_cache(doc, &op_local);
  
  doc->version++;
  
  if (callback) callback(NULL, user, doc->version);
}

#ifdef __BLOCKS__
static void _apply_op_b_cb(char *error, void *user, size_t new_version) {
  db_apply_bcb cb = (db_apply_bcb)user;
  cb(error, new_version);
  Block_release(cb);
}

void db_apply_op_b(const database *db, ot_document *doc, size_t version, const ot_op *op,
                   db_apply_bcb callback) {
  db_apply_op(db, doc, version, op, (void *)Block_copy(callback), _apply_op_b_cb);
}
#endif
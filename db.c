#include "db.h"
#include <string.h>
#include <stdlib.h>
#include <assert.h>

/*
static ot_document doc_add(database *db, char *name, ot_type *type, void *initial_data) {
  
}
*/

static unsigned int dict_sds_hash(const void *key) {
  return dictGenHashFunction((unsigned char*)key, (int)sdslen((char*)key));
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
  ot_document *doc = (ot_document *)data;
  doc->type->free(doc->snapshot);
  free(doc);
}

/* Db->document, keys are sds strings, vals are Redis objects. */
dictType db_dict_type = {
  dict_sds_hash,                /* hash function */
  NULL,                       /* key dup */
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
  free(db);
}

void db_create(database *db, const sds docName, ot_type *type,
    void *user, db_create_cb callback) {
  assert(db);
  assert(docName);
  assert(type);
  dictEntry *entry = dictFind(db->docs, docName);
  if (entry) {
    if (callback) {
      callback("Document already exists", user);
    }
  } else {
    // Create it.
    ot_document *doc = malloc(sizeof(ot_document));
    doc->type = type;
    doc->snapshot = type->create(),
    doc->version = 0;
    dictAdd(db->docs, sdsdup(docName), doc);
    if (callback) {
      callback(NULL, user);
    }
  }
}

void db_delete(database *db, const sds docName, void *user, db_delete_cb callback) {
  assert(db);
  assert(docName);
  int status = dictDelete(db->docs, docName);
  if (callback) {
    callback(status == DICT_ERR ? "Document does not exist" : NULL, user);
  }
}

void db_get(database *db, const sds docName, void *user, db_get_cb callback) {
  assert(docName);
  assert(callback);
  
  dictEntry *entry = dictFind(db->docs, docName);
  if (!entry) {
    callback("Document does not exist", user, 0, NULL, NULL);
    return;
  }
  
  ot_document *doc = dictGetVal(entry);
  callback(NULL, user, doc->version, doc->type, doc->snapshot);
}

void db_apply_op(database *db, const sds docName) {
  
}

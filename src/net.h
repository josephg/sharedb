
#ifndef share_net_h
#define share_net_h

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
typedef struct open_doc_pair_t {
  struct client_t *client;
  struct ot_document_t *doc;
  
  // The next open_doc for this client
  struct open_doc_pair_t *next;
  
  struct open_doc_pair_t *prev_client;
  struct open_doc_pair_t *next_client;
  
} open_pair;

void net_listen(struct database_t *db, struct uv_loop_s *loop, int port);

#endif

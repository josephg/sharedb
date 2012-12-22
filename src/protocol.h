#ifndef share_protocol_h
#define share_protocol_h

#include <stdint.h>

// There's no real problem #including a bunch of stuff here, but I appreciate my fast compiles.

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

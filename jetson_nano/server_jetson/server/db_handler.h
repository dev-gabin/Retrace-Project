#ifndef RETRACE_DB_HANDLER_H
#define RETRACE_DB_HANDLER_H

#include "db_store.h"
#include <pthread.h>

/* Process one already-framed database command. Returns 1 if handled, 0 if the
 * payload is not a database command, or -1 if replying failed. Replies are written to the requesting
 * client and serialized with the same per-client lock as server relays. */
int db_handler_handle(RtStore *store, int socket_fd, const char *requester,
                      char *payload, pthread_mutex_t *send_lock);

#endif /* RETRACE_DB_HANDLER_H */

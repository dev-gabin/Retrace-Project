#ifndef RETRACE_SERVER_H
#define RETRACE_SERVER_H

#include "../database/db_store.h"

/* Network server settings. */
typedef struct {
	int port;
	int max_clients;
	const char *credentials_file;
} ServerConfig;

/*
 * Start the Retrace network server.
 *
 * config: server settings; port must be in 1..65535, max_clients must be > 0.
 * database: connection and snapshot configuration. Each client worker opens
 *            its own RtStore connection and closes it on disconnect.
 *
 * Returns 0 when the server stops normally, or -1 on error.
 */
int server_run(const ServerConfig *config, const RtConfig *database);

#endif /* RETRACE_SERVER_H */

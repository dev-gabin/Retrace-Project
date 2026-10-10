#ifndef RETRACE_TCP_CLIENT_H
#define RETRACE_TCP_CLIENT_H

#include <stddef.h>

/* Connects and authenticates with the current server's [ID:PASSWORD] frame. */
int tcp_client_connect(const char *host, const char *port,
                       const char *client_id, const char *password);

/* Sends one line using the server's [TARGET]payload\n routing envelope. */
int tcp_client_send(int socket_fd, const char *target,
                    const char *payload, size_t payload_size);

/* Views a received [SENDER]payload line without modifying it. */
int tcp_client_unwrap(const char *line, size_t line_size,
                      const char **sender, size_t *sender_size,
                      const char **payload, size_t *payload_size);

#endif

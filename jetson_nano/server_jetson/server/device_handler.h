#ifndef RETRACE_DEVICE_HANDLER_H
#define RETRACE_DEVICE_HANDLER_H

#include <stddef.h>

/*
 * Validate and translate supported application SET commands for a device.
 * SET@clientID:OPEN:n and SET@clientID:BUZZER route to clientID and are
 * forwarded as clientID:OPEN:n and clientID:BUZZER, respectively.
 * Returns 1 for a translated SET, 0 when payload is not a SET command, or -1
 * for an invalid/unsupported SET.
 */
int device_handler_translate_set(const char *payload,
                                 char *device_id, size_t device_id_capacity,
                                 char *device_command,
                                 size_t device_command_capacity,
                                 char *reason, size_t reason_capacity);

#endif /* RETRACE_DEVICE_HANDLER_H */

#include "device_handler.h"

#include <stdio.h>
#include <string.h>

static int invalid(char *reason, size_t capacity, const char *token)
{
    if (reason != NULL && capacity > 0)
        snprintf(reason, capacity, "%s", token);
    return -1;
}

int device_handler_translate_set(const char *payload,
                                 char *device_id, size_t device_id_capacity,
                                 char *device_command,
                                 size_t device_command_capacity,
                                 char *reason, size_t reason_capacity)
{
    const char *target;
    const char *action;
    const char *number;
    const char *first_colon;
    const char *second_colon;
    long drawer_number;
    size_t target_length;
    size_t action_length;
    size_t i;

    if (reason != NULL && reason_capacity > 0)
        reason[0] = '\0';
    if (payload == NULL || strncmp(payload, "SET@", 4) != 0)
        return 0;
    if (device_id == NULL || device_id_capacity == 0 ||
        device_command == NULL || device_command_capacity == 0)
        return invalid(reason, reason_capacity, "INTERNAL");

    target = payload + 4;
    first_colon = strchr(target, ':');
    if (first_colon == NULL)
        return invalid(reason, reason_capacity, "FORMAT");
    action = first_colon + 1;
    second_colon = strchr(action, ':');
    target_length = (size_t)(first_colon - target);
    action_length = second_colon != NULL
                        ? (size_t)(second_colon - action)
                        : strlen(action);
    if (target_length == 0 || target_length >= device_id_capacity)
        return invalid(reason, reason_capacity, "TARGET");
    for (i = 0; i < target_length; ++i) {
        unsigned char ch = (unsigned char)target[i];
        if (!((ch >= 'A' && ch <= 'Z') ||
              (ch >= 'a' && ch <= 'z') ||
              (ch >= '0' && ch <= '9') || ch == '_' || ch == '-'))
            return invalid(reason, reason_capacity, "TARGET");
    }

    /* Keep the legacy no-argument BUZZER command, and also accept an explicit
       1/0 state so the web UI can start or stop the tag. */
    if (action_length == sizeof("BUZZER") - 1 &&
        memcmp(action, "BUZZER", action_length) == 0) {
        const char *state = second_colon != NULL ? second_colon + 1 : NULL;
        if (state != NULL &&
            (state[0] == '\0' || state[1] != '\0' ||
             (state[0] != '0' && state[0] != '1')))
            return invalid(reason, reason_capacity, "FORMAT");
        memcpy(device_id, target, target_length);
        device_id[target_length] = '\0';
        int command_length = state == NULL
                                 ? snprintf(device_command,
                                            device_command_capacity,
                                            "%.*s:BUZZER",
                                            (int)target_length, target)
                                 : snprintf(device_command,
                                            device_command_capacity,
                                            "%.*s:BUZZER:%c",
                                            (int)target_length, target,
                                            state[0]);
        if (command_length < 0 ||
            (size_t)command_length >= device_command_capacity)
            return invalid(reason, reason_capacity, "INTERNAL");
        return 1;
    }

    if (second_colon == NULL || strchr(second_colon + 1, ':') != NULL)
        return invalid(reason, reason_capacity, "FORMAT");
    number = second_colon + 1;
    if (action_length != sizeof("OPEN") - 1 ||
        memcmp(action, "OPEN", action_length) != 0)
        return invalid(reason, reason_capacity, "ACTION");
    if (number[0] == '\0' || number[1] != '\0' ||
        number[0] < '0' || number[0] > '9')
        return invalid(reason, reason_capacity, "FORMAT");
    drawer_number = number[0] - '0';
    if (drawer_number < 1 || drawer_number > 6)
        return invalid(reason, reason_capacity, "RANGE");
    /* The SET target is the authenticated client ID; do not hard-code it. */
    memcpy(device_id, target, target_length);
    device_id[target_length] = '\0';
    int command_length = snprintf(device_command, device_command_capacity,
                                  "%.*s:OPEN:%ld", (int)target_length,
                                  target, drawer_number);
    if (command_length < 0 ||
        (size_t)command_length >= device_command_capacity)
        return invalid(reason, reason_capacity, "INTERNAL");
    return 1;
}

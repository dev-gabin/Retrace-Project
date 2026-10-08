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
    if (second_colon == NULL || strchr(second_colon + 1, ':') != NULL)
        return invalid(reason, reason_capacity, "FORMAT");
    number = second_colon + 1;

    target_length = (size_t)(first_colon - target);
    action_length = (size_t)(second_colon - action);
    if (target_length != sizeof("DRAWER") - 1 ||
        memcmp(target, "DRAWER", target_length) != 0)
        return invalid(reason, reason_capacity, "TARGET");
    if (action_length != sizeof("OPEN") - 1 ||
        memcmp(action, "OPEN", action_length) != 0)
        return invalid(reason, reason_capacity, "ACTION");
    if (number[0] == '\0' || number[1] != '\0' ||
        number[0] < '0' || number[0] > '9')
        return invalid(reason, reason_capacity, "FORMAT");
    drawer_number = number[0] - '0';
    if (drawer_number < 1 || drawer_number > 6)
        return invalid(reason, reason_capacity, "RANGE");
    if (snprintf(device_id, device_id_capacity, "DRAWER") < 0 ||
        strlen("DRAWER") >= device_id_capacity)
        return invalid(reason, reason_capacity, "INTERNAL");
    int command_length = snprintf(device_command, device_command_capacity,
                                  "DRAWER:OPEN:%ld", drawer_number);
    if (command_length < 0 ||
        (size_t)command_length >= device_command_capacity)
        return invalid(reason, reason_capacity, "INTERNAL");
    return 1;
}

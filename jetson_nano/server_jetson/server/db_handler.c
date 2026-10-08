#define _POSIX_C_SOURCE 200809L

#include "db_handler.h"

#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define SQL_REPLY_SIZE 2048
#define SQL_LIST_LIMIT 10000
#define SAVE_FIELD_COUNT 7

static int send_all(int fd, const char *data, size_t size)
{
    size_t sent = 0;
    while (sent < size) {
        ssize_t n = send(fd, data + sent, size - sent, MSG_NOSIGNAL);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return -1;
        sent += (size_t)n;
    }
    return 0;
}

static int reply(pthread_mutex_t *lock, int fd, const char *requester,
                 const char *format, ...)
{
    char body[SQL_REPLY_SIZE];
    char line[SQL_REPLY_SIZE + 64];
    va_list args;
    int body_len;
    int line_len;

    va_start(args, format);
    body_len = vsnprintf(body, sizeof(body), format, args);
    va_end(args);
    if (body_len < 0 || (size_t)body_len >= sizeof(body))
        return -1;
    (void)requester;
    line_len = snprintf(line, sizeof(line), "[SQL]%s", body);
    if (line_len < 0 || (size_t)line_len >= sizeof(line))
        return -1;

    pthread_mutex_lock(lock);
    int rc = send_all(fd, line, (size_t)line_len);
    pthread_mutex_unlock(lock);
    return rc;
}

static char *next_field(char **cursor)
{
    char *field;
    char *separator;
    if (cursor == NULL || *cursor == NULL)
        return NULL;
    field = *cursor;
    separator = strchr(field, ':');
    if (separator != NULL) {
        *separator = '\0';
        *cursor = separator + 1;
    } else {
        *cursor = NULL;
    }
    return field;
}

static void encode_timestamp(const char *timestamp, char encoded[32])
{
    char *out = encoded;
    for (const char *p = timestamp; *p != '\0'; ++p) {
        if (*p == ':') {
            *out++ = '%';
            *out++ = '3';
            *out++ = 'A';
        } else {
            *out++ = *p;
        }
    }
    *out = '\0';
}

static void decode_timestamp(char *timestamp)
{
    char *read = timestamp;
    char *write = timestamp;
    while (*read != '\0') {
        if (read[0] == '%' && read[1] == '3' && read[2] == 'A') {
            *write++ = ':';
            read += 3;
        } else {
            *write++ = *read++;
        }
    }
    *write = '\0';
}

static int parse_int(const char *text, int minimum, int maximum, int *value)
{
    char *end;
    long parsed;
    if (text == NULL || *text == '\0')
        return -1;
    errno = 0;
    parsed = strtol(text, &end, 10);
    if (errno != 0 || *end != '\0' || parsed < minimum || parsed > maximum)
        return -1;
    *value = (int)parsed;
    return 0;
}

static int send_error(RtStore *store, int fd, const char *requester,
                      pthread_mutex_t *lock, const char *command,
                      const char *detail)
{
    (void)store;
    fprintf(stderr, "[%s] %s: %s\n", requester, command, detail);
    return reply(lock, fd, requester, "ERR@%s:%s\n", command, detail);
}

static int handled(int result)
{
    return result < 0 ? -1 : 1;
}

static int send_record(pthread_mutex_t *lock, int fd, const char *requester,
                       const char *kind, const RtRecord *record)
{
    const char *timestamp = record->observed ? record->seen_at : "-";
    const char *snapshot = record->snapshot[0] ? record->snapshot : "-";
    char encoded_timestamp[32];
    encode_timestamp(timestamp, encoded_timestamp);
    if (record->has_position) {
        return reply(lock, fd, requester,
                     "%s@%s:%s:%d:%d:%d:%s:%s:%d\n", kind,
                     record->item, record->state, record->observed,
                     record->pos_x, record->pos_y, encoded_timestamp,
                     snapshot, record->drawer_id);
    }
    return reply(lock, fd, requester,
                 "%s@%s:%s:%d:-:-:%s:%s:%d\n", kind,
                 record->item, record->state, record->observed,
                 encoded_timestamp, snapshot, record->drawer_id);
}

static int handle_list(RtStore *store, int fd, const char *requester,
                       pthread_mutex_t *lock)
{
    RtRecord *records = NULL;
    size_t count = 0;
    int rc = rt_list(store, &records, &count);
    if (rc != RT_OK)
        return send_error(store, fd, requester, lock, "LIST", rt_error(store));
    if (count > SQL_LIST_LIMIT) {
        free(records);
        return send_error(store, fd, requester, lock, "LIST",
                          "Catalog exceeds the response limit");
    }
    for (size_t i = 0; i < count; ++i) {
        if (send_record(lock, fd, requester, "LIST_ITEM", &records[i]) != 0) {
            free(records);
            return -1;
        }
    }
    free(records);
    return reply(lock, fd, requester, "LIST_END@%zu\n", count);
}

static int handle_get(RtStore *store, int fd, const char *requester,
                      pthread_mutex_t *lock, char *arguments)
{
    char *cursor = arguments;
    char *item = next_field(&cursor);
    char *mode = next_field(&cursor);
    RtRecord record;
    int rc;

    if (item == NULL || *item == '\0' || cursor != NULL)
        return send_error(store, fd, requester, lock, "GET",
                          "Usage: GET@ITEM[:SNAPSHOT|XYXY]");
    rc = rt_get(store, item, &record);
    if (rc == RT_NOT_FOUND)
        return send_error(store, fd, requester, lock, "GET", "RT_NOT_FOUND");
    if (rc != RT_OK)
        return send_error(store, fd, requester, lock, "GET", rt_error(store));

    if (mode != NULL && strcmp(mode, "SNAPSHOT") == 0)
        return reply(lock, fd, requester, "GET@%s:%s\n", record.item,
                     record.snapshot[0] ? record.snapshot : "-");
    if (mode != NULL && strcmp(mode, "XYXY") == 0) {
        if (record.has_position)
            return reply(lock, fd, requester, "GET@%s:%d:%d\n", record.item,
                         record.pos_x, record.pos_y);
        return reply(lock, fd, requester, "GET@%s:-:-\n", record.item);
    }
    if (mode != NULL)
        return send_error(store, fd, requester, lock, "GET", "Unknown GET mode");
    return send_record(lock, fd, requester, "GET", &record);
}

static int handle_save(RtStore *store, int fd, const char *requester,
                       pthread_mutex_t *lock, char *arguments)
{
    char *fields[SAVE_FIELD_COUNT];
    char *cursor = arguments;
    int x;
    int y;
    int drawer;

    for (size_t i = 0; i < SAVE_FIELD_COUNT; ++i)
        fields[i] = next_field(&cursor);
    if (cursor != NULL || fields[SAVE_FIELD_COUNT - 1] == NULL)
        return send_error(store, fd, requester, lock, "SAVE",
                          "Usage: SAVE@ITEM:SNAPSHOT:X:Y:UTC_TIME:DRAWER:STATE (encode time colons as %3A)");
    for (size_t i = 0; i < SAVE_FIELD_COUNT; ++i) {
        if (fields[i] == NULL || fields[i][0] == '\0')
            return send_error(store, fd, requester, lock, "SAVE",
                              "SAVE fields cannot be empty");
    }
    decode_timestamp(fields[4]);
    if (parse_int(fields[2], 0, INT_MAX, &x) != 0 ||
        parse_int(fields[3], 0, INT_MAX, &y) != 0 ||
        parse_int(fields[5], 0, 6, &drawer) != 0)
        return send_error(store, fd, requester, lock, "SAVE",
                          "Invalid X, Y, or DRAWER");

    int rc = rt_save(store, fields[0], x, y, fields[4], drawer, fields[6],
                     fields[1]);
    if (rc == RT_OK)
        return reply(lock, fd, requester, "SAVE@OK:%s\n", fields[0]);
    if (rc == RT_CLEANUP_PENDING)
        return reply(lock, fd, requester, "SAVE@OK_CLEANUP_PENDING:%s\n",
                     fields[0]);
    if (rc == RT_STALE)
        return reply(lock, fd, requester, "SAVE@STALE:%s\n", fields[0]);
    if (rc == RT_COMMIT_UNKNOWN)
        return send_error(store, fd, requester, lock, "SAVE",
                          "RT_COMMIT_UNKNOWN; verify the row before retrying");
    return send_error(store, fd, requester, lock, "SAVE", rt_error(store));
}

int db_handler_handle(RtStore *store, int socket_fd, const char *requester,
                      char *payload, pthread_mutex_t *send_lock)
{
    char *arguments;
    char *separator;

    if (store == NULL || requester == NULL || payload == NULL ||
        send_lock == NULL)
        return -1;
    separator = strchr(payload, '@');
    if (separator != NULL) {
        *separator = '\0';
        arguments = separator + 1;
    } else {
        arguments = NULL;
    }

    if (strcmp(payload, "LIST") == 0)
        return handled(arguments == NULL
            ? handle_list(store, socket_fd, requester, send_lock)
            : send_error(store, socket_fd, requester, send_lock, "LIST",
                         "LIST does not take arguments"));
    if (strcmp(payload, "GET") == 0)
        return handled(arguments == NULL
            ? send_error(store, socket_fd, requester, send_lock, "GET",
                         "Usage: GET@ITEM[:SNAPSHOT|XYXY]")
            : handle_get(store, socket_fd, requester, send_lock, arguments));
    if (strcmp(payload, "SAVE") == 0)
        return handled(arguments == NULL
            ? send_error(store, socket_fd, requester, send_lock, "SAVE",
                         "Usage: SAVE@ITEM:SNAPSHOT:X:Y:UTC_TIME:DRAWER:STATE (encode time colons as %3A)")
            : handle_save(store, socket_fd, requester, send_lock, arguments));
    if (separator != NULL)
        *separator = '@';
    return 0;
}

#define _POSIX_C_SOURCE 200809L

#include "tcp_client.h"

#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define CLIENT_ID_MAX 31
#define PASSWORD_MAX 127
#define LOGIN_RESPONSE_MAX 4096
#define SERVER_TARGET_MAX 31
#define SERVER_LINE_MAX 4095

static int send_all(int fd, const void *data, size_t size)
{
    const char *cursor = data;
    while (size > 0) {
        ssize_t sent = send(fd, cursor, size, MSG_NOSIGNAL);
        if (sent < 0 && errno == EINTR)
            continue;
        if (sent <= 0)
            return -1;
        cursor += sent;
        size -= (size_t)sent;
    }
    return 0;
}

static int valid_field(const char *text, size_t max_length)
{
    size_t length;
    if (text == NULL || (length = strlen(text)) == 0 || length > max_length)
        return 0;
    return strpbrk(text, "]:[]\r\n") == NULL;
}

static int read_login_response(int fd)
{
    char response[LOGIN_RESPONSE_MAX];
    size_t used = 0;

    while (used < sizeof(response) - 1) {
        char byte;
        ssize_t received = recv(fd, &byte, 1, 0);
        if (received < 0 && errno == EINTR)
            continue;
        if (received <= 0)
            return -1;
        if (byte == '\n')
            break;
        if (byte != '\r')
            response[used++] = byte;
    }
    response[used] = '\0';
    if (strstr(response, "Authentication Error") != NULL ||
        strstr(response, "Already logged") != NULL ||
        strstr(response, "Server is full") != NULL ||
        strstr(response, "ERR@DATABASE") != NULL) {
        fprintf(stderr, "server rejected/failed login: %s\n", response);
        return -1;
    }
    if (strstr(response, "New connected!") == NULL) {
        fprintf(stderr, "unexpected server login response: %s\n", response);
        return -1;
    }
    fprintf(stderr, "%s\n", response);
    return 0;
}

int tcp_client_connect(const char *host, const char *port,
                       const char *client_id, const char *password)
{
    struct addrinfo hints = {0};
    struct addrinfo *addresses = NULL;
    struct addrinfo *address;
    char login[CLIENT_ID_MAX + PASSWORD_MAX + 5];
    int login_size;
    int fd = -1;
    int result;

    if (!valid_field(client_id, CLIENT_ID_MAX) ||
        !valid_field(password, PASSWORD_MAX)) {
        fprintf(stderr, "invalid TCP client ID or password format\n");
        return -1;
    }
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    result = getaddrinfo(host, port, &hints, &addresses);
    if (result != 0) {
        fprintf(stderr, "resolve %s: %s\n", host, gai_strerror(result));
        return -1;
    }
    for (address = addresses; address != NULL; address = address->ai_next) {
        fd = socket(address->ai_family, address->ai_socktype,
                    address->ai_protocol);
        if (fd < 0)
            continue;
        if (connect(fd, address->ai_addr, address->ai_addrlen) == 0)
            break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(addresses);
    if (fd < 0) {
        perror("connect server");
        return -1;
    }

    login_size = snprintf(login, sizeof(login), "[%s:%s]", client_id,
                          password);
    if (login_size < 0 || (size_t)login_size >= sizeof(login) ||
        send_all(fd, login, (size_t)login_size) != 0 ||
        read_login_response(fd) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

int tcp_client_send(int socket_fd, const char *target,
                    const char *payload, size_t payload_size)
{
    char *line;
    size_t target_size;
    size_t line_size;
    int result;

    if (!valid_field(target, SERVER_TARGET_MAX) || payload == NULL ||
        payload_size > SERVER_LINE_MAX ||
        (payload_size > 0 && memchr(payload, '\n', payload_size) != NULL))
        return -1;
    target_size = strlen(target);
    line_size = target_size + payload_size + 3; /* [target]payload\n */
    if (line_size > SERVER_LINE_MAX)
        return -1;
    line = malloc(line_size);
    if (line == NULL)
        return -1;
    line[0] = '[';
    memcpy(line + 1, target, target_size);
    line[target_size + 1] = ']';
    memcpy(line + target_size + 2, payload, payload_size);
    line[line_size - 1] = '\n';
    result = send_all(socket_fd, line, line_size);
    free(line);
    return result;
}

int tcp_client_unwrap(const char *line, size_t line_size,
                      const char **sender, size_t *sender_size,
                      const char **payload, size_t *payload_size)
{
    const char *closing;
    if (line == NULL || sender == NULL || sender_size == NULL ||
        payload == NULL || payload_size == NULL || line_size < 3 ||
        line[0] != '[')
        return -1;
    closing = memchr(line, ']', line_size);
    if (closing == NULL || closing == line + 1)
        return -1;
    *sender = line + 1;
    *sender_size = (size_t)(closing - *sender);
    *payload = closing + 1;
    *payload_size = line_size - (size_t)(*payload - line);
    return 0;
}

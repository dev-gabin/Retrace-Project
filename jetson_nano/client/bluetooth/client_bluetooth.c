#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "../common/tcp_client.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#define TCP_LINE_MAX 8192
#define SERIAL_LINE_MAX 32

typedef struct {
    char data[TCP_LINE_MAX];
    size_t used;
    bool dropping;
} LineBuffer;

static volatile sig_atomic_t stopping;

static void on_signal(int signal_number)
{
    (void)signal_number;
    stopping = 1;
}

static int write_serial_all(int fd, const void *data, size_t size)
{
    const char *cursor = data;

    while (size > 0) {
        ssize_t written = write(fd, cursor, size);
        if (written < 0 && errno == EINTR)
            continue;
        if (written <= 0)
            return -1;
        cursor += written;
        size -= (size_t)written;
    }
    return 0;
}

static int open_serial(const char *path)
{
    struct termios options;
    int fd = open(path, O_RDWR | O_NOCTTY | O_CLOEXEC);

    if (fd < 0) {
        perror(path);
        return -1;
    }
    if (tcgetattr(fd, &options) != 0) {
        perror("tcgetattr");
        close(fd);
        return -1;
    }
    cfmakeraw(&options);
    if (cfsetispeed(&options, B9600) != 0 ||
        cfsetospeed(&options, B9600) != 0) {
        perror("set serial speed");
        close(fd);
        return -1;
    }
    options.c_cflag &= ~(PARENB | CSTOPB | CSIZE);
    options.c_cflag |= CS8 | CLOCAL | CREAD;
    options.c_cc[VMIN] = 1;
    options.c_cc[VTIME] = 0;
    if (tcsetattr(fd, TCSANOW, &options) != 0) {
        perror("tcsetattr");
        close(fd);
        return -1;
    }
    return fd;
}

static int send_serial_line_to_server(int socket_fd, const char *line,
                                      size_t length)
{
    if (length > SERIAL_LINE_MAX - 1) {
        fprintf(stderr, "drop oversized STM32 line (%zu bytes)\n", length);
        return 0;
    }
    return tcp_client_send(socket_fd, "SERVER", line, length);
}

static int send_server_line_to_serial(int serial_fd, char *line, size_t length)
{
    const char *sender;
    const char *payload;
    size_t sender_size;
    size_t payload_size;

    if (tcp_client_unwrap(line, length, &sender, &sender_size,
                          &payload, &payload_size) != 0) {
        fprintf(stderr, "drop malformed server line\n");
        return 0;
    }
    (void)sender;
    (void)sender_size;
    if (payload_size == 0 ||
        (payload_size >= strlen("New connected!") &&
         memcmp(payload, "New connected!", strlen("New connected!")) == 0))
        return 0;
    if (payload_size > SERIAL_LINE_MAX - 1) {
        fprintf(stderr, "drop oversized command for STM32 (%zu bytes)\n",
                payload_size);
        return 0;
    }
    if (write_serial_all(serial_fd, payload, payload_size) != 0 ||
        write_serial_all(serial_fd, "\n", 1) != 0)
        return -1;
    return 0;
}

static int consume_lines(int source_fd, int destination_fd, bool from_serial,
                         LineBuffer *buffer)
{
    char chunk[512];
    ssize_t received = read(source_fd, chunk, sizeof(chunk));
    if (received == 0)
        return -1;
    if (received < 0) {
        if (errno == EINTR || errno == EAGAIN)
            return 0;
        return -1;
    }

    for (ssize_t i = 0; i < received; ++i) {
        char byte = chunk[i];
        if (byte == '\r')
            continue;
        if (byte == '\n') {
            if (!buffer->dropping) {
                size_t length = buffer->used;
                buffer->data[length] = '\0';
                if (from_serial) {
                    if (send_serial_line_to_server(destination_fd,
                                                   buffer->data, length) != 0)
                        return -1;
                } else {
                    if (send_server_line_to_serial(destination_fd,
                                                   buffer->data, length) != 0)
                        return -1;
                }
            }
            buffer->used = 0;
            buffer->dropping = false;
            continue;
        }
        if (buffer->dropping)
            continue;
        if (buffer->used >= sizeof(buffer->data) - 1 ||
            (from_serial && buffer->used >= SERIAL_LINE_MAX - 1)) {
            fprintf(stderr, "drop overlong %s line\n",
                    from_serial ? "STM32" : "server");
            buffer->used = 0;
            buffer->dropping = true;
            continue;
        }
        buffer->data[buffer->used++] = byte;
    }
    return 0;
}

int main(int argc, char **argv)
{
    const char *password = getenv("RETRACE_CLIENT_PASSWORD");
    int socket_fd = -1;
    int serial_fd = -1;
    LineBuffer serial_buffer = {0};
    LineBuffer socket_buffer = {0};

    if (argc != 5 || password == NULL || *password == '\0') {
        fprintf(stderr,
                "Usage: RETRACE_CLIENT_PASSWORD=<secret> %s "
                "<server-ip-or-host> <port> <client-id> <rfcomm-device>\n",
                argv[0]);
        return EXIT_FAILURE;
    }
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    socket_fd = tcp_client_connect(argv[1], argv[2], argv[3], password);
    if (socket_fd < 0)
        goto fail;
    serial_fd = open_serial(argv[4]);
    if (serial_fd < 0)
        goto fail;

    fprintf(stderr, "Bluetooth SPP bridge ready: client=%s serial=%s baud=9600\n",
            argv[3], argv[4]);
    while (!stopping) {
        struct pollfd descriptors[2] = {
            {.fd = socket_fd, .events = POLLIN},
            {.fd = serial_fd, .events = POLLIN}
        };
        int ready = poll(descriptors, 2, 500);
        if (ready < 0) {
            if (errno == EINTR)
                continue;
            perror("poll");
            goto fail;
        }
        if (ready == 0)
            continue;
        if (descriptors[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
            fprintf(stderr, "server connection closed\n");
            goto fail;
        }
        if (descriptors[1].revents & (POLLERR | POLLHUP | POLLNVAL)) {
            fprintf(stderr, "HC-06 serial connection closed\n");
            goto fail;
        }
        if ((descriptors[0].revents & POLLIN) &&
            consume_lines(socket_fd, serial_fd, false, &socket_buffer) != 0) {
            fprintf(stderr, "server disconnected\n");
            goto fail;
        }
        if ((descriptors[1].revents & POLLIN) &&
            consume_lines(serial_fd, socket_fd, true, &serial_buffer) != 0) {
            fprintf(stderr, "HC-06 serial disconnected\n");
            goto fail;
        }
    }

    if (serial_fd >= 0)
        close(serial_fd);
    if (socket_fd >= 0)
        close(socket_fd);
    return EXIT_SUCCESS;

fail:
    if (serial_fd >= 0)
        close(serial_fd);
    if (socket_fd >= 0)
        close(socket_fd);
    return EXIT_FAILURE;
}

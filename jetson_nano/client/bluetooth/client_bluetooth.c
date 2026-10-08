#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "../common/tcp_client.h"

#include <bluetooth/bluetooth.h>
#include <bluetooth/rfcomm.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define TCP_LINE_MAX 8192
#define BLUETOOTH_LINE_MAX 32

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

static int write_bluetooth_all(int fd, const void *data, size_t size)
{
    const char *cursor = data;

    while (size > 0) {
        ssize_t written = send(fd, cursor, size, MSG_NOSIGNAL);
        if (written < 0 && errno == EINTR)
            continue;
        if (written <= 0)
            return -1;
        cursor += written;
        size -= (size_t)written;
    }
    return 0;
}

static int connect_rfcomm(const char *mac_address, unsigned long channel)
{
    struct sockaddr_rc address = {0};
    int fd;

    if (channel < 1 || channel > 30) {
        fprintf(stderr, "invalid RFCOMM channel: %lu\n", channel);
        return -1;
    }
    fd = socket(AF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM);
    if (fd < 0) {
        perror("create Bluetooth RFCOMM socket");
        return -1;
    }
    address.rc_family = AF_BLUETOOTH;
    address.rc_channel = (uint8_t)channel;
    if (str2ba(mac_address, &address.rc_bdaddr) != 0) {
        fprintf(stderr, "invalid Bluetooth MAC address: %s\n", mac_address);
        close(fd);
        return -1;
    }
    if (connect(fd, (struct sockaddr *)&address, sizeof(address)) != 0) {
        perror("connect HC-06 RFCOMM");
        close(fd);
        return -1;
    }
    return fd;
}

static int send_serial_line_to_server(int socket_fd, const char *line,
                                      size_t length)
{
    if (length > BLUETOOTH_LINE_MAX - 1) {
        fprintf(stderr, "drop oversized STM32 line (%zu bytes)\n", length);
        return 0;
    }
    return tcp_client_send(socket_fd, "SERVER", line, length);
}

static int send_server_line_to_bluetooth(int bluetooth_fd, char *line,
                                         size_t length)
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
    if (payload_size > BLUETOOTH_LINE_MAX - 1) {
        fprintf(stderr, "drop oversized command for STM32 (%zu bytes)\n",
                payload_size);
        return 0;
    }
    if (write_bluetooth_all(bluetooth_fd, payload, payload_size) != 0 ||
        write_bluetooth_all(bluetooth_fd, "\n", 1) != 0)
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
                    if (send_server_line_to_bluetooth(destination_fd,
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
            (from_serial && buffer->used >= BLUETOOTH_LINE_MAX - 1)) {
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
    int bluetooth_fd = -1;
    char *channel_end = NULL;
    unsigned long channel;
    LineBuffer bluetooth_buffer = {0};
    LineBuffer socket_buffer = {0};

    if (argc != 6 || password == NULL || *password == '\0') {
        fprintf(stderr,
                "Usage: RETRACE_CLIENT_PASSWORD=<secret> %s "
                "<server-ip-or-host> <port> <client-id> "
                "<bluetooth-mac> <rfcomm-channel>\n",
                argv[0]);
        return EXIT_FAILURE;
    }
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    errno = 0;
    channel = strtoul(argv[5], &channel_end, 10);
    if (errno != 0 || channel_end == argv[5] || *channel_end != '\0' ||
        channel < 1 || channel > 30) {
        fprintf(stderr, "RFCOMM channel must be an integer from 1 to 30\n");
        goto fail;
    }
    bluetooth_fd = connect_rfcomm(argv[4], channel);
    if (bluetooth_fd < 0)
        goto fail;
    socket_fd = tcp_client_connect(argv[1], argv[2], argv[3], password);
    if (socket_fd < 0)
        goto fail;

    fprintf(stderr, "Bluetooth SPP bridge ready: client=%s peer=%s channel=%lu\n",
            argv[3], argv[4], channel);
    while (!stopping) {
        struct pollfd descriptors[2] = {
            {.fd = socket_fd, .events = POLLIN},
            {.fd = bluetooth_fd, .events = POLLIN}
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
            fprintf(stderr, "HC-06 Bluetooth connection closed\n");
            goto fail;
        }
        if ((descriptors[0].revents & POLLIN) &&
            consume_lines(socket_fd, bluetooth_fd, false, &socket_buffer) != 0) {
            fprintf(stderr, "server disconnected\n");
            goto fail;
        }
        if ((descriptors[1].revents & POLLIN) &&
            consume_lines(bluetooth_fd, socket_fd, true, &bluetooth_buffer) != 0) {
            fprintf(stderr, "HC-06 Bluetooth disconnected\n");
            goto fail;
        }
    }

    if (bluetooth_fd >= 0)
        close(bluetooth_fd);
    if (socket_fd >= 0)
        close(socket_fd);
    return EXIT_SUCCESS;

fail:
    if (bluetooth_fd >= 0)
        close(bluetooth_fd);
    if (socket_fd >= 0)
        close(socket_fd);
    return EXIT_FAILURE;
}

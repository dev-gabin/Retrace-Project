#define _POSIX_C_SOURCE 200809L

/* BLE/GATT bridge for the ESP32-C3 token. */

#include "../common/tcp_client.h"

#include <errno.h>
#include <gio/gio.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define TCP_LINE_MAX 8192
#define TOKEN_LINE_MAX 256
#define SERVER_HOST "127.0.0.1"
#define SERVER_PORT "5000"
#define SERVICE_UUID "4951013c-1654-43f1-919f-a3ff928dc0b6"
#define CHARACTERISTIC_UUID "36b342d0-153b-418e-b5d7-f95af507ec1f"

typedef struct {
    char data[TCP_LINE_MAX];
    size_t used;
    bool dropping;
} LineBuffer;

typedef struct {
    int socket_fd;
    char data[TOKEN_LINE_MAX];
    size_t used;
    bool dropping;
} NotificationBuffer;

static volatile sig_atomic_t stopping;

static void on_signal(int signal_number)
{
    (void)signal_number;
    stopping = 1;
}

static char *find_bluez_object(GDBusConnection *bus, const char *interface,
                               const char *property, const char *expected,
                               const char *path_prefix)
{
    GError *error = NULL;
    GVariant *reply;
    GVariantIter *objects;
    const char *path;
    GVariant *interfaces;
    char *result = NULL;

    reply = g_dbus_connection_call_sync(
        bus, "org.bluez", "/", "org.freedesktop.DBus.ObjectManager",
        "GetManagedObjects", NULL, G_VARIANT_TYPE("(a{oa{sa{sv}}})"),
        G_DBUS_CALL_FLAGS_NONE, 10000, NULL, &error);
    if (reply == NULL) {
        fprintf(stderr, "BlueZ GetManagedObjects: %s\n", error->message);
        g_error_free(error);
        return NULL;
    }

    g_variant_get(reply, "(a{oa{sa{sv}}})", &objects);
    while (g_variant_iter_loop(objects, "{&o@a{sa{sv}}}", &path,
                               &interfaces)) {
        GVariantIter interface_iter;
        const char *name;
        GVariant *properties;

        if (path_prefix != NULL && !g_str_has_prefix(path, path_prefix))
            continue;
        g_variant_iter_init(&interface_iter, interfaces);
        while (g_variant_iter_loop(&interface_iter, "{&s@a{sv}}", &name,
                                   &properties)) {
            const char *value = NULL;
            if (strcmp(name, interface) == 0 &&
                g_variant_lookup(properties, property, "&s", &value) &&
                g_ascii_strcasecmp(value, expected) == 0) {
                result = g_strdup(path);
            }
            if (result != NULL)
                break;
        }
        if (result != NULL)
            break;
    }
    g_variant_iter_free(objects);
    g_variant_unref(reply);
    return result;
}

static int call_no_args(GDBusConnection *bus, const char *path,
                        const char *interface, const char *method,
                        bool allow_existing)
{
    GError *error = NULL;
    GVariant *reply = g_dbus_connection_call_sync(
        bus, "org.bluez", path, interface, method, NULL, NULL,
        G_DBUS_CALL_FLAGS_NONE, 15000, NULL, &error);

    if (reply != NULL) {
        g_variant_unref(reply);
        return 0;
    }
    if (allow_existing &&
        (strstr(error->message, "Already") != NULL ||
         strstr(error->message, "InProgress") != NULL)) {
        g_error_free(error);
        return 0;
    }
    fprintf(stderr, "BlueZ %s: %s\n", method, error->message);
    g_error_free(error);
    return -1;
}

static int write_characteristic(GDBusConnection *bus, const char *path,
                                const char *data, size_t size)
{
    GError *error = NULL;
    GVariantBuilder options;
    GVariant *bytes;
    GVariant *reply;

    bytes = g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE, data, size,
                                      sizeof(guint8));
    g_variant_builder_init(&options, G_VARIANT_TYPE("a{sv}"));
    reply = g_dbus_connection_call_sync(
        bus, "org.bluez", path, "org.bluez.GattCharacteristic1",
        "WriteValue",
        g_variant_new("(@ay@a{sv})", bytes, g_variant_builder_end(&options)),
        NULL, G_DBUS_CALL_FLAGS_NONE, 10000, NULL, &error);
    if (reply == NULL) {
        fprintf(stderr, "TOKEN BLE write: %s\n", error->message);
        g_error_free(error);
        return -1;
    }
    g_variant_unref(reply);
    return 0;
}

static void send_notification_line(NotificationBuffer *buffer)
{
    if (!buffer->dropping && buffer->used > 0 &&
        tcp_client_send(buffer->socket_fd, "SERVER", buffer->data,
                        buffer->used) != 0) {
        fprintf(stderr, "send TOKEN response to server failed\n");
        stopping = 1;
    }
    buffer->used = 0;
    buffer->dropping = false;
}

static void consume_notification(NotificationBuffer *buffer,
                                 const guint8 *data, gsize size)
{
    for (gsize i = 0; i < size; ++i) {
        char byte = (char)data[i];
        if (byte == '\r')
            continue;
        if (byte == '\n') {
            send_notification_line(buffer);
            continue;
        }
        if (buffer->dropping)
            continue;
        if (buffer->used >= sizeof(buffer->data) - 1) {
            fprintf(stderr, "drop oversized TOKEN response\n");
            buffer->used = 0;
            buffer->dropping = true;
            continue;
        }
        buffer->data[buffer->used++] = byte;
    }
}

static void on_properties_changed(GDBusConnection *connection,
                                  const gchar *sender_name,
                                  const gchar *object_path,
                                  const gchar *interface_name,
                                  const gchar *signal_name,
                                  GVariant *parameters,
                                  gpointer user_data)
{
    NotificationBuffer *buffer = user_data;
    const char *changed_interface;
    GVariant *changed;
    GVariant *invalidated;
    GVariant *value = NULL;
    gsize size = 0;
    const guint8 *data;

    (void)connection;
    (void)sender_name;
    (void)object_path;
    (void)interface_name;
    (void)signal_name;
    g_variant_get(parameters, "(&s@a{sv}@as)", &changed_interface, &changed,
                  &invalidated);
    if (strcmp(changed_interface, "org.bluez.GattCharacteristic1") == 0 &&
        g_variant_lookup(changed, "Value", "@ay", &value)) {
        data = g_variant_get_fixed_array(value, &size, sizeof(guint8));
        consume_notification(buffer, data, size);
        g_variant_unref(value);
    }
    g_variant_unref(changed);
    g_variant_unref(invalidated);
}

static int send_server_line_to_token(GDBusConnection *bus,
                                     const char *characteristic_path,
                                     char *line, size_t length)
{
    const char *sender;
    const char *payload;
    size_t sender_size;
    size_t payload_size;
    char command[TOKEN_LINE_MAX + 2];

    if (tcp_client_unwrap(line, length, &sender, &sender_size, &payload,
                          &payload_size) != 0) {
        fprintf(stderr, "drop malformed server line\n");
        return 0;
    }
    (void)sender;
    (void)sender_size;
    if (payload_size == 0 ||
        (payload_size >= strlen("New connected!") &&
         memcmp(payload, "New connected!", strlen("New connected!")) == 0))
        return 0;
    if (payload_size > TOKEN_LINE_MAX - 1) {
        fprintf(stderr, "drop oversized TOKEN command\n");
        return 0;
    }
    memcpy(command, payload, payload_size);
    command[payload_size] = '\r';
    command[payload_size + 1] = '\n';
    return write_characteristic(bus, characteristic_path, command,
                                payload_size + 2);
}

static int consume_server_data(int socket_fd, GDBusConnection *bus,
                               const char *characteristic_path,
                               LineBuffer *buffer)
{
    char chunk[512];
    ssize_t received = recv(socket_fd, chunk, sizeof(chunk), 0);
    if (received <= 0)
        return -1;

    for (ssize_t i = 0; i < received; ++i) {
        char byte = chunk[i];
        if (byte == '\r')
            continue;
        if (byte == '\n') {
            if (!buffer->dropping &&
                send_server_line_to_token(bus, characteristic_path,
                                          buffer->data, buffer->used) != 0)
                return -1;
            buffer->used = 0;
            buffer->dropping = false;
            continue;
        }
        if (buffer->dropping)
            continue;
        if (buffer->used >= sizeof(buffer->data) - 1) {
            fprintf(stderr, "drop overlong server line\n");
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
    GError *error = NULL;
    GDBusConnection *bus = NULL;
    char *device_path = NULL;
    char *characteristic_path = NULL;
    guint subscription = 0;
    int socket_fd = -1;
    LineBuffer socket_buffer = {0};
    NotificationBuffer notification_buffer = {0};
    int result = EXIT_FAILURE;

    if (argc != 3 || password == NULL || *password == '\0') {
        fprintf(stderr,
                "Usage: RETRACE_CLIENT_PASSWORD=<secret> %s "
                "<client-id> <ble-mac>\n",
                argv[0]);
        return EXIT_FAILURE;
    }
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);
    if (bus == NULL) {
        fprintf(stderr, "connect system D-Bus: %s\n", error->message);
        g_error_free(error);
        goto cleanup;
    }
    device_path = find_bluez_object(bus, "org.bluez.Device1", "Address",
                                    argv[2], NULL);
    if (device_path == NULL) {
        fprintf(stderr, "TOKEN is unknown to BlueZ; scan it first: %s\n",
                argv[2]);
        goto cleanup;
    }
    if (call_no_args(bus, device_path, "org.bluez.Device1", "Connect", true) !=
        0)
        goto cleanup;

    for (int attempt = 0; attempt < 100 && characteristic_path == NULL;
         ++attempt) {
        while (g_main_context_iteration(NULL, FALSE))
            ;
        characteristic_path = find_bluez_object(
            bus, "org.bluez.GattCharacteristic1", "UUID",
            CHARACTERISTIC_UUID, device_path);
        if (characteristic_path == NULL)
            g_usleep(100000);
    }
    if (characteristic_path == NULL) {
        fprintf(stderr, "TOKEN BLE characteristic not found\n");
        goto cleanup;
    }

    socket_fd = tcp_client_connect(SERVER_HOST, SERVER_PORT, argv[1], password);
    if (socket_fd < 0)
        goto cleanup;
    notification_buffer.socket_fd = socket_fd;
    subscription = g_dbus_connection_signal_subscribe(
        bus, "org.bluez", "org.freedesktop.DBus.Properties",
        "PropertiesChanged", characteristic_path, NULL,
        G_DBUS_SIGNAL_FLAGS_NONE, on_properties_changed,
        &notification_buffer, NULL);
    if (call_no_args(bus, characteristic_path,
                     "org.bluez.GattCharacteristic1", "StartNotify", true) !=
        0)
        goto cleanup;

    fprintf(stderr,
            "TOKEN BLE bridge ready: server=%s:%s client=%s peer=%s\n",
            SERVER_HOST, SERVER_PORT, argv[1], argv[2]);
    while (!stopping) {
        struct pollfd descriptor = {.fd = socket_fd, .events = POLLIN};
        int ready;
        while (g_main_context_iteration(NULL, FALSE))
            ;
        ready = poll(&descriptor, 1, 50);
        if (ready < 0) {
            if (errno == EINTR)
                continue;
            perror("poll");
            goto cleanup;
        }
        if (descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) {
            fprintf(stderr, "server connection closed\n");
            goto cleanup;
        }
        if ((descriptor.revents & POLLIN) &&
            consume_server_data(socket_fd, bus, characteristic_path,
                                &socket_buffer) != 0) {
            fprintf(stderr, "TOKEN/server relay disconnected\n");
            goto cleanup;
        }
    }
    result = EXIT_SUCCESS;

cleanup:
    if (subscription != 0 && bus != NULL)
        g_dbus_connection_signal_unsubscribe(bus, subscription);
    if (socket_fd >= 0)
        close(socket_fd);
    if (bus != NULL && characteristic_path != NULL)
        (void)call_no_args(bus, characteristic_path,
                           "org.bluez.GattCharacteristic1", "StopNotify",
                           true);
    if (bus != NULL && device_path != NULL)
        (void)call_no_args(bus, device_path, "org.bluez.Device1", "Disconnect",
                           true);
    g_free(characteristic_path);
    g_free(device_path);
    if (bus != NULL)
        g_object_unref(bus);
    return result;
}

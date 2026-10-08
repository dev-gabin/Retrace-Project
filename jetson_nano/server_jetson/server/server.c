#define _POSIX_C_SOURCE 200809L

#include "server.h"
#include "db_handler.h"
#include "device_handler.h"

#include <arpa/inet.h>
#include <errno.h>
#include <pthread.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define CLIENT_ID_SIZE 32
#define PASSWORD_SIZE 128
#define MAX_CLIENT_LIMIT 64
#define MAX_LOGIN_SIZE (CLIENT_ID_SIZE + PASSWORD_SIZE + 4)
#define MAX_LINE_SIZE 4096
#define MAX_CREDENTIALS 256
#define PING_TIMEOUT_MS 3000

typedef struct {
    char id[CLIENT_ID_SIZE];
    char password[PASSWORD_SIZE];
} Credential;

typedef struct ServerState ServerState;

typedef struct {
    bool active;
    char target_id[CLIENT_ID_SIZE];
    char requester_id[CLIENT_ID_SIZE];
    struct timespec deadline;
} PendingPing;

typedef struct {
    ServerState *server;
    int fd;
    bool active;
    char id[CLIENT_ID_SIZE];
    char ip[INET_ADDRSTRLEN];
    pthread_mutex_t send_lock;
} ClientInfo;

struct ServerState {
    const RtConfig *database;
    ClientInfo *clients;
    size_t max_clients;
    size_t active_clients;
    pthread_mutex_t clients_lock;
    PendingPing pending_pings[MAX_CLIENT_LIMIT];
};

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

static int send_client_line(ClientInfo *client, const char *line)
{
    int rc;
    pthread_mutex_lock(&client->send_lock);
    rc = send_all(client->fd, line, strlen(line));
    pthread_mutex_unlock(&client->send_lock);
    return rc;
}

static int send_to_client(ClientInfo *client, const char *sender,
                          const char *payload)
{
    char line[MAX_LINE_SIZE + CLIENT_ID_SIZE + 4];
    int length = snprintf(line, sizeof(line), "[%s]%s\n", sender, payload);
    if (length < 0 || (size_t)length >= sizeof(line))
        return -1;
    return send_client_line(client, line);
}

static int read_credentials(const char *path, Credential **out,
                            size_t *credential_count)
{
    FILE *file = fopen(path, "r");
    Credential *credentials;
    size_t count = 0;
    char line[CLIENT_ID_SIZE + PASSWORD_SIZE + 16];

    if (file == NULL) {
        fprintf(stderr, "Cannot open credentials file '%s': %s\n", path,
                strerror(errno));
        return -1;
    }
    credentials = calloc(MAX_CREDENTIALS, sizeof(*credentials));
    if (credentials == NULL) {
        fclose(file);
        return -1;
    }
    while (fgets(line, sizeof(line), file) != NULL) {
        char id[CLIENT_ID_SIZE + 1];
        char password[PASSWORD_SIZE + 1];
        char extra;
        int fields;
        char *p = line;
        while (*p == ' ' || *p == '\t')
            ++p;
        if (*p == '\0' || *p == '\n' || *p == '#')
            continue;
        fields = sscanf(p, "%32s %128s %c", id, password, &extra);
        if (fields != 2 || strlen(id) >= CLIENT_ID_SIZE ||
            strlen(password) >= PASSWORD_SIZE || count == MAX_CREDENTIALS) {
            fprintf(stderr, "Invalid or excessive credential entry in '%s'\n",
                    path);
            free(credentials);
            fclose(file);
            return -1;
        }
        for (size_t i = 0; i < count; ++i) {
            if (strcmp(credentials[i].id, id) == 0) {
                fprintf(stderr, "Duplicate client ID in credentials file\n");
                free(credentials);
                fclose(file);
                return -1;
            }
        }
        memcpy(credentials[count].id, id, strlen(id) + 1);
        memcpy(credentials[count].password, password, strlen(password) + 1);
        ++count;
    }
    if (ferror(file) || count == 0) {
        fprintf(stderr, "Credentials file is empty or unreadable: '%s'\n", path);
        free(credentials);
        fclose(file);
        return -1;
    }
    fclose(file);
    *out = credentials;
    *credential_count = count;
    return 0;
}

static int read_login(int fd, char *id, size_t id_size,
                      const Credential *credentials, size_t count)
{
    char input[MAX_LOGIN_SIZE];
    size_t used = 0;
    char *colon;

    while (used < sizeof(input) - 1) {
        char byte;
        ssize_t n = recv(fd, &byte, 1, 0);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return -1;
        input[used++] = byte;
        if (byte == ']')
            break;
    }
    input[used] = '\0';
    if (used < 5 || input[0] != '[' || input[used - 1] != ']')
        return -1;
    input[used - 1] = '\0';
    colon = strchr(input + 1, ':');
    if (colon == NULL || colon == input + 1 || colon[1] == '\0')
        return -1;
    *colon++ = '\0';
    if (strlen(input + 1) >= id_size || strlen(colon) >= PASSWORD_SIZE)
        return -1;

    for (size_t i = 0; i < count; ++i) {
        if (strcmp(credentials[i].id, input + 1) == 0 &&
            strcmp(credentials[i].password, colon) == 0) {
            snprintf(id, id_size, "%s", input + 1);
            return 0;
        }
    }
    return -1;
}

static ClientInfo *find_free_client(ServerState *server)
{
    for (size_t i = 0; i < server->max_clients; ++i)
        if (!server->clients[i].active)
            return &server->clients[i];
    return NULL;
}

static ClientInfo *find_client(ServerState *server, const char *id)
{
    for (size_t i = 0; i < server->max_clients; ++i) {
        ClientInfo *client = &server->clients[i];
        if (client->active && strcmp(client->id, id) == 0)
            return client;
    }
    return NULL;
}

static int is_database_command(const char *payload)
{
    const char *end = strchr(payload, '@');
    size_t length = end == NULL ? strlen(payload) : (size_t)(end - payload);
    return (length == 4 && strncmp(payload, "LIST", length) == 0) ||
           (length == 3 && strncmp(payload, "GET", length) == 0) ||
           (length == 4 && strncmp(payload, "SAVE", length) == 0);
}

static int is_ping_command(const char *payload)
{
    return strcmp(payload, "PING") == 0 || strncmp(payload, "PING@", 5) == 0;
}

static int is_ping_response(const char *payload)
{
    return strcmp(payload, "OK@PING") == 0 || strncmp(payload, "ERR@PING:", 9) == 0;
}

static int is_server_target(const char *target)
{
    return strcmp(target, "SERVER") == 0 || strcmp(target, "JETSON") == 0 ||
           strcmp(target, "SQL") == 0;
}

static void send_id_list(ServerState *server, ClientInfo *requester)
{
    char payload[MAX_LINE_SIZE];
    size_t used = (size_t)snprintf(payload, sizeof(payload), "IDLIST@");
    pthread_mutex_lock(&server->clients_lock);
    for (size_t i = 0; i < server->max_clients; ++i) {
        ClientInfo *client = &server->clients[i];
        if (!client->active)
            continue;
        int n = snprintf(payload + used, sizeof(payload) - used, "%s%s",
                         used > sizeof("IDLIST@") - 1 ? "," : "", client->id);
        if (n < 0 || (size_t)n >= sizeof(payload) - used)
            break;
        used += (size_t)n;
    }
    pthread_mutex_unlock(&server->clients_lock);
    (void)send_to_client(requester, requester->id, payload);
}

static void ping_deadline(struct timespec *deadline)
{
    clock_gettime(CLOCK_MONOTONIC, deadline);
    deadline->tv_sec += PING_TIMEOUT_MS / 1000;
    deadline->tv_nsec += (long)(PING_TIMEOUT_MS % 1000) * 1000000L;
    if (deadline->tv_nsec >= 1000000000L) {
        ++deadline->tv_sec;
        deadline->tv_nsec -= 1000000000L;
    }
}

static int ping_expired(const PendingPing *pending, const struct timespec *now)
{
    return now->tv_sec > pending->deadline.tv_sec ||
           (now->tv_sec == pending->deadline.tv_sec &&
            now->tv_nsec >= pending->deadline.tv_nsec);
}

static void expire_pending_pings(ServerState *server)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    pthread_mutex_lock(&server->clients_lock);
    for (size_t i = 0; i < MAX_CLIENT_LIMIT; ++i) {
        PendingPing *pending = &server->pending_pings[i];
        if (pending->active && ping_expired(pending, &now)) {
            ClientInfo *requester = find_client(server, pending->requester_id);
            if (requester != NULL)
                (void)send_to_client(requester, "SERVER", "ERR@PING:TIMEOUT");
            memset(pending, 0, sizeof(*pending));
        }
    }
    pthread_mutex_unlock(&server->clients_lock);
}

static void handle_ping_request(ServerState *server, ClientInfo *requester,
                                const char *payload)
{
    const char *target_id = strncmp(payload, "PING@", 5) == 0 ? payload + 5 : "";
    ClientInfo *target;
    PendingPing *slot = NULL;
    const char *error = NULL;

    if (*target_id == '\0' || strlen(target_id) >= CLIENT_ID_SIZE ||
        strchr(target_id, ':') != NULL || strchr(target_id, '@') != NULL) {
        (void)send_to_client(requester, "SERVER", "ERR@PING:FORMAT");
        return;
    }

    expire_pending_pings(server);
    pthread_mutex_lock(&server->clients_lock);
    target = find_client(server, target_id);
    if (target == NULL) {
        error = "ERR@PING:UNKNOWN_ID";
    } else {
        for (size_t i = 0; i < MAX_CLIENT_LIMIT; ++i) {
            PendingPing *pending = &server->pending_pings[i];
            if (pending->active && strcmp(pending->target_id, target_id) == 0) {
                error = "ERR@PING:BUSY";
                break;
            }
            if (!pending->active && slot == NULL)
                slot = pending;
        }
        if (error == NULL && slot == NULL)
            error = "ERR@PING:BUSY";
    }

    if (error == NULL) {
        slot->active = true;
        snprintf(slot->target_id, sizeof(slot->target_id), "%s", target_id);
        snprintf(slot->requester_id, sizeof(slot->requester_id), "%s", requester->id);
        ping_deadline(&slot->deadline);
        if (send_to_client(target, requester->id, "PING") != 0) {
            memset(slot, 0, sizeof(*slot));
            error = "ERR@PING:DELIVERY";
        }
    }
    pthread_mutex_unlock(&server->clients_lock);

    if (error != NULL)
        (void)send_to_client(requester, "SERVER", error);
}

static void handle_ping_response(ServerState *server, ClientInfo *responder,
                                 const char *payload)
{
    int matched = 0;
    expire_pending_pings(server);
    pthread_mutex_lock(&server->clients_lock);
    for (size_t i = 0; i < MAX_CLIENT_LIMIT; ++i) {
        PendingPing *pending = &server->pending_pings[i];
        if (pending->active && strcmp(pending->target_id, responder->id) == 0) {
            ClientInfo *requester = find_client(server, pending->requester_id);
            memset(pending, 0, sizeof(*pending));
            if (requester != NULL)
                (void)send_to_client(requester, "SERVER", payload);
            matched = 1;
            break;
        }
    }
    pthread_mutex_unlock(&server->clients_lock);
    if (!matched)
        printf("[%s] unsolicited PING response: %s\n", responder->id, payload);
}

static void cancel_client_pings(ServerState *server, ClientInfo *client)
{
    pthread_mutex_lock(&server->clients_lock);
    for (size_t i = 0; i < MAX_CLIENT_LIMIT; ++i) {
        PendingPing *pending = &server->pending_pings[i];
        if (!pending->active)
            continue;
        if (strcmp(pending->target_id, client->id) == 0) {
            ClientInfo *requester = find_client(server, pending->requester_id);
            if (requester != NULL && requester != client)
                (void)send_to_client(requester, "SERVER", "ERR@PING:DISCONNECTED");
            memset(pending, 0, sizeof(*pending));
        } else if (strcmp(pending->requester_id, client->id) == 0) {
            memset(pending, 0, sizeof(*pending));
        }
    }
    pthread_mutex_unlock(&server->clients_lock);
}

static void route_message(ServerState *server, ClientInfo *sender,
                          const char *target, const char *payload)
{
    char line[MAX_LINE_SIZE + CLIENT_ID_SIZE + 4];
    if (strcmp(target, "ALLMSG") == 0) {
        int length = snprintf(line, sizeof(line), "[%s]%s\n", sender->id,
                              payload);
        if (length < 0 || (size_t)length >= sizeof(line))
            return;
        pthread_mutex_lock(&server->clients_lock);
        for (size_t i = 0; i < server->max_clients; ++i) {
            ClientInfo *client = &server->clients[i];
            if (client->active)
                (void)send_client_line(client, line);
        }
        pthread_mutex_unlock(&server->clients_lock);
        return;
    }

    pthread_mutex_lock(&server->clients_lock);
    ClientInfo *recipient = find_client(server, target);
    if (recipient != NULL)
        (void)send_to_client(recipient, sender->id, payload);
    pthread_mutex_unlock(&server->clients_lock);
    if (recipient == NULL)
        (void)send_to_client(sender, "SERVER", "ERROR@ROUTE:UNKNOWN_ID");
}

static void dispatch_line(ServerState *server, ClientInfo *client,
                          RtStore *store, char *line)
{
    char target[CLIENT_ID_SIZE] = "SERVER";
    char *payload = line;
    bool addressed = false;

    if (line[0] == '[') {
        char *closing = strchr(line, ']');
        size_t length;
        if (closing == NULL || closing == line + 1) {
            (void)send_to_client(client, "SERVER", "ERROR@MESSAGE:BAD_PREFIX");
            return;
        }
        length = (size_t)(closing - line - 1);
        if (length >= sizeof(target)) {
            (void)send_to_client(client, "SERVER", "ERROR@MESSAGE:ID_TOO_LONG");
            return;
        }
        memcpy(target, line + 1, length);
        target[length] = '\0';
        payload = closing + 1;
        addressed = true;
    }

    if (!addressed && !is_database_command(payload) &&
        !is_ping_command(payload)) {
        (void)send_to_client(client, "SERVER", "ERROR@MESSAGE:EXPECTED_TARGET");
        return;
    }
    if (is_server_target(target) ||
        (!addressed && (is_database_command(payload) || is_ping_command(payload)))) {
        if (is_ping_command(payload)) {
            handle_ping_request(server, client, payload);
        } else if (is_ping_response(payload)) {
            handle_ping_response(server, client, payload);
        } else if (is_database_command(payload)) {
            int rc = db_handler_handle(store, client->fd, client->id, payload,
                                       &client->send_lock);
            if (rc < 0)
                shutdown(client->fd, SHUT_RDWR);
        } else if (strncmp(payload, "SET@", 4) == 0) {
            char device_id[CLIENT_ID_SIZE];
            char device_command[64];
            char reason[32];
            int result = device_handler_translate_set(
                payload, device_id, sizeof(device_id), device_command,
                sizeof(device_command), reason, sizeof(reason));
            if (result == 1)
                route_message(server, client, device_id, device_command);
            else {
                char error[64];
                snprintf(error, sizeof(error), "ERROR@SET:%s",
                         result == 0 ? "FORMAT" : reason);
                (void)send_to_client(client, "SERVER", error);
            }
        } else if (strncmp(payload, "EVT@", 4) == 0) {
            printf("[%s] %s\n", client->id, payload);
        } else if (strncmp(payload, "EVT:", 4) == 0 ||
                   strncmp(payload, "OK:", 3) == 0 ||
                   strncmp(payload, "ERR:", 4) == 0) {
            printf("[%s] %s\n", client->id, payload);
        } else {
            (void)send_to_client(client, "SERVER", "ERROR@COMMAND:UNKNOWN");
        }
        return;
    }
    if (strcmp(target, "IDLIST") == 0) {
        send_id_list(server, client);
        return;
    }
    if (strcmp(target, "GETTIME") == 0) {
        time_t now = time(NULL);
        struct tm local;
        char text[64];
        if (localtime_r(&now, &local) != NULL &&
            strftime(text, sizeof(text), "GETTIME@%y.%m.%d %H:%M:%S", &local))
            (void)send_to_client(client, "SERVER", text);
        return;
    }
    route_message(server, client, target, payload);
}

static void *client_worker(void *argument)
{
    ClientInfo *client = argument;
    ServerState *server = client->server;
    RtStore *store = NULL;
    char error[512];
    char pending[MAX_LINE_SIZE];
    size_t used = 0;

    if (rt_open(&store, server->database, error, sizeof(error)) != RT_OK) {
        fprintf(stderr, "DB open for client %s failed: %s\n", client->id, error);
        (void)send_to_client(client, "SERVER", "ERROR@DATABASE:UNAVAILABLE");
        goto finished;
    }
    char connected[MAX_LINE_SIZE];
    snprintf(connected, sizeof(connected), "New connected! (ip:%s,fd:%d)",
             client->ip, client->fd);
    (void)send_to_client(client, client->id, connected);
    printf("Client %s connected from %s\n", client->id, client->ip);

    for (;;) {
        ssize_t received;
        char *line;
        char *newline;
        struct pollfd descriptor = {.fd = client->fd, .events = POLLIN};
        int ready;

        expire_pending_pings(server);
        ready = poll(&descriptor, 1, 250);
        if (ready == 0)
            continue;
        if (ready < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        if ((descriptor.revents & (POLLERR | POLLNVAL)) ||
            ((descriptor.revents & POLLHUP) && !(descriptor.revents & POLLIN)))
            break;
        if (!(descriptor.revents & POLLIN))
            continue;
        received = recv(client->fd, pending + used,
                        sizeof(pending) - used - 1, 0);
        if (received == 0)
            break;
        if (received < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        used += (size_t)received;
        pending[used] = '\0';
        line = pending;
        while ((newline = strchr(line, '\n')) != NULL) {
            *newline = '\0';
            if (newline > line && newline[-1] == '\r')
                newline[-1] = '\0';
            if (*line != '\0')
                dispatch_line(server, client, store, line);
            line = newline + 1;
        }
        used -= (size_t)(line - pending);
        memmove(pending, line, used);
        if (used == sizeof(pending) - 1) {
            (void)send_to_client(client, "SERVER", "ERROR@MESSAGE:TOO_LONG");
            break;
        }
    }

finished:
    if (store != NULL)
        rt_close(store);
    cancel_client_pings(server, client);
    pthread_mutex_lock(&server->clients_lock);
    int fd = client->fd;
    client->active = false;
    client->fd = -1;
    if (server->active_clients > 0)
        --server->active_clients;
    pthread_mutex_unlock(&server->clients_lock);
    shutdown(fd, SHUT_RDWR);
    close(fd);
    printf("Client %s disconnected\n", client->id);
    return NULL;
}

int server_run(const ServerConfig *config, const RtConfig *database)
{
    int server_fd = -1;
    int option = 1;
    struct sockaddr_in address;
    Credential *credentials = NULL;
    size_t credential_count = 0;
    ServerState server = {0};
    int result = -1;
    bool clients_lock_initialized = false;
    size_t send_locks_initialized = 0;

    if (config == NULL || database == NULL || config->port < 1 ||
        config->port > 65535 || config->max_clients < 1 ||
        config->max_clients > MAX_CLIENT_LIMIT ||
        config->credentials_file == NULL) {
        fprintf(stderr, "Invalid server configuration\n");
        return -1;
    }
    if (read_credentials(config->credentials_file, &credentials,
                        &credential_count) != 0)
        return -1;
    server.clients = calloc((size_t)config->max_clients,
                            sizeof(*server.clients));
    if (server.clients == NULL)
        goto cleanup;
    server.database = database;
    server.max_clients = (size_t)config->max_clients;
    if (pthread_mutex_init(&server.clients_lock, NULL) != 0)
        goto cleanup;
    clients_lock_initialized = true;
    for (size_t i = 0; i < server.max_clients; ++i) {
        server.clients[i].server = &server;
        server.clients[i].fd = -1;
        if (pthread_mutex_init(&server.clients[i].send_lock, NULL) != 0)
            goto cleanup_mutexes;
        ++send_locks_initialized;
    }

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        goto cleanup_mutexes;
    }
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &option,
                   sizeof(option)) != 0) {
        perror("setsockopt");
        goto cleanup_mutexes;
    }
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons((unsigned short)config->port);
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) != 0) {
        perror("bind");
        goto cleanup_mutexes;
    }
    if (listen(server_fd, config->max_clients) != 0) {
        perror("listen");
        goto cleanup_mutexes;
    }

    printf("server_jetson listening on port %d\n", config->port);
    for (;;) {
        struct sockaddr_in peer;
        socklen_t peer_size = sizeof(peer);
        char id[CLIENT_ID_SIZE];
        char ip[INET_ADDRSTRLEN] = "unknown";
        int client_fd = accept(server_fd, (struct sockaddr *)&peer, &peer_size);
        if (client_fd < 0) {
            if (errno == EINTR)
                continue;
            perror("accept");
            sleep(1);
            continue;
        }
        if (inet_ntop(AF_INET, &peer.sin_addr, ip, sizeof(ip)) == NULL)
            snprintf(ip, sizeof(ip), "unknown");
        if (read_login(client_fd, id, sizeof(id), credentials,
                       credential_count) != 0) {
            static const char rejected[] = "[SERVER]Authentication Error!\n";
            (void)send_all(client_fd, rejected, sizeof(rejected) - 1);
            close(client_fd);
            continue;
        }

        pthread_mutex_lock(&server.clients_lock);
        ClientInfo *slot = find_free_client(&server);
        bool duplicate = find_client(&server, id) != NULL;
        if (slot != NULL && !duplicate) {
            slot->fd = client_fd;
            slot->active = true;
            snprintf(slot->id, sizeof(slot->id), "%s", id);
            snprintf(slot->ip, sizeof(slot->ip), "%s", ip);
            ++server.active_clients;
        }
        pthread_mutex_unlock(&server.clients_lock);
        if (slot == NULL || duplicate) {
            const char *reason = duplicate ? "[SERVER]Already logged!\n"
                                           : "[SERVER]Server is full\n";
            (void)send_all(client_fd, reason, strlen(reason));
            close(client_fd);
            continue;
        }

        pthread_t thread;
        if (pthread_create(&thread, NULL, client_worker, slot) != 0) {
            perror("pthread_create");
            pthread_mutex_lock(&server.clients_lock);
            slot->fd = -1;
            slot->active = false;
            --server.active_clients;
            pthread_mutex_unlock(&server.clients_lock);
            shutdown(client_fd, SHUT_RDWR);
            close(client_fd);
            continue;
        }
        pthread_detach(thread);
    }

cleanup_mutexes:
    for (size_t i = 0; i < send_locks_initialized; ++i)
        pthread_mutex_destroy(&server.clients[i].send_lock);
    if (clients_lock_initialized)
        pthread_mutex_destroy(&server.clients_lock);
cleanup:
    if (server_fd >= 0)
        close(server_fd);
    free(server.clients);
    free(credentials);
    return result;
}

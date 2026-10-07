#define _POSIX_C_SOURCE 200809L

#include "server/server.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *env_or(const char *name, const char *fallback)
{
    const char *value = getenv(name);
    return value != NULL && value[0] != '\0' ? value : fallback;
}

static int parse_port(const char *text, unsigned int *port)
{
    char *end;
    long value;
    if (text == NULL || text[0] == '\0')
        return -1;
    errno = 0;
    value = strtol(text, &end, 10);
    if (errno != 0 || *end != '\0' || value < 1 || value > 65535)
        return -1;
    *port = (unsigned int)value;
    return 0;
}

int main(int argc, char *argv[])
{
    RtConfig database;
    ServerConfig server;
    RtStore *check = NULL;
    char error[512];
    unsigned int database_port;
    unsigned int server_port;
    const char *password = getenv("RETRACE_DB_PASSWORD");

    if (argc > 2) {
        fprintf(stderr, "Usage: %s [server-port]\n", argv[0]);
        return EXIT_FAILURE;
    }
    if (parse_port(env_or("RETRACE_DB_PORT", "3306"), &database_port) != 0) {
        fputs("Invalid RETRACE_DB_PORT\n", stderr);
        return EXIT_FAILURE;
    }
    if (parse_port(argc == 2 ? argv[1] :
                   env_or("RETRACE_SERVER_PORT", "5000"), &server_port) != 0) {
        fputs("Invalid server port\n", stderr);
        return EXIT_FAILURE;
    }
    if (password == NULL || password[0] == '\0') {
        fputs("Set RETRACE_DB_PASSWORD before starting server_jetson\n", stderr);
        return EXIT_FAILURE;
    }

    database.host = env_or("RETRACE_DB_HOST", "127.0.0.1");
    database.user = env_or("RETRACE_DB_USER", "retrace");
    database.password = password;
    database.database = env_or("RETRACE_DB_NAME", "retrace");
    database.data_dir = env_or("RETRACE_DATA_DIR", "./data");
    database.port = database_port;

    if (rt_open(&check, &database, error, sizeof(error)) != RT_OK) {
        fprintf(stderr, "Storage startup failed: %s\n", error);
        return EXIT_FAILURE;
    }
    rt_close(check);

    server.port = (int)server_port;
    server.max_clients = 36;
    server.credentials_file = env_or("RETRACE_CLIENTS_FILE",
                                     "jetson_nano/server_jetson/server/idpasswd.txt");
    signal(SIGPIPE, SIG_IGN);
    return server_run(&server, &database) == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

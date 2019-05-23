/*
 * Copyright 6WIND S.A., 2014
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 */

#include "qemu/osdep.h"

#include "ivshmem-client.h"

#define IVSHMEM_CLIENT_DEFAULT_VERBOSE        0
#define IVSHMEM_CLIENT_DEFAULT_UNIX_SOCK_PATH "/tmp/ivshmem_socket"

typedef struct IvshmemClientArgs {
    bool verbose;
    const char *unix_sock_path;
} IvshmemClientArgs;

/* show ivshmem_client_usage and exit with given error code */
static void
ivshmem_client_usage(const char *name, int code)
{
    fprintf(stderr, "%s [opts]\n", name);
    fprintf(stderr, "  -h: show this help\n");
    fprintf(stderr, "  -v: verbose mode\n");
    fprintf(stderr, "  -S <unix_sock_path>: path to the unix socket\n"
                    "     to connect to.\n"
                    "     default=%s\n", IVSHMEM_CLIENT_DEFAULT_UNIX_SOCK_PATH);
    exit(code);
}

/* parse the program arguments, exit on error */
static void
ivshmem_client_parse_args(IvshmemClientArgs *args, int argc, char *argv[])
{
    int c;

    while ((c = getopt(argc, argv,
                       "h"  /* help */
                       "v"  /* verbose */
                       "S:" /* unix_sock_path */
                      )) != -1) {

        switch (c) {
        case 'h': /* help */
            ivshmem_client_usage(argv[0], 0);
            break;

        case 'v': /* verbose */
            args->verbose = 1;
            break;

        case 'S': /* unix_sock_path */
            args->unix_sock_path = optarg;
            break;

        default:
            ivshmem_client_usage(argv[0], 1);
            break;
        }
    }
}

/* show command line help */
static void
ivshmem_client_cmdline_help(void)
{
    printf("dump: dump peers (including us)\n"
           "int <peer> <vector>: notify one vector on a peer\n"
           "int <peer> all: notify all vectors of a peer\n"
           "int all: notify all vectors of all peers (excepting us)\n");
}

/* read stdin and handle commands */
static int
ivshmem_client_handle_stdin_command(IvshmemClient *client)
{
    IvshmemClientPeer *peer;
    char buf[128];
    char *s, *token;
    int ret;
    int peer_id, vector;

    memset(buf, 0, sizeof(buf));
    ret = read(0, buf, sizeof(buf) - 1);
    if (ret < 0) {
        return -1;
    }

    s = buf;
    while ((token = strsep(&s, "\n\r;")) != NULL) {
        if (!strcmp(token, "")) {
            continue;
        }
        if (!strcmp(token, "?")) {
            ivshmem_client_cmdline_help();
        }
        if (!strcmp(token, "help")) {
            ivshmem_client_cmdline_help();
        } else if (!strcmp(token, "dump")) {
            ivshmem_client_dump(client);
        } else if (!strcmp(token, "int all")) {
            ivshmem_client_notify_broadcast(client);
        } else if (sscanf(token, "int %d %d", &peer_id, &vector) == 2) {
            peer = ivshmem_client_search_peer(client, peer_id);
            if (peer == NULL) {
                printf("cannot find peer_id = %d\n", peer_id);
                continue;
            }
            ivshmem_client_notify(client, peer, vector);
        } else if (sscanf(token, "int %d all", &peer_id) == 1) {
            peer = ivshmem_client_search_peer(client, peer_id);
            if (peer == NULL) {
                printf("cannot find peer_id = %d\n", peer_id);
                continue;
            }
            ivshmem_client_notify_all_vects(client, peer);
        } else {
            printf("invalid command, type help\n");
        }
    }

    printf("cmd> ");
    fflush(stdout);
    return 0;
}

/* listen on stdin (command line), on unix socket (notifications of new
 * and dead peers), and on eventfd (IRQ request) */
static int
ivshmem_client_poll_events(IvshmemClient *client)
{
    fd_set fds;
    int ret, maxfd;

    while (1) {

        FD_ZERO(&fds);
        FD_SET(0, &fds); /* add stdin in fd_set */
        maxfd = 1;

        ivshmem_client_get_fds(client, &fds, &maxfd);

        ret = select(maxfd, &fds, NULL, NULL, NULL);
        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }

            fprintf(stderr, "select error: %s\n", strerror(errno));
            break;
        }
        if (ret == 0) {
            continue;
        }

        if (FD_ISSET(0, &fds) &&
            ivshmem_client_handle_stdin_command(client) < 0 && errno != EINTR) {
            fprintf(stderr, "ivshmem_client_handle_stdin_command() failed\n");
            break;
        }

        if (ivshmem_client_handle_fds(client, &fds, maxfd) < 0) {
            fprintf(stderr, "ivshmem_client_handle_fds() failed\n");
            break;
        }
    }

    return ret;
}

/* callback when we receive a notification (just display it) */
static void
ivshmem_client_notification_cb(const IvshmemClient *client,
                               const IvshmemClientPeer *peer,
                               unsigned vect, void *arg)
{
    (void)client;
    (void)arg;
    printf("receive notification from peer_id=%" PRId64 " vector=%u\n",
           peer->id, vect);
}

int
main(int argc, char *argv[])
{
    struct sigaction sa;
    IvshmemClient client;
    IvshmemClientArgs args = {
        .verbose = IVSHMEM_CLIENT_DEFAULT_VERBOSE,
        .unix_sock_path = IVSHMEM_CLIENT_DEFAULT_UNIX_SOCK_PATH,
    };

    /* parse arguments, will exit on error */
    ivshmem_client_parse_args(&args, argc, argv);

    /* Ignore SIGPIPE, see this link for more info:
     * http://www.mail-archive.com/libevent-users@monkey.org/msg01606.html */
    sa.sa_handler = SIG_IGN;
    sa.sa_flags = 0;
    if (sigemptyset(&sa.sa_mask) == -1 ||
        sigaction(SIGPIPE, &sa, 0) == -1) {
        perror("failed to ignore SIGPIPE; sigaction");
        return 1;
    }

    ivshmem_client_cmdline_help();
    printf("cmd> ");
    fflush(stdout);

    if (ivshmem_client_init(&client, args.unix_sock_path,
                            ivshmem_client_notification_cb, NULL,
                            args.verbose) < 0) {
        fprintf(stderr, "cannot init client\n");
        return 1;
    }

    while (1) {
        if (ivshmem_client_connect(&client) < 0) {
            fprintf(stderr, "cannot connect to server, retry in 1 second\n");
            sleep(1);
            continue;
        }

        fprintf(stdout, "listen on server socket %d\n", client.sock_fd);

        if (ivshmem_client_poll_events(&client) == 0) {
            continue;
        }

        /* disconnected from server, reset all peers */
        fprintf(stdout, "disconnected from server\n");

        ivshmem_client_close(&client);
    }

    return 0;
}

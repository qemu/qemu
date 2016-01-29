/*
 * Copyright 6WIND S.A., 2014
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu-common.h"

#include "ivshmem-server.h"

#define IVSHMEM_SERVER_DEFAULT_VERBOSE        0
#define IVSHMEM_SERVER_DEFAULT_FOREGROUND     0
#define IVSHMEM_SERVER_DEFAULT_PID_FILE       "/var/run/ivshmem-server.pid"
#define IVSHMEM_SERVER_DEFAULT_UNIX_SOCK_PATH "/tmp/ivshmem_socket"
#define IVSHMEM_SERVER_DEFAULT_SHM_PATH       "ivshmem"
#define IVSHMEM_SERVER_DEFAULT_SHM_SIZE       (4*1024*1024)
#define IVSHMEM_SERVER_DEFAULT_N_VECTORS      1

/* used to quit on signal SIGTERM */
static int ivshmem_server_quit;

/* arguments given by the user */
typedef struct IvshmemServerArgs {
    bool verbose;
    bool foreground;
    const char *pid_file;
    const char *unix_socket_path;
    const char *shm_path;
    uint64_t shm_size;
    unsigned n_vectors;
} IvshmemServerArgs;

/* show ivshmem_server_usage and exit with given error code */
static void
ivshmem_server_usage(const char *name, int code)
{
    fprintf(stderr, "%s [opts]\n", name);
    fprintf(stderr, "  -h: show this help\n");
    fprintf(stderr, "  -v: verbose mode\n");
    fprintf(stderr, "  -F: foreground mode (default is to daemonize)\n");
    fprintf(stderr, "  -p <pid_file>: path to the PID file (used in daemon\n"
                    "     mode only).\n"
                    "     Default=%s\n", IVSHMEM_SERVER_DEFAULT_SHM_PATH);
    fprintf(stderr, "  -S <unix_socket_path>: path to the unix socket\n"
                    "     to listen to.\n"
                    "     Default=%s\n", IVSHMEM_SERVER_DEFAULT_UNIX_SOCK_PATH);
    fprintf(stderr, "  -m <shm_path>: path to the shared memory.\n"
                    "     The path corresponds to a POSIX shm name or a\n"
                    "     hugetlbfs mount point.\n"
                    "     default=%s\n", IVSHMEM_SERVER_DEFAULT_SHM_PATH);
    fprintf(stderr, "  -l <size>: size of shared memory in bytes. The suffix\n"
                    "     K, M and G can be used (ex: 1K means 1024).\n"
                    "     default=%u\n", IVSHMEM_SERVER_DEFAULT_SHM_SIZE);
    fprintf(stderr, "  -n <n_vects>: number of vectors.\n"
                    "     default=%u\n", IVSHMEM_SERVER_DEFAULT_N_VECTORS);

    exit(code);
}

/* parse the program arguments, exit on error */
static void
ivshmem_server_parse_args(IvshmemServerArgs *args, int argc, char *argv[])
{
    int c;
    unsigned long long v;
    Error *err = NULL;

    while ((c = getopt(argc, argv,
                       "h"  /* help */
                       "v"  /* verbose */
                       "F"  /* foreground */
                       "p:" /* pid_file */
                       "S:" /* unix_socket_path */
                       "m:" /* shm_path */
                       "l:" /* shm_size */
                       "n:" /* n_vectors */
                      )) != -1) {

        switch (c) {
        case 'h': /* help */
            ivshmem_server_usage(argv[0], 0);
            break;

        case 'v': /* verbose */
            args->verbose = 1;
            break;

        case 'F': /* foreground */
            args->foreground = 1;
            break;

        case 'p': /* pid_file */
            args->pid_file = optarg;
            break;

        case 'S': /* unix_socket_path */
            args->unix_socket_path = optarg;
            break;

        case 'm': /* shm_path */
            args->shm_path = optarg;
            break;

        case 'l': /* shm_size */
            parse_option_size("shm_size", optarg, &args->shm_size, &err);
            if (err) {
                error_report_err(err);
                ivshmem_server_usage(argv[0], 1);
            }
            break;

        case 'n': /* n_vectors */
            if (parse_uint_full(optarg, &v, 0) < 0) {
                fprintf(stderr, "cannot parse n_vectors\n");
                ivshmem_server_usage(argv[0], 1);
            }
            args->n_vectors = v;
            break;

        default:
            ivshmem_server_usage(argv[0], 1);
            break;
        }
    }

    if (args->n_vectors > IVSHMEM_SERVER_MAX_VECTORS) {
        fprintf(stderr, "too many requested vectors (max is %d)\n",
                IVSHMEM_SERVER_MAX_VECTORS);
        ivshmem_server_usage(argv[0], 1);
    }

    if (args->verbose == 1 && args->foreground == 0) {
        fprintf(stderr, "cannot use verbose in daemon mode\n");
        ivshmem_server_usage(argv[0], 1);
    }
}

/* wait for events on listening server unix socket and connected client
 * sockets */
static int
ivshmem_server_poll_events(IvshmemServer *server)
{
    fd_set fds;
    int ret = 0, maxfd;

    while (!ivshmem_server_quit) {

        FD_ZERO(&fds);
        maxfd = 0;
        ivshmem_server_get_fds(server, &fds, &maxfd);

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

        if (ivshmem_server_handle_fds(server, &fds, maxfd) < 0) {
            fprintf(stderr, "ivshmem_server_handle_fds() failed\n");
            break;
        }
    }

    return ret;
}

static void
ivshmem_server_quit_cb(int signum)
{
    ivshmem_server_quit = 1;
}

int
main(int argc, char *argv[])
{
    IvshmemServer server;
    struct sigaction sa, sa_quit;
    IvshmemServerArgs args = {
        .verbose = IVSHMEM_SERVER_DEFAULT_VERBOSE,
        .foreground = IVSHMEM_SERVER_DEFAULT_FOREGROUND,
        .pid_file = IVSHMEM_SERVER_DEFAULT_PID_FILE,
        .unix_socket_path = IVSHMEM_SERVER_DEFAULT_UNIX_SOCK_PATH,
        .shm_path = IVSHMEM_SERVER_DEFAULT_SHM_PATH,
        .shm_size = IVSHMEM_SERVER_DEFAULT_SHM_SIZE,
        .n_vectors = IVSHMEM_SERVER_DEFAULT_N_VECTORS,
    };
    int ret = 1;

    /* parse arguments, will exit on error */
    ivshmem_server_parse_args(&args, argc, argv);

    /* Ignore SIGPIPE, see this link for more info:
     * http://www.mail-archive.com/libevent-users@monkey.org/msg01606.html */
    sa.sa_handler = SIG_IGN;
    sa.sa_flags = 0;
    if (sigemptyset(&sa.sa_mask) == -1 ||
        sigaction(SIGPIPE, &sa, 0) == -1) {
        perror("failed to ignore SIGPIPE; sigaction");
        goto err;
    }

    sa_quit.sa_handler = ivshmem_server_quit_cb;
    sa_quit.sa_flags = 0;
    if (sigemptyset(&sa_quit.sa_mask) == -1 ||
        sigaction(SIGTERM, &sa_quit, 0) == -1) {
        perror("failed to add SIGTERM handler; sigaction");
        goto err;
    }

    /* init the ivshms structure */
    if (ivshmem_server_init(&server, args.unix_socket_path, args.shm_path,
                            args.shm_size, args.n_vectors, args.verbose) < 0) {
        fprintf(stderr, "cannot init server\n");
        goto err;
    }

    /* start the ivshmem server (open shm & unix socket) */
    if (ivshmem_server_start(&server) < 0) {
        fprintf(stderr, "cannot bind\n");
        goto err;
    }

    /* daemonize if asked to */
    if (!args.foreground) {
        FILE *fp;

        if (qemu_daemon(1, 1) < 0) {
            fprintf(stderr, "cannot daemonize: %s\n", strerror(errno));
            goto err_close;
        }

        /* write pid file */
        fp = fopen(args.pid_file, "w");
        if (fp == NULL) {
            fprintf(stderr, "cannot write pid file: %s\n", strerror(errno));
            goto err_close;
        }

        fprintf(fp, "%d\n", (int) getpid());
        fclose(fp);
    }

    ivshmem_server_poll_events(&server);
    fprintf(stdout, "server disconnected\n");
    ret = 0;

err_close:
    ivshmem_server_close(&server);
err:
    return ret;
}

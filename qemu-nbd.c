/*
 *  Copyright (C) 2005  Anthony Liguori <anthony@codemonkey.ws>
 *
 *  Network Block Device
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; under version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu-common.h"
#include "block/block.h"
#include "block/nbd.h"
#include "qemu/main-loop.h"
#include "qemu/sockets.h"
#include "qemu/error-report.h"
#include "block/snapshot.h"

#include <stdarg.h>
#include <stdio.h>
#include <getopt.h>
#include <err.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <signal.h>
#include <libgen.h>
#include <pthread.h>

#define SOCKET_PATH          "/var/lock/qemu-nbd-%s"
#define QEMU_NBD_OPT_CACHE   1
#define QEMU_NBD_OPT_AIO     2
#define QEMU_NBD_OPT_DISCARD 3

static NBDExport *exp;
static int verbose;
static char *srcpath;
static char *sockpath;
static int persistent = 0;
static enum { RUNNING, TERMINATE, TERMINATING, TERMINATED } state;
static int shared = 1;
static int nb_fds;

static void usage(const char *name)
{
    (printf) (
"Usage: %s [OPTIONS] FILE\n"
"QEMU Disk Network Block Device Server\n"
"\n"
"  -h, --help           display this help and exit\n"
"  -V, --version        output version information and exit\n"
"\n"
"Connection properties:\n"
"  -p, --port=PORT      port to listen on (default `%d')\n"
"  -b, --bind=IFACE     interface to bind to (default `0.0.0.0')\n"
"  -k, --socket=PATH    path to the unix socket\n"
"                       (default '"SOCKET_PATH"')\n"
"  -e, --shared=NUM     device can be shared by NUM clients (default '1')\n"
"  -t, --persistent     don't exit on the last connection\n"
"  -v, --verbose        display extra debugging information\n"
"\n"
"Exposing part of the image:\n"
"  -o, --offset=OFFSET  offset into the image\n"
"  -P, --partition=NUM  only expose partition NUM\n"
"\n"
#ifdef __linux__
"Kernel NBD client support:\n"
"  -c, --connect=DEV    connect FILE to the local NBD device DEV\n"
"  -d, --disconnect     disconnect the specified device\n"
"\n"
#endif
"\n"
"Block device options:\n"
"  -f, --format=FORMAT  set image format (raw, qcow2, ...)\n"
"  -r, --read-only      export read-only\n"
"  -s, --snapshot       use FILE as an external snapshot, create a temporary\n"
"                       file with backing_file=FILE, redirect the write to\n"
"                       the temporary one\n"
"  -l, --load-snapshot=SNAPSHOT_PARAM\n"
"                       load an internal snapshot inside FILE and export it\n"
"                       as an read-only device, SNAPSHOT_PARAM format is\n"
"                       'snapshot.id=[ID],snapshot.name=[NAME]', or\n"
"                       '[ID_OR_NAME]'\n"
"  -n, --nocache        disable host cache\n"
"      --cache=MODE     set cache mode (none, writeback, ...)\n"
#ifdef CONFIG_LINUX_AIO
"      --aio=MODE       set AIO mode (native or threads)\n"
#endif
"\n"
"Report bugs to <qemu-devel@nongnu.org>\n"
    , name, NBD_DEFAULT_PORT, "DEVICE");
}

static void version(const char *name)
{
    printf(
"%s version 0.0.1\n"
"Written by Anthony Liguori.\n"
"\n"
"Copyright (C) 2006 Anthony Liguori <anthony@codemonkey.ws>.\n"
"This is free software; see the source for copying conditions.  There is NO\n"
"warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\n"
    , name);
}

struct partition_record
{
    uint8_t bootable;
    uint8_t start_head;
    uint32_t start_cylinder;
    uint8_t start_sector;
    uint8_t system;
    uint8_t end_head;
    uint8_t end_cylinder;
    uint8_t end_sector;
    uint32_t start_sector_abs;
    uint32_t nb_sectors_abs;
};

static void read_partition(uint8_t *p, struct partition_record *r)
{
    r->bootable = p[0];
    r->start_head = p[1];
    r->start_cylinder = p[3] | ((p[2] << 2) & 0x0300);
    r->start_sector = p[2] & 0x3f;
    r->system = p[4];
    r->end_head = p[5];
    r->end_cylinder = p[7] | ((p[6] << 2) & 0x300);
    r->end_sector = p[6] & 0x3f;
    r->start_sector_abs = p[8] | p[9] << 8 | p[10] << 16 | p[11] << 24;
    r->nb_sectors_abs = p[12] | p[13] << 8 | p[14] << 16 | p[15] << 24;
}

static int find_partition(BlockDriverState *bs, int partition,
                          off_t *offset, off_t *size)
{
    struct partition_record mbr[4];
    uint8_t data[512];
    int i;
    int ext_partnum = 4;
    int ret;

    if ((ret = bdrv_read(bs, 0, data, 1)) < 0) {
        errno = -ret;
        err(EXIT_FAILURE, "error while reading");
    }

    if (data[510] != 0x55 || data[511] != 0xaa) {
        return -EINVAL;
    }

    for (i = 0; i < 4; i++) {
        read_partition(&data[446 + 16 * i], &mbr[i]);

        if (!mbr[i].nb_sectors_abs)
            continue;

        if (mbr[i].system == 0xF || mbr[i].system == 0x5) {
            struct partition_record ext[4];
            uint8_t data1[512];
            int j;

            if ((ret = bdrv_read(bs, mbr[i].start_sector_abs, data1, 1)) < 0) {
                errno = -ret;
                err(EXIT_FAILURE, "error while reading");
            }

            for (j = 0; j < 4; j++) {
                read_partition(&data1[446 + 16 * j], &ext[j]);
                if (!ext[j].nb_sectors_abs)
                    continue;

                if ((ext_partnum + j + 1) == partition) {
                    *offset = (uint64_t)ext[j].start_sector_abs << 9;
                    *size = (uint64_t)ext[j].nb_sectors_abs << 9;
                    return 0;
                }
            }
            ext_partnum += 4;
        } else if ((i + 1) == partition) {
            *offset = (uint64_t)mbr[i].start_sector_abs << 9;
            *size = (uint64_t)mbr[i].nb_sectors_abs << 9;
            return 0;
        }
    }

    return -ENOENT;
}

static void termsig_handler(int signum)
{
    state = TERMINATE;
    qemu_notify_event();
}

static void combine_addr(char *buf, size_t len, const char* address,
                         uint16_t port)
{
    /* If the address-part contains a colon, it's an IPv6 IP so needs [] */
    if (strstr(address, ":")) {
        snprintf(buf, len, "[%s]:%u", address, port);
    } else {
        snprintf(buf, len, "%s:%u", address, port);
    }
}

static int tcp_socket_incoming(const char *address, uint16_t port)
{
    char address_and_port[128];
    Error *local_err = NULL;

    combine_addr(address_and_port, 128, address, port);
    int fd = inet_listen(address_and_port, NULL, 0, SOCK_STREAM, 0, &local_err);

    if (local_err != NULL) {
        qerror_report_err(local_err);
        error_free(local_err);
    }
    return fd;
}

static int unix_socket_incoming(const char *path)
{
    Error *local_err = NULL;
    int fd = unix_listen(path, NULL, 0, &local_err);

    if (local_err != NULL) {
        qerror_report_err(local_err);
        error_free(local_err);
    }
    return fd;
}

static int unix_socket_outgoing(const char *path)
{
    Error *local_err = NULL;
    int fd = unix_connect(path, &local_err);

    if (local_err != NULL) {
        qerror_report_err(local_err);
        error_free(local_err);
    }
    return fd;
}

static void *show_parts(void *arg)
{
    char *device = arg;
    int nbd;

    /* linux just needs an open() to trigger
     * the partition table update
     * but remember to load the module with max_part != 0 :
     *     modprobe nbd max_part=63
     */
    nbd = open(device, O_RDWR);
    if (nbd >= 0) {
        close(nbd);
    }
    return NULL;
}

static void *nbd_client_thread(void *arg)
{
    char *device = arg;
    off_t size;
    size_t blocksize;
    uint32_t nbdflags;
    int fd, sock;
    int ret;
    pthread_t show_parts_thread;

    sock = unix_socket_outgoing(sockpath);
    if (sock < 0) {
        goto out;
    }

    ret = nbd_receive_negotiate(sock, NULL, &nbdflags,
                                &size, &blocksize);
    if (ret < 0) {
        goto out_socket;
    }

    fd = open(device, O_RDWR);
    if (fd < 0) {
        /* Linux-only, we can use %m in printf.  */
        fprintf(stderr, "Failed to open %s: %m", device);
        goto out_socket;
    }

    ret = nbd_init(fd, sock, nbdflags, size, blocksize);
    if (ret < 0) {
        goto out_fd;
    }

    /* update partition table */
    pthread_create(&show_parts_thread, NULL, show_parts, device);

    if (verbose) {
        fprintf(stderr, "NBD device %s is now connected to %s\n",
                device, srcpath);
    } else {
        /* Close stderr so that the qemu-nbd process exits.  */
        dup2(STDOUT_FILENO, STDERR_FILENO);
    }

    ret = nbd_client(fd);
    if (ret) {
        goto out_fd;
    }
    close(fd);
    kill(getpid(), SIGTERM);
    return (void *) EXIT_SUCCESS;

out_fd:
    close(fd);
out_socket:
    closesocket(sock);
out:
    kill(getpid(), SIGTERM);
    return (void *) EXIT_FAILURE;
}

static int nbd_can_accept(void *opaque)
{
    return nb_fds < shared;
}

static void nbd_export_closed(NBDExport *exp)
{
    assert(state == TERMINATING);
    state = TERMINATED;
}

static void nbd_client_closed(NBDClient *client)
{
    nb_fds--;
    if (nb_fds == 0 && !persistent && state == RUNNING) {
        state = TERMINATE;
    }
    qemu_notify_event();
    nbd_client_put(client);
}

static void nbd_accept(void *opaque)
{
    int server_fd = (uintptr_t) opaque;
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);

    int fd = accept(server_fd, (struct sockaddr *)&addr, &addr_len);
    if (fd < 0) {
        perror("accept");
        return;
    }

    if (state >= TERMINATE) {
        close(fd);
        return;
    }

    if (nbd_client_new(exp, fd, nbd_client_closed)) {
        nb_fds++;
    } else {
        shutdown(fd, 2);
        close(fd);
    }
}

int main(int argc, char **argv)
{
    BlockDriverState *bs;
    BlockDriver *drv;
    off_t dev_offset = 0;
    uint32_t nbdflags = 0;
    bool disconnect = false;
    const char *bindto = "0.0.0.0";
    char *device = NULL;
    int port = NBD_DEFAULT_PORT;
    off_t fd_size;
    QemuOpts *sn_opts = NULL;
    const char *sn_id_or_name = NULL;
    const char *sopt = "hVb:o:p:rsnP:c:dvk:e:f:tl:";
    struct option lopt[] = {
        { "help", 0, NULL, 'h' },
        { "version", 0, NULL, 'V' },
        { "bind", 1, NULL, 'b' },
        { "port", 1, NULL, 'p' },
        { "socket", 1, NULL, 'k' },
        { "offset", 1, NULL, 'o' },
        { "read-only", 0, NULL, 'r' },
        { "partition", 1, NULL, 'P' },
        { "connect", 1, NULL, 'c' },
        { "disconnect", 0, NULL, 'd' },
        { "snapshot", 0, NULL, 's' },
        { "load-snapshot", 1, NULL, 'l' },
        { "nocache", 0, NULL, 'n' },
        { "cache", 1, NULL, QEMU_NBD_OPT_CACHE },
#ifdef CONFIG_LINUX_AIO
        { "aio", 1, NULL, QEMU_NBD_OPT_AIO },
#endif
        { "discard", 1, NULL, QEMU_NBD_OPT_DISCARD },
        { "shared", 1, NULL, 'e' },
        { "format", 1, NULL, 'f' },
        { "persistent", 0, NULL, 't' },
        { "verbose", 0, NULL, 'v' },
        { NULL, 0, NULL, 0 }
    };
    int ch;
    int opt_ind = 0;
    int li;
    char *end;
    int flags = BDRV_O_RDWR;
    int partition = -1;
    int ret;
    int fd;
    bool seen_cache = false;
    bool seen_discard = false;
#ifdef CONFIG_LINUX_AIO
    bool seen_aio = false;
#endif
    pthread_t client_thread;
    const char *fmt = NULL;
    Error *local_err = NULL;

    /* The client thread uses SIGTERM to interrupt the server.  A signal
     * handler ensures that "qemu-nbd -v -c" exits with a nice status code.
     */
    struct sigaction sa_sigterm;
    memset(&sa_sigterm, 0, sizeof(sa_sigterm));
    sa_sigterm.sa_handler = termsig_handler;
    sigaction(SIGTERM, &sa_sigterm, NULL);
    qemu_init_exec_dir(argv[0]);

    while ((ch = getopt_long(argc, argv, sopt, lopt, &opt_ind)) != -1) {
        switch (ch) {
        case 's':
            flags |= BDRV_O_SNAPSHOT;
            break;
        case 'n':
            optarg = (char *) "none";
            /* fallthrough */
        case QEMU_NBD_OPT_CACHE:
            if (seen_cache) {
                errx(EXIT_FAILURE, "-n and --cache can only be specified once");
            }
            seen_cache = true;
            if (bdrv_parse_cache_flags(optarg, &flags) == -1) {
                errx(EXIT_FAILURE, "Invalid cache mode `%s'", optarg);
            }
            break;
#ifdef CONFIG_LINUX_AIO
        case QEMU_NBD_OPT_AIO:
            if (seen_aio) {
                errx(EXIT_FAILURE, "--aio can only be specified once");
            }
            seen_aio = true;
            if (!strcmp(optarg, "native")) {
                flags |= BDRV_O_NATIVE_AIO;
            } else if (!strcmp(optarg, "threads")) {
                /* this is the default */
            } else {
               errx(EXIT_FAILURE, "invalid aio mode `%s'", optarg);
            }
            break;
#endif
        case QEMU_NBD_OPT_DISCARD:
            if (seen_discard) {
                errx(EXIT_FAILURE, "--discard can only be specified once");
            }
            seen_discard = true;
            if (bdrv_parse_discard_flags(optarg, &flags) == -1) {
                errx(EXIT_FAILURE, "Invalid discard mode `%s'", optarg);
            }
            break;
        case 'b':
            bindto = optarg;
            break;
        case 'p':
            li = strtol(optarg, &end, 0);
            if (*end) {
                errx(EXIT_FAILURE, "Invalid port `%s'", optarg);
            }
            if (li < 1 || li > 65535) {
                errx(EXIT_FAILURE, "Port out of range `%s'", optarg);
            }
            port = (uint16_t)li;
            break;
        case 'o':
                dev_offset = strtoll (optarg, &end, 0);
            if (*end) {
                errx(EXIT_FAILURE, "Invalid offset `%s'", optarg);
            }
            if (dev_offset < 0) {
                errx(EXIT_FAILURE, "Offset must be positive `%s'", optarg);
            }
            break;
        case 'l':
            if (strstart(optarg, SNAPSHOT_OPT_BASE, NULL)) {
                sn_opts = qemu_opts_parse(&internal_snapshot_opts, optarg, 0);
                if (!sn_opts) {
                    errx(EXIT_FAILURE, "Failed in parsing snapshot param `%s'",
                         optarg);
                }
            } else {
                sn_id_or_name = optarg;
            }
            /* fall through */
        case 'r':
            nbdflags |= NBD_FLAG_READ_ONLY;
            flags &= ~BDRV_O_RDWR;
            break;
        case 'P':
            partition = strtol(optarg, &end, 0);
            if (*end)
                errx(EXIT_FAILURE, "Invalid partition `%s'", optarg);
            if (partition < 1 || partition > 8)
                errx(EXIT_FAILURE, "Invalid partition %d", partition);
            break;
        case 'k':
            sockpath = optarg;
            if (sockpath[0] != '/')
                errx(EXIT_FAILURE, "socket path must be absolute\n");
            break;
        case 'd':
            disconnect = true;
            break;
        case 'c':
            device = optarg;
            break;
        case 'e':
            shared = strtol(optarg, &end, 0);
            if (*end) {
                errx(EXIT_FAILURE, "Invalid shared device number '%s'", optarg);
            }
            if (shared < 1) {
                errx(EXIT_FAILURE, "Shared device number must be greater than 0\n");
            }
            break;
        case 'f':
            fmt = optarg;
            break;
	case 't':
	    persistent = 1;
	    break;
        case 'v':
            verbose = 1;
            break;
        case 'V':
            version(argv[0]);
            exit(0);
            break;
        case 'h':
            usage(argv[0]);
            exit(0);
            break;
        case '?':
            errx(EXIT_FAILURE, "Try `%s --help' for more information.",
                 argv[0]);
        }
    }

    if ((argc - optind) != 1) {
        errx(EXIT_FAILURE, "Invalid number of argument.\n"
             "Try `%s --help' for more information.",
             argv[0]);
    }

    if (disconnect) {
        fd = open(argv[optind], O_RDWR);
        if (fd < 0) {
            err(EXIT_FAILURE, "Cannot open %s", argv[optind]);
        }
        nbd_disconnect(fd);

        close(fd);

        printf("%s disconnected\n", argv[optind]);

	return 0;
    }

    if (device && !verbose) {
        int stderr_fd[2];
        pid_t pid;
        int ret;

        if (qemu_pipe(stderr_fd) < 0) {
            err(EXIT_FAILURE, "Error setting up communication pipe");
        }

        /* Now daemonize, but keep a communication channel open to
         * print errors and exit with the proper status code.
         */
        pid = fork();
        if (pid == 0) {
            close(stderr_fd[0]);
            ret = qemu_daemon(1, 0);

            /* Temporarily redirect stderr to the parent's pipe...  */
            dup2(stderr_fd[1], STDERR_FILENO);
            if (ret < 0) {
                err(EXIT_FAILURE, "Failed to daemonize");
            }

            /* ... close the descriptor we inherited and go on.  */
            close(stderr_fd[1]);
        } else {
            bool errors = false;
            char *buf;

            /* In the parent.  Print error messages from the child until
             * it closes the pipe.
             */
            close(stderr_fd[1]);
            buf = g_malloc(1024);
            while ((ret = read(stderr_fd[0], buf, 1024)) > 0) {
                errors = true;
                ret = qemu_write_full(STDERR_FILENO, buf, ret);
                if (ret < 0) {
                    exit(EXIT_FAILURE);
                }
            }
            if (ret < 0) {
                err(EXIT_FAILURE, "Cannot read from daemon");
            }

            /* Usually the daemon should not print any message.
             * Exit with zero status in that case.
             */
            exit(errors);
        }
    }

    if (device != NULL && sockpath == NULL) {
        sockpath = g_malloc(128);
        snprintf(sockpath, 128, SOCKET_PATH, basename(device));
    }

    qemu_init_main_loop();
    bdrv_init();
    atexit(bdrv_close_all);

    if (fmt) {
        drv = bdrv_find_format(fmt);
        if (!drv) {
            errx(EXIT_FAILURE, "Unknown file format '%s'", fmt);
        }
    } else {
        drv = NULL;
    }

    bs = bdrv_new("hda");
    srcpath = argv[optind];
    ret = bdrv_open(&bs, srcpath, NULL, NULL, flags, drv, &local_err);
    if (ret < 0) {
        errno = -ret;
        err(EXIT_FAILURE, "Failed to bdrv_open '%s': %s", argv[optind],
            error_get_pretty(local_err));
    }

    if (sn_opts) {
        ret = bdrv_snapshot_load_tmp(bs,
                                     qemu_opt_get(sn_opts, SNAPSHOT_OPT_ID),
                                     qemu_opt_get(sn_opts, SNAPSHOT_OPT_NAME),
                                     &local_err);
    } else if (sn_id_or_name) {
        ret = bdrv_snapshot_load_tmp_by_id_or_name(bs, sn_id_or_name,
                                                   &local_err);
    }
    if (ret < 0) {
        errno = -ret;
        err(EXIT_FAILURE,
            "Failed to load snapshot: %s",
            error_get_pretty(local_err));
    }

    fd_size = bdrv_getlength(bs);

    if (partition != -1) {
        ret = find_partition(bs, partition, &dev_offset, &fd_size);
        if (ret < 0) {
            errno = -ret;
            err(EXIT_FAILURE, "Could not find partition %d", partition);
        }
    }

    exp = nbd_export_new(bs, dev_offset, fd_size, nbdflags, nbd_export_closed);

    if (sockpath) {
        fd = unix_socket_incoming(sockpath);
    } else {
        fd = tcp_socket_incoming(bindto, port);
    }

    if (fd < 0) {
        return 1;
    }

    if (device) {
        int ret;

        ret = pthread_create(&client_thread, NULL, nbd_client_thread, device);
        if (ret != 0) {
            errx(EXIT_FAILURE, "Failed to create client thread: %s",
                 strerror(ret));
        }
    } else {
        /* Shut up GCC warnings.  */
        memset(&client_thread, 0, sizeof(client_thread));
    }

    qemu_set_fd_handler2(fd, nbd_can_accept, nbd_accept, NULL,
                         (void *)(uintptr_t)fd);

    /* now when the initialization is (almost) complete, chdir("/")
     * to free any busy filesystems */
    if (chdir("/") < 0) {
        err(EXIT_FAILURE, "Could not chdir to root directory");
    }

    state = RUNNING;
    do {
        main_loop_wait(false);
        if (state == TERMINATE) {
            state = TERMINATING;
            nbd_export_close(exp);
            nbd_export_put(exp);
            exp = NULL;
        }
    } while (state != TERMINATED);

    bdrv_close(bs);
    if (sockpath) {
        unlink(sockpath);
    }

    if (sn_opts) {
        qemu_opts_del(sn_opts);
    }

    if (device) {
        void *ret;
        pthread_join(client_thread, &ret);
        exit(ret != NULL);
    } else {
        exit(EXIT_SUCCESS);
    }
}

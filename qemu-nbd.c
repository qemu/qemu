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

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu-common.h"
#include "qemu/cutils.h"
#include "sysemu/block-backend.h"
#include "block/block_int.h"
#include "block/nbd.h"
#include "qemu/main-loop.h"
#include "qemu/error-report.h"
#include "qemu/config-file.h"
#include "qemu/bswap.h"
#include "block/snapshot.h"
#include "qapi/util.h"
#include "qapi/qmp/qstring.h"
#include "qom/object_interfaces.h"
#include "io/channel-socket.h"
#include "crypto/init.h"

#include <getopt.h>
#include <libgen.h>
#include <pthread.h>

#define SOCKET_PATH                "/var/lock/qemu-nbd-%s"
#define QEMU_NBD_OPT_CACHE         256
#define QEMU_NBD_OPT_AIO           257
#define QEMU_NBD_OPT_DISCARD       258
#define QEMU_NBD_OPT_DETECT_ZEROES 259
#define QEMU_NBD_OPT_OBJECT        260
#define QEMU_NBD_OPT_TLSCREDS      261
#define QEMU_NBD_OPT_IMAGE_OPTS    262

#define MBR_SIZE 512

static NBDExport *exp;
static bool newproto;
static int verbose;
static char *srcpath;
static SocketAddress *saddr;
static int persistent = 0;
static enum { RUNNING, TERMINATE, TERMINATING, TERMINATED } state;
static int shared = 1;
static int nb_fds;
static QIOChannelSocket *server_ioc;
static int server_watch = -1;
static QCryptoTLSCreds *tlscreds;

static void usage(const char *name)
{
    (printf) (
"Usage: %s [OPTIONS] FILE\n"
"QEMU Disk Network Block Device Server\n"
"\n"
"  -h, --help                display this help and exit\n"
"  -V, --version             output version information and exit\n"
"\n"
"Connection properties:\n"
"  -p, --port=PORT           port to listen on (default `%d')\n"
"  -b, --bind=IFACE          interface to bind to (default `0.0.0.0')\n"
"  -k, --socket=PATH         path to the unix socket\n"
"                            (default '"SOCKET_PATH"')\n"
"  -e, --shared=NUM          device can be shared by NUM clients (default '1')\n"
"  -t, --persistent          don't exit on the last connection\n"
"  -v, --verbose             display extra debugging information\n"
"  -x, --export-name=NAME    expose export by name\n"
"\n"
"Exposing part of the image:\n"
"  -o, --offset=OFFSET       offset into the image\n"
"  -P, --partition=NUM       only expose partition NUM\n"
"\n"
"General purpose options:\n"
"  --object type,id=ID,...   define an object such as 'secret' for providing\n"
"                            passwords and/or encryption keys\n"
#ifdef __linux__
"Kernel NBD client support:\n"
"  -c, --connect=DEV         connect FILE to the local NBD device DEV\n"
"  -d, --disconnect          disconnect the specified device\n"
"\n"
#endif
"\n"
"Block device options:\n"
"  -f, --format=FORMAT       set image format (raw, qcow2, ...)\n"
"  -r, --read-only           export read-only\n"
"  -s, --snapshot            use FILE as an external snapshot, create a temporary\n"
"                            file with backing_file=FILE, redirect the write to\n"
"                            the temporary one\n"
"  -l, --load-snapshot=SNAPSHOT_PARAM\n"
"                            load an internal snapshot inside FILE and export it\n"
"                            as an read-only device, SNAPSHOT_PARAM format is\n"
"                            'snapshot.id=[ID],snapshot.name=[NAME]', or\n"
"                            '[ID_OR_NAME]'\n"
"  -n, --nocache             disable host cache\n"
"      --cache=MODE          set cache mode (none, writeback, ...)\n"
"      --aio=MODE            set AIO mode (native or threads)\n"
"      --discard=MODE        set discard mode (ignore, unmap)\n"
"      --detect-zeroes=MODE  set detect-zeroes mode (off, on, unmap)\n"
"      --image-opts          treat FILE as a full set of image options\n"
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

    r->start_sector_abs = le32_to_cpup((uint32_t *)(p +  8));
    r->nb_sectors_abs   = le32_to_cpup((uint32_t *)(p + 12));
}

static int find_partition(BlockBackend *blk, int partition,
                          off_t *offset, off_t *size)
{
    struct partition_record mbr[4];
    uint8_t data[MBR_SIZE];
    int i;
    int ext_partnum = 4;
    int ret;

    ret = blk_pread(blk, 0, data, sizeof(data));
    if (ret < 0) {
        error_report("error while reading: %s", strerror(-ret));
        exit(EXIT_FAILURE);
    }

    if (data[510] != 0x55 || data[511] != 0xaa) {
        return -EINVAL;
    }

    for (i = 0; i < 4; i++) {
        read_partition(&data[446 + 16 * i], &mbr[i]);

        if (!mbr[i].system || !mbr[i].nb_sectors_abs) {
            continue;
        }

        if (mbr[i].system == 0xF || mbr[i].system == 0x5) {
            struct partition_record ext[4];
            uint8_t data1[MBR_SIZE];
            int j;

            ret = blk_pread(blk, mbr[i].start_sector_abs * MBR_SIZE,
                            data1, sizeof(data1));
            if (ret < 0) {
                error_report("error while reading: %s", strerror(-ret));
                exit(EXIT_FAILURE);
            }

            for (j = 0; j < 4; j++) {
                read_partition(&data1[446 + 16 * j], &ext[j]);
                if (!ext[j].system || !ext[j].nb_sectors_abs) {
                    continue;
                }

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
    atomic_cmpxchg(&state, RUNNING, TERMINATE);
    qemu_notify_event();
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
    uint32_t nbdflags;
    QIOChannelSocket *sioc;
    int fd;
    int ret;
    pthread_t show_parts_thread;
    Error *local_error = NULL;

    sioc = qio_channel_socket_new();
    if (qio_channel_socket_connect_sync(sioc,
                                        saddr,
                                        &local_error) < 0) {
        error_report_err(local_error);
        goto out;
    }

    ret = nbd_receive_negotiate(QIO_CHANNEL(sioc), NULL, &nbdflags,
                                NULL, NULL, NULL,
                                &size, &local_error);
    if (ret < 0) {
        if (local_error) {
            error_report_err(local_error);
        }
        goto out_socket;
    }

    fd = open(device, O_RDWR);
    if (fd < 0) {
        /* Linux-only, we can use %m in printf.  */
        error_report("Failed to open %s: %m", device);
        goto out_socket;
    }

    ret = nbd_init(fd, sioc, nbdflags, size);
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
    object_unref(OBJECT(sioc));
    kill(getpid(), SIGTERM);
    return (void *) EXIT_SUCCESS;

out_fd:
    close(fd);
out_socket:
    object_unref(OBJECT(sioc));
out:
    kill(getpid(), SIGTERM);
    return (void *) EXIT_FAILURE;
}

static int nbd_can_accept(void)
{
    return nb_fds < shared;
}

static void nbd_export_closed(NBDExport *exp)
{
    assert(state == TERMINATING);
    state = TERMINATED;
}

static void nbd_update_server_watch(void);

static void nbd_client_closed(NBDClient *client)
{
    nb_fds--;
    if (nb_fds == 0 && !persistent && state == RUNNING) {
        state = TERMINATE;
    }
    nbd_update_server_watch();
    nbd_client_put(client);
}

static gboolean nbd_accept(QIOChannel *ioc, GIOCondition cond, gpointer opaque)
{
    QIOChannelSocket *cioc;

    cioc = qio_channel_socket_accept(QIO_CHANNEL_SOCKET(ioc),
                                     NULL);
    if (!cioc) {
        return TRUE;
    }

    if (state >= TERMINATE) {
        object_unref(OBJECT(cioc));
        return TRUE;
    }

    nb_fds++;
    nbd_update_server_watch();
    nbd_client_new(newproto ? NULL : exp, cioc,
                   tlscreds, NULL, nbd_client_closed);
    object_unref(OBJECT(cioc));

    return TRUE;
}

static void nbd_update_server_watch(void)
{
    if (nbd_can_accept()) {
        if (server_watch == -1) {
            server_watch = qio_channel_add_watch(QIO_CHANNEL(server_ioc),
                                                 G_IO_IN,
                                                 nbd_accept,
                                                 NULL, NULL);
        }
    } else {
        if (server_watch != -1) {
            g_source_remove(server_watch);
            server_watch = -1;
        }
    }
}


static SocketAddress *nbd_build_socket_address(const char *sockpath,
                                               const char *bindto,
                                               const char *port)
{
    SocketAddress *saddr;

    saddr = g_new0(SocketAddress, 1);
    if (sockpath) {
        saddr->type = SOCKET_ADDRESS_KIND_UNIX;
        saddr->u.q_unix.data = g_new0(UnixSocketAddress, 1);
        saddr->u.q_unix.data->path = g_strdup(sockpath);
    } else {
        InetSocketAddress *inet;
        saddr->type = SOCKET_ADDRESS_KIND_INET;
        inet = saddr->u.inet.data = g_new0(InetSocketAddress, 1);
        inet->host = g_strdup(bindto);
        if (port) {
            inet->port = g_strdup(port);
        } else  {
            inet->port = g_strdup_printf("%d", NBD_DEFAULT_PORT);
        }
    }

    return saddr;
}


static QemuOptsList file_opts = {
    .name = "file",
    .implied_opt_name = "file",
    .head = QTAILQ_HEAD_INITIALIZER(file_opts.head),
    .desc = {
        /* no elements => accept any params */
        { /* end of list */ }
    },
};

static QemuOptsList qemu_object_opts = {
    .name = "object",
    .implied_opt_name = "qom-type",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_object_opts.head),
    .desc = {
        { }
    },
};



static QCryptoTLSCreds *nbd_get_tls_creds(const char *id, Error **errp)
{
    Object *obj;
    QCryptoTLSCreds *creds;

    obj = object_resolve_path_component(
        object_get_objects_root(), id);
    if (!obj) {
        error_setg(errp, "No TLS credentials with id '%s'",
                   id);
        return NULL;
    }
    creds = (QCryptoTLSCreds *)
        object_dynamic_cast(obj, TYPE_QCRYPTO_TLS_CREDS);
    if (!creds) {
        error_setg(errp, "Object with id '%s' is not TLS credentials",
                   id);
        return NULL;
    }

    if (creds->endpoint != QCRYPTO_TLS_CREDS_ENDPOINT_SERVER) {
        error_setg(errp,
                   "Expecting TLS credentials with a server endpoint");
        return NULL;
    }
    object_ref(obj);
    return creds;
}


int main(int argc, char **argv)
{
    BlockBackend *blk;
    BlockDriverState *bs;
    off_t dev_offset = 0;
    uint32_t nbdflags = 0;
    bool disconnect = false;
    const char *bindto = "0.0.0.0";
    const char *port = NULL;
    char *sockpath = NULL;
    char *device = NULL;
    off_t fd_size;
    QemuOpts *sn_opts = NULL;
    const char *sn_id_or_name = NULL;
    const char *sopt = "hVb:o:p:rsnP:c:dvk:e:f:tl:x:";
    struct option lopt[] = {
        { "help", no_argument, NULL, 'h' },
        { "version", no_argument, NULL, 'V' },
        { "bind", required_argument, NULL, 'b' },
        { "port", required_argument, NULL, 'p' },
        { "socket", required_argument, NULL, 'k' },
        { "offset", required_argument, NULL, 'o' },
        { "read-only", no_argument, NULL, 'r' },
        { "partition", required_argument, NULL, 'P' },
        { "connect", required_argument, NULL, 'c' },
        { "disconnect", no_argument, NULL, 'd' },
        { "snapshot", no_argument, NULL, 's' },
        { "load-snapshot", required_argument, NULL, 'l' },
        { "nocache", no_argument, NULL, 'n' },
        { "cache", required_argument, NULL, QEMU_NBD_OPT_CACHE },
        { "aio", required_argument, NULL, QEMU_NBD_OPT_AIO },
        { "discard", required_argument, NULL, QEMU_NBD_OPT_DISCARD },
        { "detect-zeroes", required_argument, NULL,
          QEMU_NBD_OPT_DETECT_ZEROES },
        { "shared", required_argument, NULL, 'e' },
        { "format", required_argument, NULL, 'f' },
        { "persistent", no_argument, NULL, 't' },
        { "verbose", no_argument, NULL, 'v' },
        { "object", required_argument, NULL, QEMU_NBD_OPT_OBJECT },
        { "export-name", required_argument, NULL, 'x' },
        { "tls-creds", required_argument, NULL, QEMU_NBD_OPT_TLSCREDS },
        { "image-opts", no_argument, NULL, QEMU_NBD_OPT_IMAGE_OPTS },
        { NULL, 0, NULL, 0 }
    };
    int ch;
    int opt_ind = 0;
    char *end;
    int flags = BDRV_O_RDWR;
    int partition = -1;
    int ret = 0;
    bool seen_cache = false;
    bool seen_discard = false;
    bool seen_aio = false;
    pthread_t client_thread;
    const char *fmt = NULL;
    Error *local_err = NULL;
    BlockdevDetectZeroesOptions detect_zeroes = BLOCKDEV_DETECT_ZEROES_OPTIONS_OFF;
    QDict *options = NULL;
    const char *export_name = NULL;
    const char *tlscredsid = NULL;
    bool imageOpts = false;
    bool writethrough = true;

    /* The client thread uses SIGTERM to interrupt the server.  A signal
     * handler ensures that "qemu-nbd -v -c" exits with a nice status code.
     */
    struct sigaction sa_sigterm;
    memset(&sa_sigterm, 0, sizeof(sa_sigterm));
    sa_sigterm.sa_handler = termsig_handler;
    sigaction(SIGTERM, &sa_sigterm, NULL);

    qcrypto_init(&error_fatal);

    module_call_init(MODULE_INIT_QOM);
    qemu_add_opts(&qemu_object_opts);
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
                error_report("-n and --cache can only be specified once");
                exit(EXIT_FAILURE);
            }
            seen_cache = true;
            if (bdrv_parse_cache_mode(optarg, &flags, &writethrough) == -1) {
                error_report("Invalid cache mode `%s'", optarg);
                exit(EXIT_FAILURE);
            }
            break;
        case QEMU_NBD_OPT_AIO:
            if (seen_aio) {
                error_report("--aio can only be specified once");
                exit(EXIT_FAILURE);
            }
            seen_aio = true;
            if (!strcmp(optarg, "native")) {
                flags |= BDRV_O_NATIVE_AIO;
            } else if (!strcmp(optarg, "threads")) {
                /* this is the default */
            } else {
               error_report("invalid aio mode `%s'", optarg);
               exit(EXIT_FAILURE);
            }
            break;
        case QEMU_NBD_OPT_DISCARD:
            if (seen_discard) {
                error_report("--discard can only be specified once");
                exit(EXIT_FAILURE);
            }
            seen_discard = true;
            if (bdrv_parse_discard_flags(optarg, &flags) == -1) {
                error_report("Invalid discard mode `%s'", optarg);
                exit(EXIT_FAILURE);
            }
            break;
        case QEMU_NBD_OPT_DETECT_ZEROES:
            detect_zeroes =
                qapi_enum_parse(BlockdevDetectZeroesOptions_lookup,
                                optarg,
                                BLOCKDEV_DETECT_ZEROES_OPTIONS__MAX,
                                BLOCKDEV_DETECT_ZEROES_OPTIONS_OFF,
                                &local_err);
            if (local_err) {
                error_reportf_err(local_err,
                                  "Failed to parse detect_zeroes mode: ");
                exit(EXIT_FAILURE);
            }
            if (detect_zeroes == BLOCKDEV_DETECT_ZEROES_OPTIONS_UNMAP &&
                !(flags & BDRV_O_UNMAP)) {
                error_report("setting detect-zeroes to unmap is not allowed "
                             "without setting discard operation to unmap");
                exit(EXIT_FAILURE);
            }
            break;
        case 'b':
            bindto = optarg;
            break;
        case 'p':
            port = optarg;
            break;
        case 'o':
                dev_offset = strtoll (optarg, &end, 0);
            if (*end) {
                error_report("Invalid offset `%s'", optarg);
                exit(EXIT_FAILURE);
            }
            if (dev_offset < 0) {
                error_report("Offset must be positive `%s'", optarg);
                exit(EXIT_FAILURE);
            }
            break;
        case 'l':
            if (strstart(optarg, SNAPSHOT_OPT_BASE, NULL)) {
                sn_opts = qemu_opts_parse_noisily(&internal_snapshot_opts,
                                                  optarg, false);
                if (!sn_opts) {
                    error_report("Failed in parsing snapshot param `%s'",
                                 optarg);
                    exit(EXIT_FAILURE);
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
            if (*end) {
                error_report("Invalid partition `%s'", optarg);
                exit(EXIT_FAILURE);
            }
            if (partition < 1 || partition > 8) {
                error_report("Invalid partition %d", partition);
                exit(EXIT_FAILURE);
            }
            break;
        case 'k':
            sockpath = optarg;
            if (sockpath[0] != '/') {
                error_report("socket path must be absolute");
                exit(EXIT_FAILURE);
            }
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
                error_report("Invalid shared device number '%s'", optarg);
                exit(EXIT_FAILURE);
            }
            if (shared < 1) {
                error_report("Shared device number must be greater than 0");
                exit(EXIT_FAILURE);
            }
            break;
        case 'f':
            fmt = optarg;
            break;
        case 't':
            persistent = 1;
            break;
        case 'x':
            export_name = optarg;
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
            error_report("Try `%s --help' for more information.", argv[0]);
            exit(EXIT_FAILURE);
        case QEMU_NBD_OPT_OBJECT: {
            QemuOpts *opts;
            opts = qemu_opts_parse_noisily(&qemu_object_opts,
                                           optarg, true);
            if (!opts) {
                exit(EXIT_FAILURE);
            }
        }   break;
        case QEMU_NBD_OPT_TLSCREDS:
            tlscredsid = optarg;
            break;
        case QEMU_NBD_OPT_IMAGE_OPTS:
            imageOpts = true;
            break;
        }
    }

    if ((argc - optind) != 1) {
        error_report("Invalid number of arguments");
        error_printf("Try `%s --help' for more information.\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    if (qemu_opts_foreach(&qemu_object_opts,
                          user_creatable_add_opts_foreach,
                          NULL, NULL)) {
        exit(EXIT_FAILURE);
    }

    if (tlscredsid) {
        if (sockpath) {
            error_report("TLS is only supported with IPv4/IPv6");
            exit(EXIT_FAILURE);
        }
        if (device) {
            error_report("TLS is not supported with a host device");
            exit(EXIT_FAILURE);
        }
        if (!export_name) {
            /* Set the default NBD protocol export name, since
             * we *must* use new style protocol for TLS */
            export_name = "";
        }
        tlscreds = nbd_get_tls_creds(tlscredsid, &local_err);
        if (local_err) {
            error_report("Failed to get TLS creds %s",
                         error_get_pretty(local_err));
            exit(EXIT_FAILURE);
        }
    }

    if (disconnect) {
        int nbdfd = open(argv[optind], O_RDWR);
        if (nbdfd < 0) {
            error_report("Cannot open %s: %s", argv[optind],
                         strerror(errno));
            exit(EXIT_FAILURE);
        }
        nbd_disconnect(nbdfd);

        close(nbdfd);

        printf("%s disconnected\n", argv[optind]);

        return 0;
    }

    if (device && !verbose) {
        int stderr_fd[2];
        pid_t pid;
        int ret;

        if (qemu_pipe(stderr_fd) < 0) {
            error_report("Error setting up communication pipe: %s",
                         strerror(errno));
            exit(EXIT_FAILURE);
        }

        /* Now daemonize, but keep a communication channel open to
         * print errors and exit with the proper status code.
         */
        pid = fork();
        if (pid < 0) {
            error_report("Failed to fork: %s", strerror(errno));
            exit(EXIT_FAILURE);
        } else if (pid == 0) {
            close(stderr_fd[0]);
            ret = qemu_daemon(1, 0);

            /* Temporarily redirect stderr to the parent's pipe...  */
            dup2(stderr_fd[1], STDERR_FILENO);
            if (ret < 0) {
                error_report("Failed to daemonize: %s", strerror(errno));
                exit(EXIT_FAILURE);
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
                error_report("Cannot read from daemon: %s",
                             strerror(errno));
                exit(EXIT_FAILURE);
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

    saddr = nbd_build_socket_address(sockpath, bindto, port);

    if (qemu_init_main_loop(&local_err)) {
        error_report_err(local_err);
        exit(EXIT_FAILURE);
    }
    bdrv_init();
    atexit(bdrv_close_all);

    srcpath = argv[optind];
    if (imageOpts) {
        QemuOpts *opts;
        if (fmt) {
            error_report("--image-opts and -f are mutually exclusive");
            exit(EXIT_FAILURE);
        }
        opts = qemu_opts_parse_noisily(&file_opts, srcpath, true);
        if (!opts) {
            qemu_opts_reset(&file_opts);
            exit(EXIT_FAILURE);
        }
        options = qemu_opts_to_qdict(opts, NULL);
        qemu_opts_reset(&file_opts);
        blk = blk_new_open(NULL, NULL, options, flags, &local_err);
    } else {
        if (fmt) {
            options = qdict_new();
            qdict_put(options, "driver", qstring_from_str(fmt));
        }
        blk = blk_new_open(srcpath, NULL, options, flags, &local_err);
    }

    if (!blk) {
        error_reportf_err(local_err, "Failed to blk_new_open '%s': ",
                          argv[optind]);
        exit(EXIT_FAILURE);
    }
    bs = blk_bs(blk);

    blk_set_enable_write_cache(blk, !writethrough);

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
        error_reportf_err(local_err, "Failed to load snapshot: ");
        exit(EXIT_FAILURE);
    }

    bs->detect_zeroes = detect_zeroes;
    fd_size = blk_getlength(blk);
    if (fd_size < 0) {
        error_report("Failed to determine the image length: %s",
                     strerror(-fd_size));
        exit(EXIT_FAILURE);
    }

    if (partition != -1) {
        ret = find_partition(blk, partition, &dev_offset, &fd_size);
        if (ret < 0) {
            error_report("Could not find partition %d: %s", partition,
                         strerror(-ret));
            exit(EXIT_FAILURE);
        }
    }

    exp = nbd_export_new(blk, dev_offset, fd_size, nbdflags, nbd_export_closed,
                         &local_err);
    if (!exp) {
        error_report_err(local_err);
        exit(EXIT_FAILURE);
    }
    if (export_name) {
        nbd_export_set_name(exp, export_name);
        newproto = true;
    }

    server_ioc = qio_channel_socket_new();
    if (qio_channel_socket_listen_sync(server_ioc, saddr, &local_err) < 0) {
        object_unref(OBJECT(server_ioc));
        error_report_err(local_err);
        return 1;
    }

    if (device) {
        int ret;

        ret = pthread_create(&client_thread, NULL, nbd_client_thread, device);
        if (ret != 0) {
            error_report("Failed to create client thread: %s", strerror(ret));
            exit(EXIT_FAILURE);
        }
    } else {
        /* Shut up GCC warnings.  */
        memset(&client_thread, 0, sizeof(client_thread));
    }

    nbd_update_server_watch();

    /* now when the initialization is (almost) complete, chdir("/")
     * to free any busy filesystems */
    if (chdir("/") < 0) {
        error_report("Could not chdir to root directory: %s",
                     strerror(errno));
        exit(EXIT_FAILURE);
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

    blk_unref(blk);
    if (sockpath) {
        unlink(sockpath);
    }

    qemu_opts_del(sn_opts);

    if (device) {
        void *ret;
        pthread_join(client_thread, &ret);
        exit(ret != NULL);
    } else {
        exit(EXIT_SUCCESS);
    }
}

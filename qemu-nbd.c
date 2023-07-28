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
#include <getopt.h>
#include <libgen.h>
#include <pthread.h>

#include "qemu/help-texts.h"
#include "qapi/error.h"
#include "qemu/cutils.h"
#include "sysemu/block-backend.h"
#include "sysemu/runstate.h" /* for qemu_system_killed() prototype */
#include "block/block_int.h"
#include "block/nbd.h"
#include "qemu/main-loop.h"
#include "qemu/module.h"
#include "qemu/option.h"
#include "qemu/error-report.h"
#include "qemu/config-file.h"
#include "qemu/bswap.h"
#include "qemu/log.h"
#include "qemu/systemd.h"
#include "block/snapshot.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qstring.h"
#include "qom/object_interfaces.h"
#include "io/channel-socket.h"
#include "io/net-listener.h"
#include "crypto/init.h"
#include "crypto/tlscreds.h"
#include "trace/control.h"
#include "qemu-version.h"

#ifdef CONFIG_SELINUX
#include <selinux/selinux.h>
#endif

#ifdef __linux__
#define HAVE_NBD_DEVICE 1
#else
#define HAVE_NBD_DEVICE 0
#endif

#define SOCKET_PATH                "/var/lock/qemu-nbd-%s"
#define QEMU_NBD_OPT_CACHE         256
#define QEMU_NBD_OPT_AIO           257
#define QEMU_NBD_OPT_DISCARD       258
#define QEMU_NBD_OPT_DETECT_ZEROES 259
#define QEMU_NBD_OPT_OBJECT        260
#define QEMU_NBD_OPT_TLSCREDS      261
#define QEMU_NBD_OPT_IMAGE_OPTS    262
#define QEMU_NBD_OPT_FORK          263
#define QEMU_NBD_OPT_TLSAUTHZ      264
#define QEMU_NBD_OPT_PID_FILE      265
#define QEMU_NBD_OPT_SELINUX_LABEL 266
#define QEMU_NBD_OPT_TLSHOSTNAME   267

#define MBR_SIZE 512

static char *srcpath;
static SocketAddress *saddr;
static int persistent = 0;
static enum { RUNNING, TERMINATE, TERMINATED } state;
static int shared = 1;
static int nb_fds;
static QIONetListener *server;
static QCryptoTLSCreds *tlscreds;
static const char *tlsauthz;

static void usage(const char *name)
{
    (printf) (
"Usage: %s [OPTIONS] FILE\n"
"  or:  %s -L [OPTIONS]\n"
"QEMU Disk Network Block Device Utility\n"
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
"  -x, --export-name=NAME    expose export by name (default is empty string)\n"
"  -D, --description=TEXT    export a human-readable description\n"
"\n"
"Exposing part of the image:\n"
"  -o, --offset=OFFSET       offset into the image\n"
"  -A, --allocation-depth    expose the allocation depth\n"
"  -B, --bitmap=NAME         expose a persistent dirty bitmap\n"
"\n"
"General purpose options:\n"
"  -L, --list                list exports available from another NBD server\n"
"  --object type,id=ID,...   define an object such as 'secret' for providing\n"
"                            passwords and/or encryption keys\n"
"  --tls-creds=ID            use id of an earlier --object to provide TLS\n"
"  --tls-authz=ID            use id of an earlier --object to provide\n"
"                            authorization\n"
"  -T, --trace [[enable=]<pattern>][,events=<file>][,file=<file>]\n"
"                            specify tracing options\n"
"  --fork                    fork off the server process and exit the parent\n"
"                            once the server is running\n"
"  --pid-file=PATH           store the server's process ID in the given file\n"
#ifdef CONFIG_SELINUX
"  --selinux-label=LABEL     set SELinux process label on listening socket\n"
#endif
#if HAVE_NBD_DEVICE
"\n"
"Kernel NBD client support:\n"
"  -c, --connect=DEV         connect FILE to the local NBD device DEV\n"
"  -d, --disconnect          disconnect the specified device\n"
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
"      --cache=MODE          set cache mode used to access the disk image, the\n"
"                            valid options are: 'none', 'writeback' (default),\n"
"                            'writethrough', 'directsync' and 'unsafe'\n"
"      --aio=MODE            set AIO mode (native, io_uring or threads)\n"
"      --discard=MODE        set discard mode (ignore, unmap)\n"
"      --detect-zeroes=MODE  set detect-zeroes mode (off, on, unmap)\n"
"      --image-opts          treat FILE as a full set of image options\n"
"\n"
QEMU_HELP_BOTTOM "\n"
    , name, name, NBD_DEFAULT_PORT, "DEVICE");
}

static void version(const char *name)
{
    printf(
"%s " QEMU_FULL_VERSION "\n"
"Written by Anthony Liguori.\n"
"\n"
QEMU_COPYRIGHT "\n"
"This is free software; see the source for copying conditions.  There is NO\n"
"warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\n"
    , name);
}

#ifdef CONFIG_POSIX
/*
 * The client thread uses SIGTERM to interrupt the server.  A signal
 * handler ensures that "qemu-nbd -v -c" exits with a nice status code.
 */
void qemu_system_killed(int signum, pid_t pid)
{
    qatomic_cmpxchg(&state, RUNNING, TERMINATE);
    qemu_notify_event();
}
#endif /* CONFIG_POSIX */

static int qemu_nbd_client_list(SocketAddress *saddr, QCryptoTLSCreds *tls,
                                const char *hostname)
{
    int ret = EXIT_FAILURE;
    int rc;
    Error *err = NULL;
    QIOChannelSocket *sioc;
    NBDExportInfo *list;
    int i, j;

    sioc = qio_channel_socket_new();
    if (qio_channel_socket_connect_sync(sioc, saddr, &err) < 0) {
        error_report_err(err);
        goto out;
    }
    rc = nbd_receive_export_list(QIO_CHANNEL(sioc), tls, hostname, &list,
                                 &err);
    if (rc < 0) {
        if (err) {
            error_report_err(err);
        }
        goto out;
    }
    printf("exports available: %d\n", rc);
    for (i = 0; i < rc; i++) {
        printf(" export: '%s'\n", list[i].name);
        if (list[i].description && *list[i].description) {
            printf("  description: %s\n", list[i].description);
        }
        if (list[i].flags & NBD_FLAG_HAS_FLAGS) {
            static const char *const flag_names[] = {
                [NBD_FLAG_READ_ONLY_BIT]            = "readonly",
                [NBD_FLAG_SEND_FLUSH_BIT]           = "flush",
                [NBD_FLAG_SEND_FUA_BIT]             = "fua",
                [NBD_FLAG_ROTATIONAL_BIT]           = "rotational",
                [NBD_FLAG_SEND_TRIM_BIT]            = "trim",
                [NBD_FLAG_SEND_WRITE_ZEROES_BIT]    = "zeroes",
                [NBD_FLAG_SEND_DF_BIT]              = "df",
                [NBD_FLAG_CAN_MULTI_CONN_BIT]       = "multi",
                [NBD_FLAG_SEND_RESIZE_BIT]          = "resize",
                [NBD_FLAG_SEND_CACHE_BIT]           = "cache",
                [NBD_FLAG_SEND_FAST_ZERO_BIT]       = "fast-zero",
            };

            printf("  size:  %" PRIu64 "\n", list[i].size);
            printf("  flags: 0x%x (", list[i].flags);
            for (size_t bit = 0; bit < ARRAY_SIZE(flag_names); bit++) {
                if (flag_names[bit] && (list[i].flags & (1 << bit))) {
                    printf(" %s", flag_names[bit]);
                }
            }
            printf(" )\n");
        }
        if (list[i].min_block) {
            printf("  min block: %u\n", list[i].min_block);
            printf("  opt block: %u\n", list[i].opt_block);
            printf("  max block: %u\n", list[i].max_block);
        }
        if (list[i].n_contexts) {
            printf("  available meta contexts: %d\n", list[i].n_contexts);
            for (j = 0; j < list[i].n_contexts; j++) {
                printf("   %s\n", list[i].contexts[j]);
            }
        }
    }
    nbd_free_export_list(list, rc);

    ret = EXIT_SUCCESS;
 out:
    object_unref(OBJECT(sioc));
    return ret;
}


#if HAVE_NBD_DEVICE
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

struct NbdClientOpts {
    char *device;
    bool fork_process;
    bool verbose;
};

static void *nbd_client_thread(void *arg)
{
    struct NbdClientOpts *opts = arg;
    NBDExportInfo info = { .request_sizes = false, .name = g_strdup("") };
    QIOChannelSocket *sioc;
    int fd = -1;
    int ret = EXIT_FAILURE;
    pthread_t show_parts_thread;
    Error *local_error = NULL;

    sioc = qio_channel_socket_new();
    if (qio_channel_socket_connect_sync(sioc,
                                        saddr,
                                        &local_error) < 0) {
        error_report_err(local_error);
        goto out;
    }

    if (nbd_receive_negotiate(NULL, QIO_CHANNEL(sioc),
                              NULL, NULL, NULL, &info, &local_error) < 0) {
        if (local_error) {
            error_report_err(local_error);
        }
        goto out;
    }

    fd = open(opts->device, O_RDWR);
    if (fd < 0) {
        /* Linux-only, we can use %m in printf.  */
        error_report("Failed to open %s: %m", opts->device);
        goto out;
    }

    if (nbd_init(fd, sioc, &info, &local_error) < 0) {
        error_report_err(local_error);
        goto out;
    }

    /* update partition table */
    pthread_create(&show_parts_thread, NULL, show_parts, opts->device);

    if (opts->verbose && !opts->fork_process) {
        fprintf(stderr, "NBD device %s is now connected to %s\n",
                opts->device, srcpath);
    } else {
        /* Close stderr so that the qemu-nbd process exits.  */
        if (dup2(STDOUT_FILENO, STDERR_FILENO) < 0) {
            error_report("Could not set stderr to /dev/null: %s",
                         strerror(errno));
            exit(EXIT_FAILURE);
        }
    }

    if (nbd_client(fd) < 0) {
        goto out;
    }

    ret = EXIT_SUCCESS;

 out:
    if (fd >= 0) {
        close(fd);
    }
    object_unref(OBJECT(sioc));
    g_free(info.name);
    kill(getpid(), SIGTERM);
    return (void *) (intptr_t) ret;
}
#endif /* HAVE_NBD_DEVICE */

static int nbd_can_accept(void)
{
    return state == RUNNING && (shared == 0 || nb_fds < shared);
}

static void nbd_update_server_watch(void);

static void nbd_client_closed(NBDClient *client, bool negotiated)
{
    nb_fds--;
    if (negotiated && nb_fds == 0 && !persistent && state == RUNNING) {
        state = TERMINATE;
    }
    nbd_update_server_watch();
    nbd_client_put(client);
}

static void nbd_accept(QIONetListener *listener, QIOChannelSocket *cioc,
                       gpointer opaque)
{
    if (state >= TERMINATE) {
        return;
    }

    nb_fds++;
    nbd_update_server_watch();
    nbd_client_new(cioc, tlscreds, tlsauthz, nbd_client_closed);
}

static void nbd_update_server_watch(void)
{
    if (nbd_can_accept()) {
        qio_net_listener_set_client_func(server, nbd_accept, NULL, NULL);
    } else {
        qio_net_listener_set_client_func(server, NULL, NULL, NULL);
    }
}


static SocketAddress *nbd_build_socket_address(const char *sockpath,
                                               const char *bindto,
                                               const char *port)
{
    SocketAddress *saddr;

    saddr = g_new0(SocketAddress, 1);
    if (sockpath) {
        saddr->type = SOCKET_ADDRESS_TYPE_UNIX;
        saddr->u.q_unix.path = g_strdup(sockpath);
    } else {
        InetSocketAddress *inet;
        saddr->type = SOCKET_ADDRESS_TYPE_INET;
        inet = &saddr->u.inet;
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

static QCryptoTLSCreds *nbd_get_tls_creds(const char *id, bool list,
                                          Error **errp)
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

    if (!qcrypto_tls_creds_check_endpoint(creds,
                                          list
                                          ? QCRYPTO_TLS_CREDS_ENDPOINT_CLIENT
                                          : QCRYPTO_TLS_CREDS_ENDPOINT_SERVER,
                                          errp)) {
        return NULL;
    }
    object_ref(obj);
    return creds;
}

static void setup_address_and_port(const char **address, const char **port)
{
    if (*address == NULL) {
        *address = "0.0.0.0";
    }

    if (*port == NULL) {
        *port = stringify(NBD_DEFAULT_PORT);
    }
}

/*
 * Check socket parameters compatibility when socket activation is used.
 */
static const char *socket_activation_validate_opts(const char *device,
                                                   const char *sockpath,
                                                   const char *address,
                                                   const char *port,
                                                   const char *selinux,
                                                   bool list)
{
    if (device != NULL) {
        return "NBD device can't be set when using socket activation";
    }

    if (sockpath != NULL) {
        return "Unix socket can't be set when using socket activation";
    }

    if (address != NULL) {
        return "The interface can't be set when using socket activation";
    }

    if (port != NULL) {
        return "TCP port number can't be set when using socket activation";
    }

    if (selinux != NULL) {
        return "SELinux label can't be set when using socket activation";
    }

    if (list) {
        return "List mode is incompatible with socket activation";
    }

    return NULL;
}

static void qemu_nbd_shutdown(void)
{
    job_cancel_sync_all();
    blk_exp_close_all();
    bdrv_close_all();
}

int main(int argc, char **argv)
{
    BlockBackend *blk;
    BlockDriverState *bs;
    uint64_t dev_offset = 0;
    bool readonly = false;
    bool disconnect = false;
    const char *bindto = NULL;
    const char *port = NULL;
    char *sockpath = NULL;
    char *device = NULL;
    QemuOpts *sn_opts = NULL;
    const char *sn_id_or_name = NULL;
    const char *sopt = "hVb:o:p:rsnc:dvk:e:f:tl:x:T:D:AB:L";
    struct option lopt[] = {
        { "help", no_argument, NULL, 'h' },
        { "version", no_argument, NULL, 'V' },
        { "bind", required_argument, NULL, 'b' },
        { "port", required_argument, NULL, 'p' },
        { "socket", required_argument, NULL, 'k' },
        { "offset", required_argument, NULL, 'o' },
        { "read-only", no_argument, NULL, 'r' },
        { "allocation-depth", no_argument, NULL, 'A' },
        { "bitmap", required_argument, NULL, 'B' },
        { "connect", required_argument, NULL, 'c' },
        { "disconnect", no_argument, NULL, 'd' },
        { "list", no_argument, NULL, 'L' },
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
        { "description", required_argument, NULL, 'D' },
        { "tls-creds", required_argument, NULL, QEMU_NBD_OPT_TLSCREDS },
        { "tls-hostname", required_argument, NULL, QEMU_NBD_OPT_TLSHOSTNAME },
        { "tls-authz", required_argument, NULL, QEMU_NBD_OPT_TLSAUTHZ },
        { "image-opts", no_argument, NULL, QEMU_NBD_OPT_IMAGE_OPTS },
        { "trace", required_argument, NULL, 'T' },
        { "fork", no_argument, NULL, QEMU_NBD_OPT_FORK },
        { "pid-file", required_argument, NULL, QEMU_NBD_OPT_PID_FILE },
        { "selinux-label", required_argument, NULL,
          QEMU_NBD_OPT_SELINUX_LABEL },
        { NULL, 0, NULL, 0 }
    };
    int ch;
    int opt_ind = 0;
    int flags = BDRV_O_RDWR;
    int ret = 0;
    bool seen_cache = false;
    bool seen_discard = false;
    bool seen_aio = false;
    pthread_t client_thread;
    const char *fmt = NULL;
    Error *local_err = NULL;
    BlockdevDetectZeroesOptions detect_zeroes = BLOCKDEV_DETECT_ZEROES_OPTIONS_OFF;
    QDict *options = NULL;
    const char *export_name = NULL; /* defaults to "" later for server mode */
    const char *export_description = NULL;
    BlockDirtyBitmapOrStrList *bitmaps = NULL;
    bool alloc_depth = false;
    const char *tlscredsid = NULL;
    const char *tlshostname = NULL;
    bool imageOpts = false;
    bool writethrough = false; /* Client will flush as needed. */
    bool verbose = false;
    bool fork_process = false;
    bool list = false;
    unsigned socket_activation;
    const char *pid_file_name = NULL;
    const char *selinux_label = NULL;
    BlockExportOptions *export_opts;
#if HAVE_NBD_DEVICE
    struct NbdClientOpts opts;
#endif

#ifdef CONFIG_POSIX
    os_setup_early_signal_handling();
    os_setup_signal_handling();
#endif

    socket_init();
    error_init(argv[0]);
    module_call_init(MODULE_INIT_TRACE);
    qcrypto_init(&error_fatal);

    module_call_init(MODULE_INIT_QOM);
    qemu_add_opts(&qemu_trace_opts);
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
            if (bdrv_parse_aio(optarg, &flags) < 0) {
                error_report("Invalid aio mode '%s'", optarg);
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
                qapi_enum_parse(&BlockdevDetectZeroesOptions_lookup,
                                optarg,
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
            if (qemu_strtou64(optarg, NULL, 0, &dev_offset) < 0) {
                error_report("Invalid offset '%s'", optarg);
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
            readonly = true;
            flags &= ~BDRV_O_RDWR;
            break;
        case 'A':
            alloc_depth = true;
            break;
        case 'B':
            {
                BlockDirtyBitmapOrStr *el = g_new(BlockDirtyBitmapOrStr, 1);
                *el = (BlockDirtyBitmapOrStr) {
                    .type = QTYPE_QSTRING,
                    .u.local = g_strdup(optarg),
                };
                QAPI_LIST_PREPEND(bitmaps, el);
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
            if (qemu_strtoi(optarg, NULL, 0, &shared) < 0 ||
                shared < 0) {
                error_report("Invalid shared device number '%s'", optarg);
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
            if (strlen(export_name) > NBD_MAX_STRING_SIZE) {
                error_report("export name '%s' too long", export_name);
                exit(EXIT_FAILURE);
            }
            break;
        case 'D':
            export_description = optarg;
            if (strlen(export_description) > NBD_MAX_STRING_SIZE) {
                error_report("export description '%s' too long",
                             export_description);
                exit(EXIT_FAILURE);
            }
            break;
        case 'v':
            verbose = true;
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
        case QEMU_NBD_OPT_OBJECT:
            user_creatable_process_cmdline(optarg);
            break;
        case QEMU_NBD_OPT_TLSCREDS:
            tlscredsid = optarg;
            break;
        case QEMU_NBD_OPT_TLSHOSTNAME:
            tlshostname = optarg;
            break;
        case QEMU_NBD_OPT_IMAGE_OPTS:
            imageOpts = true;
            break;
        case 'T':
            trace_opt_parse(optarg);
            break;
        case QEMU_NBD_OPT_TLSAUTHZ:
            tlsauthz = optarg;
            break;
        case QEMU_NBD_OPT_FORK:
            fork_process = true;
            break;
        case 'L':
            list = true;
            break;
        case QEMU_NBD_OPT_PID_FILE:
            pid_file_name = optarg;
            break;
        case QEMU_NBD_OPT_SELINUX_LABEL:
            selinux_label = optarg;
            break;
        }
    }

    if (list) {
        if (argc != optind) {
            error_report("List mode is incompatible with a file name");
            exit(EXIT_FAILURE);
        }
        if (export_name || export_description || dev_offset ||
            device || disconnect || fmt || sn_id_or_name || bitmaps ||
            alloc_depth || seen_aio || seen_discard || seen_cache) {
            error_report("List mode is incompatible with per-device settings");
            exit(EXIT_FAILURE);
        }
        if (fork_process) {
            error_report("List mode is incompatible with forking");
            exit(EXIT_FAILURE);
        }
    } else if ((argc - optind) != 1) {
        error_report("Invalid number of arguments");
        error_printf("Try `%s --help' for more information.\n", argv[0]);
        exit(EXIT_FAILURE);
    } else if (!export_name) {
        export_name = "";
    }

    if (!trace_init_backends()) {
        exit(1);
    }
    trace_init_file();
    qemu_set_log(LOG_TRACE, &error_fatal);

    socket_activation = check_socket_activation();
    if (socket_activation == 0) {
        if (!sockpath) {
            setup_address_and_port(&bindto, &port);
        }
    } else {
        /* Using socket activation - check user didn't use -p etc. */
        const char *err_msg = socket_activation_validate_opts(device, sockpath,
                                                              bindto, port,
                                                              selinux_label,
                                                              list);
        if (err_msg != NULL) {
            error_report("%s", err_msg);
            exit(EXIT_FAILURE);
        }

        /* qemu-nbd can only listen on a single socket.  */
        if (socket_activation > 1) {
            error_report("qemu-nbd does not support socket activation with %s > 1",
                         "LISTEN_FDS");
            exit(EXIT_FAILURE);
        }
    }

    if (tlscredsid) {
        if (device) {
            error_report("TLS is not supported with a host device");
            exit(EXIT_FAILURE);
        }
        if (tlsauthz && list) {
            error_report("TLS authorization is incompatible with export list");
            exit(EXIT_FAILURE);
        }
        if (tlshostname && !list) {
            error_report("TLS hostname is only supported with export list");
            exit(EXIT_FAILURE);
        }
        tlscreds = nbd_get_tls_creds(tlscredsid, list, &local_err);
        if (local_err) {
            error_reportf_err(local_err, "Failed to get TLS creds: ");
            exit(EXIT_FAILURE);
        }
    } else {
        if (tlsauthz) {
            error_report("--tls-authz is not permitted without --tls-creds");
            exit(EXIT_FAILURE);
        }
        if (tlshostname) {
            error_report("--tls-hostname is not permitted without --tls-creds");
            exit(EXIT_FAILURE);
        }
    }

    if (selinux_label) {
#ifdef CONFIG_SELINUX
        if (sockpath == NULL && device == NULL) {
            error_report("--selinux-label is not permitted without --socket");
            exit(EXIT_FAILURE);
        }
#else
        error_report("SELinux support not enabled in this binary");
        exit(EXIT_FAILURE);
#endif
    }

    if (list) {
        saddr = nbd_build_socket_address(sockpath, bindto, port);
        return qemu_nbd_client_list(saddr, tlscreds,
                                    tlshostname ? tlshostname : bindto);
    }

#if !HAVE_NBD_DEVICE
    if (disconnect || device) {
        error_report("Kernel /dev/nbdN support not available");
        exit(EXIT_FAILURE);
    }
#else /* HAVE_NBD_DEVICE */
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
#endif

    if ((device && !verbose) || fork_process) {
#ifndef WIN32
        g_autoptr(GError) err = NULL;
        int stderr_fd[2];
        pid_t pid;
        int ret;

        if (!g_unix_open_pipe(stderr_fd, FD_CLOEXEC, &err)) {
            error_report("Error setting up communication pipe: %s",
                         err->message);
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
            int saved_errno;

            close(stderr_fd[0]);

            ret = qemu_daemon(1, 0);
            saved_errno = errno;    /* dup2 will overwrite error below */

            /* Temporarily redirect stderr to the parent's pipe...  */
            if (dup2(stderr_fd[1], STDERR_FILENO) < 0) {
                char str[256];
                snprintf(str, sizeof(str),
                         "%s: Failed to link stderr to the pipe: %s\n",
                         g_get_prgname(), strerror(errno));
                /*
                 * We are unable to use error_report() here as we need to get
                 * stderr pointed to the parent's pipe. Write to that pipe
                 * manually.
                 */
                ret = write(stderr_fd[1], str, strlen(str));
                exit(EXIT_FAILURE);
            }

            if (ret < 0) {
                error_report("Failed to daemonize: %s", strerror(saved_errno));
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
#else /* WIN32 */
        error_report("Unable to fork into background on Windows hosts");
        exit(EXIT_FAILURE);
#endif /* WIN32 */
    }

    if (device != NULL && sockpath == NULL) {
        sockpath = g_malloc(128);
        snprintf(sockpath, 128, SOCKET_PATH, basename(device));
    }

    server = qio_net_listener_new();
    if (socket_activation == 0) {
        int backlog;

        if (persistent || shared == 0) {
            backlog = SOMAXCONN;
        } else {
            backlog = MIN(shared, SOMAXCONN);
        }
#ifdef CONFIG_SELINUX
        if (selinux_label && setsockcreatecon_raw(selinux_label) == -1) {
            error_report("Cannot set SELinux socket create context to %s: %s",
                         selinux_label, strerror(errno));
            exit(EXIT_FAILURE);
        }
#endif
        saddr = nbd_build_socket_address(sockpath, bindto, port);
        if (qio_net_listener_open_sync(server, saddr, backlog,
                                       &local_err) < 0) {
            object_unref(OBJECT(server));
            error_report_err(local_err);
            exit(EXIT_FAILURE);
        }
#ifdef CONFIG_SELINUX
        if (selinux_label && setsockcreatecon_raw(NULL) == -1) {
            error_report("Cannot clear SELinux socket create context: %s",
                         strerror(errno));
            exit(EXIT_FAILURE);
        }
#endif
    } else {
        size_t i;
        /* See comment in check_socket_activation above. */
        for (i = 0; i < socket_activation; i++) {
            QIOChannelSocket *sioc;
            sioc = qio_channel_socket_new_fd(FIRST_SOCKET_ACTIVATION_FD + i,
                                             &local_err);
            if (sioc == NULL) {
                object_unref(OBJECT(server));
                error_reportf_err(local_err,
                                  "Failed to use socket activation: ");
                exit(EXIT_FAILURE);
            }
            qio_net_listener_add(server, sioc);
            object_unref(OBJECT(sioc));
        }
    }

    qemu_init_main_loop(&error_fatal);
    bdrv_init();
    atexit(qemu_nbd_shutdown);

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
            qdict_put_str(options, "driver", fmt);
        }
        blk = blk_new_open(srcpath, NULL, options, flags, &local_err);
    }

    if (!blk) {
        error_reportf_err(local_err, "Failed to blk_new_open '%s': ",
                          argv[optind]);
        exit(EXIT_FAILURE);
    }
    bs = blk_bs(blk);

    if (dev_offset) {
        QDict *raw_opts = qdict_new();
        qdict_put_str(raw_opts, "driver", "raw");
        qdict_put_str(raw_opts, "file", bs->node_name);
        qdict_put_int(raw_opts, "offset", dev_offset);

        aio_context_acquire(qemu_get_aio_context());
        bs = bdrv_open(NULL, NULL, raw_opts, flags, &error_fatal);
        aio_context_release(qemu_get_aio_context());

        blk_remove_bs(blk);
        blk_insert_bs(blk, bs, &error_fatal);
        bdrv_unref(bs);
    }

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

    nbd_server_is_qemu_nbd(shared);

    export_opts = g_new(BlockExportOptions, 1);
    *export_opts = (BlockExportOptions) {
        .type               = BLOCK_EXPORT_TYPE_NBD,
        .id                 = g_strdup("qemu-nbd-export"),
        .node_name          = g_strdup(bdrv_get_node_name(bs)),
        .has_writethrough   = true,
        .writethrough       = writethrough,
        .has_writable       = true,
        .writable           = !readonly,
        .u.nbd = {
            .name                 = g_strdup(export_name),
            .description          = g_strdup(export_description),
            .has_bitmaps          = !!bitmaps,
            .bitmaps              = bitmaps,
            .has_allocation_depth = alloc_depth,
            .allocation_depth     = alloc_depth,
        },
    };
    blk_exp_add(export_opts, &error_fatal);
    qapi_free_BlockExportOptions(export_opts);

    if (device) {
#if HAVE_NBD_DEVICE
        int ret;
        opts = (struct NbdClientOpts) {
            .device = device,
            .fork_process = fork_process,
            .verbose = verbose,
        };

        ret = pthread_create(&client_thread, NULL, nbd_client_thread, &opts);
        if (ret != 0) {
            error_report("Failed to create client thread: %s", strerror(ret));
            exit(EXIT_FAILURE);
        }
#endif
    } else {
        /* Shut up GCC warnings.  */
        memset(&client_thread, 0, sizeof(client_thread));
    }

    nbd_update_server_watch();

    if (pid_file_name) {
        qemu_write_pidfile(pid_file_name, &error_fatal);
    }

    /* now when the initialization is (almost) complete, chdir("/")
     * to free any busy filesystems */
    if (chdir("/") < 0) {
        error_report("Could not chdir to root directory: %s",
                     strerror(errno));
        exit(EXIT_FAILURE);
    }

    if (fork_process) {
        if (dup2(STDOUT_FILENO, STDERR_FILENO) < 0) {
            error_report("Could not set stderr to /dev/null: %s",
                         strerror(errno));
            exit(EXIT_FAILURE);
        }
    }

    state = RUNNING;
    do {
        main_loop_wait(false);
        if (state == TERMINATE) {
            blk_exp_close_all();
            state = TERMINATED;
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

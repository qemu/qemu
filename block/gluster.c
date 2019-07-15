/*
 * GlusterFS backend for QEMU
 *
 * Copyright (C) 2012 Bharata B Rao <bharata@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include <glusterfs/api/glfs.h>
#include "block/block_int.h"
#include "block/qdict.h"
#include "qapi/error.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qerror.h"
#include "qemu/uri.h"
#include "qemu/error-report.h"
#include "qemu/module.h"
#include "qemu/option.h"
#include "qemu/cutils.h"

#ifdef CONFIG_GLUSTERFS_FTRUNCATE_HAS_STAT
# define glfs_ftruncate(fd, offset) glfs_ftruncate(fd, offset, NULL, NULL)
#endif

#define GLUSTER_OPT_FILENAME        "filename"
#define GLUSTER_OPT_VOLUME          "volume"
#define GLUSTER_OPT_PATH            "path"
#define GLUSTER_OPT_TYPE            "type"
#define GLUSTER_OPT_SERVER_PATTERN  "server."
#define GLUSTER_OPT_HOST            "host"
#define GLUSTER_OPT_PORT            "port"
#define GLUSTER_OPT_TO              "to"
#define GLUSTER_OPT_IPV4            "ipv4"
#define GLUSTER_OPT_IPV6            "ipv6"
#define GLUSTER_OPT_SOCKET          "socket"
#define GLUSTER_OPT_DEBUG           "debug"
#define GLUSTER_DEFAULT_PORT        24007
#define GLUSTER_DEBUG_DEFAULT       4
#define GLUSTER_DEBUG_MAX           9
#define GLUSTER_OPT_LOGFILE         "logfile"
#define GLUSTER_LOGFILE_DEFAULT     "-" /* handled in libgfapi as /dev/stderr */
/*
 * Several versions of GlusterFS (3.12? -> 6.0.1) fail when the transfer size
 * is greater or equal to 1024 MiB, so we are limiting the transfer size to 512
 * MiB to avoid this rare issue.
 */
#define GLUSTER_MAX_TRANSFER        (512 * MiB)

#define GERR_INDEX_HINT "hint: check in 'server' array index '%d'\n"

typedef struct GlusterAIOCB {
    int64_t size;
    int ret;
    Coroutine *coroutine;
    AioContext *aio_context;
} GlusterAIOCB;

typedef struct BDRVGlusterState {
    struct glfs *glfs;
    struct glfs_fd *fd;
    char *logfile;
    bool supports_seek_data;
    int debug;
} BDRVGlusterState;

typedef struct BDRVGlusterReopenState {
    struct glfs *glfs;
    struct glfs_fd *fd;
} BDRVGlusterReopenState;


typedef struct GlfsPreopened {
    char *volume;
    glfs_t *fs;
    int ref;
} GlfsPreopened;

typedef struct ListElement {
    QLIST_ENTRY(ListElement) list;
    GlfsPreopened saved;
} ListElement;

static QLIST_HEAD(, ListElement) glfs_list;

static QemuOptsList qemu_gluster_create_opts = {
    .name = "qemu-gluster-create-opts",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_gluster_create_opts.head),
    .desc = {
        {
            .name = BLOCK_OPT_SIZE,
            .type = QEMU_OPT_SIZE,
            .help = "Virtual disk size"
        },
        {
            .name = BLOCK_OPT_PREALLOC,
            .type = QEMU_OPT_STRING,
            .help = "Preallocation mode (allowed values: off"
#ifdef CONFIG_GLUSTERFS_FALLOCATE
                    ", falloc"
#endif
#ifdef CONFIG_GLUSTERFS_ZEROFILL
                    ", full"
#endif
                    ")"
        },
        {
            .name = GLUSTER_OPT_DEBUG,
            .type = QEMU_OPT_NUMBER,
            .help = "Gluster log level, valid range is 0-9",
        },
        {
            .name = GLUSTER_OPT_LOGFILE,
            .type = QEMU_OPT_STRING,
            .help = "Logfile path of libgfapi",
        },
        { /* end of list */ }
    }
};

static QemuOptsList runtime_opts = {
    .name = "gluster",
    .head = QTAILQ_HEAD_INITIALIZER(runtime_opts.head),
    .desc = {
        {
            .name = GLUSTER_OPT_FILENAME,
            .type = QEMU_OPT_STRING,
            .help = "URL to the gluster image",
        },
        {
            .name = GLUSTER_OPT_DEBUG,
            .type = QEMU_OPT_NUMBER,
            .help = "Gluster log level, valid range is 0-9",
        },
        {
            .name = GLUSTER_OPT_LOGFILE,
            .type = QEMU_OPT_STRING,
            .help = "Logfile path of libgfapi",
        },
        { /* end of list */ }
    },
};

static QemuOptsList runtime_json_opts = {
    .name = "gluster_json",
    .head = QTAILQ_HEAD_INITIALIZER(runtime_json_opts.head),
    .desc = {
        {
            .name = GLUSTER_OPT_VOLUME,
            .type = QEMU_OPT_STRING,
            .help = "name of gluster volume where VM image resides",
        },
        {
            .name = GLUSTER_OPT_PATH,
            .type = QEMU_OPT_STRING,
            .help = "absolute path to image file in gluster volume",
        },
        {
            .name = GLUSTER_OPT_DEBUG,
            .type = QEMU_OPT_NUMBER,
            .help = "Gluster log level, valid range is 0-9",
        },
        { /* end of list */ }
    },
};

static QemuOptsList runtime_type_opts = {
    .name = "gluster_type",
    .head = QTAILQ_HEAD_INITIALIZER(runtime_type_opts.head),
    .desc = {
        {
            .name = GLUSTER_OPT_TYPE,
            .type = QEMU_OPT_STRING,
            .help = "inet|unix",
        },
        { /* end of list */ }
    },
};

static QemuOptsList runtime_unix_opts = {
    .name = "gluster_unix",
    .head = QTAILQ_HEAD_INITIALIZER(runtime_unix_opts.head),
    .desc = {
        {
            .name = GLUSTER_OPT_SOCKET,
            .type = QEMU_OPT_STRING,
            .help = "socket file path (legacy)",
        },
        {
            .name = GLUSTER_OPT_PATH,
            .type = QEMU_OPT_STRING,
            .help = "socket file path (QAPI)",
        },
        { /* end of list */ }
    },
};

static QemuOptsList runtime_inet_opts = {
    .name = "gluster_inet",
    .head = QTAILQ_HEAD_INITIALIZER(runtime_inet_opts.head),
    .desc = {
        {
            .name = GLUSTER_OPT_TYPE,
            .type = QEMU_OPT_STRING,
            .help = "inet|unix",
        },
        {
            .name = GLUSTER_OPT_HOST,
            .type = QEMU_OPT_STRING,
            .help = "host address (hostname/ipv4/ipv6 addresses)",
        },
        {
            .name = GLUSTER_OPT_PORT,
            .type = QEMU_OPT_STRING,
            .help = "port number on which glusterd is listening (default 24007)",
        },
        {
            .name = "to",
            .type = QEMU_OPT_NUMBER,
            .help = "max port number, not supported by gluster",
        },
        {
            .name = "ipv4",
            .type = QEMU_OPT_BOOL,
            .help = "ipv4 bool value, not supported by gluster",
        },
        {
            .name = "ipv6",
            .type = QEMU_OPT_BOOL,
            .help = "ipv6 bool value, not supported by gluster",
        },
        { /* end of list */ }
    },
};

static void glfs_set_preopened(const char *volume, glfs_t *fs)
{
    ListElement *entry = NULL;

    entry = g_new(ListElement, 1);

    entry->saved.volume = g_strdup(volume);

    entry->saved.fs = fs;
    entry->saved.ref = 1;

    QLIST_INSERT_HEAD(&glfs_list, entry, list);
}

static glfs_t *glfs_find_preopened(const char *volume)
{
    ListElement *entry = NULL;

     QLIST_FOREACH(entry, &glfs_list, list) {
        if (strcmp(entry->saved.volume, volume) == 0) {
            entry->saved.ref++;
            return entry->saved.fs;
        }
     }

    return NULL;
}

static void glfs_clear_preopened(glfs_t *fs)
{
    ListElement *entry = NULL;
    ListElement *next;

    if (fs == NULL) {
        return;
    }

    QLIST_FOREACH_SAFE(entry, &glfs_list, list, next) {
        if (entry->saved.fs == fs) {
            if (--entry->saved.ref) {
                return;
            }

            QLIST_REMOVE(entry, list);

            glfs_fini(entry->saved.fs);
            g_free(entry->saved.volume);
            g_free(entry);
        }
    }
}

static int parse_volume_options(BlockdevOptionsGluster *gconf, char *path)
{
    char *p, *q;

    if (!path) {
        return -EINVAL;
    }

    /* volume */
    p = q = path + strspn(path, "/");
    p += strcspn(p, "/");
    if (*p == '\0') {
        return -EINVAL;
    }
    gconf->volume = g_strndup(q, p - q);

    /* path */
    p += strspn(p, "/");
    if (*p == '\0') {
        return -EINVAL;
    }
    gconf->path = g_strdup(p);
    return 0;
}

/*
 * file=gluster[+transport]://[host[:port]]/volume/path[?socket=...]
 *
 * 'gluster' is the protocol.
 *
 * 'transport' specifies the transport type used to connect to gluster
 * management daemon (glusterd). Valid transport types are
 * tcp or unix. If a transport type isn't specified, then tcp type is assumed.
 *
 * 'host' specifies the host where the volume file specification for
 * the given volume resides. This can be either hostname or ipv4 address.
 * If transport type is 'unix', then 'host' field should not be specified.
 * The 'socket' field needs to be populated with the path to unix domain
 * socket.
 *
 * 'port' is the port number on which glusterd is listening. This is optional
 * and if not specified, QEMU will send 0 which will make gluster to use the
 * default port. If the transport type is unix, then 'port' should not be
 * specified.
 *
 * 'volume' is the name of the gluster volume which contains the VM image.
 *
 * 'path' is the path to the actual VM image that resides on gluster volume.
 *
 * Examples:
 *
 * file=gluster://1.2.3.4/testvol/a.img
 * file=gluster+tcp://1.2.3.4/testvol/a.img
 * file=gluster+tcp://1.2.3.4:24007/testvol/dir/a.img
 * file=gluster+tcp://host.domain.com:24007/testvol/dir/a.img
 * file=gluster+unix:///testvol/dir/a.img?socket=/tmp/glusterd.socket
 */
static int qemu_gluster_parse_uri(BlockdevOptionsGluster *gconf,
                                  const char *filename)
{
    SocketAddress *gsconf;
    URI *uri;
    QueryParams *qp = NULL;
    bool is_unix = false;
    int ret = 0;

    uri = uri_parse(filename);
    if (!uri) {
        return -EINVAL;
    }

    gconf->server = g_new0(SocketAddressList, 1);
    gconf->server->value = gsconf = g_new0(SocketAddress, 1);

    /* transport */
    if (!uri->scheme || !strcmp(uri->scheme, "gluster")) {
        gsconf->type = SOCKET_ADDRESS_TYPE_INET;
    } else if (!strcmp(uri->scheme, "gluster+tcp")) {
        gsconf->type = SOCKET_ADDRESS_TYPE_INET;
    } else if (!strcmp(uri->scheme, "gluster+unix")) {
        gsconf->type = SOCKET_ADDRESS_TYPE_UNIX;
        is_unix = true;
    } else if (!strcmp(uri->scheme, "gluster+rdma")) {
        gsconf->type = SOCKET_ADDRESS_TYPE_INET;
        warn_report("rdma feature is not supported, falling back to tcp");
    } else {
        ret = -EINVAL;
        goto out;
    }

    ret = parse_volume_options(gconf, uri->path);
    if (ret < 0) {
        goto out;
    }

    qp = query_params_parse(uri->query);
    if (qp->n > 1 || (is_unix && !qp->n) || (!is_unix && qp->n)) {
        ret = -EINVAL;
        goto out;
    }

    if (is_unix) {
        if (uri->server || uri->port) {
            ret = -EINVAL;
            goto out;
        }
        if (strcmp(qp->p[0].name, "socket")) {
            ret = -EINVAL;
            goto out;
        }
        gsconf->u.q_unix.path = g_strdup(qp->p[0].value);
    } else {
        gsconf->u.inet.host = g_strdup(uri->server ? uri->server : "localhost");
        if (uri->port) {
            gsconf->u.inet.port = g_strdup_printf("%d", uri->port);
        } else {
            gsconf->u.inet.port = g_strdup_printf("%d", GLUSTER_DEFAULT_PORT);
        }
    }

out:
    if (qp) {
        query_params_free(qp);
    }
    uri_free(uri);
    return ret;
}

static struct glfs *qemu_gluster_glfs_init(BlockdevOptionsGluster *gconf,
                                           Error **errp)
{
    struct glfs *glfs;
    int ret;
    int old_errno;
    SocketAddressList *server;
    unsigned long long port;

    glfs = glfs_find_preopened(gconf->volume);
    if (glfs) {
        return glfs;
    }

    glfs = glfs_new(gconf->volume);
    if (!glfs) {
        goto out;
    }

    glfs_set_preopened(gconf->volume, glfs);

    for (server = gconf->server; server; server = server->next) {
        switch (server->value->type) {
        case SOCKET_ADDRESS_TYPE_UNIX:
            ret = glfs_set_volfile_server(glfs, "unix",
                                   server->value->u.q_unix.path, 0);
            break;
        case SOCKET_ADDRESS_TYPE_INET:
            if (parse_uint_full(server->value->u.inet.port, &port, 10) < 0 ||
                port > 65535) {
                error_setg(errp, "'%s' is not a valid port number",
                           server->value->u.inet.port);
                errno = EINVAL;
                goto out;
            }
            ret = glfs_set_volfile_server(glfs, "tcp",
                                   server->value->u.inet.host,
                                   (int)port);
            break;
        case SOCKET_ADDRESS_TYPE_VSOCK:
        case SOCKET_ADDRESS_TYPE_FD:
        default:
            abort();
        }

        if (ret < 0) {
            goto out;
        }
    }

    ret = glfs_set_logging(glfs, gconf->logfile, gconf->debug);
    if (ret < 0) {
        goto out;
    }

    ret = glfs_init(glfs);
    if (ret) {
        error_setg(errp, "Gluster connection for volume %s, path %s failed"
                         " to connect", gconf->volume, gconf->path);
        for (server = gconf->server; server; server = server->next) {
            if (server->value->type  == SOCKET_ADDRESS_TYPE_UNIX) {
                error_append_hint(errp, "hint: failed on socket %s ",
                                  server->value->u.q_unix.path);
            } else {
                error_append_hint(errp, "hint: failed on host %s and port %s ",
                                  server->value->u.inet.host,
                                  server->value->u.inet.port);
            }
        }

        error_append_hint(errp, "Please refer to gluster logs for more info\n");

        /* glfs_init sometimes doesn't set errno although docs suggest that */
        if (errno == 0) {
            errno = EINVAL;
        }

        goto out;
    }
    return glfs;

out:
    if (glfs) {
        old_errno = errno;
        glfs_clear_preopened(glfs);
        errno = old_errno;
    }
    return NULL;
}

/*
 * Convert the json formatted command line into qapi.
*/
static int qemu_gluster_parse_json(BlockdevOptionsGluster *gconf,
                                  QDict *options, Error **errp)
{
    QemuOpts *opts;
    SocketAddress *gsconf = NULL;
    SocketAddressList *curr = NULL;
    QDict *backing_options = NULL;
    Error *local_err = NULL;
    char *str = NULL;
    const char *ptr;
    int i, type, num_servers;

    /* create opts info from runtime_json_opts list */
    opts = qemu_opts_create(&runtime_json_opts, NULL, 0, &error_abort);
    qemu_opts_absorb_qdict(opts, options, &local_err);
    if (local_err) {
        goto out;
    }

    num_servers = qdict_array_entries(options, GLUSTER_OPT_SERVER_PATTERN);
    if (num_servers < 1) {
        error_setg(&local_err, QERR_MISSING_PARAMETER, "server");
        goto out;
    }

    ptr = qemu_opt_get(opts, GLUSTER_OPT_VOLUME);
    if (!ptr) {
        error_setg(&local_err, QERR_MISSING_PARAMETER, GLUSTER_OPT_VOLUME);
        goto out;
    }
    gconf->volume = g_strdup(ptr);

    ptr = qemu_opt_get(opts, GLUSTER_OPT_PATH);
    if (!ptr) {
        error_setg(&local_err, QERR_MISSING_PARAMETER, GLUSTER_OPT_PATH);
        goto out;
    }
    gconf->path = g_strdup(ptr);
    qemu_opts_del(opts);

    for (i = 0; i < num_servers; i++) {
        str = g_strdup_printf(GLUSTER_OPT_SERVER_PATTERN"%d.", i);
        qdict_extract_subqdict(options, &backing_options, str);

        /* create opts info from runtime_type_opts list */
        opts = qemu_opts_create(&runtime_type_opts, NULL, 0, &error_abort);
        qemu_opts_absorb_qdict(opts, backing_options, &local_err);
        if (local_err) {
            goto out;
        }

        ptr = qemu_opt_get(opts, GLUSTER_OPT_TYPE);
        if (!ptr) {
            error_setg(&local_err, QERR_MISSING_PARAMETER, GLUSTER_OPT_TYPE);
            error_append_hint(&local_err, GERR_INDEX_HINT, i);
            goto out;

        }
        gsconf = g_new0(SocketAddress, 1);
        if (!strcmp(ptr, "tcp")) {
            ptr = "inet";       /* accept legacy "tcp" */
        }
        type = qapi_enum_parse(&SocketAddressType_lookup, ptr, -1, NULL);
        if (type != SOCKET_ADDRESS_TYPE_INET
            && type != SOCKET_ADDRESS_TYPE_UNIX) {
            error_setg(&local_err,
                       "Parameter '%s' may be 'inet' or 'unix'",
                       GLUSTER_OPT_TYPE);
            error_append_hint(&local_err, GERR_INDEX_HINT, i);
            goto out;
        }
        gsconf->type = type;
        qemu_opts_del(opts);

        if (gsconf->type == SOCKET_ADDRESS_TYPE_INET) {
            /* create opts info from runtime_inet_opts list */
            opts = qemu_opts_create(&runtime_inet_opts, NULL, 0, &error_abort);
            qemu_opts_absorb_qdict(opts, backing_options, &local_err);
            if (local_err) {
                goto out;
            }

            ptr = qemu_opt_get(opts, GLUSTER_OPT_HOST);
            if (!ptr) {
                error_setg(&local_err, QERR_MISSING_PARAMETER,
                           GLUSTER_OPT_HOST);
                error_append_hint(&local_err, GERR_INDEX_HINT, i);
                goto out;
            }
            gsconf->u.inet.host = g_strdup(ptr);
            ptr = qemu_opt_get(opts, GLUSTER_OPT_PORT);
            if (!ptr) {
                error_setg(&local_err, QERR_MISSING_PARAMETER,
                           GLUSTER_OPT_PORT);
                error_append_hint(&local_err, GERR_INDEX_HINT, i);
                goto out;
            }
            gsconf->u.inet.port = g_strdup(ptr);

            /* defend for unsupported fields in InetSocketAddress,
             * i.e. @ipv4, @ipv6  and @to
             */
            ptr = qemu_opt_get(opts, GLUSTER_OPT_TO);
            if (ptr) {
                gsconf->u.inet.has_to = true;
            }
            ptr = qemu_opt_get(opts, GLUSTER_OPT_IPV4);
            if (ptr) {
                gsconf->u.inet.has_ipv4 = true;
            }
            ptr = qemu_opt_get(opts, GLUSTER_OPT_IPV6);
            if (ptr) {
                gsconf->u.inet.has_ipv6 = true;
            }
            if (gsconf->u.inet.has_to) {
                error_setg(&local_err, "Parameter 'to' not supported");
                goto out;
            }
            if (gsconf->u.inet.has_ipv4 || gsconf->u.inet.has_ipv6) {
                error_setg(&local_err, "Parameters 'ipv4/ipv6' not supported");
                goto out;
            }
            qemu_opts_del(opts);
        } else {
            /* create opts info from runtime_unix_opts list */
            opts = qemu_opts_create(&runtime_unix_opts, NULL, 0, &error_abort);
            qemu_opts_absorb_qdict(opts, backing_options, &local_err);
            if (local_err) {
                goto out;
            }

            ptr = qemu_opt_get(opts, GLUSTER_OPT_PATH);
            if (!ptr) {
                ptr = qemu_opt_get(opts, GLUSTER_OPT_SOCKET);
            } else if (qemu_opt_get(opts, GLUSTER_OPT_SOCKET)) {
                error_setg(&local_err,
                           "Conflicting parameters 'path' and 'socket'");
                error_append_hint(&local_err, GERR_INDEX_HINT, i);
                goto out;
            }
            if (!ptr) {
                error_setg(&local_err, QERR_MISSING_PARAMETER,
                           GLUSTER_OPT_PATH);
                error_append_hint(&local_err, GERR_INDEX_HINT, i);
                goto out;
            }
            gsconf->u.q_unix.path = g_strdup(ptr);
            qemu_opts_del(opts);
        }

        if (gconf->server == NULL) {
            gconf->server = g_new0(SocketAddressList, 1);
            gconf->server->value = gsconf;
            curr = gconf->server;
        } else {
            curr->next = g_new0(SocketAddressList, 1);
            curr->next->value = gsconf;
            curr = curr->next;
        }
        gsconf = NULL;

        qobject_unref(backing_options);
        backing_options = NULL;
        g_free(str);
        str = NULL;
    }

    return 0;

out:
    error_propagate(errp, local_err);
    qapi_free_SocketAddress(gsconf);
    qemu_opts_del(opts);
    g_free(str);
    qobject_unref(backing_options);
    errno = EINVAL;
    return -errno;
}

/* Converts options given in @filename and the @options QDict into the QAPI
 * object @gconf. */
static int qemu_gluster_parse(BlockdevOptionsGluster *gconf,
                              const char *filename,
                              QDict *options, Error **errp)
{
    int ret;
    if (filename) {
        ret = qemu_gluster_parse_uri(gconf, filename);
        if (ret < 0) {
            error_setg(errp, "invalid URI %s", filename);
            error_append_hint(errp, "Usage: file=gluster[+transport]://"
                                    "[host[:port]]volume/path[?socket=...]"
                                    "[,file.debug=N]"
                                    "[,file.logfile=/path/filename.log]\n");
            return ret;
        }
    } else {
        ret = qemu_gluster_parse_json(gconf, options, errp);
        if (ret < 0) {
            error_append_hint(errp, "Usage: "
                             "-drive driver=qcow2,file.driver=gluster,"
                             "file.volume=testvol,file.path=/path/a.qcow2"
                             "[,file.debug=9]"
                             "[,file.logfile=/path/filename.log],"
                             "file.server.0.type=inet,"
                             "file.server.0.host=1.2.3.4,"
                             "file.server.0.port=24007,"
                             "file.server.1.transport=unix,"
                             "file.server.1.path=/var/run/glusterd.socket ..."
                             "\n");
            return ret;
        }
    }

    return 0;
}

static struct glfs *qemu_gluster_init(BlockdevOptionsGluster *gconf,
                                      const char *filename,
                                      QDict *options, Error **errp)
{
    int ret;

    ret = qemu_gluster_parse(gconf, filename, options, errp);
    if (ret < 0) {
        errno = -ret;
        return NULL;
    }

    return qemu_gluster_glfs_init(gconf, errp);
}

/*
 * AIO callback routine called from GlusterFS thread.
 */
static void gluster_finish_aiocb(struct glfs_fd *fd, ssize_t ret,
#ifdef CONFIG_GLUSTERFS_IOCB_HAS_STAT
                                 struct glfs_stat *pre, struct glfs_stat *post,
#endif
                                 void *arg)
{
    GlusterAIOCB *acb = (GlusterAIOCB *)arg;

    if (!ret || ret == acb->size) {
        acb->ret = 0; /* Success */
    } else if (ret < 0) {
        acb->ret = -errno; /* Read/Write failed */
    } else {
        acb->ret = -EIO; /* Partial read/write - fail it */
    }

    aio_co_schedule(acb->aio_context, acb->coroutine);
}

static void qemu_gluster_parse_flags(int bdrv_flags, int *open_flags)
{
    assert(open_flags != NULL);

    *open_flags |= O_BINARY;

    if (bdrv_flags & BDRV_O_RDWR) {
        *open_flags |= O_RDWR;
    } else {
        *open_flags |= O_RDONLY;
    }

    if ((bdrv_flags & BDRV_O_NOCACHE)) {
        *open_flags |= O_DIRECT;
    }
}

/*
 * Do SEEK_DATA/HOLE to detect if it is functional. Older broken versions of
 * gfapi incorrectly return the current offset when SEEK_DATA/HOLE is used.
 * - Corrected versions return -1 and set errno to EINVAL.
 * - Versions that support SEEK_DATA/HOLE correctly, will return -1 and set
 *   errno to ENXIO when SEEK_DATA is called with a position of EOF.
 */
static bool qemu_gluster_test_seek(struct glfs_fd *fd)
{
    off_t ret = 0;

#if defined SEEK_HOLE && defined SEEK_DATA
    off_t eof;

    eof = glfs_lseek(fd, 0, SEEK_END);
    if (eof < 0) {
        /* this should never occur */
        return false;
    }

    /* this should always fail with ENXIO if SEEK_DATA is supported */
    ret = glfs_lseek(fd, eof, SEEK_DATA);
#endif

    return (ret < 0) && (errno == ENXIO);
}

static int qemu_gluster_open(BlockDriverState *bs,  QDict *options,
                             int bdrv_flags, Error **errp)
{
    BDRVGlusterState *s = bs->opaque;
    int open_flags = 0;
    int ret = 0;
    BlockdevOptionsGluster *gconf = NULL;
    QemuOpts *opts;
    Error *local_err = NULL;
    const char *filename, *logfile;

    opts = qemu_opts_create(&runtime_opts, NULL, 0, &error_abort);
    qemu_opts_absorb_qdict(opts, options, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        ret = -EINVAL;
        goto out;
    }

    filename = qemu_opt_get(opts, GLUSTER_OPT_FILENAME);

    s->debug = qemu_opt_get_number(opts, GLUSTER_OPT_DEBUG,
                                   GLUSTER_DEBUG_DEFAULT);
    if (s->debug < 0) {
        s->debug = 0;
    } else if (s->debug > GLUSTER_DEBUG_MAX) {
        s->debug = GLUSTER_DEBUG_MAX;
    }

    gconf = g_new0(BlockdevOptionsGluster, 1);
    gconf->debug = s->debug;
    gconf->has_debug = true;

    logfile = qemu_opt_get(opts, GLUSTER_OPT_LOGFILE);
    s->logfile = g_strdup(logfile ? logfile : GLUSTER_LOGFILE_DEFAULT);

    gconf->logfile = g_strdup(s->logfile);
    gconf->has_logfile = true;

    s->glfs = qemu_gluster_init(gconf, filename, options, errp);
    if (!s->glfs) {
        ret = -errno;
        goto out;
    }

#ifdef CONFIG_GLUSTERFS_XLATOR_OPT
    /* Without this, if fsync fails for a recoverable reason (for instance,
     * ENOSPC), gluster will dump its cache, preventing retries.  This means
     * almost certain data loss.  Not all gluster versions support the
     * 'resync-failed-syncs-after-fsync' key value, but there is no way to
     * discover during runtime if it is supported (this api returns success for
     * unknown key/value pairs) */
    ret = glfs_set_xlator_option(s->glfs, "*-write-behind",
                                          "resync-failed-syncs-after-fsync",
                                          "on");
    if (ret < 0) {
        error_setg_errno(errp, errno, "Unable to set xlator key/value pair");
        ret = -errno;
        goto out;
    }
#endif

    qemu_gluster_parse_flags(bdrv_flags, &open_flags);

    s->fd = glfs_open(s->glfs, gconf->path, open_flags);
    ret = s->fd ? 0 : -errno;

    if (ret == -EACCES || ret == -EROFS) {
        /* Try to degrade to read-only, but if it doesn't work, still use the
         * normal error message. */
        if (bdrv_apply_auto_read_only(bs, NULL, NULL) == 0) {
            open_flags = (open_flags & ~O_RDWR) | O_RDONLY;
            s->fd = glfs_open(s->glfs, gconf->path, open_flags);
            ret = s->fd ? 0 : -errno;
        }
    }

    s->supports_seek_data = qemu_gluster_test_seek(s->fd);

out:
    qemu_opts_del(opts);
    qapi_free_BlockdevOptionsGluster(gconf);
    if (!ret) {
        return ret;
    }
    g_free(s->logfile);
    if (s->fd) {
        glfs_close(s->fd);
    }

    glfs_clear_preopened(s->glfs);

    return ret;
}

static void qemu_gluster_refresh_limits(BlockDriverState *bs, Error **errp)
{
    bs->bl.max_transfer = GLUSTER_MAX_TRANSFER;
}

static int qemu_gluster_reopen_prepare(BDRVReopenState *state,
                                       BlockReopenQueue *queue, Error **errp)
{
    int ret = 0;
    BDRVGlusterState *s;
    BDRVGlusterReopenState *reop_s;
    BlockdevOptionsGluster *gconf;
    int open_flags = 0;

    assert(state != NULL);
    assert(state->bs != NULL);

    s = state->bs->opaque;

    state->opaque = g_new0(BDRVGlusterReopenState, 1);
    reop_s = state->opaque;

    qemu_gluster_parse_flags(state->flags, &open_flags);

    gconf = g_new0(BlockdevOptionsGluster, 1);
    gconf->debug = s->debug;
    gconf->has_debug = true;
    gconf->logfile = g_strdup(s->logfile);
    gconf->has_logfile = true;

    /*
     * If 'state->bs->exact_filename' is empty, 'state->options' should contain
     * the JSON parameters already parsed.
     */
    if (state->bs->exact_filename[0] != '\0') {
        reop_s->glfs = qemu_gluster_init(gconf, state->bs->exact_filename, NULL,
                                         errp);
    } else {
        reop_s->glfs = qemu_gluster_init(gconf, NULL, state->options, errp);
    }
    if (reop_s->glfs == NULL) {
        ret = -errno;
        goto exit;
    }

#ifdef CONFIG_GLUSTERFS_XLATOR_OPT
    ret = glfs_set_xlator_option(reop_s->glfs, "*-write-behind",
                                 "resync-failed-syncs-after-fsync", "on");
    if (ret < 0) {
        error_setg_errno(errp, errno, "Unable to set xlator key/value pair");
        ret = -errno;
        goto exit;
    }
#endif

    reop_s->fd = glfs_open(reop_s->glfs, gconf->path, open_flags);
    if (reop_s->fd == NULL) {
        /* reops->glfs will be cleaned up in _abort */
        ret = -errno;
        goto exit;
    }

exit:
    /* state->opaque will be freed in either the _abort or _commit */
    qapi_free_BlockdevOptionsGluster(gconf);
    return ret;
}

static void qemu_gluster_reopen_commit(BDRVReopenState *state)
{
    BDRVGlusterReopenState *reop_s = state->opaque;
    BDRVGlusterState *s = state->bs->opaque;


    /* close the old */
    if (s->fd) {
        glfs_close(s->fd);
    }

    glfs_clear_preopened(s->glfs);

    /* use the newly opened image / connection */
    s->fd         = reop_s->fd;
    s->glfs       = reop_s->glfs;

    g_free(state->opaque);
    state->opaque = NULL;

    return;
}


static void qemu_gluster_reopen_abort(BDRVReopenState *state)
{
    BDRVGlusterReopenState *reop_s = state->opaque;

    if (reop_s == NULL) {
        return;
    }

    if (reop_s->fd) {
        glfs_close(reop_s->fd);
    }

    glfs_clear_preopened(reop_s->glfs);

    g_free(state->opaque);
    state->opaque = NULL;

    return;
}

#ifdef CONFIG_GLUSTERFS_ZEROFILL
static coroutine_fn int qemu_gluster_co_pwrite_zeroes(BlockDriverState *bs,
                                                      int64_t offset,
                                                      int size,
                                                      BdrvRequestFlags flags)
{
    int ret;
    GlusterAIOCB acb;
    BDRVGlusterState *s = bs->opaque;

    acb.size = size;
    acb.ret = 0;
    acb.coroutine = qemu_coroutine_self();
    acb.aio_context = bdrv_get_aio_context(bs);

    ret = glfs_zerofill_async(s->fd, offset, size, gluster_finish_aiocb, &acb);
    if (ret < 0) {
        return -errno;
    }

    qemu_coroutine_yield();
    return acb.ret;
}
#endif

static int qemu_gluster_do_truncate(struct glfs_fd *fd, int64_t offset,
                                    PreallocMode prealloc, Error **errp)
{
    int64_t current_length;

    current_length = glfs_lseek(fd, 0, SEEK_END);
    if (current_length < 0) {
        error_setg_errno(errp, errno, "Failed to determine current size");
        return -errno;
    }

    if (current_length > offset && prealloc != PREALLOC_MODE_OFF) {
        error_setg(errp, "Cannot use preallocation for shrinking files");
        return -ENOTSUP;
    }

    if (current_length == offset) {
        return 0;
    }

    switch (prealloc) {
#ifdef CONFIG_GLUSTERFS_FALLOCATE
    case PREALLOC_MODE_FALLOC:
        if (glfs_fallocate(fd, 0, current_length, offset - current_length)) {
            error_setg_errno(errp, errno, "Could not preallocate data");
            return -errno;
        }
        break;
#endif /* CONFIG_GLUSTERFS_FALLOCATE */
#ifdef CONFIG_GLUSTERFS_ZEROFILL
    case PREALLOC_MODE_FULL:
        if (glfs_ftruncate(fd, offset)) {
            error_setg_errno(errp, errno, "Could not resize file");
            return -errno;
        }
        if (glfs_zerofill(fd, current_length, offset - current_length)) {
            error_setg_errno(errp, errno, "Could not zerofill the new area");
            return -errno;
        }
        break;
#endif /* CONFIG_GLUSTERFS_ZEROFILL */
    case PREALLOC_MODE_OFF:
        if (glfs_ftruncate(fd, offset)) {
            error_setg_errno(errp, errno, "Could not resize file");
            return -errno;
        }
        break;
    default:
        error_setg(errp, "Unsupported preallocation mode: %s",
                   PreallocMode_str(prealloc));
        return -EINVAL;
    }

    return 0;
}

static int qemu_gluster_co_create(BlockdevCreateOptions *options,
                                  Error **errp)
{
    BlockdevCreateOptionsGluster *opts = &options->u.gluster;
    struct glfs *glfs;
    struct glfs_fd *fd = NULL;
    int ret = 0;

    assert(options->driver == BLOCKDEV_DRIVER_GLUSTER);

    glfs = qemu_gluster_glfs_init(opts->location, errp);
    if (!glfs) {
        ret = -errno;
        goto out;
    }

    fd = glfs_creat(glfs, opts->location->path,
                    O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, S_IRUSR | S_IWUSR);
    if (!fd) {
        ret = -errno;
        goto out;
    }

    ret = qemu_gluster_do_truncate(fd, opts->size, opts->preallocation, errp);

out:
    if (fd) {
        if (glfs_close(fd) != 0 && ret == 0) {
            ret = -errno;
        }
    }
    glfs_clear_preopened(glfs);
    return ret;
}

static int coroutine_fn qemu_gluster_co_create_opts(const char *filename,
                                                    QemuOpts *opts,
                                                    Error **errp)
{
    BlockdevCreateOptions *options;
    BlockdevCreateOptionsGluster *gopts;
    BlockdevOptionsGluster *gconf;
    char *tmp = NULL;
    Error *local_err = NULL;
    int ret;

    options = g_new0(BlockdevCreateOptions, 1);
    options->driver = BLOCKDEV_DRIVER_GLUSTER;
    gopts = &options->u.gluster;

    gconf = g_new0(BlockdevOptionsGluster, 1);
    gopts->location = gconf;

    gopts->size = ROUND_UP(qemu_opt_get_size_del(opts, BLOCK_OPT_SIZE, 0),
                           BDRV_SECTOR_SIZE);

    tmp = qemu_opt_get_del(opts, BLOCK_OPT_PREALLOC);
    gopts->preallocation = qapi_enum_parse(&PreallocMode_lookup, tmp,
                                           PREALLOC_MODE_OFF, &local_err);
    g_free(tmp);
    if (local_err) {
        error_propagate(errp, local_err);
        ret = -EINVAL;
        goto fail;
    }

    gconf->debug = qemu_opt_get_number_del(opts, GLUSTER_OPT_DEBUG,
                                           GLUSTER_DEBUG_DEFAULT);
    if (gconf->debug < 0) {
        gconf->debug = 0;
    } else if (gconf->debug > GLUSTER_DEBUG_MAX) {
        gconf->debug = GLUSTER_DEBUG_MAX;
    }
    gconf->has_debug = true;

    gconf->logfile = qemu_opt_get_del(opts, GLUSTER_OPT_LOGFILE);
    if (!gconf->logfile) {
        gconf->logfile = g_strdup(GLUSTER_LOGFILE_DEFAULT);
    }
    gconf->has_logfile = true;

    ret = qemu_gluster_parse(gconf, filename, NULL, errp);
    if (ret < 0) {
        goto fail;
    }

    ret = qemu_gluster_co_create(options, errp);
    if (ret < 0) {
        goto fail;
    }

    ret = 0;
fail:
    qapi_free_BlockdevCreateOptions(options);
    return ret;
}

static coroutine_fn int qemu_gluster_co_rw(BlockDriverState *bs,
                                           int64_t sector_num, int nb_sectors,
                                           QEMUIOVector *qiov, int write)
{
    int ret;
    GlusterAIOCB acb;
    BDRVGlusterState *s = bs->opaque;
    size_t size = nb_sectors * BDRV_SECTOR_SIZE;
    off_t offset = sector_num * BDRV_SECTOR_SIZE;

    acb.size = size;
    acb.ret = 0;
    acb.coroutine = qemu_coroutine_self();
    acb.aio_context = bdrv_get_aio_context(bs);

    if (write) {
        ret = glfs_pwritev_async(s->fd, qiov->iov, qiov->niov, offset, 0,
                                 gluster_finish_aiocb, &acb);
    } else {
        ret = glfs_preadv_async(s->fd, qiov->iov, qiov->niov, offset, 0,
                                gluster_finish_aiocb, &acb);
    }

    if (ret < 0) {
        return -errno;
    }

    qemu_coroutine_yield();
    return acb.ret;
}

static coroutine_fn int qemu_gluster_co_truncate(BlockDriverState *bs,
                                                 int64_t offset,
                                                 PreallocMode prealloc,
                                                 Error **errp)
{
    BDRVGlusterState *s = bs->opaque;
    return qemu_gluster_do_truncate(s->fd, offset, prealloc, errp);
}

static coroutine_fn int qemu_gluster_co_readv(BlockDriverState *bs,
                                              int64_t sector_num,
                                              int nb_sectors,
                                              QEMUIOVector *qiov)
{
    return qemu_gluster_co_rw(bs, sector_num, nb_sectors, qiov, 0);
}

static coroutine_fn int qemu_gluster_co_writev(BlockDriverState *bs,
                                               int64_t sector_num,
                                               int nb_sectors,
                                               QEMUIOVector *qiov,
                                               int flags)
{
    assert(!flags);
    return qemu_gluster_co_rw(bs, sector_num, nb_sectors, qiov, 1);
}

static void qemu_gluster_close(BlockDriverState *bs)
{
    BDRVGlusterState *s = bs->opaque;

    g_free(s->logfile);
    if (s->fd) {
        glfs_close(s->fd);
        s->fd = NULL;
    }
    glfs_clear_preopened(s->glfs);
}

static coroutine_fn int qemu_gluster_co_flush_to_disk(BlockDriverState *bs)
{
    int ret;
    GlusterAIOCB acb;
    BDRVGlusterState *s = bs->opaque;

    acb.size = 0;
    acb.ret = 0;
    acb.coroutine = qemu_coroutine_self();
    acb.aio_context = bdrv_get_aio_context(bs);

    ret = glfs_fsync_async(s->fd, gluster_finish_aiocb, &acb);
    if (ret < 0) {
        ret = -errno;
        goto error;
    }

    qemu_coroutine_yield();
    if (acb.ret < 0) {
        ret = acb.ret;
        goto error;
    }

    return acb.ret;

error:
    /* Some versions of Gluster (3.5.6 -> 3.5.8?) will not retain its cache
     * after a fsync failure, so we have no way of allowing the guest to safely
     * continue.  Gluster versions prior to 3.5.6 don't retain the cache
     * either, but will invalidate the fd on error, so this is again our only
     * option.
     *
     * The 'resync-failed-syncs-after-fsync' xlator option for the
     * write-behind cache will cause later gluster versions to retain its
     * cache after error, so long as the fd remains open.  However, we
     * currently have no way of knowing if this option is supported.
     *
     * TODO: Once gluster provides a way for us to determine if the option
     * is supported, bypass the closure and setting drv to NULL.  */
    qemu_gluster_close(bs);
    bs->drv = NULL;
    return ret;
}

#ifdef CONFIG_GLUSTERFS_DISCARD
static coroutine_fn int qemu_gluster_co_pdiscard(BlockDriverState *bs,
                                                 int64_t offset, int size)
{
    int ret;
    GlusterAIOCB acb;
    BDRVGlusterState *s = bs->opaque;

    acb.size = 0;
    acb.ret = 0;
    acb.coroutine = qemu_coroutine_self();
    acb.aio_context = bdrv_get_aio_context(bs);

    ret = glfs_discard_async(s->fd, offset, size, gluster_finish_aiocb, &acb);
    if (ret < 0) {
        return -errno;
    }

    qemu_coroutine_yield();
    return acb.ret;
}
#endif

static int64_t qemu_gluster_getlength(BlockDriverState *bs)
{
    BDRVGlusterState *s = bs->opaque;
    int64_t ret;

    ret = glfs_lseek(s->fd, 0, SEEK_END);
    if (ret < 0) {
        return -errno;
    } else {
        return ret;
    }
}

static int64_t qemu_gluster_allocated_file_size(BlockDriverState *bs)
{
    BDRVGlusterState *s = bs->opaque;
    struct stat st;
    int ret;

    ret = glfs_fstat(s->fd, &st);
    if (ret < 0) {
        return -errno;
    } else {
        return st.st_blocks * 512;
    }
}

static int qemu_gluster_has_zero_init(BlockDriverState *bs)
{
    /* GlusterFS volume could be backed by a block device */
    return 0;
}

/*
 * Find allocation range in @bs around offset @start.
 * May change underlying file descriptor's file offset.
 * If @start is not in a hole, store @start in @data, and the
 * beginning of the next hole in @hole, and return 0.
 * If @start is in a non-trailing hole, store @start in @hole and the
 * beginning of the next non-hole in @data, and return 0.
 * If @start is in a trailing hole or beyond EOF, return -ENXIO.
 * If we can't find out, return a negative errno other than -ENXIO.
 *
 * (Shamefully copied from file-posix.c, only minuscule adaptions.)
 */
static int find_allocation(BlockDriverState *bs, off_t start,
                           off_t *data, off_t *hole)
{
    BDRVGlusterState *s = bs->opaque;

    if (!s->supports_seek_data) {
        goto exit;
    }

#if defined SEEK_HOLE && defined SEEK_DATA
    off_t offs;

    /*
     * SEEK_DATA cases:
     * D1. offs == start: start is in data
     * D2. offs > start: start is in a hole, next data at offs
     * D3. offs < 0, errno = ENXIO: either start is in a trailing hole
     *                              or start is beyond EOF
     *     If the latter happens, the file has been truncated behind
     *     our back since we opened it.  All bets are off then.
     *     Treating like a trailing hole is simplest.
     * D4. offs < 0, errno != ENXIO: we learned nothing
     */
    offs = glfs_lseek(s->fd, start, SEEK_DATA);
    if (offs < 0) {
        return -errno;          /* D3 or D4 */
    }

    if (offs < start) {
        /* This is not a valid return by lseek().  We are safe to just return
         * -EIO in this case, and we'll treat it like D4. Unfortunately some
         *  versions of gluster server will return offs < start, so an assert
         *  here will unnecessarily abort QEMU. */
        return -EIO;
    }

    if (offs > start) {
        /* D2: in hole, next data at offs */
        *hole = start;
        *data = offs;
        return 0;
    }

    /* D1: in data, end not yet known */

    /*
     * SEEK_HOLE cases:
     * H1. offs == start: start is in a hole
     *     If this happens here, a hole has been dug behind our back
     *     since the previous lseek().
     * H2. offs > start: either start is in data, next hole at offs,
     *                   or start is in trailing hole, EOF at offs
     *     Linux treats trailing holes like any other hole: offs ==
     *     start.  Solaris seeks to EOF instead: offs > start (blech).
     *     If that happens here, a hole has been dug behind our back
     *     since the previous lseek().
     * H3. offs < 0, errno = ENXIO: start is beyond EOF
     *     If this happens, the file has been truncated behind our
     *     back since we opened it.  Treat it like a trailing hole.
     * H4. offs < 0, errno != ENXIO: we learned nothing
     *     Pretend we know nothing at all, i.e. "forget" about D1.
     */
    offs = glfs_lseek(s->fd, start, SEEK_HOLE);
    if (offs < 0) {
        return -errno;          /* D1 and (H3 or H4) */
    }

    if (offs < start) {
        /* This is not a valid return by lseek().  We are safe to just return
         * -EIO in this case, and we'll treat it like H4. Unfortunately some
         *  versions of gluster server will return offs < start, so an assert
         *  here will unnecessarily abort QEMU. */
        return -EIO;
    }

    if (offs > start) {
        /*
         * D1 and H2: either in data, next hole at offs, or it was in
         * data but is now in a trailing hole.  In the latter case,
         * all bets are off.  Treating it as if it there was data all
         * the way to EOF is safe, so simply do that.
         */
        *data = start;
        *hole = offs;
        return 0;
    }

    /* D1 and H1 */
    return -EBUSY;
#endif

exit:
    return -ENOTSUP;
}

/*
 * Returns the allocation status of the specified offset.
 *
 * The block layer guarantees 'offset' and 'bytes' are within bounds.
 *
 * 'pnum' is set to the number of bytes (including and immediately following
 * the specified offset) that are known to be in the same
 * allocated/unallocated state.
 *
 * 'bytes' is the max value 'pnum' should be set to.
 *
 * (Based on raw_co_block_status() from file-posix.c.)
 */
static int coroutine_fn qemu_gluster_co_block_status(BlockDriverState *bs,
                                                     bool want_zero,
                                                     int64_t offset,
                                                     int64_t bytes,
                                                     int64_t *pnum,
                                                     int64_t *map,
                                                     BlockDriverState **file)
{
    BDRVGlusterState *s = bs->opaque;
    off_t data = 0, hole = 0;
    int ret = -EINVAL;

    if (!s->fd) {
        return ret;
    }

    if (!want_zero) {
        *pnum = bytes;
        *map = offset;
        *file = bs;
        return BDRV_BLOCK_DATA | BDRV_BLOCK_OFFSET_VALID;
    }

    ret = find_allocation(bs, offset, &data, &hole);
    if (ret == -ENXIO) {
        /* Trailing hole */
        *pnum = bytes;
        ret = BDRV_BLOCK_ZERO;
    } else if (ret < 0) {
        /* No info available, so pretend there are no holes */
        *pnum = bytes;
        ret = BDRV_BLOCK_DATA;
    } else if (data == offset) {
        /* On a data extent, compute bytes to the end of the extent,
         * possibly including a partial sector at EOF. */
        *pnum = MIN(bytes, hole - offset);
        ret = BDRV_BLOCK_DATA;
    } else {
        /* On a hole, compute bytes to the beginning of the next extent.  */
        assert(hole == offset);
        *pnum = MIN(bytes, data - offset);
        ret = BDRV_BLOCK_ZERO;
    }

    *map = offset;
    *file = bs;

    return ret | BDRV_BLOCK_OFFSET_VALID;
}


static const char *const gluster_strong_open_opts[] = {
    GLUSTER_OPT_VOLUME,
    GLUSTER_OPT_PATH,
    GLUSTER_OPT_TYPE,
    GLUSTER_OPT_SERVER_PATTERN,
    GLUSTER_OPT_HOST,
    GLUSTER_OPT_PORT,
    GLUSTER_OPT_TO,
    GLUSTER_OPT_IPV4,
    GLUSTER_OPT_IPV6,
    GLUSTER_OPT_SOCKET,

    NULL
};

static BlockDriver bdrv_gluster = {
    .format_name                  = "gluster",
    .protocol_name                = "gluster",
    .instance_size                = sizeof(BDRVGlusterState),
    .bdrv_needs_filename          = false,
    .bdrv_file_open               = qemu_gluster_open,
    .bdrv_reopen_prepare          = qemu_gluster_reopen_prepare,
    .bdrv_reopen_commit           = qemu_gluster_reopen_commit,
    .bdrv_reopen_abort            = qemu_gluster_reopen_abort,
    .bdrv_close                   = qemu_gluster_close,
    .bdrv_co_create               = qemu_gluster_co_create,
    .bdrv_co_create_opts          = qemu_gluster_co_create_opts,
    .bdrv_getlength               = qemu_gluster_getlength,
    .bdrv_get_allocated_file_size = qemu_gluster_allocated_file_size,
    .bdrv_co_truncate             = qemu_gluster_co_truncate,
    .bdrv_co_readv                = qemu_gluster_co_readv,
    .bdrv_co_writev               = qemu_gluster_co_writev,
    .bdrv_co_flush_to_disk        = qemu_gluster_co_flush_to_disk,
    .bdrv_has_zero_init           = qemu_gluster_has_zero_init,
#ifdef CONFIG_GLUSTERFS_DISCARD
    .bdrv_co_pdiscard             = qemu_gluster_co_pdiscard,
#endif
#ifdef CONFIG_GLUSTERFS_ZEROFILL
    .bdrv_co_pwrite_zeroes        = qemu_gluster_co_pwrite_zeroes,
#endif
    .bdrv_co_block_status         = qemu_gluster_co_block_status,
    .bdrv_refresh_limits          = qemu_gluster_refresh_limits,
    .create_opts                  = &qemu_gluster_create_opts,
    .strong_runtime_opts          = gluster_strong_open_opts,
};

static BlockDriver bdrv_gluster_tcp = {
    .format_name                  = "gluster",
    .protocol_name                = "gluster+tcp",
    .instance_size                = sizeof(BDRVGlusterState),
    .bdrv_needs_filename          = false,
    .bdrv_file_open               = qemu_gluster_open,
    .bdrv_reopen_prepare          = qemu_gluster_reopen_prepare,
    .bdrv_reopen_commit           = qemu_gluster_reopen_commit,
    .bdrv_reopen_abort            = qemu_gluster_reopen_abort,
    .bdrv_close                   = qemu_gluster_close,
    .bdrv_co_create               = qemu_gluster_co_create,
    .bdrv_co_create_opts          = qemu_gluster_co_create_opts,
    .bdrv_getlength               = qemu_gluster_getlength,
    .bdrv_get_allocated_file_size = qemu_gluster_allocated_file_size,
    .bdrv_co_truncate             = qemu_gluster_co_truncate,
    .bdrv_co_readv                = qemu_gluster_co_readv,
    .bdrv_co_writev               = qemu_gluster_co_writev,
    .bdrv_co_flush_to_disk        = qemu_gluster_co_flush_to_disk,
    .bdrv_has_zero_init           = qemu_gluster_has_zero_init,
#ifdef CONFIG_GLUSTERFS_DISCARD
    .bdrv_co_pdiscard             = qemu_gluster_co_pdiscard,
#endif
#ifdef CONFIG_GLUSTERFS_ZEROFILL
    .bdrv_co_pwrite_zeroes        = qemu_gluster_co_pwrite_zeroes,
#endif
    .bdrv_co_block_status         = qemu_gluster_co_block_status,
    .bdrv_refresh_limits          = qemu_gluster_refresh_limits,
    .create_opts                  = &qemu_gluster_create_opts,
    .strong_runtime_opts          = gluster_strong_open_opts,
};

static BlockDriver bdrv_gluster_unix = {
    .format_name                  = "gluster",
    .protocol_name                = "gluster+unix",
    .instance_size                = sizeof(BDRVGlusterState),
    .bdrv_needs_filename          = true,
    .bdrv_file_open               = qemu_gluster_open,
    .bdrv_reopen_prepare          = qemu_gluster_reopen_prepare,
    .bdrv_reopen_commit           = qemu_gluster_reopen_commit,
    .bdrv_reopen_abort            = qemu_gluster_reopen_abort,
    .bdrv_close                   = qemu_gluster_close,
    .bdrv_co_create               = qemu_gluster_co_create,
    .bdrv_co_create_opts          = qemu_gluster_co_create_opts,
    .bdrv_getlength               = qemu_gluster_getlength,
    .bdrv_get_allocated_file_size = qemu_gluster_allocated_file_size,
    .bdrv_co_truncate             = qemu_gluster_co_truncate,
    .bdrv_co_readv                = qemu_gluster_co_readv,
    .bdrv_co_writev               = qemu_gluster_co_writev,
    .bdrv_co_flush_to_disk        = qemu_gluster_co_flush_to_disk,
    .bdrv_has_zero_init           = qemu_gluster_has_zero_init,
#ifdef CONFIG_GLUSTERFS_DISCARD
    .bdrv_co_pdiscard             = qemu_gluster_co_pdiscard,
#endif
#ifdef CONFIG_GLUSTERFS_ZEROFILL
    .bdrv_co_pwrite_zeroes        = qemu_gluster_co_pwrite_zeroes,
#endif
    .bdrv_co_block_status         = qemu_gluster_co_block_status,
    .bdrv_refresh_limits          = qemu_gluster_refresh_limits,
    .create_opts                  = &qemu_gluster_create_opts,
    .strong_runtime_opts          = gluster_strong_open_opts,
};

/* rdma is deprecated (actually never supported for volfile fetch).
 * Let's maintain it for the protocol compatibility, to make sure things
 * won't break immediately. For now, gluster+rdma will fall back to gluster+tcp
 * protocol with a warning.
 * TODO: remove gluster+rdma interface support
 */
static BlockDriver bdrv_gluster_rdma = {
    .format_name                  = "gluster",
    .protocol_name                = "gluster+rdma",
    .instance_size                = sizeof(BDRVGlusterState),
    .bdrv_needs_filename          = true,
    .bdrv_file_open               = qemu_gluster_open,
    .bdrv_reopen_prepare          = qemu_gluster_reopen_prepare,
    .bdrv_reopen_commit           = qemu_gluster_reopen_commit,
    .bdrv_reopen_abort            = qemu_gluster_reopen_abort,
    .bdrv_close                   = qemu_gluster_close,
    .bdrv_co_create               = qemu_gluster_co_create,
    .bdrv_co_create_opts          = qemu_gluster_co_create_opts,
    .bdrv_getlength               = qemu_gluster_getlength,
    .bdrv_get_allocated_file_size = qemu_gluster_allocated_file_size,
    .bdrv_co_truncate             = qemu_gluster_co_truncate,
    .bdrv_co_readv                = qemu_gluster_co_readv,
    .bdrv_co_writev               = qemu_gluster_co_writev,
    .bdrv_co_flush_to_disk        = qemu_gluster_co_flush_to_disk,
    .bdrv_has_zero_init           = qemu_gluster_has_zero_init,
#ifdef CONFIG_GLUSTERFS_DISCARD
    .bdrv_co_pdiscard             = qemu_gluster_co_pdiscard,
#endif
#ifdef CONFIG_GLUSTERFS_ZEROFILL
    .bdrv_co_pwrite_zeroes        = qemu_gluster_co_pwrite_zeroes,
#endif
    .bdrv_co_block_status         = qemu_gluster_co_block_status,
    .bdrv_refresh_limits          = qemu_gluster_refresh_limits,
    .create_opts                  = &qemu_gluster_create_opts,
    .strong_runtime_opts          = gluster_strong_open_opts,
};

static void bdrv_gluster_init(void)
{
    bdrv_register(&bdrv_gluster_rdma);
    bdrv_register(&bdrv_gluster_unix);
    bdrv_register(&bdrv_gluster_tcp);
    bdrv_register(&bdrv_gluster);
}

block_init(bdrv_gluster_init);

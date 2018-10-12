/*
 * QEMU Block driver for RADOS (Ceph)
 *
 * Copyright (C) 2010-2011 Christian Brunner <chb@muc.de>,
 *                         Josh Durgin <josh.durgin@dreamhost.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu/osdep.h"

#include <rbd/librbd.h>
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/option.h"
#include "block/block_int.h"
#include "block/qdict.h"
#include "crypto/secret.h"
#include "qemu/cutils.h"
#include "qapi/qmp/qstring.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qjson.h"
#include "qapi/qmp/qlist.h"
#include "qapi/qobject-input-visitor.h"
#include "qapi/qapi-visit-block-core.h"

/*
 * When specifying the image filename use:
 *
 * rbd:poolname/devicename[@snapshotname][:option1=value1[:option2=value2...]]
 *
 * poolname must be the name of an existing rados pool.
 *
 * devicename is the name of the rbd image.
 *
 * Each option given is used to configure rados, and may be any valid
 * Ceph option, "id", or "conf".
 *
 * The "id" option indicates what user we should authenticate as to
 * the Ceph cluster.  If it is excluded we will use the Ceph default
 * (normally 'admin').
 *
 * The "conf" option specifies a Ceph configuration file to read.  If
 * it is not specified, we will read from the default Ceph locations
 * (e.g., /etc/ceph/ceph.conf).  To avoid reading _any_ configuration
 * file, specify conf=/dev/null.
 *
 * Configuration values containing :, @, or = can be escaped with a
 * leading "\".
 */

/* rbd_aio_discard added in 0.1.2 */
#if LIBRBD_VERSION_CODE >= LIBRBD_VERSION(0, 1, 2)
#define LIBRBD_SUPPORTS_DISCARD
#else
#undef LIBRBD_SUPPORTS_DISCARD
#endif

#define OBJ_MAX_SIZE (1UL << OBJ_DEFAULT_OBJ_ORDER)

#define RBD_MAX_SNAPS 100

/* The LIBRBD_SUPPORTS_IOVEC is defined in librbd.h */
#ifdef LIBRBD_SUPPORTS_IOVEC
#define LIBRBD_USE_IOVEC 1
#else
#define LIBRBD_USE_IOVEC 0
#endif

typedef enum {
    RBD_AIO_READ,
    RBD_AIO_WRITE,
    RBD_AIO_DISCARD,
    RBD_AIO_FLUSH
} RBDAIOCmd;

typedef struct RBDAIOCB {
    BlockAIOCB common;
    int64_t ret;
    QEMUIOVector *qiov;
    char *bounce;
    RBDAIOCmd cmd;
    int error;
    struct BDRVRBDState *s;
} RBDAIOCB;

typedef struct RADOSCB {
    RBDAIOCB *acb;
    struct BDRVRBDState *s;
    int64_t size;
    char *buf;
    int64_t ret;
} RADOSCB;

typedef struct BDRVRBDState {
    rados_t cluster;
    rados_ioctx_t io_ctx;
    rbd_image_t image;
    char *image_name;
    char *snap;
} BDRVRBDState;

static int qemu_rbd_connect(rados_t *cluster, rados_ioctx_t *io_ctx,
                            BlockdevOptionsRbd *opts, bool cache,
                            const char *keypairs, const char *secretid,
                            Error **errp);

static char *qemu_rbd_next_tok(char *src, char delim, char **p)
{
    char *end;

    *p = NULL;

    for (end = src; *end; ++end) {
        if (*end == delim) {
            break;
        }
        if (*end == '\\' && end[1] != '\0') {
            end++;
        }
    }
    if (*end == delim) {
        *p = end + 1;
        *end = '\0';
    }
    return src;
}

static void qemu_rbd_unescape(char *src)
{
    char *p;

    for (p = src; *src; ++src, ++p) {
        if (*src == '\\' && src[1] != '\0') {
            src++;
        }
        *p = *src;
    }
    *p = '\0';
}

static void qemu_rbd_parse_filename(const char *filename, QDict *options,
                                    Error **errp)
{
    const char *start;
    char *p, *buf;
    QList *keypairs = NULL;
    char *found_str;

    if (!strstart(filename, "rbd:", &start)) {
        error_setg(errp, "File name must start with 'rbd:'");
        return;
    }

    buf = g_strdup(start);
    p = buf;

    found_str = qemu_rbd_next_tok(p, '/', &p);
    if (!p) {
        error_setg(errp, "Pool name is required");
        goto done;
    }
    qemu_rbd_unescape(found_str);
    qdict_put_str(options, "pool", found_str);

    if (strchr(p, '@')) {
        found_str = qemu_rbd_next_tok(p, '@', &p);
        qemu_rbd_unescape(found_str);
        qdict_put_str(options, "image", found_str);

        found_str = qemu_rbd_next_tok(p, ':', &p);
        qemu_rbd_unescape(found_str);
        qdict_put_str(options, "snapshot", found_str);
    } else {
        found_str = qemu_rbd_next_tok(p, ':', &p);
        qemu_rbd_unescape(found_str);
        qdict_put_str(options, "image", found_str);
    }
    if (!p) {
        goto done;
    }

    /* The following are essentially all key/value pairs, and we treat
     * 'id' and 'conf' a bit special.  Key/value pairs may be in any order. */
    while (p) {
        char *name, *value;
        name = qemu_rbd_next_tok(p, '=', &p);
        if (!p) {
            error_setg(errp, "conf option %s has no value", name);
            break;
        }

        qemu_rbd_unescape(name);

        value = qemu_rbd_next_tok(p, ':', &p);
        qemu_rbd_unescape(value);

        if (!strcmp(name, "conf")) {
            qdict_put_str(options, "conf", value);
        } else if (!strcmp(name, "id")) {
            qdict_put_str(options, "user", value);
        } else {
            /*
             * We pass these internally to qemu_rbd_set_keypairs(), so
             * we can get away with the simpler list of [ "key1",
             * "value1", "key2", "value2" ] rather than a raw dict
             * { "key1": "value1", "key2": "value2" } where we can't
             * guarantee order, or even a more correct but complex
             * [ { "key1": "value1" }, { "key2": "value2" } ]
             */
            if (!keypairs) {
                keypairs = qlist_new();
            }
            qlist_append_str(keypairs, name);
            qlist_append_str(keypairs, value);
        }
    }

    if (keypairs) {
        qdict_put(options, "=keyvalue-pairs",
                  qobject_to_json(QOBJECT(keypairs)));
    }

done:
    g_free(buf);
    qobject_unref(keypairs);
    return;
}


static void qemu_rbd_refresh_limits(BlockDriverState *bs, Error **errp)
{
    /* XXX Does RBD support AIO on less than 512-byte alignment? */
    bs->bl.request_alignment = 512;
}


static int qemu_rbd_set_auth(rados_t cluster, BlockdevOptionsRbd *opts,
                             Error **errp)
{
    char *key, *acr;
    int r;
    GString *accu;
    RbdAuthModeList *auth;

    if (opts->key_secret) {
        key = qcrypto_secret_lookup_as_base64(opts->key_secret, errp);
        if (!key) {
            return -EIO;
        }
        r = rados_conf_set(cluster, "key", key);
        g_free(key);
        if (r < 0) {
            error_setg_errno(errp, -r, "Could not set 'key'");
            return r;
        }
    }

    if (opts->has_auth_client_required) {
        accu = g_string_new("");
        for (auth = opts->auth_client_required; auth; auth = auth->next) {
            if (accu->str[0]) {
                g_string_append_c(accu, ';');
            }
            g_string_append(accu, RbdAuthMode_str(auth->value));
        }
        acr = g_string_free(accu, FALSE);
        r = rados_conf_set(cluster, "auth_client_required", acr);
        g_free(acr);
        if (r < 0) {
            error_setg_errno(errp, -r,
                             "Could not set 'auth_client_required'");
            return r;
        }
    }

    return 0;
}

static int qemu_rbd_set_keypairs(rados_t cluster, const char *keypairs_json,
                                 Error **errp)
{
    QList *keypairs;
    QString *name;
    QString *value;
    const char *key;
    size_t remaining;
    int ret = 0;

    if (!keypairs_json) {
        return ret;
    }
    keypairs = qobject_to(QList,
                          qobject_from_json(keypairs_json, &error_abort));
    remaining = qlist_size(keypairs) / 2;
    assert(remaining);

    while (remaining--) {
        name = qobject_to(QString, qlist_pop(keypairs));
        value = qobject_to(QString, qlist_pop(keypairs));
        assert(name && value);
        key = qstring_get_str(name);

        ret = rados_conf_set(cluster, key, qstring_get_str(value));
        qobject_unref(value);
        if (ret < 0) {
            error_setg_errno(errp, -ret, "invalid conf option %s", key);
            qobject_unref(name);
            ret = -EINVAL;
            break;
        }
        qobject_unref(name);
    }

    qobject_unref(keypairs);
    return ret;
}

static void qemu_rbd_memset(RADOSCB *rcb, int64_t offs)
{
    if (LIBRBD_USE_IOVEC) {
        RBDAIOCB *acb = rcb->acb;
        iov_memset(acb->qiov->iov, acb->qiov->niov, offs, 0,
                   acb->qiov->size - offs);
    } else {
        memset(rcb->buf + offs, 0, rcb->size - offs);
    }
}

static QemuOptsList runtime_opts = {
    .name = "rbd",
    .head = QTAILQ_HEAD_INITIALIZER(runtime_opts.head),
    .desc = {
        {
            .name = "pool",
            .type = QEMU_OPT_STRING,
            .help = "Rados pool name",
        },
        {
            .name = "image",
            .type = QEMU_OPT_STRING,
            .help = "Image name in the pool",
        },
        {
            .name = "conf",
            .type = QEMU_OPT_STRING,
            .help = "Rados config file location",
        },
        {
            .name = "snapshot",
            .type = QEMU_OPT_STRING,
            .help = "Ceph snapshot name",
        },
        {
            /* maps to 'id' in rados_create() */
            .name = "user",
            .type = QEMU_OPT_STRING,
            .help = "Rados id name",
        },
        /*
         * server.* extracted manually, see qemu_rbd_mon_host()
         */
        { /* end of list */ }
    },
};

/* FIXME Deprecate and remove keypairs or make it available in QMP. */
static int qemu_rbd_do_create(BlockdevCreateOptions *options,
                              const char *keypairs, const char *password_secret,
                              Error **errp)
{
    BlockdevCreateOptionsRbd *opts = &options->u.rbd;
    rados_t cluster;
    rados_ioctx_t io_ctx;
    int obj_order = 0;
    int ret;

    assert(options->driver == BLOCKDEV_DRIVER_RBD);
    if (opts->location->has_snapshot) {
        error_setg(errp, "Can't use snapshot name for image creation");
        return -EINVAL;
    }

    if (opts->has_cluster_size) {
        int64_t objsize = opts->cluster_size;
        if ((objsize - 1) & objsize) {    /* not a power of 2? */
            error_setg(errp, "obj size needs to be power of 2");
            return -EINVAL;
        }
        if (objsize < 4096) {
            error_setg(errp, "obj size too small");
            return -EINVAL;
        }
        obj_order = ctz32(objsize);
    }

    ret = qemu_rbd_connect(&cluster, &io_ctx, opts->location, false, keypairs,
                           password_secret, errp);
    if (ret < 0) {
        return ret;
    }

    ret = rbd_create(io_ctx, opts->location->image, opts->size, &obj_order);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "error rbd create");
        goto out;
    }

    ret = 0;
out:
    rados_ioctx_destroy(io_ctx);
    rados_shutdown(cluster);
    return ret;
}

static int qemu_rbd_co_create(BlockdevCreateOptions *options, Error **errp)
{
    return qemu_rbd_do_create(options, NULL, NULL, errp);
}

static int coroutine_fn qemu_rbd_co_create_opts(const char *filename,
                                                QemuOpts *opts,
                                                Error **errp)
{
    BlockdevCreateOptions *create_options;
    BlockdevCreateOptionsRbd *rbd_opts;
    BlockdevOptionsRbd *loc;
    Error *local_err = NULL;
    const char *keypairs, *password_secret;
    QDict *options = NULL;
    int ret = 0;

    create_options = g_new0(BlockdevCreateOptions, 1);
    create_options->driver = BLOCKDEV_DRIVER_RBD;
    rbd_opts = &create_options->u.rbd;

    rbd_opts->location = g_new0(BlockdevOptionsRbd, 1);

    password_secret = qemu_opt_get(opts, "password-secret");

    /* Read out options */
    rbd_opts->size = ROUND_UP(qemu_opt_get_size_del(opts, BLOCK_OPT_SIZE, 0),
                              BDRV_SECTOR_SIZE);
    rbd_opts->cluster_size = qemu_opt_get_size_del(opts,
                                                   BLOCK_OPT_CLUSTER_SIZE, 0);
    rbd_opts->has_cluster_size = (rbd_opts->cluster_size != 0);

    options = qdict_new();
    qemu_rbd_parse_filename(filename, options, &local_err);
    if (local_err) {
        ret = -EINVAL;
        error_propagate(errp, local_err);
        goto exit;
    }

    /*
     * Caution: while qdict_get_try_str() is fine, getting non-string
     * types would require more care.  When @options come from -blockdev
     * or blockdev_add, its members are typed according to the QAPI
     * schema, but when they come from -drive, they're all QString.
     */
    loc = rbd_opts->location;
    loc->pool     = g_strdup(qdict_get_try_str(options, "pool"));
    loc->conf     = g_strdup(qdict_get_try_str(options, "conf"));
    loc->has_conf = !!loc->conf;
    loc->user     = g_strdup(qdict_get_try_str(options, "user"));
    loc->has_user = !!loc->user;
    loc->image    = g_strdup(qdict_get_try_str(options, "image"));
    keypairs      = qdict_get_try_str(options, "=keyvalue-pairs");

    ret = qemu_rbd_do_create(create_options, keypairs, password_secret, errp);
    if (ret < 0) {
        goto exit;
    }

exit:
    qobject_unref(options);
    qapi_free_BlockdevCreateOptions(create_options);
    return ret;
}

/*
 * This aio completion is being called from rbd_finish_bh() and runs in qemu
 * BH context.
 */
static void qemu_rbd_complete_aio(RADOSCB *rcb)
{
    RBDAIOCB *acb = rcb->acb;
    int64_t r;

    r = rcb->ret;

    if (acb->cmd != RBD_AIO_READ) {
        if (r < 0) {
            acb->ret = r;
            acb->error = 1;
        } else if (!acb->error) {
            acb->ret = rcb->size;
        }
    } else {
        if (r < 0) {
            qemu_rbd_memset(rcb, 0);
            acb->ret = r;
            acb->error = 1;
        } else if (r < rcb->size) {
            qemu_rbd_memset(rcb, r);
            if (!acb->error) {
                acb->ret = rcb->size;
            }
        } else if (!acb->error) {
            acb->ret = r;
        }
    }

    g_free(rcb);

    if (!LIBRBD_USE_IOVEC) {
        if (acb->cmd == RBD_AIO_READ) {
            qemu_iovec_from_buf(acb->qiov, 0, acb->bounce, acb->qiov->size);
        }
        qemu_vfree(acb->bounce);
    }

    acb->common.cb(acb->common.opaque, (acb->ret > 0 ? 0 : acb->ret));

    qemu_aio_unref(acb);
}

static char *qemu_rbd_mon_host(BlockdevOptionsRbd *opts, Error **errp)
{
    const char **vals;
    const char *host, *port;
    char *rados_str;
    InetSocketAddressBaseList *p;
    int i, cnt;

    if (!opts->has_server) {
        return NULL;
    }

    for (cnt = 0, p = opts->server; p; p = p->next) {
        cnt++;
    }

    vals = g_new(const char *, cnt + 1);

    for (i = 0, p = opts->server; p; p = p->next, i++) {
        host = p->value->host;
        port = p->value->port;

        if (strchr(host, ':')) {
            vals[i] = g_strdup_printf("[%s]:%s", host, port);
        } else {
            vals[i] = g_strdup_printf("%s:%s", host, port);
        }
    }
    vals[i] = NULL;

    rados_str = i ? g_strjoinv(";", (char **)vals) : NULL;
    g_strfreev((char **)vals);
    return rados_str;
}

static int qemu_rbd_connect(rados_t *cluster, rados_ioctx_t *io_ctx,
                            BlockdevOptionsRbd *opts, bool cache,
                            const char *keypairs, const char *secretid,
                            Error **errp)
{
    char *mon_host = NULL;
    Error *local_err = NULL;
    int r;

    if (secretid) {
        if (opts->key_secret) {
            error_setg(errp,
                       "Legacy 'password-secret' clashes with 'key-secret'");
            return -EINVAL;
        }
        opts->key_secret = g_strdup(secretid);
        opts->has_key_secret = true;
    }

    mon_host = qemu_rbd_mon_host(opts, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        r = -EINVAL;
        goto failed_opts;
    }

    r = rados_create(cluster, opts->user);
    if (r < 0) {
        error_setg_errno(errp, -r, "error initializing");
        goto failed_opts;
    }

    /* try default location when conf=NULL, but ignore failure */
    r = rados_conf_read_file(*cluster, opts->conf);
    if (opts->has_conf && r < 0) {
        error_setg_errno(errp, -r, "error reading conf file %s", opts->conf);
        goto failed_shutdown;
    }

    r = qemu_rbd_set_keypairs(*cluster, keypairs, errp);
    if (r < 0) {
        goto failed_shutdown;
    }

    if (mon_host) {
        r = rados_conf_set(*cluster, "mon_host", mon_host);
        if (r < 0) {
            goto failed_shutdown;
        }
    }

    r = qemu_rbd_set_auth(*cluster, opts, errp);
    if (r < 0) {
        goto failed_shutdown;
    }

    /*
     * Fallback to more conservative semantics if setting cache
     * options fails. Ignore errors from setting rbd_cache because the
     * only possible error is that the option does not exist, and
     * librbd defaults to no caching. If write through caching cannot
     * be set up, fall back to no caching.
     */
    if (cache) {
        rados_conf_set(*cluster, "rbd_cache", "true");
    } else {
        rados_conf_set(*cluster, "rbd_cache", "false");
    }

    r = rados_connect(*cluster);
    if (r < 0) {
        error_setg_errno(errp, -r, "error connecting");
        goto failed_shutdown;
    }

    r = rados_ioctx_create(*cluster, opts->pool, io_ctx);
    if (r < 0) {
        error_setg_errno(errp, -r, "error opening pool %s", opts->pool);
        goto failed_shutdown;
    }

    return 0;

failed_shutdown:
    rados_shutdown(*cluster);
failed_opts:
    g_free(mon_host);
    return r;
}

static int qemu_rbd_convert_options(QDict *options, BlockdevOptionsRbd **opts,
                                    Error **errp)
{
    Visitor *v;
    Error *local_err = NULL;

    /* Convert the remaining options into a QAPI object */
    v = qobject_input_visitor_new_flat_confused(options, errp);
    if (!v) {
        return -EINVAL;
    }

    visit_type_BlockdevOptionsRbd(v, NULL, opts, &local_err);
    visit_free(v);

    if (local_err) {
        error_propagate(errp, local_err);
        return -EINVAL;
    }

    return 0;
}

static int qemu_rbd_attempt_legacy_options(QDict *options,
                                           BlockdevOptionsRbd **opts,
                                           char **keypairs)
{
    char *filename;
    int r;

    filename = g_strdup(qdict_get_try_str(options, "filename"));
    if (!filename) {
        return -EINVAL;
    }
    qdict_del(options, "filename");

    qemu_rbd_parse_filename(filename, options, NULL);

    /* keypairs freed by caller */
    *keypairs = g_strdup(qdict_get_try_str(options, "=keyvalue-pairs"));
    if (*keypairs) {
        qdict_del(options, "=keyvalue-pairs");
    }

    r = qemu_rbd_convert_options(options, opts, NULL);

    g_free(filename);
    return r;
}

static int qemu_rbd_open(BlockDriverState *bs, QDict *options, int flags,
                         Error **errp)
{
    BDRVRBDState *s = bs->opaque;
    BlockdevOptionsRbd *opts = NULL;
    const QDictEntry *e;
    Error *local_err = NULL;
    char *keypairs, *secretid;
    int r;

    keypairs = g_strdup(qdict_get_try_str(options, "=keyvalue-pairs"));
    if (keypairs) {
        qdict_del(options, "=keyvalue-pairs");
    }

    secretid = g_strdup(qdict_get_try_str(options, "password-secret"));
    if (secretid) {
        qdict_del(options, "password-secret");
    }

    r = qemu_rbd_convert_options(options, &opts, &local_err);
    if (local_err) {
        /* If keypairs are present, that means some options are present in
         * the modern option format.  Don't attempt to parse legacy option
         * formats, as we won't support mixed usage. */
        if (keypairs) {
            error_propagate(errp, local_err);
            goto out;
        }

        /* If the initial attempt to convert and process the options failed,
         * we may be attempting to open an image file that has the rbd options
         * specified in the older format consisting of all key/value pairs
         * encoded in the filename.  Go ahead and attempt to parse the
         * filename, and see if we can pull out the required options. */
        r = qemu_rbd_attempt_legacy_options(options, &opts, &keypairs);
        if (r < 0) {
            /* Propagate the original error, not the legacy parsing fallback
             * error, as the latter was just a best-effort attempt. */
            error_propagate(errp, local_err);
            goto out;
        }
        /* Take care whenever deciding to actually deprecate; once this ability
         * is removed, we will not be able to open any images with legacy-styled
         * backing image strings. */
        warn_report("RBD options encoded in the filename as keyvalue pairs "
                    "is deprecated");
    }

    /* Remove the processed options from the QDict (the visitor processes
     * _all_ options in the QDict) */
    while ((e = qdict_first(options))) {
        qdict_del(options, e->key);
    }

    r = qemu_rbd_connect(&s->cluster, &s->io_ctx, opts,
                         !(flags & BDRV_O_NOCACHE), keypairs, secretid, errp);
    if (r < 0) {
        goto out;
    }

    s->snap = g_strdup(opts->snapshot);
    s->image_name = g_strdup(opts->image);

    /* rbd_open is always r/w */
    r = rbd_open(s->io_ctx, s->image_name, &s->image, s->snap);
    if (r < 0) {
        error_setg_errno(errp, -r, "error reading header from %s",
                         s->image_name);
        goto failed_open;
    }

    /* If we are using an rbd snapshot, we must be r/o, otherwise
     * leave as-is */
    if (s->snap != NULL) {
        r = bdrv_apply_auto_read_only(bs, "rbd snapshots are read-only", errp);
        if (r < 0) {
            rbd_close(s->image);
            goto failed_open;
        }
    }

    r = 0;
    goto out;

failed_open:
    rados_ioctx_destroy(s->io_ctx);
    g_free(s->snap);
    g_free(s->image_name);
    rados_shutdown(s->cluster);
out:
    qapi_free_BlockdevOptionsRbd(opts);
    g_free(keypairs);
    g_free(secretid);
    return r;
}


/* Since RBD is currently always opened R/W via the API,
 * we just need to check if we are using a snapshot or not, in
 * order to determine if we will allow it to be R/W */
static int qemu_rbd_reopen_prepare(BDRVReopenState *state,
                                   BlockReopenQueue *queue, Error **errp)
{
    BDRVRBDState *s = state->bs->opaque;
    int ret = 0;

    if (s->snap && state->flags & BDRV_O_RDWR) {
        error_setg(errp,
                   "Cannot change node '%s' to r/w when using RBD snapshot",
                   bdrv_get_device_or_node_name(state->bs));
        ret = -EINVAL;
    }

    return ret;
}

static void qemu_rbd_close(BlockDriverState *bs)
{
    BDRVRBDState *s = bs->opaque;

    rbd_close(s->image);
    rados_ioctx_destroy(s->io_ctx);
    g_free(s->snap);
    g_free(s->image_name);
    rados_shutdown(s->cluster);
}

static const AIOCBInfo rbd_aiocb_info = {
    .aiocb_size = sizeof(RBDAIOCB),
};

static void rbd_finish_bh(void *opaque)
{
    RADOSCB *rcb = opaque;
    qemu_rbd_complete_aio(rcb);
}

/*
 * This is the callback function for rbd_aio_read and _write
 *
 * Note: this function is being called from a non qemu thread so
 * we need to be careful about what we do here. Generally we only
 * schedule a BH, and do the rest of the io completion handling
 * from rbd_finish_bh() which runs in a qemu context.
 */
static void rbd_finish_aiocb(rbd_completion_t c, RADOSCB *rcb)
{
    RBDAIOCB *acb = rcb->acb;

    rcb->ret = rbd_aio_get_return_value(c);
    rbd_aio_release(c);

    aio_bh_schedule_oneshot(bdrv_get_aio_context(acb->common.bs),
                            rbd_finish_bh, rcb);
}

static int rbd_aio_discard_wrapper(rbd_image_t image,
                                   uint64_t off,
                                   uint64_t len,
                                   rbd_completion_t comp)
{
#ifdef LIBRBD_SUPPORTS_DISCARD
    return rbd_aio_discard(image, off, len, comp);
#else
    return -ENOTSUP;
#endif
}

static int rbd_aio_flush_wrapper(rbd_image_t image,
                                 rbd_completion_t comp)
{
#ifdef LIBRBD_SUPPORTS_AIO_FLUSH
    return rbd_aio_flush(image, comp);
#else
    return -ENOTSUP;
#endif
}

static BlockAIOCB *rbd_start_aio(BlockDriverState *bs,
                                 int64_t off,
                                 QEMUIOVector *qiov,
                                 int64_t size,
                                 BlockCompletionFunc *cb,
                                 void *opaque,
                                 RBDAIOCmd cmd)
{
    RBDAIOCB *acb;
    RADOSCB *rcb = NULL;
    rbd_completion_t c;
    int r;

    BDRVRBDState *s = bs->opaque;

    acb = qemu_aio_get(&rbd_aiocb_info, bs, cb, opaque);
    acb->cmd = cmd;
    acb->qiov = qiov;
    assert(!qiov || qiov->size == size);

    rcb = g_new(RADOSCB, 1);

    if (!LIBRBD_USE_IOVEC) {
        if (cmd == RBD_AIO_DISCARD || cmd == RBD_AIO_FLUSH) {
            acb->bounce = NULL;
        } else {
            acb->bounce = qemu_try_blockalign(bs, qiov->size);
            if (acb->bounce == NULL) {
                goto failed;
            }
        }
        if (cmd == RBD_AIO_WRITE) {
            qemu_iovec_to_buf(acb->qiov, 0, acb->bounce, qiov->size);
        }
        rcb->buf = acb->bounce;
    }

    acb->ret = 0;
    acb->error = 0;
    acb->s = s;

    rcb->acb = acb;
    rcb->s = acb->s;
    rcb->size = size;
    r = rbd_aio_create_completion(rcb, (rbd_callback_t) rbd_finish_aiocb, &c);
    if (r < 0) {
        goto failed;
    }

    switch (cmd) {
    case RBD_AIO_WRITE:
#ifdef LIBRBD_SUPPORTS_IOVEC
            r = rbd_aio_writev(s->image, qiov->iov, qiov->niov, off, c);
#else
            r = rbd_aio_write(s->image, off, size, rcb->buf, c);
#endif
        break;
    case RBD_AIO_READ:
#ifdef LIBRBD_SUPPORTS_IOVEC
            r = rbd_aio_readv(s->image, qiov->iov, qiov->niov, off, c);
#else
            r = rbd_aio_read(s->image, off, size, rcb->buf, c);
#endif
        break;
    case RBD_AIO_DISCARD:
        r = rbd_aio_discard_wrapper(s->image, off, size, c);
        break;
    case RBD_AIO_FLUSH:
        r = rbd_aio_flush_wrapper(s->image, c);
        break;
    default:
        r = -EINVAL;
    }

    if (r < 0) {
        goto failed_completion;
    }
    return &acb->common;

failed_completion:
    rbd_aio_release(c);
failed:
    g_free(rcb);
    if (!LIBRBD_USE_IOVEC) {
        qemu_vfree(acb->bounce);
    }

    qemu_aio_unref(acb);
    return NULL;
}

static BlockAIOCB *qemu_rbd_aio_preadv(BlockDriverState *bs,
                                       uint64_t offset, uint64_t bytes,
                                       QEMUIOVector *qiov, int flags,
                                       BlockCompletionFunc *cb,
                                       void *opaque)
{
    return rbd_start_aio(bs, offset, qiov, bytes, cb, opaque,
                         RBD_AIO_READ);
}

static BlockAIOCB *qemu_rbd_aio_pwritev(BlockDriverState *bs,
                                        uint64_t offset, uint64_t bytes,
                                        QEMUIOVector *qiov, int flags,
                                        BlockCompletionFunc *cb,
                                        void *opaque)
{
    return rbd_start_aio(bs, offset, qiov, bytes, cb, opaque,
                         RBD_AIO_WRITE);
}

#ifdef LIBRBD_SUPPORTS_AIO_FLUSH
static BlockAIOCB *qemu_rbd_aio_flush(BlockDriverState *bs,
                                      BlockCompletionFunc *cb,
                                      void *opaque)
{
    return rbd_start_aio(bs, 0, NULL, 0, cb, opaque, RBD_AIO_FLUSH);
}

#else

static int qemu_rbd_co_flush(BlockDriverState *bs)
{
#if LIBRBD_VERSION_CODE >= LIBRBD_VERSION(0, 1, 1)
    /* rbd_flush added in 0.1.1 */
    BDRVRBDState *s = bs->opaque;
    return rbd_flush(s->image);
#else
    return 0;
#endif
}
#endif

static int qemu_rbd_getinfo(BlockDriverState *bs, BlockDriverInfo *bdi)
{
    BDRVRBDState *s = bs->opaque;
    rbd_image_info_t info;
    int r;

    r = rbd_stat(s->image, &info, sizeof(info));
    if (r < 0) {
        return r;
    }

    bdi->cluster_size = info.obj_size;
    return 0;
}

static int64_t qemu_rbd_getlength(BlockDriverState *bs)
{
    BDRVRBDState *s = bs->opaque;
    rbd_image_info_t info;
    int r;

    r = rbd_stat(s->image, &info, sizeof(info));
    if (r < 0) {
        return r;
    }

    return info.size;
}

static int coroutine_fn qemu_rbd_co_truncate(BlockDriverState *bs,
                                             int64_t offset,
                                             PreallocMode prealloc,
                                             Error **errp)
{
    BDRVRBDState *s = bs->opaque;
    int r;

    if (prealloc != PREALLOC_MODE_OFF) {
        error_setg(errp, "Unsupported preallocation mode '%s'",
                   PreallocMode_str(prealloc));
        return -ENOTSUP;
    }

    r = rbd_resize(s->image, offset);
    if (r < 0) {
        error_setg_errno(errp, -r, "Failed to resize file");
        return r;
    }

    return 0;
}

static int qemu_rbd_snap_create(BlockDriverState *bs,
                                QEMUSnapshotInfo *sn_info)
{
    BDRVRBDState *s = bs->opaque;
    int r;

    if (sn_info->name[0] == '\0') {
        return -EINVAL; /* we need a name for rbd snapshots */
    }

    /*
     * rbd snapshots are using the name as the user controlled unique identifier
     * we can't use the rbd snapid for that purpose, as it can't be set
     */
    if (sn_info->id_str[0] != '\0' &&
        strcmp(sn_info->id_str, sn_info->name) != 0) {
        return -EINVAL;
    }

    if (strlen(sn_info->name) >= sizeof(sn_info->id_str)) {
        return -ERANGE;
    }

    r = rbd_snap_create(s->image, sn_info->name);
    if (r < 0) {
        error_report("failed to create snap: %s", strerror(-r));
        return r;
    }

    return 0;
}

static int qemu_rbd_snap_remove(BlockDriverState *bs,
                                const char *snapshot_id,
                                const char *snapshot_name,
                                Error **errp)
{
    BDRVRBDState *s = bs->opaque;
    int r;

    if (!snapshot_name) {
        error_setg(errp, "rbd need a valid snapshot name");
        return -EINVAL;
    }

    /* If snapshot_id is specified, it must be equal to name, see
       qemu_rbd_snap_list() */
    if (snapshot_id && strcmp(snapshot_id, snapshot_name)) {
        error_setg(errp,
                   "rbd do not support snapshot id, it should be NULL or "
                   "equal to snapshot name");
        return -EINVAL;
    }

    r = rbd_snap_remove(s->image, snapshot_name);
    if (r < 0) {
        error_setg_errno(errp, -r, "Failed to remove the snapshot");
    }
    return r;
}

static int qemu_rbd_snap_rollback(BlockDriverState *bs,
                                  const char *snapshot_name)
{
    BDRVRBDState *s = bs->opaque;

    return rbd_snap_rollback(s->image, snapshot_name);
}

static int qemu_rbd_snap_list(BlockDriverState *bs,
                              QEMUSnapshotInfo **psn_tab)
{
    BDRVRBDState *s = bs->opaque;
    QEMUSnapshotInfo *sn_info, *sn_tab = NULL;
    int i, snap_count;
    rbd_snap_info_t *snaps;
    int max_snaps = RBD_MAX_SNAPS;

    do {
        snaps = g_new(rbd_snap_info_t, max_snaps);
        snap_count = rbd_snap_list(s->image, snaps, &max_snaps);
        if (snap_count <= 0) {
            g_free(snaps);
        }
    } while (snap_count == -ERANGE);

    if (snap_count <= 0) {
        goto done;
    }

    sn_tab = g_new0(QEMUSnapshotInfo, snap_count);

    for (i = 0; i < snap_count; i++) {
        const char *snap_name = snaps[i].name;

        sn_info = sn_tab + i;
        pstrcpy(sn_info->id_str, sizeof(sn_info->id_str), snap_name);
        pstrcpy(sn_info->name, sizeof(sn_info->name), snap_name);

        sn_info->vm_state_size = snaps[i].size;
        sn_info->date_sec = 0;
        sn_info->date_nsec = 0;
        sn_info->vm_clock_nsec = 0;
    }
    rbd_snap_list_end(snaps);
    g_free(snaps);

 done:
    *psn_tab = sn_tab;
    return snap_count;
}

#ifdef LIBRBD_SUPPORTS_DISCARD
static BlockAIOCB *qemu_rbd_aio_pdiscard(BlockDriverState *bs,
                                         int64_t offset,
                                         int bytes,
                                         BlockCompletionFunc *cb,
                                         void *opaque)
{
    return rbd_start_aio(bs, offset, NULL, bytes, cb, opaque,
                         RBD_AIO_DISCARD);
}
#endif

#ifdef LIBRBD_SUPPORTS_INVALIDATE
static void coroutine_fn qemu_rbd_co_invalidate_cache(BlockDriverState *bs,
                                                      Error **errp)
{
    BDRVRBDState *s = bs->opaque;
    int r = rbd_invalidate_cache(s->image);
    if (r < 0) {
        error_setg_errno(errp, -r, "Failed to invalidate the cache");
    }
}
#endif

static QemuOptsList qemu_rbd_create_opts = {
    .name = "rbd-create-opts",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_rbd_create_opts.head),
    .desc = {
        {
            .name = BLOCK_OPT_SIZE,
            .type = QEMU_OPT_SIZE,
            .help = "Virtual disk size"
        },
        {
            .name = BLOCK_OPT_CLUSTER_SIZE,
            .type = QEMU_OPT_SIZE,
            .help = "RBD object size"
        },
        {
            .name = "password-secret",
            .type = QEMU_OPT_STRING,
            .help = "ID of secret providing the password",
        },
        { /* end of list */ }
    }
};

static BlockDriver bdrv_rbd = {
    .format_name            = "rbd",
    .instance_size          = sizeof(BDRVRBDState),
    .bdrv_parse_filename    = qemu_rbd_parse_filename,
    .bdrv_refresh_limits    = qemu_rbd_refresh_limits,
    .bdrv_file_open         = qemu_rbd_open,
    .bdrv_close             = qemu_rbd_close,
    .bdrv_reopen_prepare    = qemu_rbd_reopen_prepare,
    .bdrv_co_create         = qemu_rbd_co_create,
    .bdrv_co_create_opts    = qemu_rbd_co_create_opts,
    .bdrv_has_zero_init     = bdrv_has_zero_init_1,
    .bdrv_get_info          = qemu_rbd_getinfo,
    .create_opts            = &qemu_rbd_create_opts,
    .bdrv_getlength         = qemu_rbd_getlength,
    .bdrv_co_truncate       = qemu_rbd_co_truncate,
    .protocol_name          = "rbd",

    .bdrv_aio_preadv        = qemu_rbd_aio_preadv,
    .bdrv_aio_pwritev       = qemu_rbd_aio_pwritev,

#ifdef LIBRBD_SUPPORTS_AIO_FLUSH
    .bdrv_aio_flush         = qemu_rbd_aio_flush,
#else
    .bdrv_co_flush_to_disk  = qemu_rbd_co_flush,
#endif

#ifdef LIBRBD_SUPPORTS_DISCARD
    .bdrv_aio_pdiscard      = qemu_rbd_aio_pdiscard,
#endif

    .bdrv_snapshot_create   = qemu_rbd_snap_create,
    .bdrv_snapshot_delete   = qemu_rbd_snap_remove,
    .bdrv_snapshot_list     = qemu_rbd_snap_list,
    .bdrv_snapshot_goto     = qemu_rbd_snap_rollback,
#ifdef LIBRBD_SUPPORTS_INVALIDATE
    .bdrv_co_invalidate_cache = qemu_rbd_co_invalidate_cache,
#endif
};

static void bdrv_rbd_init(void)
{
    bdrv_register(&bdrv_rbd);
}

block_init(bdrv_rbd_init);

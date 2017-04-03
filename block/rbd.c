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
#include "block/block_int.h"
#include "crypto/secret.h"
#include "qemu/cutils.h"
#include "qapi/qmp/qstring.h"
#include "qapi/qmp/qjson.h"

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
    char *name;
    char *snap;
} BDRVRBDState;

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
    qdict_put(options, "pool", qstring_from_str(found_str));

    if (strchr(p, '@')) {
        found_str = qemu_rbd_next_tok(p, '@', &p);
        qemu_rbd_unescape(found_str);
        qdict_put(options, "image", qstring_from_str(found_str));

        found_str = qemu_rbd_next_tok(p, ':', &p);
        qemu_rbd_unescape(found_str);
        qdict_put(options, "snapshot", qstring_from_str(found_str));
    } else {
        found_str = qemu_rbd_next_tok(p, ':', &p);
        qemu_rbd_unescape(found_str);
        qdict_put(options, "image", qstring_from_str(found_str));
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
            qdict_put(options, "conf", qstring_from_str(value));
        } else if (!strcmp(name, "id")) {
            qdict_put(options, "user" , qstring_from_str(value));
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
            qlist_append(keypairs, qstring_from_str(name));
            qlist_append(keypairs, qstring_from_str(value));
        }
    }

    if (keypairs) {
        qdict_put(options, "=keyvalue-pairs",
                  qobject_to_json(QOBJECT(keypairs)));
    }

done:
    g_free(buf);
    QDECREF(keypairs);
    return;
}


static int qemu_rbd_set_auth(rados_t cluster, const char *secretid,
                             Error **errp)
{
    if (secretid == 0) {
        return 0;
    }

    gchar *secret = qcrypto_secret_lookup_as_base64(secretid,
                                                    errp);
    if (!secret) {
        return -1;
    }

    rados_conf_set(cluster, "key", secret);
    g_free(secret);

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
    keypairs = qobject_to_qlist(qobject_from_json(keypairs_json,
                                                  &error_abort));
    remaining = qlist_size(keypairs) / 2;
    assert(remaining);

    while (remaining--) {
        name = qobject_to_qstring(qlist_pop(keypairs));
        value = qobject_to_qstring(qlist_pop(keypairs));
        assert(name && value);
        key = qstring_get_str(name);

        ret = rados_conf_set(cluster, key, qstring_get_str(value));
        QDECREF(name);
        QDECREF(value);
        if (ret < 0) {
            error_setg_errno(errp, -ret, "invalid conf option %s", key);
            ret = -EINVAL;
            break;
        }
    }

    QDECREF(keypairs);
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
        {
            .name = "password-secret",
            .type = QEMU_OPT_STRING,
            .help = "ID of secret providing the password",
        },

        /*
         * Keys for qemu_rbd_parse_filename(), not in the QAPI schema
         */
        {
            /*
             * HACK: name starts with '=' so that qemu_opts_parse()
             * can't set it
             */
            .name = "=keyvalue-pairs",
            .type = QEMU_OPT_STRING,
            .help = "Legacy rados key/value option parameters",
        },
        { /* end of list */ }
    },
};

static int qemu_rbd_create(const char *filename, QemuOpts *opts, Error **errp)
{
    Error *local_err = NULL;
    int64_t bytes = 0;
    int64_t objsize;
    int obj_order = 0;
    const char *pool, *name, *conf, *clientname, *keypairs;
    const char *secretid;
    rados_t cluster;
    rados_ioctx_t io_ctx;
    QDict *options = NULL;
    int ret = 0;

    secretid = qemu_opt_get(opts, "password-secret");

    /* Read out options */
    bytes = ROUND_UP(qemu_opt_get_size_del(opts, BLOCK_OPT_SIZE, 0),
                     BDRV_SECTOR_SIZE);
    objsize = qemu_opt_get_size_del(opts, BLOCK_OPT_CLUSTER_SIZE, 0);
    if (objsize) {
        if ((objsize - 1) & objsize) {    /* not a power of 2? */
            error_setg(errp, "obj size needs to be power of 2");
            ret = -EINVAL;
            goto exit;
        }
        if (objsize < 4096) {
            error_setg(errp, "obj size too small");
            ret = -EINVAL;
            goto exit;
        }
        obj_order = ctz32(objsize);
    }

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
    pool       = qdict_get_try_str(options, "pool");
    conf       = qdict_get_try_str(options, "conf");
    clientname = qdict_get_try_str(options, "user");
    name       = qdict_get_try_str(options, "image");
    keypairs   = qdict_get_try_str(options, "=keyvalue-pairs");

    ret = rados_create(&cluster, clientname);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "error initializing");
        goto exit;
    }

    /* try default location when conf=NULL, but ignore failure */
    ret = rados_conf_read_file(cluster, conf);
    if (conf && ret < 0) {
        error_setg_errno(errp, -ret, "error reading conf file %s", conf);
        ret = -EIO;
        goto shutdown;
    }

    ret = qemu_rbd_set_keypairs(cluster, keypairs, errp);
    if (ret < 0) {
        ret = -EIO;
        goto shutdown;
    }

    if (qemu_rbd_set_auth(cluster, secretid, errp) < 0) {
        ret = -EIO;
        goto shutdown;
    }

    ret = rados_connect(cluster);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "error connecting");
        goto shutdown;
    }

    ret = rados_ioctx_create(cluster, pool, &io_ctx);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "error opening pool %s", pool);
        goto shutdown;
    }

    ret = rbd_create(io_ctx, name, bytes, &obj_order);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "error rbd create");
    }

    rados_ioctx_destroy(io_ctx);

shutdown:
    rados_shutdown(cluster);

exit:
    QDECREF(options);
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

static char *qemu_rbd_mon_host(QDict *options, Error **errp)
{
    const char **vals = g_new(const char *, qdict_size(options) + 1);
    char keybuf[32];
    const char *host, *port;
    char *rados_str;
    int i;

    for (i = 0;; i++) {
        sprintf(keybuf, "server.%d.host", i);
        host = qdict_get_try_str(options, keybuf);
        qdict_del(options, keybuf);
        sprintf(keybuf, "server.%d.port", i);
        port = qdict_get_try_str(options, keybuf);
        qdict_del(options, keybuf);
        if (!host && !port) {
            break;
        }
        if (!host) {
            error_setg(errp, "Parameter server.%d.host is missing", i);
            rados_str = NULL;
            goto out;
        }

        if (strchr(host, ':')) {
            vals[i] = port ? g_strdup_printf("[%s]:%s", host, port)
                : g_strdup_printf("[%s]", host);
        } else {
            vals[i] = port ? g_strdup_printf("%s:%s", host, port)
                : g_strdup(host);
        }
    }
    vals[i] = NULL;

    rados_str = i ? g_strjoinv(";", (char **)vals) : NULL;
out:
    g_strfreev((char **)vals);
    return rados_str;
}

static int qemu_rbd_open(BlockDriverState *bs, QDict *options, int flags,
                         Error **errp)
{
    BDRVRBDState *s = bs->opaque;
    const char *pool, *snap, *conf, *clientname, *name, *keypairs;
    const char *secretid;
    QemuOpts *opts;
    Error *local_err = NULL;
    char *mon_host = NULL;
    int r;

    opts = qemu_opts_create(&runtime_opts, NULL, 0, &error_abort);
    qemu_opts_absorb_qdict(opts, options, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        r = -EINVAL;
        goto failed_opts;
    }

    mon_host = qemu_rbd_mon_host(options, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        r = -EINVAL;
        goto failed_opts;
    }

    secretid = qemu_opt_get(opts, "password-secret");

    pool           = qemu_opt_get(opts, "pool");
    conf           = qemu_opt_get(opts, "conf");
    snap           = qemu_opt_get(opts, "snapshot");
    clientname     = qemu_opt_get(opts, "user");
    name           = qemu_opt_get(opts, "image");
    keypairs       = qemu_opt_get(opts, "=keyvalue-pairs");

    if (!pool || !name) {
        error_setg(errp, "Parameters 'pool' and 'image' are required");
        r = -EINVAL;
        goto failed_opts;
    }

    r = rados_create(&s->cluster, clientname);
    if (r < 0) {
        error_setg_errno(errp, -r, "error initializing");
        goto failed_opts;
    }

    s->snap = g_strdup(snap);
    s->name = g_strdup(name);

    /* try default location when conf=NULL, but ignore failure */
    r = rados_conf_read_file(s->cluster, conf);
    if (conf && r < 0) {
        error_setg_errno(errp, -r, "error reading conf file %s", conf);
        goto failed_shutdown;
    }

    r = qemu_rbd_set_keypairs(s->cluster, keypairs, errp);
    if (r < 0) {
        goto failed_shutdown;
    }

    if (mon_host) {
        r = rados_conf_set(s->cluster, "mon_host", mon_host);
        if (r < 0) {
            goto failed_shutdown;
        }
    }

    if (qemu_rbd_set_auth(s->cluster, secretid, errp) < 0) {
        r = -EIO;
        goto failed_shutdown;
    }

    /*
     * Fallback to more conservative semantics if setting cache
     * options fails. Ignore errors from setting rbd_cache because the
     * only possible error is that the option does not exist, and
     * librbd defaults to no caching. If write through caching cannot
     * be set up, fall back to no caching.
     */
    if (flags & BDRV_O_NOCACHE) {
        rados_conf_set(s->cluster, "rbd_cache", "false");
    } else {
        rados_conf_set(s->cluster, "rbd_cache", "true");
    }

    r = rados_connect(s->cluster);
    if (r < 0) {
        error_setg_errno(errp, -r, "error connecting");
        goto failed_shutdown;
    }

    r = rados_ioctx_create(s->cluster, pool, &s->io_ctx);
    if (r < 0) {
        error_setg_errno(errp, -r, "error opening pool %s", pool);
        goto failed_shutdown;
    }

    r = rbd_open(s->io_ctx, s->name, &s->image, s->snap);
    if (r < 0) {
        error_setg_errno(errp, -r, "error reading header from %s", s->name);
        goto failed_open;
    }

    bs->read_only = (s->snap != NULL);

    qemu_opts_del(opts);
    return 0;

failed_open:
    rados_ioctx_destroy(s->io_ctx);
failed_shutdown:
    rados_shutdown(s->cluster);
    g_free(s->snap);
    g_free(s->name);
failed_opts:
    qemu_opts_del(opts);
    g_free(mon_host);
    return r;
}

static void qemu_rbd_close(BlockDriverState *bs)
{
    BDRVRBDState *s = bs->opaque;

    rbd_close(s->image);
    rados_ioctx_destroy(s->io_ctx);
    g_free(s->snap);
    g_free(s->name);
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

static BlockAIOCB *qemu_rbd_aio_readv(BlockDriverState *bs,
                                      int64_t sector_num,
                                      QEMUIOVector *qiov,
                                      int nb_sectors,
                                      BlockCompletionFunc *cb,
                                      void *opaque)
{
    return rbd_start_aio(bs, sector_num << BDRV_SECTOR_BITS, qiov,
                         (int64_t) nb_sectors << BDRV_SECTOR_BITS, cb, opaque,
                         RBD_AIO_READ);
}

static BlockAIOCB *qemu_rbd_aio_writev(BlockDriverState *bs,
                                       int64_t sector_num,
                                       QEMUIOVector *qiov,
                                       int nb_sectors,
                                       BlockCompletionFunc *cb,
                                       void *opaque)
{
    return rbd_start_aio(bs, sector_num << BDRV_SECTOR_BITS, qiov,
                         (int64_t) nb_sectors << BDRV_SECTOR_BITS, cb, opaque,
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

static int qemu_rbd_truncate(BlockDriverState *bs, int64_t offset)
{
    BDRVRBDState *s = bs->opaque;
    int r;

    r = rbd_resize(s->image, offset);
    if (r < 0) {
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
                                         int count,
                                         BlockCompletionFunc *cb,
                                         void *opaque)
{
    return rbd_start_aio(bs, offset, NULL, count, cb, opaque,
                         RBD_AIO_DISCARD);
}
#endif

#ifdef LIBRBD_SUPPORTS_INVALIDATE
static void qemu_rbd_invalidate_cache(BlockDriverState *bs,
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
    .bdrv_file_open         = qemu_rbd_open,
    .bdrv_close             = qemu_rbd_close,
    .bdrv_create            = qemu_rbd_create,
    .bdrv_has_zero_init     = bdrv_has_zero_init_1,
    .bdrv_get_info          = qemu_rbd_getinfo,
    .create_opts            = &qemu_rbd_create_opts,
    .bdrv_getlength         = qemu_rbd_getlength,
    .bdrv_truncate          = qemu_rbd_truncate,
    .protocol_name          = "rbd",

    .bdrv_aio_readv         = qemu_rbd_aio_readv,
    .bdrv_aio_writev        = qemu_rbd_aio_writev,

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
    .bdrv_invalidate_cache  = qemu_rbd_invalidate_cache,
#endif
};

static void bdrv_rbd_init(void)
{
    bdrv_register(&bdrv_rbd);
}

block_init(bdrv_rbd_init);

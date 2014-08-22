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

#include <inttypes.h>

#include "qemu-common.h"
#include "qemu/error-report.h"
#include "block/block_int.h"

#include <rbd/librbd.h>

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

#define RBD_MAX_CONF_NAME_SIZE 128
#define RBD_MAX_CONF_VAL_SIZE 512
#define RBD_MAX_CONF_SIZE 1024
#define RBD_MAX_POOL_NAME_SIZE 128
#define RBD_MAX_SNAP_NAME_SIZE 128
#define RBD_MAX_SNAPS 100

typedef enum {
    RBD_AIO_READ,
    RBD_AIO_WRITE,
    RBD_AIO_DISCARD,
    RBD_AIO_FLUSH
} RBDAIOCmd;

typedef struct RBDAIOCB {
    BlockDriverAIOCB common;
    QEMUBH *bh;
    int64_t ret;
    QEMUIOVector *qiov;
    char *bounce;
    RBDAIOCmd cmd;
    int64_t sector_num;
    int error;
    struct BDRVRBDState *s;
    int cancelled;
    int status;
} RBDAIOCB;

typedef struct RADOSCB {
    int rcbid;
    RBDAIOCB *acb;
    struct BDRVRBDState *s;
    int done;
    int64_t size;
    char *buf;
    int64_t ret;
} RADOSCB;

#define RBD_FD_READ 0
#define RBD_FD_WRITE 1

typedef struct BDRVRBDState {
    rados_t cluster;
    rados_ioctx_t io_ctx;
    rbd_image_t image;
    char name[RBD_MAX_IMAGE_NAME_SIZE];
    char *snap;
} BDRVRBDState;

static int qemu_rbd_next_tok(char *dst, int dst_len,
                             char *src, char delim,
                             const char *name,
                             char **p, Error **errp)
{
    int l;
    char *end;

    *p = NULL;

    if (delim != '\0') {
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
    }
    l = strlen(src);
    if (l >= dst_len) {
        error_setg(errp, "%s too long", name);
        return -EINVAL;
    } else if (l == 0) {
        error_setg(errp, "%s too short", name);
        return -EINVAL;
    }

    pstrcpy(dst, dst_len, src);

    return 0;
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

static int qemu_rbd_parsename(const char *filename,
                              char *pool, int pool_len,
                              char *snap, int snap_len,
                              char *name, int name_len,
                              char *conf, int conf_len,
                              Error **errp)
{
    const char *start;
    char *p, *buf;
    int ret;

    if (!strstart(filename, "rbd:", &start)) {
        error_setg(errp, "File name must start with 'rbd:'");
        return -EINVAL;
    }

    buf = g_strdup(start);
    p = buf;
    *snap = '\0';
    *conf = '\0';

    ret = qemu_rbd_next_tok(pool, pool_len, p,
                            '/', "pool name", &p, errp);
    if (ret < 0 || !p) {
        ret = -EINVAL;
        goto done;
    }
    qemu_rbd_unescape(pool);

    if (strchr(p, '@')) {
        ret = qemu_rbd_next_tok(name, name_len, p,
                                '@', "object name", &p, errp);
        if (ret < 0) {
            goto done;
        }
        ret = qemu_rbd_next_tok(snap, snap_len, p,
                                ':', "snap name", &p, errp);
        qemu_rbd_unescape(snap);
    } else {
        ret = qemu_rbd_next_tok(name, name_len, p,
                                ':', "object name", &p, errp);
    }
    qemu_rbd_unescape(name);
    if (ret < 0 || !p) {
        goto done;
    }

    ret = qemu_rbd_next_tok(conf, conf_len, p,
                            '\0', "configuration", &p, errp);

done:
    g_free(buf);
    return ret;
}

static char *qemu_rbd_parse_clientname(const char *conf, char *clientname)
{
    const char *p = conf;

    while (*p) {
        int len;
        const char *end = strchr(p, ':');

        if (end) {
            len = end - p;
        } else {
            len = strlen(p);
        }

        if (strncmp(p, "id=", 3) == 0) {
            len -= 3;
            strncpy(clientname, p + 3, len);
            clientname[len] = '\0';
            return clientname;
        }
        if (end == NULL) {
            break;
        }
        p = end + 1;
    }
    return NULL;
}

static int qemu_rbd_set_conf(rados_t cluster, const char *conf, Error **errp)
{
    char *p, *buf;
    char name[RBD_MAX_CONF_NAME_SIZE];
    char value[RBD_MAX_CONF_VAL_SIZE];
    int ret = 0;

    buf = g_strdup(conf);
    p = buf;

    while (p) {
        ret = qemu_rbd_next_tok(name, sizeof(name), p,
                                '=', "conf option name", &p, errp);
        if (ret < 0) {
            break;
        }
        qemu_rbd_unescape(name);

        if (!p) {
            error_setg(errp, "conf option %s has no value", name);
            ret = -EINVAL;
            break;
        }

        ret = qemu_rbd_next_tok(value, sizeof(value), p,
                                ':', "conf option value", &p, errp);
        if (ret < 0) {
            break;
        }
        qemu_rbd_unescape(value);

        if (strcmp(name, "conf") == 0) {
            ret = rados_conf_read_file(cluster, value);
            if (ret < 0) {
                error_setg(errp, "error reading conf file %s", value);
                break;
            }
        } else if (strcmp(name, "id") == 0) {
            /* ignore, this is parsed by qemu_rbd_parse_clientname() */
        } else {
            ret = rados_conf_set(cluster, name, value);
            if (ret < 0) {
                error_setg(errp, "invalid conf option %s", name);
                ret = -EINVAL;
                break;
            }
        }
    }

    g_free(buf);
    return ret;
}

static int qemu_rbd_create(const char *filename, QemuOpts *opts, Error **errp)
{
    Error *local_err = NULL;
    int64_t bytes = 0;
    int64_t objsize;
    int obj_order = 0;
    char pool[RBD_MAX_POOL_NAME_SIZE];
    char name[RBD_MAX_IMAGE_NAME_SIZE];
    char snap_buf[RBD_MAX_SNAP_NAME_SIZE];
    char conf[RBD_MAX_CONF_SIZE];
    char clientname_buf[RBD_MAX_CONF_SIZE];
    char *clientname;
    rados_t cluster;
    rados_ioctx_t io_ctx;
    int ret;

    if (qemu_rbd_parsename(filename, pool, sizeof(pool),
                           snap_buf, sizeof(snap_buf),
                           name, sizeof(name),
                           conf, sizeof(conf), &local_err) < 0) {
        error_propagate(errp, local_err);
        return -EINVAL;
    }

    /* Read out options */
    bytes = qemu_opt_get_size_del(opts, BLOCK_OPT_SIZE, 0);
    objsize = qemu_opt_get_size_del(opts, BLOCK_OPT_CLUSTER_SIZE, 0);
    if (objsize) {
        if ((objsize - 1) & objsize) {    /* not a power of 2? */
            error_setg(errp, "obj size needs to be power of 2");
            return -EINVAL;
        }
        if (objsize < 4096) {
            error_setg(errp, "obj size too small");
            return -EINVAL;
        }
        obj_order = ffs(objsize) - 1;
    }

    clientname = qemu_rbd_parse_clientname(conf, clientname_buf);
    if (rados_create(&cluster, clientname) < 0) {
        error_setg(errp, "error initializing");
        return -EIO;
    }

    if (strstr(conf, "conf=") == NULL) {
        /* try default location, but ignore failure */
        rados_conf_read_file(cluster, NULL);
    }

    if (conf[0] != '\0' &&
        qemu_rbd_set_conf(cluster, conf, &local_err) < 0) {
        rados_shutdown(cluster);
        error_propagate(errp, local_err);
        return -EIO;
    }

    if (rados_connect(cluster) < 0) {
        error_setg(errp, "error connecting");
        rados_shutdown(cluster);
        return -EIO;
    }

    if (rados_ioctx_create(cluster, pool, &io_ctx) < 0) {
        error_setg(errp, "error opening pool %s", pool);
        rados_shutdown(cluster);
        return -EIO;
    }

    ret = rbd_create(io_ctx, name, bytes, &obj_order);
    rados_ioctx_destroy(io_ctx);
    rados_shutdown(cluster);

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
            memset(rcb->buf, 0, rcb->size);
            acb->ret = r;
            acb->error = 1;
        } else if (r < rcb->size) {
            memset(rcb->buf + r, 0, rcb->size - r);
            if (!acb->error) {
                acb->ret = rcb->size;
            }
        } else if (!acb->error) {
            acb->ret = r;
        }
    }

    g_free(rcb);

    if (acb->cmd == RBD_AIO_READ) {
        qemu_iovec_from_buf(acb->qiov, 0, acb->bounce, acb->qiov->size);
    }
    qemu_vfree(acb->bounce);
    acb->common.cb(acb->common.opaque, (acb->ret > 0 ? 0 : acb->ret));
    acb->status = 0;

    if (!acb->cancelled) {
        qemu_aio_release(acb);
    }
}

/* TODO Convert to fine grained options */
static QemuOptsList runtime_opts = {
    .name = "rbd",
    .head = QTAILQ_HEAD_INITIALIZER(runtime_opts.head),
    .desc = {
        {
            .name = "filename",
            .type = QEMU_OPT_STRING,
            .help = "Specification of the rbd image",
        },
        { /* end of list */ }
    },
};

static int qemu_rbd_open(BlockDriverState *bs, QDict *options, int flags,
                         Error **errp)
{
    BDRVRBDState *s = bs->opaque;
    char pool[RBD_MAX_POOL_NAME_SIZE];
    char snap_buf[RBD_MAX_SNAP_NAME_SIZE];
    char conf[RBD_MAX_CONF_SIZE];
    char clientname_buf[RBD_MAX_CONF_SIZE];
    char *clientname;
    QemuOpts *opts;
    Error *local_err = NULL;
    const char *filename;
    int r;

    opts = qemu_opts_create(&runtime_opts, NULL, 0, &error_abort);
    qemu_opts_absorb_qdict(opts, options, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        qemu_opts_del(opts);
        return -EINVAL;
    }

    filename = qemu_opt_get(opts, "filename");

    if (qemu_rbd_parsename(filename, pool, sizeof(pool),
                           snap_buf, sizeof(snap_buf),
                           s->name, sizeof(s->name),
                           conf, sizeof(conf), errp) < 0) {
        r = -EINVAL;
        goto failed_opts;
    }

    clientname = qemu_rbd_parse_clientname(conf, clientname_buf);
    r = rados_create(&s->cluster, clientname);
    if (r < 0) {
        error_setg(&local_err, "error initializing");
        goto failed_opts;
    }

    s->snap = NULL;
    if (snap_buf[0] != '\0') {
        s->snap = g_strdup(snap_buf);
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

    if (strstr(conf, "conf=") == NULL) {
        /* try default location, but ignore failure */
        rados_conf_read_file(s->cluster, NULL);
    }

    if (conf[0] != '\0') {
        r = qemu_rbd_set_conf(s->cluster, conf, errp);
        if (r < 0) {
            goto failed_shutdown;
        }
    }

    r = rados_connect(s->cluster);
    if (r < 0) {
        error_setg(&local_err, "error connecting");
        goto failed_shutdown;
    }

    r = rados_ioctx_create(s->cluster, pool, &s->io_ctx);
    if (r < 0) {
        error_setg(&local_err, "error opening pool %s", pool);
        goto failed_shutdown;
    }

    r = rbd_open(s->io_ctx, s->name, &s->image, s->snap);
    if (r < 0) {
        error_setg(&local_err, "error reading header from %s", s->name);
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
failed_opts:
    qemu_opts_del(opts);
    return r;
}

static void qemu_rbd_close(BlockDriverState *bs)
{
    BDRVRBDState *s = bs->opaque;

    rbd_close(s->image);
    rados_ioctx_destroy(s->io_ctx);
    g_free(s->snap);
    rados_shutdown(s->cluster);
}

/*
 * Cancel aio. Since we don't reference acb in a non qemu threads,
 * it is safe to access it here.
 */
static void qemu_rbd_aio_cancel(BlockDriverAIOCB *blockacb)
{
    RBDAIOCB *acb = (RBDAIOCB *) blockacb;
    acb->cancelled = 1;

    while (acb->status == -EINPROGRESS) {
        aio_poll(bdrv_get_aio_context(acb->common.bs), true);
    }

    qemu_aio_release(acb);
}

static const AIOCBInfo rbd_aiocb_info = {
    .aiocb_size = sizeof(RBDAIOCB),
    .cancel = qemu_rbd_aio_cancel,
};

static void rbd_finish_bh(void *opaque)
{
    RADOSCB *rcb = opaque;
    qemu_bh_delete(rcb->acb->bh);
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

    acb->bh = aio_bh_new(bdrv_get_aio_context(acb->common.bs),
                         rbd_finish_bh, rcb);
    qemu_bh_schedule(acb->bh);
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

static BlockDriverAIOCB *rbd_start_aio(BlockDriverState *bs,
                                       int64_t sector_num,
                                       QEMUIOVector *qiov,
                                       int nb_sectors,
                                       BlockDriverCompletionFunc *cb,
                                       void *opaque,
                                       RBDAIOCmd cmd)
{
    RBDAIOCB *acb;
    RADOSCB *rcb = NULL;
    rbd_completion_t c;
    int64_t off, size;
    char *buf;
    int r;

    BDRVRBDState *s = bs->opaque;

    acb = qemu_aio_get(&rbd_aiocb_info, bs, cb, opaque);
    acb->cmd = cmd;
    acb->qiov = qiov;
    if (cmd == RBD_AIO_DISCARD || cmd == RBD_AIO_FLUSH) {
        acb->bounce = NULL;
    } else {
        acb->bounce = qemu_try_blockalign(bs, qiov->size);
        if (acb->bounce == NULL) {
            goto failed;
        }
    }
    acb->ret = 0;
    acb->error = 0;
    acb->s = s;
    acb->cancelled = 0;
    acb->bh = NULL;
    acb->status = -EINPROGRESS;

    if (cmd == RBD_AIO_WRITE) {
        qemu_iovec_to_buf(acb->qiov, 0, acb->bounce, qiov->size);
    }

    buf = acb->bounce;

    off = sector_num * BDRV_SECTOR_SIZE;
    size = nb_sectors * BDRV_SECTOR_SIZE;

    rcb = g_new(RADOSCB, 1);
    rcb->done = 0;
    rcb->acb = acb;
    rcb->buf = buf;
    rcb->s = acb->s;
    rcb->size = size;
    r = rbd_aio_create_completion(rcb, (rbd_callback_t) rbd_finish_aiocb, &c);
    if (r < 0) {
        goto failed;
    }

    switch (cmd) {
    case RBD_AIO_WRITE:
        r = rbd_aio_write(s->image, off, size, buf, c);
        break;
    case RBD_AIO_READ:
        r = rbd_aio_read(s->image, off, size, buf, c);
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
    qemu_vfree(acb->bounce);
    qemu_aio_release(acb);
    return NULL;
}

static BlockDriverAIOCB *qemu_rbd_aio_readv(BlockDriverState *bs,
                                            int64_t sector_num,
                                            QEMUIOVector *qiov,
                                            int nb_sectors,
                                            BlockDriverCompletionFunc *cb,
                                            void *opaque)
{
    return rbd_start_aio(bs, sector_num, qiov, nb_sectors, cb, opaque,
                         RBD_AIO_READ);
}

static BlockDriverAIOCB *qemu_rbd_aio_writev(BlockDriverState *bs,
                                             int64_t sector_num,
                                             QEMUIOVector *qiov,
                                             int nb_sectors,
                                             BlockDriverCompletionFunc *cb,
                                             void *opaque)
{
    return rbd_start_aio(bs, sector_num, qiov, nb_sectors, cb, opaque,
                         RBD_AIO_WRITE);
}

#ifdef LIBRBD_SUPPORTS_AIO_FLUSH
static BlockDriverAIOCB *qemu_rbd_aio_flush(BlockDriverState *bs,
                                            BlockDriverCompletionFunc *cb,
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
    int r;

    r = rbd_snap_rollback(s->image, snapshot_name);
    return r;
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
static BlockDriverAIOCB* qemu_rbd_aio_discard(BlockDriverState *bs,
                                              int64_t sector_num,
                                              int nb_sectors,
                                              BlockDriverCompletionFunc *cb,
                                              void *opaque)
{
    return rbd_start_aio(bs, sector_num, NULL, nb_sectors, cb, opaque,
                         RBD_AIO_DISCARD);
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
        { /* end of list */ }
    }
};

static BlockDriver bdrv_rbd = {
    .format_name        = "rbd",
    .instance_size      = sizeof(BDRVRBDState),
    .bdrv_needs_filename = true,
    .bdrv_file_open     = qemu_rbd_open,
    .bdrv_close         = qemu_rbd_close,
    .bdrv_create        = qemu_rbd_create,
    .bdrv_has_zero_init = bdrv_has_zero_init_1,
    .bdrv_get_info      = qemu_rbd_getinfo,
    .create_opts        = &qemu_rbd_create_opts,
    .bdrv_getlength     = qemu_rbd_getlength,
    .bdrv_truncate      = qemu_rbd_truncate,
    .protocol_name      = "rbd",

    .bdrv_aio_readv         = qemu_rbd_aio_readv,
    .bdrv_aio_writev        = qemu_rbd_aio_writev,

#ifdef LIBRBD_SUPPORTS_AIO_FLUSH
    .bdrv_aio_flush         = qemu_rbd_aio_flush,
#else
    .bdrv_co_flush_to_disk  = qemu_rbd_co_flush,
#endif

#ifdef LIBRBD_SUPPORTS_DISCARD
    .bdrv_aio_discard       = qemu_rbd_aio_discard,
#endif

    .bdrv_snapshot_create   = qemu_rbd_snap_create,
    .bdrv_snapshot_delete   = qemu_rbd_snap_remove,
    .bdrv_snapshot_list     = qemu_rbd_snap_list,
    .bdrv_snapshot_goto     = qemu_rbd_snap_rollback,
};

static void bdrv_rbd_init(void)
{
    bdrv_register(&bdrv_rbd);
}

block_init(bdrv_rbd_init);

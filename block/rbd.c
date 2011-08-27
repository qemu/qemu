/*
 * QEMU Block driver for RADOS (Ceph)
 *
 * Copyright (C) 2010-2011 Christian Brunner <chb@muc.de>,
 *                         Josh Durgin <josh.durgin@dreamhost.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#include <inttypes.h>

#include "qemu-common.h"
#include "qemu-error.h"

#include "block_int.h"

#include <rbd/librbd.h>



/*
 * When specifying the image filename use:
 *
 * rbd:poolname/devicename[@snapshotname][:option1=value1[:option2=value2...]]
 *
 * poolname must be the name of an existing rados pool
 *
 * devicename is the basename for all objects used to
 * emulate the raw device.
 *
 * Each option given is used to configure rados, and may be
 * any Ceph option, or "conf". The "conf" option specifies
 * a Ceph configuration file to read.
 *
 * Metadata information (image size, ...) is stored in an
 * object with the name "devicename.rbd".
 *
 * The raw device is split into 4MB sized objects by default.
 * The sequencenumber is encoded in a 12 byte long hex-string,
 * and is attached to the devicename, separated by a dot.
 * e.g. "devicename.1234567890ab"
 *
 */

#define OBJ_MAX_SIZE (1UL << OBJ_DEFAULT_OBJ_ORDER)

#define RBD_MAX_CONF_NAME_SIZE 128
#define RBD_MAX_CONF_VAL_SIZE 512
#define RBD_MAX_CONF_SIZE 1024
#define RBD_MAX_POOL_NAME_SIZE 128
#define RBD_MAX_SNAP_NAME_SIZE 128
#define RBD_MAX_SNAPS 100

typedef struct RBDAIOCB {
    BlockDriverAIOCB common;
    QEMUBH *bh;
    int ret;
    QEMUIOVector *qiov;
    char *bounce;
    int write;
    int64_t sector_num;
    int error;
    struct BDRVRBDState *s;
    int cancelled;
} RBDAIOCB;

typedef struct RADOSCB {
    int rcbid;
    RBDAIOCB *acb;
    struct BDRVRBDState *s;
    int done;
    int64_t size;
    char *buf;
    int ret;
} RADOSCB;

#define RBD_FD_READ 0
#define RBD_FD_WRITE 1

typedef struct BDRVRBDState {
    int fds[2];
    rados_t cluster;
    rados_ioctx_t io_ctx;
    rbd_image_t image;
    char name[RBD_MAX_IMAGE_NAME_SIZE];
    int qemu_aio_count;
    char *snap;
    int event_reader_pos;
    RADOSCB *event_rcb;
} BDRVRBDState;

static void rbd_aio_bh_cb(void *opaque);

static int qemu_rbd_next_tok(char *dst, int dst_len,
                             char *src, char delim,
                             const char *name,
                             char **p)
{
    int l;
    char *end;

    *p = NULL;

    if (delim != '\0') {
        end = strchr(src, delim);
        if (end) {
            *p = end + 1;
            *end = '\0';
        }
    }
    l = strlen(src);
    if (l >= dst_len) {
        error_report("%s too long", name);
        return -EINVAL;
    } else if (l == 0) {
        error_report("%s too short", name);
        return -EINVAL;
    }

    pstrcpy(dst, dst_len, src);

    return 0;
}

static int qemu_rbd_parsename(const char *filename,
                              char *pool, int pool_len,
                              char *snap, int snap_len,
                              char *name, int name_len,
                              char *conf, int conf_len)
{
    const char *start;
    char *p, *buf;
    int ret;

    if (!strstart(filename, "rbd:", &start)) {
        return -EINVAL;
    }

    buf = g_strdup(start);
    p = buf;
    *snap = '\0';
    *conf = '\0';

    ret = qemu_rbd_next_tok(pool, pool_len, p, '/', "pool name", &p);
    if (ret < 0 || !p) {
        ret = -EINVAL;
        goto done;
    }

    if (strchr(p, '@')) {
        ret = qemu_rbd_next_tok(name, name_len, p, '@', "object name", &p);
        if (ret < 0) {
            goto done;
        }
        ret = qemu_rbd_next_tok(snap, snap_len, p, ':', "snap name", &p);
    } else {
        ret = qemu_rbd_next_tok(name, name_len, p, ':', "object name", &p);
    }
    if (ret < 0 || !p) {
        goto done;
    }

    ret = qemu_rbd_next_tok(conf, conf_len, p, '\0', "configuration", &p);

done:
    g_free(buf);
    return ret;
}

static int qemu_rbd_set_conf(rados_t cluster, const char *conf)
{
    char *p, *buf;
    char name[RBD_MAX_CONF_NAME_SIZE];
    char value[RBD_MAX_CONF_VAL_SIZE];
    int ret = 0;

    buf = g_strdup(conf);
    p = buf;

    while (p) {
        ret = qemu_rbd_next_tok(name, sizeof(name), p,
                                '=', "conf option name", &p);
        if (ret < 0) {
            break;
        }

        if (!p) {
            error_report("conf option %s has no value", name);
            ret = -EINVAL;
            break;
        }

        ret = qemu_rbd_next_tok(value, sizeof(value), p,
                                ':', "conf option value", &p);
        if (ret < 0) {
            break;
        }

        if (strcmp(name, "conf")) {
            ret = rados_conf_set(cluster, name, value);
            if (ret < 0) {
                error_report("invalid conf option %s", name);
                ret = -EINVAL;
                break;
            }
        } else {
            ret = rados_conf_read_file(cluster, value);
            if (ret < 0) {
                error_report("error reading conf file %s", value);
                break;
            }
        }
    }

    g_free(buf);
    return ret;
}

static int qemu_rbd_create(const char *filename, QEMUOptionParameter *options)
{
    int64_t bytes = 0;
    int64_t objsize;
    int obj_order = 0;
    char pool[RBD_MAX_POOL_NAME_SIZE];
    char name[RBD_MAX_IMAGE_NAME_SIZE];
    char snap_buf[RBD_MAX_SNAP_NAME_SIZE];
    char conf[RBD_MAX_CONF_SIZE];
    rados_t cluster;
    rados_ioctx_t io_ctx;
    int ret;

    if (qemu_rbd_parsename(filename, pool, sizeof(pool),
                           snap_buf, sizeof(snap_buf),
                           name, sizeof(name),
                           conf, sizeof(conf)) < 0) {
        return -EINVAL;
    }

    /* Read out options */
    while (options && options->name) {
        if (!strcmp(options->name, BLOCK_OPT_SIZE)) {
            bytes = options->value.n;
        } else if (!strcmp(options->name, BLOCK_OPT_CLUSTER_SIZE)) {
            if (options->value.n) {
                objsize = options->value.n;
                if ((objsize - 1) & objsize) {    /* not a power of 2? */
                    error_report("obj size needs to be power of 2");
                    return -EINVAL;
                }
                if (objsize < 4096) {
                    error_report("obj size too small");
                    return -EINVAL;
                }
                obj_order = ffs(objsize) - 1;
            }
        }
        options++;
    }

    if (rados_create(&cluster, NULL) < 0) {
        error_report("error initializing");
        return -EIO;
    }

    if (strstr(conf, "conf=") == NULL) {
        if (rados_conf_read_file(cluster, NULL) < 0) {
            error_report("error reading config file");
            rados_shutdown(cluster);
            return -EIO;
        }
    }

    if (conf[0] != '\0' &&
        qemu_rbd_set_conf(cluster, conf) < 0) {
        error_report("error setting config options");
        rados_shutdown(cluster);
        return -EIO;
    }

    if (rados_connect(cluster) < 0) {
        error_report("error connecting");
        rados_shutdown(cluster);
        return -EIO;
    }

    if (rados_ioctx_create(cluster, pool, &io_ctx) < 0) {
        error_report("error opening pool %s", pool);
        rados_shutdown(cluster);
        return -EIO;
    }

    ret = rbd_create(io_ctx, name, bytes, &obj_order);
    rados_ioctx_destroy(io_ctx);
    rados_shutdown(cluster);

    return ret;
}

/*
 * This aio completion is being called from qemu_rbd_aio_event_reader()
 * and runs in qemu context. It schedules a bh, but just in case the aio
 * was not cancelled before.
 */
static void qemu_rbd_complete_aio(RADOSCB *rcb)
{
    RBDAIOCB *acb = rcb->acb;
    int64_t r;

    if (acb->cancelled) {
        qemu_vfree(acb->bounce);
        qemu_aio_release(acb);
        goto done;
    }

    r = rcb->ret;

    if (acb->write) {
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
    /* Note that acb->bh can be NULL in case where the aio was cancelled */
    acb->bh = qemu_bh_new(rbd_aio_bh_cb, acb);
    qemu_bh_schedule(acb->bh);
done:
    g_free(rcb);
}

/*
 * aio fd read handler. It runs in the qemu context and calls the
 * completion handling of completed rados aio operations.
 */
static void qemu_rbd_aio_event_reader(void *opaque)
{
    BDRVRBDState *s = opaque;

    ssize_t ret;

    do {
        char *p = (char *)&s->event_rcb;

        /* now read the rcb pointer that was sent from a non qemu thread */
        if ((ret = read(s->fds[RBD_FD_READ], p + s->event_reader_pos,
                        sizeof(s->event_rcb) - s->event_reader_pos)) > 0) {
            if (ret > 0) {
                s->event_reader_pos += ret;
                if (s->event_reader_pos == sizeof(s->event_rcb)) {
                    s->event_reader_pos = 0;
                    qemu_rbd_complete_aio(s->event_rcb);
                    s->qemu_aio_count--;
                }
            }
        }
    } while (ret < 0 && errno == EINTR);
}

static int qemu_rbd_aio_flush_cb(void *opaque)
{
    BDRVRBDState *s = opaque;

    return (s->qemu_aio_count > 0);
}

static int qemu_rbd_open(BlockDriverState *bs, const char *filename, int flags)
{
    BDRVRBDState *s = bs->opaque;
    char pool[RBD_MAX_POOL_NAME_SIZE];
    char snap_buf[RBD_MAX_SNAP_NAME_SIZE];
    char conf[RBD_MAX_CONF_SIZE];
    int r;

    if (qemu_rbd_parsename(filename, pool, sizeof(pool),
                           snap_buf, sizeof(snap_buf),
                           s->name, sizeof(s->name),
                           conf, sizeof(conf)) < 0) {
        return -EINVAL;
    }
    s->snap = NULL;
    if (snap_buf[0] != '\0') {
        s->snap = g_strdup(snap_buf);
    }

    r = rados_create(&s->cluster, NULL);
    if (r < 0) {
        error_report("error initializing");
        return r;
    }

    if (strstr(conf, "conf=") == NULL) {
        r = rados_conf_read_file(s->cluster, NULL);
        if (r < 0) {
            error_report("error reading config file");
            rados_shutdown(s->cluster);
            return r;
        }
    }

    if (conf[0] != '\0') {
        r = qemu_rbd_set_conf(s->cluster, conf);
        if (r < 0) {
            error_report("error setting config options");
            rados_shutdown(s->cluster);
            return r;
        }
    }

    r = rados_connect(s->cluster);
    if (r < 0) {
        error_report("error connecting");
        rados_shutdown(s->cluster);
        return r;
    }

    r = rados_ioctx_create(s->cluster, pool, &s->io_ctx);
    if (r < 0) {
        error_report("error opening pool %s", pool);
        rados_shutdown(s->cluster);
        return r;
    }

    r = rbd_open(s->io_ctx, s->name, &s->image, s->snap);
    if (r < 0) {
        error_report("error reading header from %s", s->name);
        rados_ioctx_destroy(s->io_ctx);
        rados_shutdown(s->cluster);
        return r;
    }

    bs->read_only = (s->snap != NULL);

    s->event_reader_pos = 0;
    r = qemu_pipe(s->fds);
    if (r < 0) {
        error_report("error opening eventfd");
        goto failed;
    }
    fcntl(s->fds[0], F_SETFL, O_NONBLOCK);
    fcntl(s->fds[1], F_SETFL, O_NONBLOCK);
    qemu_aio_set_fd_handler(s->fds[RBD_FD_READ], qemu_rbd_aio_event_reader,
                            NULL, qemu_rbd_aio_flush_cb, NULL, s);


    return 0;

failed:
    rbd_close(s->image);
    rados_ioctx_destroy(s->io_ctx);
    rados_shutdown(s->cluster);
    return r;
}

static void qemu_rbd_close(BlockDriverState *bs)
{
    BDRVRBDState *s = bs->opaque;

    close(s->fds[0]);
    close(s->fds[1]);
    qemu_aio_set_fd_handler(s->fds[RBD_FD_READ], NULL , NULL, NULL, NULL,
        NULL);

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
}

static AIOPool rbd_aio_pool = {
    .aiocb_size = sizeof(RBDAIOCB),
    .cancel = qemu_rbd_aio_cancel,
};

static int qemu_rbd_send_pipe(BDRVRBDState *s, RADOSCB *rcb)
{
    int ret = 0;
    while (1) {
        fd_set wfd;
        int fd = s->fds[RBD_FD_WRITE];

        /* send the op pointer to the qemu thread that is responsible
           for the aio/op completion. Must do it in a qemu thread context */
        ret = write(fd, (void *)&rcb, sizeof(rcb));
        if (ret >= 0) {
            break;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno != EAGAIN) {
            break;
        }

        FD_ZERO(&wfd);
        FD_SET(fd, &wfd);
        do {
            ret = select(fd + 1, NULL, &wfd, NULL, NULL);
        } while (ret < 0 && errno == EINTR);
    }

    return ret;
}

/*
 * This is the callback function for rbd_aio_read and _write
 *
 * Note: this function is being called from a non qemu thread so
 * we need to be careful about what we do here. Generally we only
 * write to the block notification pipe, and do the rest of the
 * io completion handling from qemu_rbd_aio_event_reader() which
 * runs in a qemu context.
 */
static void rbd_finish_aiocb(rbd_completion_t c, RADOSCB *rcb)
{
    int ret;
    rcb->ret = rbd_aio_get_return_value(c);
    rbd_aio_release(c);
    ret = qemu_rbd_send_pipe(rcb->s, rcb);
    if (ret < 0) {
        error_report("failed writing to acb->s->fds");
        g_free(rcb);
    }
}

/* Callback when all queued rbd_aio requests are complete */

static void rbd_aio_bh_cb(void *opaque)
{
    RBDAIOCB *acb = opaque;

    if (!acb->write) {
        qemu_iovec_from_buffer(acb->qiov, acb->bounce, acb->qiov->size);
    }
    qemu_vfree(acb->bounce);
    acb->common.cb(acb->common.opaque, (acb->ret > 0 ? 0 : acb->ret));
    qemu_bh_delete(acb->bh);
    acb->bh = NULL;

    qemu_aio_release(acb);
}

static BlockDriverAIOCB *rbd_aio_rw_vector(BlockDriverState *bs,
                                           int64_t sector_num,
                                           QEMUIOVector *qiov,
                                           int nb_sectors,
                                           BlockDriverCompletionFunc *cb,
                                           void *opaque, int write)
{
    RBDAIOCB *acb;
    RADOSCB *rcb;
    rbd_completion_t c;
    int64_t off, size;
    char *buf;
    int r;

    BDRVRBDState *s = bs->opaque;

    acb = qemu_aio_get(&rbd_aio_pool, bs, cb, opaque);
    if (!acb) {
        return NULL;
    }
    acb->write = write;
    acb->qiov = qiov;
    acb->bounce = qemu_blockalign(bs, qiov->size);
    acb->ret = 0;
    acb->error = 0;
    acb->s = s;
    acb->cancelled = 0;
    acb->bh = NULL;

    if (write) {
        qemu_iovec_to_buffer(acb->qiov, acb->bounce);
    }

    buf = acb->bounce;

    off = sector_num * BDRV_SECTOR_SIZE;
    size = nb_sectors * BDRV_SECTOR_SIZE;

    s->qemu_aio_count++; /* All the RADOSCB */

    rcb = g_malloc(sizeof(RADOSCB));
    rcb->done = 0;
    rcb->acb = acb;
    rcb->buf = buf;
    rcb->s = acb->s;
    rcb->size = size;
    r = rbd_aio_create_completion(rcb, (rbd_callback_t) rbd_finish_aiocb, &c);
    if (r < 0) {
        goto failed;
    }

    if (write) {
        r = rbd_aio_write(s->image, off, size, buf, c);
    } else {
        r = rbd_aio_read(s->image, off, size, buf, c);
    }

    if (r < 0) {
        goto failed;
    }

    return &acb->common;

failed:
    g_free(rcb);
    s->qemu_aio_count--;
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
    return rbd_aio_rw_vector(bs, sector_num, qiov, nb_sectors, cb, opaque, 0);
}

static BlockDriverAIOCB *qemu_rbd_aio_writev(BlockDriverState *bs,
                                             int64_t sector_num,
                                             QEMUIOVector *qiov,
                                             int nb_sectors,
                                             BlockDriverCompletionFunc *cb,
                                             void *opaque)
{
    return rbd_aio_rw_vector(bs, sector_num, qiov, nb_sectors, cb, opaque, 1);
}

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

static int qemu_rbd_snap_list(BlockDriverState *bs,
                              QEMUSnapshotInfo **psn_tab)
{
    BDRVRBDState *s = bs->opaque;
    QEMUSnapshotInfo *sn_info, *sn_tab = NULL;
    int i, snap_count;
    rbd_snap_info_t *snaps;
    int max_snaps = RBD_MAX_SNAPS;

    do {
        snaps = g_malloc(sizeof(*snaps) * max_snaps);
        snap_count = rbd_snap_list(s->image, snaps, &max_snaps);
        if (snap_count < 0) {
            g_free(snaps);
        }
    } while (snap_count == -ERANGE);

    if (snap_count <= 0) {
        return snap_count;
    }

    sn_tab = g_malloc0(snap_count * sizeof(QEMUSnapshotInfo));

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

    *psn_tab = sn_tab;
    return snap_count;
}

static QEMUOptionParameter qemu_rbd_create_options[] = {
    {
     .name = BLOCK_OPT_SIZE,
     .type = OPT_SIZE,
     .help = "Virtual disk size"
    },
    {
     .name = BLOCK_OPT_CLUSTER_SIZE,
     .type = OPT_SIZE,
     .help = "RBD object size"
    },
    {NULL}
};

static BlockDriver bdrv_rbd = {
    .format_name        = "rbd",
    .instance_size      = sizeof(BDRVRBDState),
    .bdrv_file_open     = qemu_rbd_open,
    .bdrv_close         = qemu_rbd_close,
    .bdrv_create        = qemu_rbd_create,
    .bdrv_get_info      = qemu_rbd_getinfo,
    .create_options     = qemu_rbd_create_options,
    .bdrv_getlength     = qemu_rbd_getlength,
    .bdrv_truncate      = qemu_rbd_truncate,
    .protocol_name      = "rbd",

    .bdrv_aio_readv     = qemu_rbd_aio_readv,
    .bdrv_aio_writev    = qemu_rbd_aio_writev,

    .bdrv_snapshot_create = qemu_rbd_snap_create,
    .bdrv_snapshot_list = qemu_rbd_snap_list,
};

static void bdrv_rbd_init(void)
{
    bdrv_register(&bdrv_rbd);
}

block_init(bdrv_rbd_init);

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
#include <glusterfs/api/glfs.h>
#include "block/block_int.h"
#include "qemu/uri.h"

typedef struct GlusterAIOCB {
    int64_t size;
    int ret;
    QEMUBH *bh;
    Coroutine *coroutine;
    AioContext *aio_context;
} GlusterAIOCB;

typedef struct BDRVGlusterState {
    struct glfs *glfs;
    struct glfs_fd *fd;
} BDRVGlusterState;

typedef struct GlusterConf {
    char *server;
    int port;
    char *volname;
    char *image;
    char *transport;
} GlusterConf;

static void qemu_gluster_gconf_free(GlusterConf *gconf)
{
    if (gconf) {
        g_free(gconf->server);
        g_free(gconf->volname);
        g_free(gconf->image);
        g_free(gconf->transport);
        g_free(gconf);
    }
}

static int parse_volume_options(GlusterConf *gconf, char *path)
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
    gconf->volname = g_strndup(q, p - q);

    /* image */
    p += strspn(p, "/");
    if (*p == '\0') {
        return -EINVAL;
    }
    gconf->image = g_strdup(p);
    return 0;
}

/*
 * file=gluster[+transport]://[server[:port]]/volname/image[?socket=...]
 *
 * 'gluster' is the protocol.
 *
 * 'transport' specifies the transport type used to connect to gluster
 * management daemon (glusterd). Valid transport types are
 * tcp, unix and rdma. If a transport type isn't specified, then tcp
 * type is assumed.
 *
 * 'server' specifies the server where the volume file specification for
 * the given volume resides. This can be either hostname, ipv4 address
 * or ipv6 address. ipv6 address needs to be within square brackets [ ].
 * If transport type is 'unix', then 'server' field should not be specified.
 * The 'socket' field needs to be populated with the path to unix domain
 * socket.
 *
 * 'port' is the port number on which glusterd is listening. This is optional
 * and if not specified, QEMU will send 0 which will make gluster to use the
 * default port. If the transport type is unix, then 'port' should not be
 * specified.
 *
 * 'volname' is the name of the gluster volume which contains the VM image.
 *
 * 'image' is the path to the actual VM image that resides on gluster volume.
 *
 * Examples:
 *
 * file=gluster://1.2.3.4/testvol/a.img
 * file=gluster+tcp://1.2.3.4/testvol/a.img
 * file=gluster+tcp://1.2.3.4:24007/testvol/dir/a.img
 * file=gluster+tcp://[1:2:3:4:5:6:7:8]/testvol/dir/a.img
 * file=gluster+tcp://[1:2:3:4:5:6:7:8]:24007/testvol/dir/a.img
 * file=gluster+tcp://server.domain.com:24007/testvol/dir/a.img
 * file=gluster+unix:///testvol/dir/a.img?socket=/tmp/glusterd.socket
 * file=gluster+rdma://1.2.3.4:24007/testvol/a.img
 */
static int qemu_gluster_parseuri(GlusterConf *gconf, const char *filename)
{
    URI *uri;
    QueryParams *qp = NULL;
    bool is_unix = false;
    int ret = 0;

    uri = uri_parse(filename);
    if (!uri) {
        return -EINVAL;
    }

    /* transport */
    if (!uri->scheme || !strcmp(uri->scheme, "gluster")) {
        gconf->transport = g_strdup("tcp");
    } else if (!strcmp(uri->scheme, "gluster+tcp")) {
        gconf->transport = g_strdup("tcp");
    } else if (!strcmp(uri->scheme, "gluster+unix")) {
        gconf->transport = g_strdup("unix");
        is_unix = true;
    } else if (!strcmp(uri->scheme, "gluster+rdma")) {
        gconf->transport = g_strdup("rdma");
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
        gconf->server = g_strdup(qp->p[0].value);
    } else {
        gconf->server = g_strdup(uri->server ? uri->server : "localhost");
        gconf->port = uri->port;
    }

out:
    if (qp) {
        query_params_free(qp);
    }
    uri_free(uri);
    return ret;
}

static struct glfs *qemu_gluster_init(GlusterConf *gconf, const char *filename,
                                      Error **errp)
{
    struct glfs *glfs = NULL;
    int ret;
    int old_errno;

    ret = qemu_gluster_parseuri(gconf, filename);
    if (ret < 0) {
        error_setg(errp, "Usage: file=gluster[+transport]://[server[:port]]/"
                   "volname/image[?socket=...]");
        errno = -ret;
        goto out;
    }

    glfs = glfs_new(gconf->volname);
    if (!glfs) {
        goto out;
    }

    ret = glfs_set_volfile_server(glfs, gconf->transport, gconf->server,
            gconf->port);
    if (ret < 0) {
        goto out;
    }

    /*
     * TODO: Use GF_LOG_ERROR instead of hard code value of 4 here when
     * GlusterFS makes GF_LOG_* macros available to libgfapi users.
     */
    ret = glfs_set_logging(glfs, "-", 4);
    if (ret < 0) {
        goto out;
    }

    ret = glfs_init(glfs);
    if (ret) {
        error_setg_errno(errp, errno,
                         "Gluster connection failed for server=%s port=%d "
                         "volume=%s image=%s transport=%s", gconf->server,
                         gconf->port, gconf->volname, gconf->image,
                         gconf->transport);

        /* glfs_init sometimes doesn't set errno although docs suggest that */
        if (errno == 0)
            errno = EINVAL;

        goto out;
    }
    return glfs;

out:
    if (glfs) {
        old_errno = errno;
        glfs_fini(glfs);
        errno = old_errno;
    }
    return NULL;
}

static void qemu_gluster_complete_aio(void *opaque)
{
    GlusterAIOCB *acb = (GlusterAIOCB *)opaque;

    qemu_bh_delete(acb->bh);
    acb->bh = NULL;
    qemu_coroutine_enter(acb->coroutine, NULL);
}

/*
 * AIO callback routine called from GlusterFS thread.
 */
static void gluster_finish_aiocb(struct glfs_fd *fd, ssize_t ret, void *arg)
{
    GlusterAIOCB *acb = (GlusterAIOCB *)arg;

    if (!ret || ret == acb->size) {
        acb->ret = 0; /* Success */
    } else if (ret < 0) {
        acb->ret = ret; /* Read/Write failed */
    } else {
        acb->ret = -EIO; /* Partial read/write - fail it */
    }

    acb->bh = aio_bh_new(acb->aio_context, qemu_gluster_complete_aio, acb);
    qemu_bh_schedule(acb->bh);
}

/* TODO Convert to fine grained options */
static QemuOptsList runtime_opts = {
    .name = "gluster",
    .head = QTAILQ_HEAD_INITIALIZER(runtime_opts.head),
    .desc = {
        {
            .name = "filename",
            .type = QEMU_OPT_STRING,
            .help = "URL to the gluster image",
        },
        { /* end of list */ }
    },
};

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

static int qemu_gluster_open(BlockDriverState *bs,  QDict *options,
                             int bdrv_flags, Error **errp)
{
    BDRVGlusterState *s = bs->opaque;
    int open_flags = 0;
    int ret = 0;
    GlusterConf *gconf = g_new0(GlusterConf, 1);
    QemuOpts *opts;
    Error *local_err = NULL;
    const char *filename;

    opts = qemu_opts_create(&runtime_opts, NULL, 0, &error_abort);
    qemu_opts_absorb_qdict(opts, options, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        ret = -EINVAL;
        goto out;
    }

    filename = qemu_opt_get(opts, "filename");

    s->glfs = qemu_gluster_init(gconf, filename, errp);
    if (!s->glfs) {
        ret = -errno;
        goto out;
    }

    qemu_gluster_parse_flags(bdrv_flags, &open_flags);

    s->fd = glfs_open(s->glfs, gconf->image, open_flags);
    if (!s->fd) {
        ret = -errno;
    }

out:
    qemu_opts_del(opts);
    qemu_gluster_gconf_free(gconf);
    if (!ret) {
        return ret;
    }
    if (s->fd) {
        glfs_close(s->fd);
    }
    if (s->glfs) {
        glfs_fini(s->glfs);
    }
    return ret;
}

typedef struct BDRVGlusterReopenState {
    struct glfs *glfs;
    struct glfs_fd *fd;
} BDRVGlusterReopenState;


static int qemu_gluster_reopen_prepare(BDRVReopenState *state,
                                       BlockReopenQueue *queue, Error **errp)
{
    int ret = 0;
    BDRVGlusterReopenState *reop_s;
    GlusterConf *gconf = NULL;
    int open_flags = 0;

    assert(state != NULL);
    assert(state->bs != NULL);

    state->opaque = g_new0(BDRVGlusterReopenState, 1);
    reop_s = state->opaque;

    qemu_gluster_parse_flags(state->flags, &open_flags);

    gconf = g_new0(GlusterConf, 1);

    reop_s->glfs = qemu_gluster_init(gconf, state->bs->filename, errp);
    if (reop_s->glfs == NULL) {
        ret = -errno;
        goto exit;
    }

    reop_s->fd = glfs_open(reop_s->glfs, gconf->image, open_flags);
    if (reop_s->fd == NULL) {
        /* reops->glfs will be cleaned up in _abort */
        ret = -errno;
        goto exit;
    }

exit:
    /* state->opaque will be freed in either the _abort or _commit */
    qemu_gluster_gconf_free(gconf);
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
    if (s->glfs) {
        glfs_fini(s->glfs);
    }

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

    if (reop_s->glfs) {
        glfs_fini(reop_s->glfs);
    }

    g_free(state->opaque);
    state->opaque = NULL;

    return;
}

#ifdef CONFIG_GLUSTERFS_ZEROFILL
static coroutine_fn int qemu_gluster_co_write_zeroes(BlockDriverState *bs,
        int64_t sector_num, int nb_sectors, BdrvRequestFlags flags)
{
    int ret;
    GlusterAIOCB acb;
    BDRVGlusterState *s = bs->opaque;
    off_t size = nb_sectors * BDRV_SECTOR_SIZE;
    off_t offset = sector_num * BDRV_SECTOR_SIZE;

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

static inline bool gluster_supports_zerofill(void)
{
    return 1;
}

static inline int qemu_gluster_zerofill(struct glfs_fd *fd, int64_t offset,
        int64_t size)
{
    return glfs_zerofill(fd, offset, size);
}

#else
static inline bool gluster_supports_zerofill(void)
{
    return 0;
}

static inline int qemu_gluster_zerofill(struct glfs_fd *fd, int64_t offset,
        int64_t size)
{
    return 0;
}
#endif

static int qemu_gluster_create(const char *filename,
                               QemuOpts *opts, Error **errp)
{
    struct glfs *glfs;
    struct glfs_fd *fd;
    int ret = 0;
    int prealloc = 0;
    int64_t total_size = 0;
    char *tmp = NULL;
    GlusterConf *gconf = g_new0(GlusterConf, 1);

    glfs = qemu_gluster_init(gconf, filename, errp);
    if (!glfs) {
        ret = -errno;
        goto out;
    }

    total_size = ROUND_UP(qemu_opt_get_size_del(opts, BLOCK_OPT_SIZE, 0),
                          BDRV_SECTOR_SIZE);

    tmp = qemu_opt_get_del(opts, BLOCK_OPT_PREALLOC);
    if (!tmp || !strcmp(tmp, "off")) {
        prealloc = 0;
    } else if (!strcmp(tmp, "full") &&
               gluster_supports_zerofill()) {
        prealloc = 1;
    } else {
        error_setg(errp, "Invalid preallocation mode: '%s'"
            " or GlusterFS doesn't support zerofill API",
            tmp);
        ret = -EINVAL;
        goto out;
    }

    fd = glfs_creat(glfs, gconf->image,
        O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, S_IRUSR | S_IWUSR);
    if (!fd) {
        ret = -errno;
    } else {
        if (!glfs_ftruncate(fd, total_size)) {
            if (prealloc && qemu_gluster_zerofill(fd, 0, total_size)) {
                ret = -errno;
            }
        } else {
            ret = -errno;
        }

        if (glfs_close(fd) != 0) {
            ret = -errno;
        }
    }
out:
    g_free(tmp);
    qemu_gluster_gconf_free(gconf);
    if (glfs) {
        glfs_fini(glfs);
    }
    return ret;
}

static coroutine_fn int qemu_gluster_co_rw(BlockDriverState *bs,
        int64_t sector_num, int nb_sectors, QEMUIOVector *qiov, int write)
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

static int qemu_gluster_truncate(BlockDriverState *bs, int64_t offset)
{
    int ret;
    BDRVGlusterState *s = bs->opaque;

    ret = glfs_ftruncate(s->fd, offset);
    if (ret < 0) {
        return -errno;
    }

    return 0;
}

static coroutine_fn int qemu_gluster_co_readv(BlockDriverState *bs,
        int64_t sector_num, int nb_sectors, QEMUIOVector *qiov)
{
    return qemu_gluster_co_rw(bs, sector_num, nb_sectors, qiov, 0);
}

static coroutine_fn int qemu_gluster_co_writev(BlockDriverState *bs,
        int64_t sector_num, int nb_sectors, QEMUIOVector *qiov)
{
    return qemu_gluster_co_rw(bs, sector_num, nb_sectors, qiov, 1);
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
        return -errno;
    }

    qemu_coroutine_yield();
    return acb.ret;
}

#ifdef CONFIG_GLUSTERFS_DISCARD
static coroutine_fn int qemu_gluster_co_discard(BlockDriverState *bs,
        int64_t sector_num, int nb_sectors)
{
    int ret;
    GlusterAIOCB acb;
    BDRVGlusterState *s = bs->opaque;
    size_t size = nb_sectors * BDRV_SECTOR_SIZE;
    off_t offset = sector_num * BDRV_SECTOR_SIZE;

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

static void qemu_gluster_close(BlockDriverState *bs)
{
    BDRVGlusterState *s = bs->opaque;

    if (s->fd) {
        glfs_close(s->fd);
        s->fd = NULL;
    }
    glfs_fini(s->glfs);
}

static int qemu_gluster_has_zero_init(BlockDriverState *bs)
{
    /* GlusterFS volume could be backed by a block device */
    return 0;
}

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
            .help = "Preallocation mode (allowed values: off, full)"
        },
        { /* end of list */ }
    }
};

static BlockDriver bdrv_gluster = {
    .format_name                  = "gluster",
    .protocol_name                = "gluster",
    .instance_size                = sizeof(BDRVGlusterState),
    .bdrv_needs_filename          = true,
    .bdrv_file_open               = qemu_gluster_open,
    .bdrv_reopen_prepare          = qemu_gluster_reopen_prepare,
    .bdrv_reopen_commit           = qemu_gluster_reopen_commit,
    .bdrv_reopen_abort            = qemu_gluster_reopen_abort,
    .bdrv_close                   = qemu_gluster_close,
    .bdrv_create                  = qemu_gluster_create,
    .bdrv_getlength               = qemu_gluster_getlength,
    .bdrv_get_allocated_file_size = qemu_gluster_allocated_file_size,
    .bdrv_truncate                = qemu_gluster_truncate,
    .bdrv_co_readv                = qemu_gluster_co_readv,
    .bdrv_co_writev               = qemu_gluster_co_writev,
    .bdrv_co_flush_to_disk        = qemu_gluster_co_flush_to_disk,
    .bdrv_has_zero_init           = qemu_gluster_has_zero_init,
#ifdef CONFIG_GLUSTERFS_DISCARD
    .bdrv_co_discard              = qemu_gluster_co_discard,
#endif
#ifdef CONFIG_GLUSTERFS_ZEROFILL
    .bdrv_co_write_zeroes         = qemu_gluster_co_write_zeroes,
#endif
    .create_opts                  = &qemu_gluster_create_opts,
};

static BlockDriver bdrv_gluster_tcp = {
    .format_name                  = "gluster",
    .protocol_name                = "gluster+tcp",
    .instance_size                = sizeof(BDRVGlusterState),
    .bdrv_needs_filename          = true,
    .bdrv_file_open               = qemu_gluster_open,
    .bdrv_reopen_prepare          = qemu_gluster_reopen_prepare,
    .bdrv_reopen_commit           = qemu_gluster_reopen_commit,
    .bdrv_reopen_abort            = qemu_gluster_reopen_abort,
    .bdrv_close                   = qemu_gluster_close,
    .bdrv_create                  = qemu_gluster_create,
    .bdrv_getlength               = qemu_gluster_getlength,
    .bdrv_get_allocated_file_size = qemu_gluster_allocated_file_size,
    .bdrv_truncate                = qemu_gluster_truncate,
    .bdrv_co_readv                = qemu_gluster_co_readv,
    .bdrv_co_writev               = qemu_gluster_co_writev,
    .bdrv_co_flush_to_disk        = qemu_gluster_co_flush_to_disk,
    .bdrv_has_zero_init           = qemu_gluster_has_zero_init,
#ifdef CONFIG_GLUSTERFS_DISCARD
    .bdrv_co_discard              = qemu_gluster_co_discard,
#endif
#ifdef CONFIG_GLUSTERFS_ZEROFILL
    .bdrv_co_write_zeroes         = qemu_gluster_co_write_zeroes,
#endif
    .create_opts                  = &qemu_gluster_create_opts,
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
    .bdrv_create                  = qemu_gluster_create,
    .bdrv_getlength               = qemu_gluster_getlength,
    .bdrv_get_allocated_file_size = qemu_gluster_allocated_file_size,
    .bdrv_truncate                = qemu_gluster_truncate,
    .bdrv_co_readv                = qemu_gluster_co_readv,
    .bdrv_co_writev               = qemu_gluster_co_writev,
    .bdrv_co_flush_to_disk        = qemu_gluster_co_flush_to_disk,
    .bdrv_has_zero_init           = qemu_gluster_has_zero_init,
#ifdef CONFIG_GLUSTERFS_DISCARD
    .bdrv_co_discard              = qemu_gluster_co_discard,
#endif
#ifdef CONFIG_GLUSTERFS_ZEROFILL
    .bdrv_co_write_zeroes         = qemu_gluster_co_write_zeroes,
#endif
    .create_opts                  = &qemu_gluster_create_opts,
};

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
    .bdrv_create                  = qemu_gluster_create,
    .bdrv_getlength               = qemu_gluster_getlength,
    .bdrv_get_allocated_file_size = qemu_gluster_allocated_file_size,
    .bdrv_truncate                = qemu_gluster_truncate,
    .bdrv_co_readv                = qemu_gluster_co_readv,
    .bdrv_co_writev               = qemu_gluster_co_writev,
    .bdrv_co_flush_to_disk        = qemu_gluster_co_flush_to_disk,
    .bdrv_has_zero_init           = qemu_gluster_has_zero_init,
#ifdef CONFIG_GLUSTERFS_DISCARD
    .bdrv_co_discard              = qemu_gluster_co_discard,
#endif
#ifdef CONFIG_GLUSTERFS_ZEROFILL
    .bdrv_co_write_zeroes         = qemu_gluster_co_write_zeroes,
#endif
    .create_opts                  = &qemu_gluster_create_opts,
};

static void bdrv_gluster_init(void)
{
    bdrv_register(&bdrv_gluster_rdma);
    bdrv_register(&bdrv_gluster_unix);
    bdrv_register(&bdrv_gluster_tcp);
    bdrv_register(&bdrv_gluster);
}

block_init(bdrv_gluster_init);

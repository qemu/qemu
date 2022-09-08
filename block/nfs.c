/*
 * QEMU Block driver for native access to files on NFS shares
 *
 * Copyright (c) 2014-2017 Peter Lieven <pl@kamp.de>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"

#if !defined(_WIN32)
#include <poll.h>
#endif
#include "qemu/config-file.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "block/block_int.h"
#include "block/qdict.h"
#include "trace.h"
#include "qemu/iov.h"
#include "qemu/main-loop.h"
#include "qemu/module.h"
#include "qemu/option.h"
#include "qemu/uri.h"
#include "qemu/cutils.h"
#include "sysemu/replay.h"
#include "qapi/qapi-visit-block-core.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qstring.h"
#include "qapi/qobject-input-visitor.h"
#include "qapi/qobject-output-visitor.h"
#include <nfsc/libnfs.h>


#define QEMU_NFS_MAX_READAHEAD_SIZE 1048576
#define QEMU_NFS_MAX_PAGECACHE_SIZE (8388608 / NFS_BLKSIZE)
#define QEMU_NFS_MAX_DEBUG_LEVEL 2

typedef struct NFSClient {
    struct nfs_context *context;
    struct nfsfh *fh;
    int events;
    bool has_zero_init;
    AioContext *aio_context;
    QemuMutex mutex;
    uint64_t st_blocks;
    bool cache_used;
    NFSServer *server;
    char *path;
    int64_t uid, gid, tcp_syncnt, readahead, pagecache, debug;
} NFSClient;

typedef struct NFSRPC {
    BlockDriverState *bs;
    int ret;
    int complete;
    QEMUIOVector *iov;
    struct stat *st;
    Coroutine *co;
    NFSClient *client;
} NFSRPC;

static int nfs_parse_uri(const char *filename, QDict *options, Error **errp)
{
    URI *uri = NULL;
    QueryParams *qp = NULL;
    int ret = -EINVAL, i;

    uri = uri_parse(filename);
    if (!uri) {
        error_setg(errp, "Invalid URI specified");
        goto out;
    }
    if (g_strcmp0(uri->scheme, "nfs") != 0) {
        error_setg(errp, "URI scheme must be 'nfs'");
        goto out;
    }

    if (!uri->server) {
        error_setg(errp, "missing hostname in URI");
        goto out;
    }

    if (!uri->path) {
        error_setg(errp, "missing file path in URI");
        goto out;
    }

    qp = query_params_parse(uri->query);
    if (!qp) {
        error_setg(errp, "could not parse query parameters");
        goto out;
    }

    qdict_put_str(options, "server.host", uri->server);
    qdict_put_str(options, "server.type", "inet");
    qdict_put_str(options, "path", uri->path);

    for (i = 0; i < qp->n; i++) {
        unsigned long long val;
        if (!qp->p[i].value) {
            error_setg(errp, "Value for NFS parameter expected: %s",
                       qp->p[i].name);
            goto out;
        }
        if (parse_uint_full(qp->p[i].value, &val, 0)) {
            error_setg(errp, "Illegal value for NFS parameter: %s",
                       qp->p[i].name);
            goto out;
        }
        if (!strcmp(qp->p[i].name, "uid")) {
            qdict_put_str(options, "user", qp->p[i].value);
        } else if (!strcmp(qp->p[i].name, "gid")) {
            qdict_put_str(options, "group", qp->p[i].value);
        } else if (!strcmp(qp->p[i].name, "tcp-syncnt")) {
            qdict_put_str(options, "tcp-syn-count", qp->p[i].value);
        } else if (!strcmp(qp->p[i].name, "readahead")) {
            qdict_put_str(options, "readahead-size", qp->p[i].value);
        } else if (!strcmp(qp->p[i].name, "pagecache")) {
            qdict_put_str(options, "page-cache-size", qp->p[i].value);
        } else if (!strcmp(qp->p[i].name, "debug")) {
            qdict_put_str(options, "debug", qp->p[i].value);
        } else {
            error_setg(errp, "Unknown NFS parameter name: %s",
                       qp->p[i].name);
            goto out;
        }
    }
    ret = 0;
out:
    if (qp) {
        query_params_free(qp);
    }
    uri_free(uri);
    return ret;
}

static bool nfs_has_filename_options_conflict(QDict *options, Error **errp)
{
    const QDictEntry *qe;

    for (qe = qdict_first(options); qe; qe = qdict_next(options, qe)) {
        if (!strcmp(qe->key, "host") ||
            !strcmp(qe->key, "path") ||
            !strcmp(qe->key, "user") ||
            !strcmp(qe->key, "group") ||
            !strcmp(qe->key, "tcp-syn-count") ||
            !strcmp(qe->key, "readahead-size") ||
            !strcmp(qe->key, "page-cache-size") ||
            !strcmp(qe->key, "debug") ||
            strstart(qe->key, "server.", NULL))
        {
            error_setg(errp, "Option %s cannot be used with a filename",
                       qe->key);
            return true;
        }
    }

    return false;
}

static void nfs_parse_filename(const char *filename, QDict *options,
                               Error **errp)
{
    if (nfs_has_filename_options_conflict(options, errp)) {
        return;
    }

    nfs_parse_uri(filename, options, errp);
}

static void nfs_process_read(void *arg);
static void nfs_process_write(void *arg);

/* Called with QemuMutex held.  */
static void nfs_set_events(NFSClient *client)
{
    int ev = nfs_which_events(client->context);
    if (ev != client->events) {
        aio_set_fd_handler(client->aio_context, nfs_get_fd(client->context),
                           false,
                           (ev & POLLIN) ? nfs_process_read : NULL,
                           (ev & POLLOUT) ? nfs_process_write : NULL,
                           NULL, NULL, client);

    }
    client->events = ev;
}

static void nfs_process_read(void *arg)
{
    NFSClient *client = arg;

    qemu_mutex_lock(&client->mutex);
    nfs_service(client->context, POLLIN);
    nfs_set_events(client);
    qemu_mutex_unlock(&client->mutex);
}

static void nfs_process_write(void *arg)
{
    NFSClient *client = arg;

    qemu_mutex_lock(&client->mutex);
    nfs_service(client->context, POLLOUT);
    nfs_set_events(client);
    qemu_mutex_unlock(&client->mutex);
}

static void coroutine_fn nfs_co_init_task(BlockDriverState *bs, NFSRPC *task)
{
    *task = (NFSRPC) {
        .co             = qemu_coroutine_self(),
        .bs             = bs,
        .client         = bs->opaque,
    };
}

static void nfs_co_generic_bh_cb(void *opaque)
{
    NFSRPC *task = opaque;

    task->complete = 1;
    aio_co_wake(task->co);
}

/* Called (via nfs_service) with QemuMutex held.  */
static void
nfs_co_generic_cb(int ret, struct nfs_context *nfs, void *data,
                  void *private_data)
{
    NFSRPC *task = private_data;
    task->ret = ret;
    assert(!task->st);
    if (task->ret > 0 && task->iov) {
        if (task->ret <= task->iov->size) {
            qemu_iovec_from_buf(task->iov, 0, data, task->ret);
        } else {
            task->ret = -EIO;
        }
    }
    if (task->ret < 0) {
        error_report("NFS Error: %s", nfs_get_error(nfs));
    }
    replay_bh_schedule_oneshot_event(task->client->aio_context,
                                     nfs_co_generic_bh_cb, task);
}

static int coroutine_fn nfs_co_preadv(BlockDriverState *bs, int64_t offset,
                                      int64_t bytes, QEMUIOVector *iov,
                                      BdrvRequestFlags flags)
{
    NFSClient *client = bs->opaque;
    NFSRPC task;

    nfs_co_init_task(bs, &task);
    task.iov = iov;

    WITH_QEMU_LOCK_GUARD(&client->mutex) {
        if (nfs_pread_async(client->context, client->fh,
                            offset, bytes, nfs_co_generic_cb, &task) != 0) {
            return -ENOMEM;
        }

        nfs_set_events(client);
    }
    while (!task.complete) {
        qemu_coroutine_yield();
    }

    if (task.ret < 0) {
        return task.ret;
    }

    /* zero pad short reads */
    if (task.ret < iov->size) {
        qemu_iovec_memset(iov, task.ret, 0, iov->size - task.ret);
    }

    return 0;
}

static int coroutine_fn nfs_co_pwritev(BlockDriverState *bs, int64_t offset,
                                       int64_t bytes, QEMUIOVector *iov,
                                       BdrvRequestFlags flags)
{
    NFSClient *client = bs->opaque;
    NFSRPC task;
    char *buf = NULL;
    bool my_buffer = false;

    nfs_co_init_task(bs, &task);

    if (iov->niov != 1) {
        buf = g_try_malloc(bytes);
        if (bytes && buf == NULL) {
            return -ENOMEM;
        }
        qemu_iovec_to_buf(iov, 0, buf, bytes);
        my_buffer = true;
    } else {
        buf = iov->iov[0].iov_base;
    }

    WITH_QEMU_LOCK_GUARD(&client->mutex) {
        if (nfs_pwrite_async(client->context, client->fh,
                             offset, bytes, buf,
                             nfs_co_generic_cb, &task) != 0) {
            if (my_buffer) {
                g_free(buf);
            }
            return -ENOMEM;
        }

        nfs_set_events(client);
    }
    while (!task.complete) {
        qemu_coroutine_yield();
    }

    if (my_buffer) {
        g_free(buf);
    }

    if (task.ret != bytes) {
        return task.ret < 0 ? task.ret : -EIO;
    }

    return 0;
}

static int coroutine_fn nfs_co_flush(BlockDriverState *bs)
{
    NFSClient *client = bs->opaque;
    NFSRPC task;

    nfs_co_init_task(bs, &task);

    WITH_QEMU_LOCK_GUARD(&client->mutex) {
        if (nfs_fsync_async(client->context, client->fh, nfs_co_generic_cb,
                            &task) != 0) {
            return -ENOMEM;
        }

        nfs_set_events(client);
    }
    while (!task.complete) {
        qemu_coroutine_yield();
    }

    return task.ret;
}

static void nfs_detach_aio_context(BlockDriverState *bs)
{
    NFSClient *client = bs->opaque;

    aio_set_fd_handler(client->aio_context, nfs_get_fd(client->context),
                       false, NULL, NULL, NULL, NULL, NULL);
    client->events = 0;
}

static void nfs_attach_aio_context(BlockDriverState *bs,
                                   AioContext *new_context)
{
    NFSClient *client = bs->opaque;

    client->aio_context = new_context;
    nfs_set_events(client);
}

static void nfs_client_close(NFSClient *client)
{
    if (client->context) {
        qemu_mutex_lock(&client->mutex);
        aio_set_fd_handler(client->aio_context, nfs_get_fd(client->context),
                           false, NULL, NULL, NULL, NULL, NULL);
        qemu_mutex_unlock(&client->mutex);
        if (client->fh) {
            nfs_close(client->context, client->fh);
            client->fh = NULL;
        }
#ifdef LIBNFS_FEATURE_UMOUNT
        nfs_umount(client->context);
#endif
        nfs_destroy_context(client->context);
        client->context = NULL;
    }
    g_free(client->path);
    qemu_mutex_destroy(&client->mutex);
    qapi_free_NFSServer(client->server);
    client->server = NULL;
}

static void nfs_file_close(BlockDriverState *bs)
{
    NFSClient *client = bs->opaque;
    nfs_client_close(client);
}

static int64_t nfs_client_open(NFSClient *client, BlockdevOptionsNfs *opts,
                               int flags, int open_flags, Error **errp)
{
    int64_t ret = -EINVAL;
#ifdef _WIN32
    struct __stat64 st;
#else
    struct stat st;
#endif
    char *file = NULL, *strp = NULL;

    qemu_mutex_init(&client->mutex);

    client->path = g_strdup(opts->path);

    strp = strrchr(client->path, '/');
    if (strp == NULL) {
        error_setg(errp, "Invalid URL specified");
        goto fail;
    }
    file = g_strdup(strp);
    *strp = 0;

    /* Steal the NFSServer object from opts; set the original pointer to NULL
     * to avoid use after free and double free. */
    client->server = opts->server;
    opts->server = NULL;

    client->context = nfs_init_context();
    if (client->context == NULL) {
        error_setg(errp, "Failed to init NFS context");
        goto fail;
    }

    if (opts->has_user) {
        client->uid = opts->user;
        nfs_set_uid(client->context, client->uid);
    }

    if (opts->has_group) {
        client->gid = opts->group;
        nfs_set_gid(client->context, client->gid);
    }

    if (opts->has_tcp_syn_count) {
        client->tcp_syncnt = opts->tcp_syn_count;
        nfs_set_tcp_syncnt(client->context, client->tcp_syncnt);
    }

#ifdef LIBNFS_FEATURE_READAHEAD
    if (opts->has_readahead_size) {
        if (open_flags & BDRV_O_NOCACHE) {
            error_setg(errp, "Cannot enable NFS readahead "
                             "if cache.direct = on");
            goto fail;
        }
        client->readahead = opts->readahead_size;
        if (client->readahead > QEMU_NFS_MAX_READAHEAD_SIZE) {
            warn_report("Truncating NFS readahead size to %d",
                        QEMU_NFS_MAX_READAHEAD_SIZE);
            client->readahead = QEMU_NFS_MAX_READAHEAD_SIZE;
        }
        nfs_set_readahead(client->context, client->readahead);
#ifdef LIBNFS_FEATURE_PAGECACHE
        nfs_set_pagecache_ttl(client->context, 0);
#endif
        client->cache_used = true;
    }
#endif

#ifdef LIBNFS_FEATURE_PAGECACHE
    if (opts->has_page_cache_size) {
        if (open_flags & BDRV_O_NOCACHE) {
            error_setg(errp, "Cannot enable NFS pagecache "
                             "if cache.direct = on");
            goto fail;
        }
        client->pagecache = opts->page_cache_size;
        if (client->pagecache > QEMU_NFS_MAX_PAGECACHE_SIZE) {
            warn_report("Truncating NFS pagecache size to %d pages",
                        QEMU_NFS_MAX_PAGECACHE_SIZE);
            client->pagecache = QEMU_NFS_MAX_PAGECACHE_SIZE;
        }
        nfs_set_pagecache(client->context, client->pagecache);
        nfs_set_pagecache_ttl(client->context, 0);
        client->cache_used = true;
    }
#endif

#ifdef LIBNFS_FEATURE_DEBUG
    if (opts->has_debug) {
        client->debug = opts->debug;
        /* limit the maximum debug level to avoid potential flooding
         * of our log files. */
        if (client->debug > QEMU_NFS_MAX_DEBUG_LEVEL) {
            warn_report("Limiting NFS debug level to %d",
                        QEMU_NFS_MAX_DEBUG_LEVEL);
            client->debug = QEMU_NFS_MAX_DEBUG_LEVEL;
        }
        nfs_set_debug(client->context, client->debug);
    }
#endif

    ret = nfs_mount(client->context, client->server->host, client->path);
    if (ret < 0) {
        error_setg(errp, "Failed to mount nfs share: %s",
                   nfs_get_error(client->context));
        goto fail;
    }

    if (flags & O_CREAT) {
        ret = nfs_creat(client->context, file, 0600, &client->fh);
        if (ret < 0) {
            error_setg(errp, "Failed to create file: %s",
                       nfs_get_error(client->context));
            goto fail;
        }
    } else {
        ret = nfs_open(client->context, file, flags, &client->fh);
        if (ret < 0) {
            error_setg(errp, "Failed to open file : %s",
                       nfs_get_error(client->context));
            goto fail;
        }
    }

    ret = nfs_fstat(client->context, client->fh, &st);
    if (ret < 0) {
        error_setg(errp, "Failed to fstat file: %s",
                   nfs_get_error(client->context));
        goto fail;
    }

    ret = DIV_ROUND_UP(st.st_size, BDRV_SECTOR_SIZE);
#if !defined(_WIN32)
    client->st_blocks = st.st_blocks;
#endif
    client->has_zero_init = S_ISREG(st.st_mode);
    *strp = '/';
    goto out;

fail:
    nfs_client_close(client);
out:
    g_free(file);
    return ret;
}

static BlockdevOptionsNfs *nfs_options_qdict_to_qapi(QDict *options,
                                                     Error **errp)
{
    BlockdevOptionsNfs *opts = NULL;
    Visitor *v;
    const QDictEntry *e;

    v = qobject_input_visitor_new_flat_confused(options, errp);
    if (!v) {
        return NULL;
    }

    visit_type_BlockdevOptionsNfs(v, NULL, &opts, errp);
    visit_free(v);
    if (!opts) {
        return NULL;
    }

    /* Remove the processed options from the QDict (the visitor processes
     * _all_ options in the QDict) */
    while ((e = qdict_first(options))) {
        qdict_del(options, e->key);
    }

    return opts;
}

static int64_t nfs_client_open_qdict(NFSClient *client, QDict *options,
                                     int flags, int open_flags, Error **errp)
{
    BlockdevOptionsNfs *opts;
    int64_t ret;

    opts = nfs_options_qdict_to_qapi(options, errp);
    if (opts == NULL) {
        ret = -EINVAL;
        goto fail;
    }

    ret = nfs_client_open(client, opts, flags, open_flags, errp);
fail:
    qapi_free_BlockdevOptionsNfs(opts);
    return ret;
}

static int nfs_file_open(BlockDriverState *bs, QDict *options, int flags,
                         Error **errp) {
    NFSClient *client = bs->opaque;
    int64_t ret;

    client->aio_context = bdrv_get_aio_context(bs);

    ret = nfs_client_open_qdict(client, options,
                                (flags & BDRV_O_RDWR) ? O_RDWR : O_RDONLY,
                                bs->open_flags, errp);
    if (ret < 0) {
        return ret;
    }

    bs->total_sectors = ret;
    if (client->has_zero_init) {
        bs->supported_truncate_flags = BDRV_REQ_ZERO_WRITE;
    }
    return 0;
}

static QemuOptsList nfs_create_opts = {
    .name = "nfs-create-opts",
    .head = QTAILQ_HEAD_INITIALIZER(nfs_create_opts.head),
    .desc = {
        {
            .name = BLOCK_OPT_SIZE,
            .type = QEMU_OPT_SIZE,
            .help = "Virtual disk size"
        },
        { /* end of list */ }
    }
};

static int nfs_file_co_create(BlockdevCreateOptions *options, Error **errp)
{
    BlockdevCreateOptionsNfs *opts = &options->u.nfs;
    NFSClient *client = g_new0(NFSClient, 1);
    int ret;

    assert(options->driver == BLOCKDEV_DRIVER_NFS);

    client->aio_context = qemu_get_aio_context();

    ret = nfs_client_open(client, opts->location, O_CREAT, 0, errp);
    if (ret < 0) {
        goto out;
    }
    ret = nfs_ftruncate(client->context, client->fh, opts->size);
    nfs_client_close(client);

out:
    g_free(client);
    return ret;
}

static int coroutine_fn nfs_file_co_create_opts(BlockDriver *drv,
                                                const char *url,
                                                QemuOpts *opts,
                                                Error **errp)
{
    BlockdevCreateOptions *create_options;
    BlockdevCreateOptionsNfs *nfs_opts;
    QDict *options;
    int ret;

    create_options = g_new0(BlockdevCreateOptions, 1);
    create_options->driver = BLOCKDEV_DRIVER_NFS;
    nfs_opts = &create_options->u.nfs;

    /* Read out options */
    nfs_opts->size = ROUND_UP(qemu_opt_get_size_del(opts, BLOCK_OPT_SIZE, 0),
                              BDRV_SECTOR_SIZE);

    options = qdict_new();
    ret = nfs_parse_uri(url, options, errp);
    if (ret < 0) {
        goto out;
    }

    nfs_opts->location = nfs_options_qdict_to_qapi(options, errp);
    if (nfs_opts->location == NULL) {
        ret = -EINVAL;
        goto out;
    }

    ret = nfs_file_co_create(create_options, errp);
    if (ret < 0) {
        goto out;
    }

    ret = 0;
out:
    qobject_unref(options);
    qapi_free_BlockdevCreateOptions(create_options);
    return ret;
}

static int nfs_has_zero_init(BlockDriverState *bs)
{
    NFSClient *client = bs->opaque;
    return client->has_zero_init;
}

#if !defined(_WIN32)
/* Called (via nfs_service) with QemuMutex held.  */
static void
nfs_get_allocated_file_size_cb(int ret, struct nfs_context *nfs, void *data,
                               void *private_data)
{
    NFSRPC *task = private_data;
    task->ret = ret;
    if (task->ret == 0) {
        memcpy(task->st, data, sizeof(struct stat));
    }
    if (task->ret < 0) {
        error_report("NFS Error: %s", nfs_get_error(nfs));
    }

    /* Set task->complete before reading bs->wakeup.  */
    qatomic_mb_set(&task->complete, 1);
    bdrv_wakeup(task->bs);
}

static int64_t nfs_get_allocated_file_size(BlockDriverState *bs)
{
    NFSClient *client = bs->opaque;
    NFSRPC task = {0};
    struct stat st;

    if (bdrv_is_read_only(bs) &&
        !(bs->open_flags & BDRV_O_NOCACHE)) {
        return client->st_blocks * 512;
    }

    task.bs = bs;
    task.st = &st;
    if (nfs_fstat_async(client->context, client->fh, nfs_get_allocated_file_size_cb,
                        &task) != 0) {
        return -ENOMEM;
    }

    nfs_set_events(client);
    BDRV_POLL_WHILE(bs, !task.complete);

    return (task.ret < 0 ? task.ret : st.st_blocks * 512);
}
#endif

static int coroutine_fn
nfs_file_co_truncate(BlockDriverState *bs, int64_t offset, bool exact,
                     PreallocMode prealloc, BdrvRequestFlags flags,
                     Error **errp)
{
    NFSClient *client = bs->opaque;
    int ret;

    if (prealloc != PREALLOC_MODE_OFF) {
        error_setg(errp, "Unsupported preallocation mode '%s'",
                   PreallocMode_str(prealloc));
        return -ENOTSUP;
    }

    ret = nfs_ftruncate(client->context, client->fh, offset);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Failed to truncate file");
        return ret;
    }

    return 0;
}

/* Note that this will not re-establish a connection with the NFS server
 * - it is effectively a NOP.  */
static int nfs_reopen_prepare(BDRVReopenState *state,
                              BlockReopenQueue *queue, Error **errp)
{
    NFSClient *client = state->bs->opaque;
#ifdef _WIN32
    struct __stat64 st;
#else
    struct stat st;
#endif
    int ret = 0;

    if (state->flags & BDRV_O_RDWR && bdrv_is_read_only(state->bs)) {
        error_setg(errp, "Cannot open a read-only mount as read-write");
        return -EACCES;
    }

    if ((state->flags & BDRV_O_NOCACHE) && client->cache_used) {
        error_setg(errp, "Cannot disable cache if libnfs readahead or"
                         " pagecache is enabled");
        return -EINVAL;
    }

    /* Update cache for read-only reopens */
    if (!(state->flags & BDRV_O_RDWR)) {
        ret = nfs_fstat(client->context, client->fh, &st);
        if (ret < 0) {
            error_setg(errp, "Failed to fstat file: %s",
                       nfs_get_error(client->context));
            return ret;
        }
#if !defined(_WIN32)
        client->st_blocks = st.st_blocks;
#endif
    }

    return 0;
}

static void nfs_refresh_filename(BlockDriverState *bs)
{
    NFSClient *client = bs->opaque;

    if (client->uid && !client->gid) {
        snprintf(bs->exact_filename, sizeof(bs->exact_filename),
                 "nfs://%s%s?uid=%" PRId64, client->server->host, client->path,
                 client->uid);
    } else if (!client->uid && client->gid) {
        snprintf(bs->exact_filename, sizeof(bs->exact_filename),
                 "nfs://%s%s?gid=%" PRId64, client->server->host, client->path,
                 client->gid);
    } else if (client->uid && client->gid) {
        snprintf(bs->exact_filename, sizeof(bs->exact_filename),
                 "nfs://%s%s?uid=%" PRId64 "&gid=%" PRId64,
                 client->server->host, client->path, client->uid, client->gid);
    } else {
        snprintf(bs->exact_filename, sizeof(bs->exact_filename),
                 "nfs://%s%s", client->server->host, client->path);
    }
}

static char *nfs_dirname(BlockDriverState *bs, Error **errp)
{
    NFSClient *client = bs->opaque;

    if (client->uid || client->gid) {
        bdrv_refresh_filename(bs);
        error_setg(errp, "Cannot generate a base directory for NFS node '%s'",
                   bs->filename);
        return NULL;
    }

    return g_strdup_printf("nfs://%s%s/", client->server->host, client->path);
}

#ifdef LIBNFS_FEATURE_PAGECACHE
static void coroutine_fn nfs_co_invalidate_cache(BlockDriverState *bs,
                                                 Error **errp)
{
    NFSClient *client = bs->opaque;
    nfs_pagecache_invalidate(client->context, client->fh);
}
#endif

static const char *nfs_strong_runtime_opts[] = {
    "path",
    "user",
    "group",
    "server.",

    NULL
};

static BlockDriver bdrv_nfs = {
    .format_name                    = "nfs",
    .protocol_name                  = "nfs",

    .instance_size                  = sizeof(NFSClient),
    .bdrv_parse_filename            = nfs_parse_filename,
    .create_opts                    = &nfs_create_opts,

    .bdrv_has_zero_init             = nfs_has_zero_init,
/* libnfs does not provide the allocated filesize of a file on win32. */
#if !defined(_WIN32)
    .bdrv_get_allocated_file_size   = nfs_get_allocated_file_size,
#endif
    .bdrv_co_truncate               = nfs_file_co_truncate,

    .bdrv_file_open                 = nfs_file_open,
    .bdrv_close                     = nfs_file_close,
    .bdrv_co_create                 = nfs_file_co_create,
    .bdrv_co_create_opts            = nfs_file_co_create_opts,
    .bdrv_reopen_prepare            = nfs_reopen_prepare,

    .bdrv_co_preadv                 = nfs_co_preadv,
    .bdrv_co_pwritev                = nfs_co_pwritev,
    .bdrv_co_flush_to_disk          = nfs_co_flush,

    .bdrv_detach_aio_context        = nfs_detach_aio_context,
    .bdrv_attach_aio_context        = nfs_attach_aio_context,
    .bdrv_refresh_filename          = nfs_refresh_filename,
    .bdrv_dirname                   = nfs_dirname,

    .strong_runtime_opts            = nfs_strong_runtime_opts,

#ifdef LIBNFS_FEATURE_PAGECACHE
    .bdrv_co_invalidate_cache       = nfs_co_invalidate_cache,
#endif
};

static void nfs_block_init(void)
{
    bdrv_register(&bdrv_nfs);
}

block_init(nfs_block_init);

/*
 * QEMU Block driver for native access to files on NFS shares
 *
 * Copyright (c) 2014 Peter Lieven <pl@kamp.de>
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

#include "config-host.h"

#include <poll.h>
#include "qemu-common.h"
#include "qemu/config-file.h"
#include "qemu/error-report.h"
#include "block/block_int.h"
#include "trace.h"
#include "qemu/iov.h"
#include "qemu/uri.h"
#include "sysemu/sysemu.h"
#include <nfsc/libnfs.h>

typedef struct NFSClient {
    struct nfs_context *context;
    struct nfsfh *fh;
    int events;
    bool has_zero_init;
    AioContext *aio_context;
} NFSClient;

typedef struct NFSRPC {
    int ret;
    int complete;
    QEMUIOVector *iov;
    struct stat *st;
    Coroutine *co;
    QEMUBH *bh;
    NFSClient *client;
} NFSRPC;

static void nfs_process_read(void *arg);
static void nfs_process_write(void *arg);

static void nfs_set_events(NFSClient *client)
{
    int ev = nfs_which_events(client->context);
    if (ev != client->events) {
        aio_set_fd_handler(client->aio_context,
                           nfs_get_fd(client->context),
                           (ev & POLLIN) ? nfs_process_read : NULL,
                           (ev & POLLOUT) ? nfs_process_write : NULL,
                           client);

    }
    client->events = ev;
}

static void nfs_process_read(void *arg)
{
    NFSClient *client = arg;
    nfs_service(client->context, POLLIN);
    nfs_set_events(client);
}

static void nfs_process_write(void *arg)
{
    NFSClient *client = arg;
    nfs_service(client->context, POLLOUT);
    nfs_set_events(client);
}

static void nfs_co_init_task(NFSClient *client, NFSRPC *task)
{
    *task = (NFSRPC) {
        .co             = qemu_coroutine_self(),
        .client         = client,
    };
}

static void nfs_co_generic_bh_cb(void *opaque)
{
    NFSRPC *task = opaque;
    task->complete = 1;
    qemu_bh_delete(task->bh);
    qemu_coroutine_enter(task->co, NULL);
}

static void
nfs_co_generic_cb(int ret, struct nfs_context *nfs, void *data,
                  void *private_data)
{
    NFSRPC *task = private_data;
    task->ret = ret;
    if (task->ret > 0 && task->iov) {
        if (task->ret <= task->iov->size) {
            qemu_iovec_from_buf(task->iov, 0, data, task->ret);
        } else {
            task->ret = -EIO;
        }
    }
    if (task->ret == 0 && task->st) {
        memcpy(task->st, data, sizeof(struct stat));
    }
    if (task->ret < 0) {
        error_report("NFS Error: %s", nfs_get_error(nfs));
    }
    if (task->co) {
        task->bh = aio_bh_new(task->client->aio_context,
                              nfs_co_generic_bh_cb, task);
        qemu_bh_schedule(task->bh);
    } else {
        task->complete = 1;
    }
}

static int coroutine_fn nfs_co_readv(BlockDriverState *bs,
                                     int64_t sector_num, int nb_sectors,
                                     QEMUIOVector *iov)
{
    NFSClient *client = bs->opaque;
    NFSRPC task;

    nfs_co_init_task(client, &task);
    task.iov = iov;

    if (nfs_pread_async(client->context, client->fh,
                        sector_num * BDRV_SECTOR_SIZE,
                        nb_sectors * BDRV_SECTOR_SIZE,
                        nfs_co_generic_cb, &task) != 0) {
        return -ENOMEM;
    }

    while (!task.complete) {
        nfs_set_events(client);
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

static int coroutine_fn nfs_co_writev(BlockDriverState *bs,
                                        int64_t sector_num, int nb_sectors,
                                        QEMUIOVector *iov)
{
    NFSClient *client = bs->opaque;
    NFSRPC task;
    char *buf = NULL;

    nfs_co_init_task(client, &task);

    buf = g_try_malloc(nb_sectors * BDRV_SECTOR_SIZE);
    if (nb_sectors && buf == NULL) {
        return -ENOMEM;
    }

    qemu_iovec_to_buf(iov, 0, buf, nb_sectors * BDRV_SECTOR_SIZE);

    if (nfs_pwrite_async(client->context, client->fh,
                         sector_num * BDRV_SECTOR_SIZE,
                         nb_sectors * BDRV_SECTOR_SIZE,
                         buf, nfs_co_generic_cb, &task) != 0) {
        g_free(buf);
        return -ENOMEM;
    }

    while (!task.complete) {
        nfs_set_events(client);
        qemu_coroutine_yield();
    }

    g_free(buf);

    if (task.ret != nb_sectors * BDRV_SECTOR_SIZE) {
        return task.ret < 0 ? task.ret : -EIO;
    }

    return 0;
}

static int coroutine_fn nfs_co_flush(BlockDriverState *bs)
{
    NFSClient *client = bs->opaque;
    NFSRPC task;

    nfs_co_init_task(client, &task);

    if (nfs_fsync_async(client->context, client->fh, nfs_co_generic_cb,
                        &task) != 0) {
        return -ENOMEM;
    }

    while (!task.complete) {
        nfs_set_events(client);
        qemu_coroutine_yield();
    }

    return task.ret;
}

/* TODO Convert to fine grained options */
static QemuOptsList runtime_opts = {
    .name = "nfs",
    .head = QTAILQ_HEAD_INITIALIZER(runtime_opts.head),
    .desc = {
        {
            .name = "filename",
            .type = QEMU_OPT_STRING,
            .help = "URL to the NFS file",
        },
        { /* end of list */ }
    },
};

static void nfs_detach_aio_context(BlockDriverState *bs)
{
    NFSClient *client = bs->opaque;

    aio_set_fd_handler(client->aio_context,
                       nfs_get_fd(client->context),
                       NULL, NULL, NULL);
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
        if (client->fh) {
            nfs_close(client->context, client->fh);
        }
        aio_set_fd_handler(client->aio_context,
                           nfs_get_fd(client->context),
                           NULL, NULL, NULL);
        nfs_destroy_context(client->context);
    }
    memset(client, 0, sizeof(NFSClient));
}

static void nfs_file_close(BlockDriverState *bs)
{
    NFSClient *client = bs->opaque;
    nfs_client_close(client);
}

static int64_t nfs_client_open(NFSClient *client, const char *filename,
                               int flags, Error **errp)
{
    int ret = -EINVAL, i;
    struct stat st;
    URI *uri;
    QueryParams *qp = NULL;
    char *file = NULL, *strp = NULL;

    uri = uri_parse(filename);
    if (!uri) {
        error_setg(errp, "Invalid URL specified");
        goto fail;
    }
    if (!uri->server) {
        error_setg(errp, "Invalid URL specified");
        goto fail;
    }
    strp = strrchr(uri->path, '/');
    if (strp == NULL) {
        error_setg(errp, "Invalid URL specified");
        goto fail;
    }
    file = g_strdup(strp);
    *strp = 0;

    client->context = nfs_init_context();
    if (client->context == NULL) {
        error_setg(errp, "Failed to init NFS context");
        goto fail;
    }

    qp = query_params_parse(uri->query);
    for (i = 0; i < qp->n; i++) {
        unsigned long long val;
        if (!qp->p[i].value) {
            error_setg(errp, "Value for NFS parameter expected: %s",
                       qp->p[i].name);
            goto fail;
        }
        if (parse_uint_full(qp->p[i].value, &val, 0)) {
            error_setg(errp, "Illegal value for NFS parameter: %s",
                       qp->p[i].name);
            goto fail;
        }
        if (!strcmp(qp->p[i].name, "uid")) {
            nfs_set_uid(client->context, val);
        } else if (!strcmp(qp->p[i].name, "gid")) {
            nfs_set_gid(client->context, val);
        } else if (!strcmp(qp->p[i].name, "tcp-syncnt")) {
            nfs_set_tcp_syncnt(client->context, val);
#ifdef LIBNFS_FEATURE_READAHEAD
        } else if (!strcmp(qp->p[i].name, "readahead")) {
            nfs_set_readahead(client->context, val);
#endif
        } else {
            error_setg(errp, "Unknown NFS parameter name: %s",
                       qp->p[i].name);
            goto fail;
        }
    }

    ret = nfs_mount(client->context, uri->server, uri->path);
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
    client->has_zero_init = S_ISREG(st.st_mode);
    goto out;
fail:
    nfs_client_close(client);
out:
    if (qp) {
        query_params_free(qp);
    }
    uri_free(uri);
    g_free(file);
    return ret;
}

static int nfs_file_open(BlockDriverState *bs, QDict *options, int flags,
                         Error **errp) {
    NFSClient *client = bs->opaque;
    int64_t ret;
    QemuOpts *opts;
    Error *local_err = NULL;

    client->aio_context = bdrv_get_aio_context(bs);

    opts = qemu_opts_create(&runtime_opts, NULL, 0, &error_abort);
    qemu_opts_absorb_qdict(opts, options, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        ret = -EINVAL;
        goto out;
    }
    ret = nfs_client_open(client, qemu_opt_get(opts, "filename"),
                          (flags & BDRV_O_RDWR) ? O_RDWR : O_RDONLY,
                          errp);
    if (ret < 0) {
        goto out;
    }
    bs->total_sectors = ret;
    ret = 0;
out:
    qemu_opts_del(opts);
    return ret;
}

static int nfs_file_create(const char *url, QemuOpts *opts, Error **errp)
{
    int ret = 0;
    int64_t total_size = 0;
    NFSClient *client = g_new0(NFSClient, 1);

    client->aio_context = qemu_get_aio_context();

    /* Read out options */
    total_size = ROUND_UP(qemu_opt_get_size_del(opts, BLOCK_OPT_SIZE, 0),
                          BDRV_SECTOR_SIZE);

    ret = nfs_client_open(client, url, O_CREAT, errp);
    if (ret < 0) {
        goto out;
    }
    ret = nfs_ftruncate(client->context, client->fh, total_size);
    nfs_client_close(client);
out:
    g_free(client);
    return ret;
}

static int nfs_has_zero_init(BlockDriverState *bs)
{
    NFSClient *client = bs->opaque;
    return client->has_zero_init;
}

static int64_t nfs_get_allocated_file_size(BlockDriverState *bs)
{
    NFSClient *client = bs->opaque;
    NFSRPC task = {0};
    struct stat st;

    task.st = &st;
    if (nfs_fstat_async(client->context, client->fh, nfs_co_generic_cb,
                        &task) != 0) {
        return -ENOMEM;
    }

    while (!task.complete) {
        nfs_set_events(client);
        aio_poll(client->aio_context, true);
    }

    return (task.ret < 0 ? task.ret : st.st_blocks * st.st_blksize);
}

static int nfs_file_truncate(BlockDriverState *bs, int64_t offset)
{
    NFSClient *client = bs->opaque;
    return nfs_ftruncate(client->context, client->fh, offset);
}

static BlockDriver bdrv_nfs = {
    .format_name                    = "nfs",
    .protocol_name                  = "nfs",

    .instance_size                  = sizeof(NFSClient),
    .bdrv_needs_filename            = true,
    .bdrv_has_zero_init             = nfs_has_zero_init,
    .bdrv_get_allocated_file_size   = nfs_get_allocated_file_size,
    .bdrv_truncate                  = nfs_file_truncate,

    .bdrv_file_open                 = nfs_file_open,
    .bdrv_close                     = nfs_file_close,
    .bdrv_create                    = nfs_file_create,

    .bdrv_co_readv                  = nfs_co_readv,
    .bdrv_co_writev                 = nfs_co_writev,
    .bdrv_co_flush_to_disk          = nfs_co_flush,

    .bdrv_detach_aio_context        = nfs_detach_aio_context,
    .bdrv_attach_aio_context        = nfs_attach_aio_context,
};

static void nfs_block_init(void)
{
    bdrv_register(&bdrv_nfs);
}

block_init(nfs_block_init);

/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * libblkio BlockDriver
 *
 * Copyright Red Hat, Inc.
 *
 * Author:
 *   Stefan Hajnoczi <stefanha@redhat.com>
 */

#include "qemu/osdep.h"
#include <blkio.h>
#include "block/block_int.h"
#include "exec/memory.h"
#include "exec/cpu-common.h" /* for qemu_ram_get_fd() */
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qapi/qmp/qdict.h"
#include "qemu/module.h"
#include "exec/memory.h" /* for ram_block_discard_disable() */

#include "block/block-io.h"

/*
 * Keep the QEMU BlockDriver names identical to the libblkio driver names.
 * Using macros instead of typing out the string literals avoids typos.
 */
#define DRIVER_IO_URING "io_uring"
#define DRIVER_NVME_IO_URING "nvme-io_uring"
#define DRIVER_VIRTIO_BLK_VFIO_PCI "virtio-blk-vfio-pci"
#define DRIVER_VIRTIO_BLK_VHOST_USER "virtio-blk-vhost-user"
#define DRIVER_VIRTIO_BLK_VHOST_VDPA "virtio-blk-vhost-vdpa"

/*
 * Allocated bounce buffers are kept in a list sorted by buffer address.
 */
typedef struct BlkioBounceBuf {
    QLIST_ENTRY(BlkioBounceBuf) next;

    /* The bounce buffer */
    struct iovec buf;
} BlkioBounceBuf;

typedef struct {
    /*
     * libblkio is not thread-safe so this lock protects ->blkio and
     * ->blkioq.
     */
    QemuMutex blkio_lock;
    struct blkio *blkio;
    struct blkioq *blkioq; /* make this multi-queue in the future... */
    int completion_fd;

    /*
     * Polling fetches the next completion into this field.
     *
     * No lock is necessary since only one thread calls aio_poll() and invokes
     * fd and poll handlers.
     */
    struct blkio_completion poll_completion;

    /*
     * Protects ->bounce_pool, ->bounce_bufs, ->bounce_available.
     *
     * Lock ordering: ->bounce_lock before ->blkio_lock.
     */
    CoMutex bounce_lock;

    /* Bounce buffer pool */
    struct blkio_mem_region bounce_pool;

    /* Sorted list of allocated bounce buffers */
    QLIST_HEAD(, BlkioBounceBuf) bounce_bufs;

    /* Queue for coroutines waiting for bounce buffer space */
    CoQueue bounce_available;

    /* The value of the "mem-region-alignment" property */
    size_t mem_region_alignment;

    /* Can we skip adding/deleting blkio_mem_regions? */
    bool needs_mem_regions;

    /* Are file descriptors necessary for blkio_mem_regions? */
    bool needs_mem_region_fd;

    /* Are madvise(MADV_DONTNEED)-style operations unavailable? */
    bool may_pin_mem_regions;
} BDRVBlkioState;

/* Called with s->bounce_lock held */
static int blkio_resize_bounce_pool(BDRVBlkioState *s, int64_t bytes)
{
    /* There can be no allocated bounce buffers during resize */
    assert(QLIST_EMPTY(&s->bounce_bufs));

    /* Pad size to reduce frequency of resize calls */
    bytes += 128 * 1024;

    WITH_QEMU_LOCK_GUARD(&s->blkio_lock) {
        int ret;

        if (s->bounce_pool.addr) {
            blkio_unmap_mem_region(s->blkio, &s->bounce_pool);
            blkio_free_mem_region(s->blkio, &s->bounce_pool);
            memset(&s->bounce_pool, 0, sizeof(s->bounce_pool));
        }

        /* Automatically freed when s->blkio is destroyed */
        ret = blkio_alloc_mem_region(s->blkio, &s->bounce_pool, bytes);
        if (ret < 0) {
            return ret;
        }

        ret = blkio_map_mem_region(s->blkio, &s->bounce_pool);
        if (ret < 0) {
            blkio_free_mem_region(s->blkio, &s->bounce_pool);
            memset(&s->bounce_pool, 0, sizeof(s->bounce_pool));
            return ret;
        }
    }

    return 0;
}

/* Called with s->bounce_lock held */
static bool
blkio_do_alloc_bounce_buffer(BDRVBlkioState *s, BlkioBounceBuf *bounce,
                             int64_t bytes)
{
    void *addr = s->bounce_pool.addr;
    BlkioBounceBuf *cur = NULL;
    BlkioBounceBuf *prev = NULL;
    ptrdiff_t space;

    /*
     * This is just a linear search over the holes between requests. An
     * efficient allocator would be nice.
     */
    QLIST_FOREACH(cur, &s->bounce_bufs, next) {
        space = cur->buf.iov_base - addr;
        if (bytes <= space) {
            QLIST_INSERT_BEFORE(cur, bounce, next);
            bounce->buf.iov_base = addr;
            bounce->buf.iov_len = bytes;
            return true;
        }

        addr = cur->buf.iov_base + cur->buf.iov_len;
        prev = cur;
    }

    /* Is there space after the last request? */
    space = s->bounce_pool.addr + s->bounce_pool.len - addr;
    if (bytes > space) {
        return false;
    }
    if (prev) {
        QLIST_INSERT_AFTER(prev, bounce, next);
    } else {
        QLIST_INSERT_HEAD(&s->bounce_bufs, bounce, next);
    }
    bounce->buf.iov_base = addr;
    bounce->buf.iov_len = bytes;
    return true;
}

static int coroutine_fn
blkio_alloc_bounce_buffer(BDRVBlkioState *s, BlkioBounceBuf *bounce,
                          int64_t bytes)
{
    /*
     * Ensure fairness: first time around we join the back of the queue,
     * subsequently we join the front so we don't lose our place.
     */
    CoQueueWaitFlags wait_flags = 0;

    QEMU_LOCK_GUARD(&s->bounce_lock);

    /* Ensure fairness: don't even try if other requests are already waiting */
    if (!qemu_co_queue_empty(&s->bounce_available)) {
        qemu_co_queue_wait_flags(&s->bounce_available, &s->bounce_lock,
                                 wait_flags);
        wait_flags = CO_QUEUE_WAIT_FRONT;
    }

    while (true) {
        if (blkio_do_alloc_bounce_buffer(s, bounce, bytes)) {
            /* Kick the next queued request since there may be space */
            qemu_co_queue_next(&s->bounce_available);
            return 0;
        }

        /*
         * If there are no in-flight requests then the pool was simply too
         * small.
         */
        if (QLIST_EMPTY(&s->bounce_bufs)) {
            bool ok;
            int ret;

            ret = blkio_resize_bounce_pool(s, bytes);
            if (ret < 0) {
                /* Kick the next queued request since that may fail too */
                qemu_co_queue_next(&s->bounce_available);
                return ret;
            }

            ok = blkio_do_alloc_bounce_buffer(s, bounce, bytes);
            assert(ok); /* must have space this time */
            return 0;
        }

        qemu_co_queue_wait_flags(&s->bounce_available, &s->bounce_lock,
                                 wait_flags);
        wait_flags = CO_QUEUE_WAIT_FRONT;
    }
}

static void coroutine_fn blkio_free_bounce_buffer(BDRVBlkioState *s,
                                                  BlkioBounceBuf *bounce)
{
    QEMU_LOCK_GUARD(&s->bounce_lock);

    QLIST_REMOVE(bounce, next);

    /* Wake up waiting coroutines since space may now be available */
    qemu_co_queue_next(&s->bounce_available);
}

/* For async to .bdrv_co_*() conversion */
typedef struct {
    Coroutine *coroutine;
    int ret;
} BlkioCoData;

static void blkio_completion_fd_read(void *opaque)
{
    BlockDriverState *bs = opaque;
    BDRVBlkioState *s = bs->opaque;
    uint64_t val;
    int ret;

    /* Polling may have already fetched a completion */
    if (s->poll_completion.user_data != NULL) {
        BlkioCoData *cod = s->poll_completion.user_data;
        cod->ret = s->poll_completion.ret;

        /* Clear it in case aio_co_wake() enters a nested event loop */
        s->poll_completion.user_data = NULL;

        aio_co_wake(cod->coroutine);
    }

    /* Reset completion fd status */
    ret = read(s->completion_fd, &val, sizeof(val));

    /* Ignore errors, there's nothing we can do */
    (void)ret;

    /*
     * Reading one completion at a time makes nested event loop re-entrancy
     * simple. Change this loop to get multiple completions in one go if it
     * becomes a performance bottleneck.
     */
    while (true) {
        struct blkio_completion completion;

        WITH_QEMU_LOCK_GUARD(&s->blkio_lock) {
            ret = blkioq_do_io(s->blkioq, &completion, 0, 1, NULL);
        }
        if (ret != 1) {
            break;
        }

        BlkioCoData *cod = completion.user_data;
        cod->ret = completion.ret;
        aio_co_wake(cod->coroutine);
    }
}

static bool blkio_completion_fd_poll(void *opaque)
{
    BlockDriverState *bs = opaque;
    BDRVBlkioState *s = bs->opaque;
    int ret;

    /* Just in case we already fetched a completion */
    if (s->poll_completion.user_data != NULL) {
        return true;
    }

    WITH_QEMU_LOCK_GUARD(&s->blkio_lock) {
        ret = blkioq_do_io(s->blkioq, &s->poll_completion, 0, 1, NULL);
    }
    return ret == 1;
}

static void blkio_completion_fd_poll_ready(void *opaque)
{
    blkio_completion_fd_read(opaque);
}

static void blkio_attach_aio_context(BlockDriverState *bs,
                                     AioContext *new_context)
{
    BDRVBlkioState *s = bs->opaque;

    aio_set_fd_handler(new_context,
                       s->completion_fd,
                       false,
                       blkio_completion_fd_read,
                       NULL,
                       blkio_completion_fd_poll,
                       blkio_completion_fd_poll_ready,
                       bs);
}

static void blkio_detach_aio_context(BlockDriverState *bs)
{
    BDRVBlkioState *s = bs->opaque;

    aio_set_fd_handler(bdrv_get_aio_context(bs),
                       s->completion_fd,
                       false, NULL, NULL, NULL, NULL, NULL);
}

/* Call with s->blkio_lock held to submit I/O after enqueuing a new request */
static void blkio_submit_io(BlockDriverState *bs)
{
    if (qatomic_read(&bs->io_plugged) == 0) {
        BDRVBlkioState *s = bs->opaque;

        blkioq_do_io(s->blkioq, NULL, 0, 0, NULL);
    }
}

static int coroutine_fn
blkio_co_pdiscard(BlockDriverState *bs, int64_t offset, int64_t bytes)
{
    BDRVBlkioState *s = bs->opaque;
    BlkioCoData cod = {
        .coroutine = qemu_coroutine_self(),
    };

    WITH_QEMU_LOCK_GUARD(&s->blkio_lock) {
        blkioq_discard(s->blkioq, offset, bytes, &cod, 0);
        blkio_submit_io(bs);
    }

    qemu_coroutine_yield();
    return cod.ret;
}

static int coroutine_fn
blkio_co_preadv(BlockDriverState *bs, int64_t offset, int64_t bytes,
                QEMUIOVector *qiov, BdrvRequestFlags flags)
{
    BlkioCoData cod = {
        .coroutine = qemu_coroutine_self(),
    };
    BDRVBlkioState *s = bs->opaque;
    bool use_bounce_buffer =
        s->needs_mem_regions && !(flags & BDRV_REQ_REGISTERED_BUF);
    BlkioBounceBuf bounce;
    struct iovec *iov = qiov->iov;
    int iovcnt = qiov->niov;

    if (use_bounce_buffer) {
        int ret = blkio_alloc_bounce_buffer(s, &bounce, bytes);
        if (ret < 0) {
            return ret;
        }

        iov = &bounce.buf;
        iovcnt = 1;
    }

    WITH_QEMU_LOCK_GUARD(&s->blkio_lock) {
        blkioq_readv(s->blkioq, offset, iov, iovcnt, &cod, 0);
        blkio_submit_io(bs);
    }

    qemu_coroutine_yield();

    if (use_bounce_buffer) {
        if (cod.ret == 0) {
            qemu_iovec_from_buf(qiov, 0,
                                bounce.buf.iov_base,
                                bounce.buf.iov_len);
        }

        blkio_free_bounce_buffer(s, &bounce);
    }

    return cod.ret;
}

static int coroutine_fn blkio_co_pwritev(BlockDriverState *bs, int64_t offset,
        int64_t bytes, QEMUIOVector *qiov, BdrvRequestFlags flags)
{
    uint32_t blkio_flags = (flags & BDRV_REQ_FUA) ? BLKIO_REQ_FUA : 0;
    BlkioCoData cod = {
        .coroutine = qemu_coroutine_self(),
    };
    BDRVBlkioState *s = bs->opaque;
    bool use_bounce_buffer =
        s->needs_mem_regions && !(flags & BDRV_REQ_REGISTERED_BUF);
    BlkioBounceBuf bounce;
    struct iovec *iov = qiov->iov;
    int iovcnt = qiov->niov;

    if (use_bounce_buffer) {
        int ret = blkio_alloc_bounce_buffer(s, &bounce, bytes);
        if (ret < 0) {
            return ret;
        }

        qemu_iovec_to_buf(qiov, 0, bounce.buf.iov_base, bytes);
        iov = &bounce.buf;
        iovcnt = 1;
    }

    WITH_QEMU_LOCK_GUARD(&s->blkio_lock) {
        blkioq_writev(s->blkioq, offset, iov, iovcnt, &cod, blkio_flags);
        blkio_submit_io(bs);
    }

    qemu_coroutine_yield();

    if (use_bounce_buffer) {
        blkio_free_bounce_buffer(s, &bounce);
    }

    return cod.ret;
}

static int coroutine_fn blkio_co_flush(BlockDriverState *bs)
{
    BDRVBlkioState *s = bs->opaque;
    BlkioCoData cod = {
        .coroutine = qemu_coroutine_self(),
    };

    WITH_QEMU_LOCK_GUARD(&s->blkio_lock) {
        blkioq_flush(s->blkioq, &cod, 0);
        blkio_submit_io(bs);
    }

    qemu_coroutine_yield();
    return cod.ret;
}

static int coroutine_fn blkio_co_pwrite_zeroes(BlockDriverState *bs,
    int64_t offset, int64_t bytes, BdrvRequestFlags flags)
{
    BDRVBlkioState *s = bs->opaque;
    BlkioCoData cod = {
        .coroutine = qemu_coroutine_self(),
    };
    uint32_t blkio_flags = 0;

    if (flags & BDRV_REQ_FUA) {
        blkio_flags |= BLKIO_REQ_FUA;
    }
    if (!(flags & BDRV_REQ_MAY_UNMAP)) {
        blkio_flags |= BLKIO_REQ_NO_UNMAP;
    }
    if (flags & BDRV_REQ_NO_FALLBACK) {
        blkio_flags |= BLKIO_REQ_NO_FALLBACK;
    }

    WITH_QEMU_LOCK_GUARD(&s->blkio_lock) {
        blkioq_write_zeroes(s->blkioq, offset, bytes, &cod, blkio_flags);
        blkio_submit_io(bs);
    }

    qemu_coroutine_yield();
    return cod.ret;
}

static void coroutine_fn blkio_co_io_unplug(BlockDriverState *bs)
{
    BDRVBlkioState *s = bs->opaque;

    WITH_QEMU_LOCK_GUARD(&s->blkio_lock) {
        blkio_submit_io(bs);
    }
}

typedef enum {
    BMRR_OK,
    BMRR_SKIP,
    BMRR_FAIL,
} BlkioMemRegionResult;

/*
 * Produce a struct blkio_mem_region for a given address and size.
 *
 * This function produces identical results when called multiple times with the
 * same arguments. This property is necessary because blkio_unmap_mem_region()
 * must receive the same struct blkio_mem_region field values that were passed
 * to blkio_map_mem_region().
 */
static BlkioMemRegionResult
blkio_mem_region_from_host(BlockDriverState *bs,
                           void *host, size_t size,
                           struct blkio_mem_region *region,
                           Error **errp)
{
    BDRVBlkioState *s = bs->opaque;
    int fd = -1;
    ram_addr_t fd_offset = 0;

    if (((uintptr_t)host | size) % s->mem_region_alignment) {
        error_setg(errp, "unaligned buf %p with size %zu", host, size);
        return BMRR_FAIL;
    }

    /* Attempt to find the fd for the underlying memory */
    if (s->needs_mem_region_fd) {
        RAMBlock *ram_block;
        RAMBlock *end_block;
        ram_addr_t offset;

        /*
         * bdrv_register_buf() is called with the BQL held so mr lives at least
         * until this function returns.
         */
        ram_block = qemu_ram_block_from_host(host, false, &fd_offset);
        if (ram_block) {
            fd = qemu_ram_get_fd(ram_block);
        }
        if (fd == -1) {
            /*
             * Ideally every RAMBlock would have an fd. pc-bios and other
             * things don't. Luckily they are usually not I/O buffers and we
             * can just ignore them.
             */
            return BMRR_SKIP;
        }

        /* Make sure the fd covers the entire range */
        end_block = qemu_ram_block_from_host(host + size - 1, false, &offset);
        if (ram_block != end_block) {
            error_setg(errp, "registered buffer at %p with size %zu extends "
                       "beyond RAMBlock", host, size);
            return BMRR_FAIL;
        }
    }

    *region = (struct blkio_mem_region){
        .addr = host,
        .len = size,
        .fd = fd,
        .fd_offset = fd_offset,
    };
    return BMRR_OK;
}

static bool blkio_register_buf(BlockDriverState *bs, void *host, size_t size,
                               Error **errp)
{
    BDRVBlkioState *s = bs->opaque;
    struct blkio_mem_region region;
    BlkioMemRegionResult region_result;
    int ret;

    /*
     * Mapping memory regions conflicts with RAM discard (virtio-mem) when
     * there is pinning, so only do it when necessary.
     */
    if (!s->needs_mem_regions && s->may_pin_mem_regions) {
        return true;
    }

    region_result = blkio_mem_region_from_host(bs, host, size, &region, errp);
    if (region_result == BMRR_SKIP) {
        return true;
    } else if (region_result != BMRR_OK) {
        return false;
    }

    WITH_QEMU_LOCK_GUARD(&s->blkio_lock) {
        ret = blkio_map_mem_region(s->blkio, &region);
    }

    if (ret < 0) {
        error_setg(errp, "Failed to add blkio mem region %p with size %zu: %s",
                   host, size, blkio_get_error_msg());
        return false;
    }
    return true;
}

static void blkio_unregister_buf(BlockDriverState *bs, void *host, size_t size)
{
    BDRVBlkioState *s = bs->opaque;
    struct blkio_mem_region region;

    /* See blkio_register_buf() */
    if (!s->needs_mem_regions && s->may_pin_mem_regions) {
        return;
    }

    if (blkio_mem_region_from_host(bs, host, size, &region, NULL) != BMRR_OK) {
        return;
    }

    WITH_QEMU_LOCK_GUARD(&s->blkio_lock) {
        blkio_unmap_mem_region(s->blkio, &region);
    }
}

static int blkio_io_uring_open(BlockDriverState *bs, QDict *options, int flags,
                               Error **errp)
{
    const char *filename = qdict_get_str(options, "filename");
    BDRVBlkioState *s = bs->opaque;
    int ret;

    ret = blkio_set_str(s->blkio, "path", filename);
    qdict_del(options, "filename");
    if (ret < 0) {
        error_setg_errno(errp, -ret, "failed to set path: %s",
                         blkio_get_error_msg());
        return ret;
    }

    if (flags & BDRV_O_NOCACHE) {
        ret = blkio_set_bool(s->blkio, "direct", true);
        if (ret < 0) {
            error_setg_errno(errp, -ret, "failed to set direct: %s",
                             blkio_get_error_msg());
            return ret;
        }
    }

    return 0;
}

static int blkio_nvme_io_uring(BlockDriverState *bs, QDict *options, int flags,
                               Error **errp)
{
    const char *path = qdict_get_try_str(options, "path");
    BDRVBlkioState *s = bs->opaque;
    int ret;

    if (!path) {
        error_setg(errp, "missing 'path' option");
        return -EINVAL;
    }

    ret = blkio_set_str(s->blkio, "path", path);
    qdict_del(options, "path");
    if (ret < 0) {
        error_setg_errno(errp, -ret, "failed to set path: %s",
                         blkio_get_error_msg());
        return ret;
    }

    if (!(flags & BDRV_O_NOCACHE)) {
        error_setg(errp, "cache.direct=off is not supported");
        return -EINVAL;
    }

    return 0;
}

static int blkio_virtio_blk_common_open(BlockDriverState *bs,
        QDict *options, int flags, Error **errp)
{
    const char *path = qdict_get_try_str(options, "path");
    BDRVBlkioState *s = bs->opaque;
    int ret;

    if (!path) {
        error_setg(errp, "missing 'path' option");
        return -EINVAL;
    }

    ret = blkio_set_str(s->blkio, "path", path);
    qdict_del(options, "path");
    if (ret < 0) {
        error_setg_errno(errp, -ret, "failed to set path: %s",
                         blkio_get_error_msg());
        return ret;
    }

    if (!(flags & BDRV_O_NOCACHE)) {
        error_setg(errp, "cache.direct=off is not supported");
        return -EINVAL;
    }
    return 0;
}

static int blkio_file_open(BlockDriverState *bs, QDict *options, int flags,
                           Error **errp)
{
    const char *blkio_driver = bs->drv->protocol_name;
    BDRVBlkioState *s = bs->opaque;
    int ret;

    ret = blkio_create(blkio_driver, &s->blkio);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "blkio_create failed: %s",
                         blkio_get_error_msg());
        return ret;
    }

    if (strcmp(blkio_driver, DRIVER_IO_URING) == 0) {
        ret = blkio_io_uring_open(bs, options, flags, errp);
    } else if (strcmp(blkio_driver, DRIVER_NVME_IO_URING) == 0) {
        ret = blkio_nvme_io_uring(bs, options, flags, errp);
    } else if (strcmp(blkio_driver, DRIVER_VIRTIO_BLK_VFIO_PCI) == 0) {
        ret = blkio_virtio_blk_common_open(bs, options, flags, errp);
    } else if (strcmp(blkio_driver, DRIVER_VIRTIO_BLK_VHOST_USER) == 0) {
        ret = blkio_virtio_blk_common_open(bs, options, flags, errp);
    } else if (strcmp(blkio_driver, DRIVER_VIRTIO_BLK_VHOST_VDPA) == 0) {
        ret = blkio_virtio_blk_common_open(bs, options, flags, errp);
    } else {
        g_assert_not_reached();
    }
    if (ret < 0) {
        blkio_destroy(&s->blkio);
        return ret;
    }

    if (!(flags & BDRV_O_RDWR)) {
        ret = blkio_set_bool(s->blkio, "read-only", true);
        if (ret < 0) {
            error_setg_errno(errp, -ret, "failed to set read-only: %s",
                             blkio_get_error_msg());
            blkio_destroy(&s->blkio);
            return ret;
        }
    }

    ret = blkio_connect(s->blkio);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "blkio_connect failed: %s",
                         blkio_get_error_msg());
        blkio_destroy(&s->blkio);
        return ret;
    }

    ret = blkio_get_bool(s->blkio,
                         "needs-mem-regions",
                         &s->needs_mem_regions);
    if (ret < 0) {
        error_setg_errno(errp, -ret,
                         "failed to get needs-mem-regions: %s",
                         blkio_get_error_msg());
        blkio_destroy(&s->blkio);
        return ret;
    }

    ret = blkio_get_bool(s->blkio,
                         "needs-mem-region-fd",
                         &s->needs_mem_region_fd);
    if (ret < 0) {
        error_setg_errno(errp, -ret,
                         "failed to get needs-mem-region-fd: %s",
                         blkio_get_error_msg());
        blkio_destroy(&s->blkio);
        return ret;
    }

    ret = blkio_get_uint64(s->blkio,
                           "mem-region-alignment",
                           &s->mem_region_alignment);
    if (ret < 0) {
        error_setg_errno(errp, -ret,
                         "failed to get mem-region-alignment: %s",
                         blkio_get_error_msg());
        blkio_destroy(&s->blkio);
        return ret;
    }

    ret = blkio_get_bool(s->blkio,
                         "may-pin-mem-regions",
                         &s->may_pin_mem_regions);
    if (ret < 0) {
        /* Be conservative (assume pinning) if the property is not supported */
        s->may_pin_mem_regions = s->needs_mem_regions;
    }

    /*
     * Notify if libblkio drivers pin memory and prevent features like
     * virtio-mem from working.
     */
    if (s->may_pin_mem_regions) {
        ret = ram_block_discard_disable(true);
        if (ret < 0) {
            error_setg_errno(errp, -ret, "ram_block_discard_disable() failed");
            blkio_destroy(&s->blkio);
            return ret;
        }
    }

    ret = blkio_start(s->blkio);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "blkio_start failed: %s",
                         blkio_get_error_msg());
        blkio_destroy(&s->blkio);
        if (s->may_pin_mem_regions) {
            ram_block_discard_disable(false);
        }
        return ret;
    }

    bs->supported_write_flags = BDRV_REQ_FUA | BDRV_REQ_REGISTERED_BUF;
    bs->supported_zero_flags = BDRV_REQ_FUA | BDRV_REQ_MAY_UNMAP |
                               BDRV_REQ_NO_FALLBACK;

    qemu_mutex_init(&s->blkio_lock);
    qemu_co_mutex_init(&s->bounce_lock);
    qemu_co_queue_init(&s->bounce_available);
    QLIST_INIT(&s->bounce_bufs);
    s->blkioq = blkio_get_queue(s->blkio, 0);
    s->completion_fd = blkioq_get_completion_fd(s->blkioq);

    blkio_attach_aio_context(bs, bdrv_get_aio_context(bs));
    return 0;
}

static void blkio_close(BlockDriverState *bs)
{
    BDRVBlkioState *s = bs->opaque;

    /* There is no destroy() API for s->bounce_lock */

    qemu_mutex_destroy(&s->blkio_lock);
    blkio_detach_aio_context(bs);
    blkio_destroy(&s->blkio);

    if (s->may_pin_mem_regions) {
        ram_block_discard_disable(false);
    }
}

static int64_t coroutine_fn blkio_co_getlength(BlockDriverState *bs)
{
    BDRVBlkioState *s = bs->opaque;
    uint64_t capacity;
    int ret;

    WITH_QEMU_LOCK_GUARD(&s->blkio_lock) {
        ret = blkio_get_uint64(s->blkio, "capacity", &capacity);
    }
    if (ret < 0) {
        return -ret;
    }

    return capacity;
}

static int coroutine_fn blkio_truncate(BlockDriverState *bs, int64_t offset,
                                       bool exact, PreallocMode prealloc,
                                       BdrvRequestFlags flags, Error **errp)
{
    int64_t current_length;

    if (prealloc != PREALLOC_MODE_OFF) {
        error_setg(errp, "Unsupported preallocation mode '%s'",
                   PreallocMode_str(prealloc));
        return -ENOTSUP;
    }

    current_length = blkio_co_getlength(bs);

    if (offset > current_length) {
        error_setg(errp, "Cannot grow device");
        return -EINVAL;
    } else if (exact && offset != current_length) {
        error_setg(errp, "Cannot resize device");
        return -ENOTSUP;
    }

    return 0;
}

static int coroutine_fn
blkio_co_get_info(BlockDriverState *bs, BlockDriverInfo *bdi)
{
    return 0;
}

static void blkio_refresh_limits(BlockDriverState *bs, Error **errp)
{
    BDRVBlkioState *s = bs->opaque;
    QEMU_LOCK_GUARD(&s->blkio_lock);
    int value;
    int ret;

    ret = blkio_get_int(s->blkio, "request-alignment", &value);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "failed to get \"request-alignment\": %s",
                         blkio_get_error_msg());
        return;
    }
    bs->bl.request_alignment = value;
    if (bs->bl.request_alignment < 1 ||
        bs->bl.request_alignment >= INT_MAX ||
        !is_power_of_2(bs->bl.request_alignment)) {
        error_setg(errp, "invalid \"request-alignment\" value %" PRIu32 ", "
                   "must be a power of 2 less than INT_MAX",
                   bs->bl.request_alignment);
        return;
    }

    ret = blkio_get_int(s->blkio, "optimal-io-size", &value);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "failed to get \"optimal-io-size\": %s",
                         blkio_get_error_msg());
        return;
    }
    bs->bl.opt_transfer = value;
    if (bs->bl.opt_transfer > INT_MAX ||
        (bs->bl.opt_transfer % bs->bl.request_alignment)) {
        error_setg(errp, "invalid \"optimal-io-size\" value %" PRIu32 ", must "
                   "be a multiple of %" PRIu32, bs->bl.opt_transfer,
                   bs->bl.request_alignment);
        return;
    }

    ret = blkio_get_int(s->blkio, "max-transfer", &value);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "failed to get \"max-transfer\": %s",
                         blkio_get_error_msg());
        return;
    }
    bs->bl.max_transfer = value;
    if ((bs->bl.max_transfer % bs->bl.request_alignment) ||
        (bs->bl.opt_transfer && (bs->bl.max_transfer % bs->bl.opt_transfer))) {
        error_setg(errp, "invalid \"max-transfer\" value %" PRIu32 ", must be "
                   "a multiple of %" PRIu32 " and %" PRIu32 " (if non-zero)",
                   bs->bl.max_transfer, bs->bl.request_alignment,
                   bs->bl.opt_transfer);
        return;
    }

    ret = blkio_get_int(s->blkio, "buf-alignment", &value);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "failed to get \"buf-alignment\": %s",
                         blkio_get_error_msg());
        return;
    }
    if (value < 1) {
        error_setg(errp, "invalid \"buf-alignment\" value %d, must be "
                   "positive", value);
        return;
    }
    bs->bl.min_mem_alignment = value;

    ret = blkio_get_int(s->blkio, "optimal-buf-alignment", &value);
    if (ret < 0) {
        error_setg_errno(errp, -ret,
                         "failed to get \"optimal-buf-alignment\": %s",
                         blkio_get_error_msg());
        return;
    }
    if (value < 1) {
        error_setg(errp, "invalid \"optimal-buf-alignment\" value %d, "
                   "must be positive", value);
        return;
    }
    bs->bl.opt_mem_alignment = value;

    ret = blkio_get_int(s->blkio, "max-segments", &value);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "failed to get \"max-segments\": %s",
                         blkio_get_error_msg());
        return;
    }
    if (value < 1) {
        error_setg(errp, "invalid \"max-segments\" value %d, must be positive",
                   value);
        return;
    }
    bs->bl.max_iov = value;
}

/*
 * TODO
 * Missing libblkio APIs:
 * - block_status
 * - co_invalidate_cache
 *
 * Out of scope?
 * - create
 * - truncate
 */

#define BLKIO_DRIVER(name, ...) \
    { \
        .format_name             = name, \
        .protocol_name           = name, \
        .instance_size           = sizeof(BDRVBlkioState), \
        .bdrv_file_open          = blkio_file_open, \
        .bdrv_close              = blkio_close, \
        .bdrv_co_getlength       = blkio_co_getlength, \
        .bdrv_co_truncate        = blkio_truncate, \
        .bdrv_co_get_info        = blkio_co_get_info, \
        .bdrv_attach_aio_context = blkio_attach_aio_context, \
        .bdrv_detach_aio_context = blkio_detach_aio_context, \
        .bdrv_co_pdiscard        = blkio_co_pdiscard, \
        .bdrv_co_preadv          = blkio_co_preadv, \
        .bdrv_co_pwritev         = blkio_co_pwritev, \
        .bdrv_co_flush_to_disk   = blkio_co_flush, \
        .bdrv_co_pwrite_zeroes   = blkio_co_pwrite_zeroes, \
        .bdrv_co_io_unplug       = blkio_co_io_unplug, \
        .bdrv_refresh_limits     = blkio_refresh_limits, \
        .bdrv_register_buf       = blkio_register_buf, \
        .bdrv_unregister_buf     = blkio_unregister_buf, \
        __VA_ARGS__ \
    }

static BlockDriver bdrv_io_uring = BLKIO_DRIVER(
    DRIVER_IO_URING,
    .bdrv_needs_filename = true,
);

static BlockDriver bdrv_nvme_io_uring = BLKIO_DRIVER(
    DRIVER_NVME_IO_URING,
);

static BlockDriver bdrv_virtio_blk_vfio_pci = BLKIO_DRIVER(
    DRIVER_VIRTIO_BLK_VFIO_PCI
);

static BlockDriver bdrv_virtio_blk_vhost_user = BLKIO_DRIVER(
    DRIVER_VIRTIO_BLK_VHOST_USER
);

static BlockDriver bdrv_virtio_blk_vhost_vdpa = BLKIO_DRIVER(
    DRIVER_VIRTIO_BLK_VHOST_VDPA
);

static void bdrv_blkio_init(void)
{
    bdrv_register(&bdrv_io_uring);
    bdrv_register(&bdrv_nvme_io_uring);
    bdrv_register(&bdrv_virtio_blk_vfio_pci);
    bdrv_register(&bdrv_virtio_blk_vhost_user);
    bdrv_register(&bdrv_virtio_blk_vhost_vdpa);
}

block_init(bdrv_blkio_init);

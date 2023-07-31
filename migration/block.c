/*
 * QEMU live block migration
 *
 * Copyright IBM, Corp. 2009
 *
 * Authors:
 *  Liran Schour   <lirans@il.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include "qemu/cutils.h"
#include "qemu/queue.h"
#include "block.h"
#include "block/dirty-bitmap.h"
#include "migration/misc.h"
#include "migration.h"
#include "migration/register.h"
#include "qemu-file.h"
#include "migration/vmstate.h"
#include "sysemu/block-backend.h"
#include "trace.h"

#define BLK_MIG_BLOCK_SIZE           (1ULL << 20)
#define BDRV_SECTORS_PER_DIRTY_CHUNK (BLK_MIG_BLOCK_SIZE >> BDRV_SECTOR_BITS)

#define BLK_MIG_FLAG_DEVICE_BLOCK       0x01
#define BLK_MIG_FLAG_EOS                0x02
#define BLK_MIG_FLAG_PROGRESS           0x04
#define BLK_MIG_FLAG_ZERO_BLOCK         0x08

#define MAX_IS_ALLOCATED_SEARCH (65536 * BDRV_SECTOR_SIZE)

#define MAX_IO_BUFFERS 512
#define MAX_PARALLEL_IO 16

typedef struct BlkMigDevState {
    /* Written during setup phase.  Can be read without a lock.  */
    BlockBackend *blk;
    char *blk_name;
    int shared_base;
    int64_t total_sectors;
    QSIMPLEQ_ENTRY(BlkMigDevState) entry;
    Error *blocker;

    /* Only used by migration thread.  Does not need a lock.  */
    int bulk_completed;
    int64_t cur_sector;
    int64_t cur_dirty;

    /* Data in the aio_bitmap is protected by block migration lock.
     * Allocation and free happen during setup and cleanup respectively.
     */
    unsigned long *aio_bitmap;

    /* Protected by block migration lock.  */
    int64_t completed_sectors;

    /* During migration this is protected by iothread lock / AioContext.
     * Allocation and free happen during setup and cleanup respectively.
     */
    BdrvDirtyBitmap *dirty_bitmap;
} BlkMigDevState;

typedef struct BlkMigBlock {
    /* Only used by migration thread.  */
    uint8_t *buf;
    BlkMigDevState *bmds;
    int64_t sector;
    int nr_sectors;
    QEMUIOVector qiov;
    BlockAIOCB *aiocb;

    /* Protected by block migration lock.  */
    int ret;
    QSIMPLEQ_ENTRY(BlkMigBlock) entry;
} BlkMigBlock;

typedef struct BlkMigState {
    QSIMPLEQ_HEAD(, BlkMigDevState) bmds_list;
    int64_t total_sector_sum;
    bool zero_blocks;

    /* Protected by lock.  */
    QSIMPLEQ_HEAD(, BlkMigBlock) blk_list;
    int submitted;
    int read_done;

    /* Only used by migration thread.  Does not need a lock.  */
    int transferred;
    int prev_progress;
    int bulk_completed;

    /* Lock must be taken _inside_ the iothread lock and any AioContexts.  */
    QemuMutex lock;
} BlkMigState;

static BlkMigState block_mig_state;

static void blk_mig_lock(void)
{
    qemu_mutex_lock(&block_mig_state.lock);
}

static void blk_mig_unlock(void)
{
    qemu_mutex_unlock(&block_mig_state.lock);
}

/* Must run outside of the iothread lock during the bulk phase,
 * or the VM will stall.
 */

static void blk_send(QEMUFile *f, BlkMigBlock * blk)
{
    int len;
    uint64_t flags = BLK_MIG_FLAG_DEVICE_BLOCK;

    if (block_mig_state.zero_blocks &&
        buffer_is_zero(blk->buf, BLK_MIG_BLOCK_SIZE)) {
        flags |= BLK_MIG_FLAG_ZERO_BLOCK;
    }

    /* sector number and flags */
    qemu_put_be64(f, (blk->sector << BDRV_SECTOR_BITS)
                     | flags);

    /* device name */
    len = strlen(blk->bmds->blk_name);
    qemu_put_byte(f, len);
    qemu_put_buffer(f, (uint8_t *) blk->bmds->blk_name, len);

    /* if a block is zero we need to flush here since the network
     * bandwidth is now a lot higher than the storage device bandwidth.
     * thus if we queue zero blocks we slow down the migration */
    if (flags & BLK_MIG_FLAG_ZERO_BLOCK) {
        qemu_fflush(f);
        return;
    }

    qemu_put_buffer(f, blk->buf, BLK_MIG_BLOCK_SIZE);
}

int blk_mig_active(void)
{
    return !QSIMPLEQ_EMPTY(&block_mig_state.bmds_list);
}

int blk_mig_bulk_active(void)
{
    return blk_mig_active() && !block_mig_state.bulk_completed;
}

uint64_t blk_mig_bytes_transferred(void)
{
    BlkMigDevState *bmds;
    uint64_t sum = 0;

    blk_mig_lock();
    QSIMPLEQ_FOREACH(bmds, &block_mig_state.bmds_list, entry) {
        sum += bmds->completed_sectors;
    }
    blk_mig_unlock();
    return sum << BDRV_SECTOR_BITS;
}

uint64_t blk_mig_bytes_remaining(void)
{
    return blk_mig_bytes_total() - blk_mig_bytes_transferred();
}

uint64_t blk_mig_bytes_total(void)
{
    BlkMigDevState *bmds;
    uint64_t sum = 0;

    QSIMPLEQ_FOREACH(bmds, &block_mig_state.bmds_list, entry) {
        sum += bmds->total_sectors;
    }
    return sum << BDRV_SECTOR_BITS;
}


/* Called with migration lock held.  */

static int bmds_aio_inflight(BlkMigDevState *bmds, int64_t sector)
{
    int64_t chunk = sector / (int64_t)BDRV_SECTORS_PER_DIRTY_CHUNK;

    if (sector < bmds->total_sectors) {
        return !!(bmds->aio_bitmap[chunk / (sizeof(unsigned long) * 8)] &
            (1UL << (chunk % (sizeof(unsigned long) * 8))));
    } else {
        return 0;
    }
}

/* Called with migration lock held.  */

static void bmds_set_aio_inflight(BlkMigDevState *bmds, int64_t sector_num,
                             int nb_sectors, int set)
{
    int64_t start, end;
    unsigned long val, idx, bit;

    start = sector_num / BDRV_SECTORS_PER_DIRTY_CHUNK;
    end = (sector_num + nb_sectors - 1) / BDRV_SECTORS_PER_DIRTY_CHUNK;

    for (; start <= end; start++) {
        idx = start / (sizeof(unsigned long) * 8);
        bit = start % (sizeof(unsigned long) * 8);
        val = bmds->aio_bitmap[idx];
        if (set) {
            val |= 1UL << bit;
        } else {
            val &= ~(1UL << bit);
        }
        bmds->aio_bitmap[idx] = val;
    }
}

static void alloc_aio_bitmap(BlkMigDevState *bmds)
{
    int64_t bitmap_size;

    bitmap_size = bmds->total_sectors + BDRV_SECTORS_PER_DIRTY_CHUNK * 8 - 1;
    bitmap_size /= BDRV_SECTORS_PER_DIRTY_CHUNK * 8;

    bmds->aio_bitmap = g_malloc0(bitmap_size);
}

/* Never hold migration lock when yielding to the main loop!  */

static void blk_mig_read_cb(void *opaque, int ret)
{
    BlkMigBlock *blk = opaque;

    blk_mig_lock();
    blk->ret = ret;

    QSIMPLEQ_INSERT_TAIL(&block_mig_state.blk_list, blk, entry);
    bmds_set_aio_inflight(blk->bmds, blk->sector, blk->nr_sectors, 0);

    block_mig_state.submitted--;
    block_mig_state.read_done++;
    assert(block_mig_state.submitted >= 0);
    blk_mig_unlock();
}

/* Called with no lock taken.  */

static int mig_save_device_bulk(QEMUFile *f, BlkMigDevState *bmds)
{
    int64_t total_sectors = bmds->total_sectors;
    int64_t cur_sector = bmds->cur_sector;
    BlockBackend *bb = bmds->blk;
    BlkMigBlock *blk;
    int nr_sectors;
    int64_t count;

    if (bmds->shared_base) {
        qemu_mutex_lock_iothread();
        aio_context_acquire(blk_get_aio_context(bb));
        /* Skip unallocated sectors; intentionally treats failure or
         * partial sector as an allocated sector */
        while (cur_sector < total_sectors &&
               !bdrv_is_allocated(blk_bs(bb), cur_sector * BDRV_SECTOR_SIZE,
                                  MAX_IS_ALLOCATED_SEARCH, &count)) {
            if (count < BDRV_SECTOR_SIZE) {
                break;
            }
            cur_sector += count >> BDRV_SECTOR_BITS;
        }
        aio_context_release(blk_get_aio_context(bb));
        qemu_mutex_unlock_iothread();
    }

    if (cur_sector >= total_sectors) {
        bmds->cur_sector = bmds->completed_sectors = total_sectors;
        return 1;
    }

    bmds->completed_sectors = cur_sector;

    cur_sector &= ~((int64_t)BDRV_SECTORS_PER_DIRTY_CHUNK - 1);

    /* we are going to transfer a full block even if it is not allocated */
    nr_sectors = BDRV_SECTORS_PER_DIRTY_CHUNK;

    if (total_sectors - cur_sector < BDRV_SECTORS_PER_DIRTY_CHUNK) {
        nr_sectors = total_sectors - cur_sector;
    }

    blk = g_new(BlkMigBlock, 1);
    blk->buf = g_malloc(BLK_MIG_BLOCK_SIZE);
    blk->bmds = bmds;
    blk->sector = cur_sector;
    blk->nr_sectors = nr_sectors;

    qemu_iovec_init_buf(&blk->qiov, blk->buf, nr_sectors * BDRV_SECTOR_SIZE);

    blk_mig_lock();
    block_mig_state.submitted++;
    blk_mig_unlock();

    /* We do not know if bs is under the main thread (and thus does
     * not acquire the AioContext when doing AIO) or rather under
     * dataplane.  Thus acquire both the iothread mutex and the
     * AioContext.
     *
     * This is ugly and will disappear when we make bdrv_* thread-safe,
     * without the need to acquire the AioContext.
     */
    qemu_mutex_lock_iothread();
    aio_context_acquire(blk_get_aio_context(bmds->blk));
    bdrv_reset_dirty_bitmap(bmds->dirty_bitmap, cur_sector * BDRV_SECTOR_SIZE,
                            nr_sectors * BDRV_SECTOR_SIZE);
    blk->aiocb = blk_aio_preadv(bb, cur_sector * BDRV_SECTOR_SIZE, &blk->qiov,
                                0, blk_mig_read_cb, blk);
    aio_context_release(blk_get_aio_context(bmds->blk));
    qemu_mutex_unlock_iothread();

    bmds->cur_sector = cur_sector + nr_sectors;
    return (bmds->cur_sector >= total_sectors);
}

/* Called with iothread lock taken.  */

static int set_dirty_tracking(void)
{
    BlkMigDevState *bmds;
    int ret;

    QSIMPLEQ_FOREACH(bmds, &block_mig_state.bmds_list, entry) {
        bmds->dirty_bitmap = bdrv_create_dirty_bitmap(blk_bs(bmds->blk),
                                                      BLK_MIG_BLOCK_SIZE,
                                                      NULL, NULL);
        if (!bmds->dirty_bitmap) {
            ret = -errno;
            goto fail;
        }
    }
    return 0;

fail:
    QSIMPLEQ_FOREACH(bmds, &block_mig_state.bmds_list, entry) {
        if (bmds->dirty_bitmap) {
            bdrv_release_dirty_bitmap(bmds->dirty_bitmap);
        }
    }
    return ret;
}

/* Called with iothread lock taken.  */

static void unset_dirty_tracking(void)
{
    BlkMigDevState *bmds;

    QSIMPLEQ_FOREACH(bmds, &block_mig_state.bmds_list, entry) {
        if (bmds->dirty_bitmap) {
            bdrv_release_dirty_bitmap(bmds->dirty_bitmap);
        }
    }
}

static int init_blk_migration(QEMUFile *f)
{
    BlockDriverState *bs;
    BlkMigDevState *bmds;
    int64_t sectors;
    BdrvNextIterator it;
    int i, num_bs = 0;
    struct {
        BlkMigDevState *bmds;
        BlockDriverState *bs;
    } *bmds_bs;
    Error *local_err = NULL;
    int ret;

    block_mig_state.submitted = 0;
    block_mig_state.read_done = 0;
    block_mig_state.transferred = 0;
    block_mig_state.total_sector_sum = 0;
    block_mig_state.prev_progress = -1;
    block_mig_state.bulk_completed = 0;
    block_mig_state.zero_blocks = migrate_zero_blocks();

    for (bs = bdrv_first(&it); bs; bs = bdrv_next(&it)) {
        num_bs++;
    }
    bmds_bs = g_malloc0(num_bs * sizeof(*bmds_bs));

    for (i = 0, bs = bdrv_first(&it); bs; bs = bdrv_next(&it), i++) {
        if (bdrv_is_read_only(bs)) {
            continue;
        }

        sectors = bdrv_nb_sectors(bs);
        if (sectors <= 0) {
            ret = sectors;
            bdrv_next_cleanup(&it);
            goto out;
        }

        bmds = g_new0(BlkMigDevState, 1);
        bmds->blk = blk_new(qemu_get_aio_context(),
                            BLK_PERM_CONSISTENT_READ, BLK_PERM_ALL);
        bmds->blk_name = g_strdup(bdrv_get_device_name(bs));
        bmds->bulk_completed = 0;
        bmds->total_sectors = sectors;
        bmds->completed_sectors = 0;
        bmds->shared_base = migrate_use_block_incremental();

        assert(i < num_bs);
        bmds_bs[i].bmds = bmds;
        bmds_bs[i].bs = bs;

        block_mig_state.total_sector_sum += sectors;

        if (bmds->shared_base) {
            trace_migration_block_init_shared(bdrv_get_device_name(bs));
        } else {
            trace_migration_block_init_full(bdrv_get_device_name(bs));
        }

        QSIMPLEQ_INSERT_TAIL(&block_mig_state.bmds_list, bmds, entry);
    }

    /* Can only insert new BDSes now because doing so while iterating block
     * devices may end up in a deadlock (iterating the new BDSes, too). */
    for (i = 0; i < num_bs; i++) {
        BlkMigDevState *bmds = bmds_bs[i].bmds;
        BlockDriverState *bs = bmds_bs[i].bs;

        if (bmds) {
            ret = blk_insert_bs(bmds->blk, bs, &local_err);
            if (ret < 0) {
                error_report_err(local_err);
                goto out;
            }

            alloc_aio_bitmap(bmds);
            error_setg(&bmds->blocker, "block device is in use by migration");
            bdrv_op_block_all(bs, bmds->blocker);
        }
    }

    ret = 0;
out:
    g_free(bmds_bs);
    return ret;
}

/* Called with no lock taken.  */

static int blk_mig_save_bulked_block(QEMUFile *f)
{
    int64_t completed_sector_sum = 0;
    BlkMigDevState *bmds;
    int progress;
    int ret = 0;

    QSIMPLEQ_FOREACH(bmds, &block_mig_state.bmds_list, entry) {
        if (bmds->bulk_completed == 0) {
            if (mig_save_device_bulk(f, bmds) == 1) {
                /* completed bulk section for this device */
                bmds->bulk_completed = 1;
            }
            completed_sector_sum += bmds->completed_sectors;
            ret = 1;
            break;
        } else {
            completed_sector_sum += bmds->completed_sectors;
        }
    }

    if (block_mig_state.total_sector_sum != 0) {
        progress = completed_sector_sum * 100 /
                   block_mig_state.total_sector_sum;
    } else {
        progress = 100;
    }
    if (progress != block_mig_state.prev_progress) {
        block_mig_state.prev_progress = progress;
        qemu_put_be64(f, (progress << BDRV_SECTOR_BITS)
                         | BLK_MIG_FLAG_PROGRESS);
        trace_migration_block_progression(progress);
    }

    return ret;
}

static void blk_mig_reset_dirty_cursor(void)
{
    BlkMigDevState *bmds;

    QSIMPLEQ_FOREACH(bmds, &block_mig_state.bmds_list, entry) {
        bmds->cur_dirty = 0;
    }
}

/* Called with iothread lock and AioContext taken.  */

static int mig_save_device_dirty(QEMUFile *f, BlkMigDevState *bmds,
                                 int is_async)
{
    BlkMigBlock *blk;
    int64_t total_sectors = bmds->total_sectors;
    int64_t sector;
    int nr_sectors;
    int ret = -EIO;

    for (sector = bmds->cur_dirty; sector < bmds->total_sectors;) {
        blk_mig_lock();
        if (bmds_aio_inflight(bmds, sector)) {
            blk_mig_unlock();
            blk_drain(bmds->blk);
        } else {
            blk_mig_unlock();
        }
        bdrv_dirty_bitmap_lock(bmds->dirty_bitmap);
        if (bdrv_dirty_bitmap_get_locked(bmds->dirty_bitmap,
                                         sector * BDRV_SECTOR_SIZE)) {
            if (total_sectors - sector < BDRV_SECTORS_PER_DIRTY_CHUNK) {
                nr_sectors = total_sectors - sector;
            } else {
                nr_sectors = BDRV_SECTORS_PER_DIRTY_CHUNK;
            }
            bdrv_reset_dirty_bitmap_locked(bmds->dirty_bitmap,
                                           sector * BDRV_SECTOR_SIZE,
                                           nr_sectors * BDRV_SECTOR_SIZE);
            bdrv_dirty_bitmap_unlock(bmds->dirty_bitmap);

            blk = g_new(BlkMigBlock, 1);
            blk->buf = g_malloc(BLK_MIG_BLOCK_SIZE);
            blk->bmds = bmds;
            blk->sector = sector;
            blk->nr_sectors = nr_sectors;

            if (is_async) {
                qemu_iovec_init_buf(&blk->qiov, blk->buf,
                                    nr_sectors * BDRV_SECTOR_SIZE);

                blk->aiocb = blk_aio_preadv(bmds->blk,
                                            sector * BDRV_SECTOR_SIZE,
                                            &blk->qiov, 0, blk_mig_read_cb,
                                            blk);

                blk_mig_lock();
                block_mig_state.submitted++;
                bmds_set_aio_inflight(bmds, sector, nr_sectors, 1);
                blk_mig_unlock();
            } else {
                ret = blk_pread(bmds->blk, sector * BDRV_SECTOR_SIZE,
                                nr_sectors * BDRV_SECTOR_SIZE, blk->buf, 0);
                if (ret < 0) {
                    goto error;
                }
                blk_send(f, blk);

                g_free(blk->buf);
                g_free(blk);
            }

            sector += nr_sectors;
            bmds->cur_dirty = sector;
            break;
        }

        bdrv_dirty_bitmap_unlock(bmds->dirty_bitmap);
        sector += BDRV_SECTORS_PER_DIRTY_CHUNK;
        bmds->cur_dirty = sector;
    }

    return (bmds->cur_dirty >= bmds->total_sectors);

error:
    trace_migration_block_save_device_dirty(sector);
    g_free(blk->buf);
    g_free(blk);
    return ret;
}

/* Called with iothread lock taken.
 *
 * return value:
 * 0: too much data for max_downtime
 * 1: few enough data for max_downtime
*/
static int blk_mig_save_dirty_block(QEMUFile *f, int is_async)
{
    BlkMigDevState *bmds;
    int ret = 1;

    QSIMPLEQ_FOREACH(bmds, &block_mig_state.bmds_list, entry) {
        aio_context_acquire(blk_get_aio_context(bmds->blk));
        ret = mig_save_device_dirty(f, bmds, is_async);
        aio_context_release(blk_get_aio_context(bmds->blk));
        if (ret <= 0) {
            break;
        }
    }

    return ret;
}

/* Called with no locks taken.  */

static int flush_blks(QEMUFile *f)
{
    BlkMigBlock *blk;
    int ret = 0;

    trace_migration_block_flush_blks("Enter", block_mig_state.submitted,
                                     block_mig_state.read_done,
                                     block_mig_state.transferred);

    blk_mig_lock();
    while ((blk = QSIMPLEQ_FIRST(&block_mig_state.blk_list)) != NULL) {
        if (qemu_file_rate_limit(f)) {
            break;
        }
        if (blk->ret < 0) {
            ret = blk->ret;
            break;
        }

        QSIMPLEQ_REMOVE_HEAD(&block_mig_state.blk_list, entry);
        blk_mig_unlock();
        blk_send(f, blk);
        blk_mig_lock();

        g_free(blk->buf);
        g_free(blk);

        block_mig_state.read_done--;
        block_mig_state.transferred++;
        assert(block_mig_state.read_done >= 0);
    }
    blk_mig_unlock();

    trace_migration_block_flush_blks("Exit", block_mig_state.submitted,
                                     block_mig_state.read_done,
                                     block_mig_state.transferred);
    return ret;
}

/* Called with iothread lock taken.  */

static int64_t get_remaining_dirty(void)
{
    BlkMigDevState *bmds;
    int64_t dirty = 0;

    QSIMPLEQ_FOREACH(bmds, &block_mig_state.bmds_list, entry) {
        aio_context_acquire(blk_get_aio_context(bmds->blk));
        dirty += bdrv_get_dirty_count(bmds->dirty_bitmap);
        aio_context_release(blk_get_aio_context(bmds->blk));
    }

    return dirty;
}



/* Called with iothread lock taken.  */
static void block_migration_cleanup_bmds(void)
{
    BlkMigDevState *bmds;
    BlockDriverState *bs;
    AioContext *ctx;

    unset_dirty_tracking();

    while ((bmds = QSIMPLEQ_FIRST(&block_mig_state.bmds_list)) != NULL) {
        QSIMPLEQ_REMOVE_HEAD(&block_mig_state.bmds_list, entry);

        bs = blk_bs(bmds->blk);
        if (bs) {
            bdrv_op_unblock_all(bs, bmds->blocker);
        }
        error_free(bmds->blocker);

        /* Save ctx, because bmds->blk can disappear during blk_unref.  */
        ctx = blk_get_aio_context(bmds->blk);
        aio_context_acquire(ctx);
        blk_unref(bmds->blk);
        aio_context_release(ctx);

        g_free(bmds->blk_name);
        g_free(bmds->aio_bitmap);
        g_free(bmds);
    }
}

/* Called with iothread lock taken.  */
static void block_migration_cleanup(void *opaque)
{
    BlkMigBlock *blk;

    bdrv_drain_all();

    block_migration_cleanup_bmds();

    blk_mig_lock();
    while ((blk = QSIMPLEQ_FIRST(&block_mig_state.blk_list)) != NULL) {
        QSIMPLEQ_REMOVE_HEAD(&block_mig_state.blk_list, entry);
        g_free(blk->buf);
        g_free(blk);
    }
    blk_mig_unlock();
}

static int block_save_setup(QEMUFile *f, void *opaque)
{
    int ret;

    trace_migration_block_save("setup", block_mig_state.submitted,
                               block_mig_state.transferred);

    qemu_mutex_lock_iothread();
    ret = init_blk_migration(f);
    if (ret < 0) {
        qemu_mutex_unlock_iothread();
        return ret;
    }

    /* start track dirty blocks */
    ret = set_dirty_tracking();

    qemu_mutex_unlock_iothread();

    if (ret) {
        return ret;
    }

    ret = flush_blks(f);
    blk_mig_reset_dirty_cursor();
    qemu_put_be64(f, BLK_MIG_FLAG_EOS);

    return ret;
}

static int block_save_iterate(QEMUFile *f, void *opaque)
{
    int ret;
    int64_t last_bytes = qemu_file_total_transferred(f);
    int64_t delta_bytes;

    trace_migration_block_save("iterate", block_mig_state.submitted,
                               block_mig_state.transferred);

    ret = flush_blks(f);
    if (ret) {
        return ret;
    }

    blk_mig_reset_dirty_cursor();

    /* control the rate of transfer */
    blk_mig_lock();
    while (block_mig_state.read_done * BLK_MIG_BLOCK_SIZE <
           qemu_file_get_rate_limit(f) &&
           block_mig_state.submitted < MAX_PARALLEL_IO &&
           (block_mig_state.submitted + block_mig_state.read_done) <
           MAX_IO_BUFFERS) {
        blk_mig_unlock();
        if (block_mig_state.bulk_completed == 0) {
            /* first finish the bulk phase */
            if (blk_mig_save_bulked_block(f) == 0) {
                /* finished saving bulk on all devices */
                block_mig_state.bulk_completed = 1;
            }
            ret = 0;
        } else {
            /* Always called with iothread lock taken for
             * simplicity, block_save_complete also calls it.
             */
            qemu_mutex_lock_iothread();
            ret = blk_mig_save_dirty_block(f, 1);
            qemu_mutex_unlock_iothread();
        }
        if (ret < 0) {
            return ret;
        }
        blk_mig_lock();
        if (ret != 0) {
            /* no more dirty blocks */
            break;
        }
    }
    blk_mig_unlock();

    ret = flush_blks(f);
    if (ret) {
        return ret;
    }

    qemu_put_be64(f, BLK_MIG_FLAG_EOS);
    delta_bytes = qemu_file_total_transferred(f) - last_bytes;
    if (delta_bytes > 0) {
        return 1;
    } else if (delta_bytes < 0) {
        return -1;
    } else {
        return 0;
    }
}

/* Called with iothread lock taken.  */

static int block_save_complete(QEMUFile *f, void *opaque)
{
    int ret;

    trace_migration_block_save("complete", block_mig_state.submitted,
                               block_mig_state.transferred);

    ret = flush_blks(f);
    if (ret) {
        return ret;
    }

    blk_mig_reset_dirty_cursor();

    /* we know for sure that save bulk is completed and
       all async read completed */
    blk_mig_lock();
    assert(block_mig_state.submitted == 0);
    blk_mig_unlock();

    do {
        ret = blk_mig_save_dirty_block(f, 0);
        if (ret < 0) {
            return ret;
        }
    } while (ret == 0);

    /* report completion */
    qemu_put_be64(f, (100 << BDRV_SECTOR_BITS) | BLK_MIG_FLAG_PROGRESS);

    trace_migration_block_save_complete();

    qemu_put_be64(f, BLK_MIG_FLAG_EOS);

    /* Make sure that our BlockBackends are gone, so that the block driver
     * nodes can be inactivated. */
    block_migration_cleanup_bmds();

    return 0;
}

static void block_state_pending(void *opaque, uint64_t *must_precopy,
                                uint64_t *can_postcopy)
{
    /* Estimate pending number of bytes to send */
    uint64_t pending;

    qemu_mutex_lock_iothread();
    pending = get_remaining_dirty();
    qemu_mutex_unlock_iothread();

    blk_mig_lock();
    pending += block_mig_state.submitted * BLK_MIG_BLOCK_SIZE +
               block_mig_state.read_done * BLK_MIG_BLOCK_SIZE;
    blk_mig_unlock();

    /* Report at least one block pending during bulk phase */
    if (!pending && !block_mig_state.bulk_completed) {
        pending = BLK_MIG_BLOCK_SIZE;
    }

    trace_migration_block_state_pending(pending);
    /* We don't do postcopy */
    *must_precopy += pending;
}

static int block_load(QEMUFile *f, void *opaque, int version_id)
{
    static int banner_printed;
    int len, flags;
    char device_name[256];
    int64_t addr;
    BlockBackend *blk, *blk_prev = NULL;
    Error *local_err = NULL;
    uint8_t *buf;
    int64_t total_sectors = 0;
    int nr_sectors;
    int ret;
    BlockDriverInfo bdi;
    int cluster_size = BLK_MIG_BLOCK_SIZE;

    do {
        addr = qemu_get_be64(f);

        flags = addr & (BDRV_SECTOR_SIZE - 1);
        addr >>= BDRV_SECTOR_BITS;

        if (flags & BLK_MIG_FLAG_DEVICE_BLOCK) {
            /* get device name */
            len = qemu_get_byte(f);
            qemu_get_buffer(f, (uint8_t *)device_name, len);
            device_name[len] = '\0';

            blk = blk_by_name(device_name);
            if (!blk) {
                fprintf(stderr, "Error unknown block device %s\n",
                        device_name);
                return -EINVAL;
            }

            if (blk != blk_prev) {
                blk_prev = blk;
                total_sectors = blk_nb_sectors(blk);
                if (total_sectors <= 0) {
                    error_report("Error getting length of block device %s",
                                 device_name);
                    return -EINVAL;
                }

                blk_activate(blk, &local_err);
                if (local_err) {
                    error_report_err(local_err);
                    return -EINVAL;
                }

                ret = bdrv_get_info(blk_bs(blk), &bdi);
                if (ret == 0 && bdi.cluster_size > 0 &&
                    bdi.cluster_size <= BLK_MIG_BLOCK_SIZE &&
                    BLK_MIG_BLOCK_SIZE % bdi.cluster_size == 0) {
                    cluster_size = bdi.cluster_size;
                } else {
                    cluster_size = BLK_MIG_BLOCK_SIZE;
                }
            }

            if (total_sectors - addr < BDRV_SECTORS_PER_DIRTY_CHUNK) {
                nr_sectors = total_sectors - addr;
            } else {
                nr_sectors = BDRV_SECTORS_PER_DIRTY_CHUNK;
            }

            if (flags & BLK_MIG_FLAG_ZERO_BLOCK) {
                ret = blk_pwrite_zeroes(blk, addr * BDRV_SECTOR_SIZE,
                                        nr_sectors * BDRV_SECTOR_SIZE,
                                        BDRV_REQ_MAY_UNMAP);
            } else {
                int i;
                int64_t cur_addr;
                uint8_t *cur_buf;

                buf = g_malloc(BLK_MIG_BLOCK_SIZE);
                qemu_get_buffer(f, buf, BLK_MIG_BLOCK_SIZE);
                for (i = 0; i < BLK_MIG_BLOCK_SIZE / cluster_size; i++) {
                    cur_addr = addr * BDRV_SECTOR_SIZE + i * cluster_size;
                    cur_buf = buf + i * cluster_size;

                    if ((!block_mig_state.zero_blocks ||
                        cluster_size < BLK_MIG_BLOCK_SIZE) &&
                        buffer_is_zero(cur_buf, cluster_size)) {
                        ret = blk_pwrite_zeroes(blk, cur_addr,
                                                cluster_size,
                                                BDRV_REQ_MAY_UNMAP);
                    } else {
                        ret = blk_pwrite(blk, cur_addr, cluster_size, cur_buf,
                                         0);
                    }
                    if (ret < 0) {
                        break;
                    }
                }
                g_free(buf);
            }

            if (ret < 0) {
                return ret;
            }
        } else if (flags & BLK_MIG_FLAG_PROGRESS) {
            if (!banner_printed) {
                printf("Receiving block device images\n");
                banner_printed = 1;
            }
            printf("Completed %d %%%c", (int)addr,
                   (addr == 100) ? '\n' : '\r');
            fflush(stdout);
        } else if (!(flags & BLK_MIG_FLAG_EOS)) {
            fprintf(stderr, "Unknown block migration flags: 0x%x\n", flags);
            return -EINVAL;
        }
        ret = qemu_file_get_error(f);
        if (ret != 0) {
            return ret;
        }
    } while (!(flags & BLK_MIG_FLAG_EOS));

    return 0;
}

static bool block_is_active(void *opaque)
{
    return migrate_use_block();
}

static SaveVMHandlers savevm_block_handlers = {
    .save_setup = block_save_setup,
    .save_live_iterate = block_save_iterate,
    .save_live_complete_precopy = block_save_complete,
    .state_pending_exact = block_state_pending,
    .state_pending_estimate = block_state_pending,
    .load_state = block_load,
    .save_cleanup = block_migration_cleanup,
    .is_active = block_is_active,
};

void blk_mig_init(void)
{
    QSIMPLEQ_INIT(&block_mig_state.bmds_list);
    QSIMPLEQ_INIT(&block_mig_state.blk_list);
    qemu_mutex_init(&block_mig_state.lock);

    register_savevm_live("block", 0, 1, &savevm_block_handlers,
                         &block_mig_state);
}

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
 */

#include "qemu-common.h"
#include "block_int.h"
#include "hw/hw.h"
#include "qemu-queue.h"
#include "monitor.h"
#include "block-migration.h"
#include <assert.h>

#define BLOCK_SIZE (BDRV_SECTORS_PER_DIRTY_CHUNK << BDRV_SECTOR_BITS)

#define BLK_MIG_FLAG_DEVICE_BLOCK       0x01
#define BLK_MIG_FLAG_EOS                0x02

#define MAX_IS_ALLOCATED_SEARCH 65536
#define MAX_BLOCKS_READ 10000
#define BLOCKS_READ_CHANGE 100
#define INITIAL_BLOCKS_READ 100

//#define DEBUG_BLK_MIGRATION

#ifdef DEBUG_BLK_MIGRATION
#define dprintf(fmt, ...) \
    do { printf("blk_migration: " fmt, ## __VA_ARGS__); } while (0)
#else
#define dprintf(fmt, ...) \
    do { } while (0)
#endif

typedef struct BlkMigDevState {
    BlockDriverState *bs;
    int bulk_completed;
    int shared_base;
    int64_t cur_sector;
    int64_t completed_sectors;
    int64_t total_sectors;
    int64_t dirty;
    QSIMPLEQ_ENTRY(BlkMigDevState) entry;
} BlkMigDevState;

typedef struct BlkMigBlock {
    uint8_t *buf;
    BlkMigDevState *bmds;
    int64_t sector;
    struct iovec iov;
    QEMUIOVector qiov;
    BlockDriverAIOCB *aiocb;
    int ret;
    QSIMPLEQ_ENTRY(BlkMigBlock) entry;
} BlkMigBlock;

typedef struct BlkMigState {
    int blk_enable;
    int shared_base;
    QSIMPLEQ_HEAD(bmds_list, BlkMigDevState) bmds_list;
    QSIMPLEQ_HEAD(blk_list, BlkMigBlock) blk_list;
    int submitted;
    int read_done;
    int transferred;
    int64_t total_sector_sum;
    int64_t print_completion;
} BlkMigState;

static BlkMigState block_mig_state;

static void blk_send(QEMUFile *f, BlkMigBlock * blk)
{
    int len;

    /* sector number and flags */
    qemu_put_be64(f, (blk->sector << BDRV_SECTOR_BITS)
                     | BLK_MIG_FLAG_DEVICE_BLOCK);

    /* device name */
    len = strlen(blk->bmds->bs->device_name);
    qemu_put_byte(f, len);
    qemu_put_buffer(f, (uint8_t *)blk->bmds->bs->device_name, len);

    qemu_put_buffer(f, blk->buf, BLOCK_SIZE);
}

int blk_mig_active(void)
{
    return !QSIMPLEQ_EMPTY(&block_mig_state.bmds_list);
}

uint64_t blk_mig_bytes_transferred(void)
{
    BlkMigDevState *bmds;
    uint64_t sum = 0;

    QSIMPLEQ_FOREACH(bmds, &block_mig_state.bmds_list, entry) {
        sum += bmds->completed_sectors;
    }
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

static void blk_mig_read_cb(void *opaque, int ret)
{
    BlkMigBlock *blk = opaque;

    blk->ret = ret;

    QSIMPLEQ_INSERT_TAIL(&block_mig_state.blk_list, blk, entry);

    block_mig_state.submitted--;
    block_mig_state.read_done++;
    assert(block_mig_state.submitted >= 0);
}

static int mig_save_device_bulk(Monitor *mon, QEMUFile *f,
                                BlkMigDevState *bmds, int is_async)
{
    int64_t total_sectors = bmds->total_sectors;
    int64_t cur_sector = bmds->cur_sector;
    BlockDriverState *bs = bmds->bs;
    BlkMigBlock *blk;
    int nr_sectors;

    if (bmds->shared_base) {
        while (cur_sector < total_sectors &&
               !bdrv_is_allocated(bs, cur_sector, MAX_IS_ALLOCATED_SEARCH,
                                  &nr_sectors)) {
            cur_sector += nr_sectors;
        }
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

    blk = qemu_malloc(sizeof(BlkMigBlock));
    blk->buf = qemu_malloc(BLOCK_SIZE);
    blk->bmds = bmds;
    blk->sector = cur_sector;

    if (is_async) {
        blk->iov.iov_base = blk->buf;
        blk->iov.iov_len = nr_sectors * BDRV_SECTOR_SIZE;
        qemu_iovec_init_external(&blk->qiov, &blk->iov, 1);

        blk->aiocb = bdrv_aio_readv(bs, cur_sector, &blk->qiov,
                                    nr_sectors, blk_mig_read_cb, blk);
        if (!blk->aiocb) {
            goto error;
        }
        block_mig_state.submitted++;
    } else {
        if (bdrv_read(bs, cur_sector, blk->buf, nr_sectors) < 0) {
            goto error;
        }
        blk_send(f, blk);

        qemu_free(blk->buf);
        qemu_free(blk);
    }

    bdrv_reset_dirty(bs, cur_sector, nr_sectors);
    bmds->cur_sector = cur_sector + nr_sectors;

    return (bmds->cur_sector >= total_sectors);

error:
    monitor_printf(mon, "Error reading sector %" PRId64 "\n", cur_sector);
    qemu_file_set_error(f);
    qemu_free(blk->buf);
    qemu_free(blk);
    return 0;
}

static void set_dirty_tracking(int enable)
{
    BlkMigDevState *bmds;

    QSIMPLEQ_FOREACH(bmds, &block_mig_state.bmds_list, entry) {
        bdrv_set_dirty_tracking(bmds->bs, enable);
    }
}

static void init_blk_migration(Monitor *mon, QEMUFile *f)
{
    BlkMigDevState *bmds;
    BlockDriverState *bs;

    block_mig_state.submitted = 0;
    block_mig_state.read_done = 0;
    block_mig_state.transferred = 0;
    block_mig_state.total_sector_sum = 0;
    block_mig_state.print_completion = 0;

    for (bs = bdrv_first; bs != NULL; bs = bs->next) {
        if (bs->type == BDRV_TYPE_HD) {
            bmds = qemu_mallocz(sizeof(BlkMigDevState));
            bmds->bs = bs;
            bmds->bulk_completed = 0;
            bmds->total_sectors = bdrv_getlength(bs) >> BDRV_SECTOR_BITS;
            bmds->completed_sectors = 0;
            bmds->shared_base = block_mig_state.shared_base;

            block_mig_state.total_sector_sum += bmds->total_sectors;

            if (bmds->shared_base) {
                monitor_printf(mon, "Start migration for %s with shared base "
                                    "image\n",
                               bs->device_name);
            } else {
                monitor_printf(mon, "Start full migration for %s\n",
                               bs->device_name);
            }

            QSIMPLEQ_INSERT_TAIL(&block_mig_state.bmds_list, bmds, entry);
        }
    }
}

static int blk_mig_save_bulked_block(Monitor *mon, QEMUFile *f, int is_async)
{
    int64_t completed_sector_sum = 0;
    BlkMigDevState *bmds;
    int ret = 0;

    QSIMPLEQ_FOREACH(bmds, &block_mig_state.bmds_list, entry) {
        if (bmds->bulk_completed == 0) {
            if (mig_save_device_bulk(mon, f, bmds, is_async) == 1) {
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

    if (completed_sector_sum >= block_mig_state.print_completion) {
        monitor_printf(mon, "Completed %" PRId64 " %%\r",
                       completed_sector_sum * 100 /
                       block_mig_state.total_sector_sum);
        monitor_flush(mon);
        block_mig_state.print_completion +=
            (BDRV_SECTORS_PER_DIRTY_CHUNK * 10000);
    }

    return ret;
}

#define MAX_NUM_BLOCKS 4

static void blk_mig_save_dirty_blocks(Monitor *mon, QEMUFile *f)
{
    BlkMigDevState *bmds;
    BlkMigBlock blk;
    int64_t sector;

    blk.buf = qemu_malloc(BLOCK_SIZE);

    QSIMPLEQ_FOREACH(bmds, &block_mig_state.bmds_list, entry) {
        for (sector = 0; sector < bmds->cur_sector;) {
            if (bdrv_get_dirty(bmds->bs, sector)) {
                if (bdrv_read(bmds->bs, sector, blk.buf,
                              BDRV_SECTORS_PER_DIRTY_CHUNK) < 0) {
                    monitor_printf(mon, "Error reading sector %" PRId64 "\n",
                                   sector);
                    qemu_file_set_error(f);
                    qemu_free(blk.buf);
                    return;
                }
                blk.bmds = bmds;
                blk.sector = sector;
                blk_send(f, &blk);

                bdrv_reset_dirty(bmds->bs, sector,
                                 BDRV_SECTORS_PER_DIRTY_CHUNK);
            }
            sector += BDRV_SECTORS_PER_DIRTY_CHUNK;
        }
    }

    qemu_free(blk.buf);
}

static void flush_blks(QEMUFile* f)
{
    BlkMigBlock *blk;

    dprintf("%s Enter submitted %d read_done %d transferred %d\n",
            __FUNCTION__, block_mig_state.submitted, block_mig_state.read_done,
            block_mig_state.transferred);

    while ((blk = QSIMPLEQ_FIRST(&block_mig_state.blk_list)) != NULL) {
        if (qemu_file_rate_limit(f)) {
            break;
        }
        if (blk->ret < 0) {
            qemu_file_set_error(f);
            break;
        }
        blk_send(f, blk);

        QSIMPLEQ_REMOVE_HEAD(&block_mig_state.blk_list, entry);
        qemu_free(blk->buf);
        qemu_free(blk);

        block_mig_state.read_done--;
        block_mig_state.transferred++;
        assert(block_mig_state.read_done >= 0);
    }

    dprintf("%s Exit submitted %d read_done %d transferred %d\n", __FUNCTION__,
            block_mig_state.submitted, block_mig_state.read_done,
            block_mig_state.transferred);
}

static int is_stage2_completed(void)
{
    BlkMigDevState *bmds;

    if (block_mig_state.submitted > 0) {
        return 0;
    }

    QSIMPLEQ_FOREACH(bmds, &block_mig_state.bmds_list, entry) {
        if (bmds->bulk_completed == 0) {
            return 0;
        }
    }

    return 1;
}

static void blk_mig_cleanup(Monitor *mon)
{
    BlkMigDevState *bmds;
    BlkMigBlock *blk;

    while ((bmds = QSIMPLEQ_FIRST(&block_mig_state.bmds_list)) != NULL) {
        QSIMPLEQ_REMOVE_HEAD(&block_mig_state.bmds_list, entry);
        qemu_free(bmds);
    }

    while ((blk = QSIMPLEQ_FIRST(&block_mig_state.blk_list)) != NULL) {
        QSIMPLEQ_REMOVE_HEAD(&block_mig_state.blk_list, entry);
        qemu_free(blk->buf);
        qemu_free(blk);
    }

    set_dirty_tracking(0);

    monitor_printf(mon, "\n");
}

static int block_save_live(Monitor *mon, QEMUFile *f, int stage, void *opaque)
{
    dprintf("Enter save live stage %d submitted %d transferred %d\n",
            stage, block_mig_state.submitted, block_mig_state.transferred);

    if (stage < 0) {
        blk_mig_cleanup(mon);
        return 0;
    }

    if (block_mig_state.blk_enable != 1) {
        /* no need to migrate storage */
        qemu_put_be64(f, BLK_MIG_FLAG_EOS);
        return 1;
    }

    if (stage == 1) {
        init_blk_migration(mon, f);

        /* start track dirty blocks */
        set_dirty_tracking(1);
    }

    flush_blks(f);

    if (qemu_file_has_error(f)) {
        blk_mig_cleanup(mon);
        return 0;
    }

    /* control the rate of transfer */
    while ((block_mig_state.submitted +
            block_mig_state.read_done) * BLOCK_SIZE <
           qemu_file_get_rate_limit(f)) {
        if (blk_mig_save_bulked_block(mon, f, 1) == 0) {
            /* no more bulk blocks for now */
            break;
        }
    }

    flush_blks(f);

    if (qemu_file_has_error(f)) {
        blk_mig_cleanup(mon);
        return 0;
    }

    if (stage == 3) {
        while (blk_mig_save_bulked_block(mon, f, 0) != 0) {
            /* empty */
        }

        blk_mig_save_dirty_blocks(mon, f);
        blk_mig_cleanup(mon);

        if (qemu_file_has_error(f)) {
            return 0;
        }

        monitor_printf(mon, "Block migration completed\n");
    }

    qemu_put_be64(f, BLK_MIG_FLAG_EOS);

    return ((stage == 2) && is_stage2_completed());
}

static int block_load(QEMUFile *f, void *opaque, int version_id)
{
    int len, flags;
    char device_name[256];
    int64_t addr;
    BlockDriverState *bs;
    uint8_t *buf;

    do {
        addr = qemu_get_be64(f);

        flags = addr & ~BDRV_SECTOR_MASK;
        addr >>= BDRV_SECTOR_BITS;

        if (flags & BLK_MIG_FLAG_DEVICE_BLOCK) {
            /* get device name */
            len = qemu_get_byte(f);
            qemu_get_buffer(f, (uint8_t *)device_name, len);
            device_name[len] = '\0';

            bs = bdrv_find(device_name);
            if (!bs) {
                fprintf(stderr, "Error unknown block device %s\n",
                        device_name);
                return -EINVAL;
            }

            buf = qemu_malloc(BLOCK_SIZE);

            qemu_get_buffer(f, buf, BLOCK_SIZE);
            bdrv_write(bs, addr, buf, BDRV_SECTORS_PER_DIRTY_CHUNK);

            qemu_free(buf);
        } else if (!(flags & BLK_MIG_FLAG_EOS)) {
            fprintf(stderr, "Unknown flags\n");
            return -EINVAL;
        }
        if (qemu_file_has_error(f)) {
            return -EIO;
        }
    } while (!(flags & BLK_MIG_FLAG_EOS));

    return 0;
}

static void block_set_params(int blk_enable, int shared_base, void *opaque)
{
    block_mig_state.blk_enable = blk_enable;
    block_mig_state.shared_base = shared_base;

    /* shared base means that blk_enable = 1 */
    block_mig_state.blk_enable |= shared_base;
}

void blk_mig_init(void)
{
    QSIMPLEQ_INIT(&block_mig_state.bmds_list);
    QSIMPLEQ_INIT(&block_mig_state.blk_list);

    register_savevm_live("block", 0, 1, block_set_params, block_save_live,
                         NULL, block_load, &block_mig_state);
}

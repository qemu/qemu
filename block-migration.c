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
    int64_t print_completion;
} BlkMigState;

static BlkMigState block_mig_state;

static void blk_mig_read_cb(void *opaque, int ret)
{
    BlkMigBlock *blk = opaque;

    blk->ret = ret;

    QSIMPLEQ_INSERT_TAIL(&block_mig_state.blk_list, blk, entry);

    block_mig_state.submitted--;
    block_mig_state.read_done++;
    assert(block_mig_state.submitted >= 0);
}

static int mig_read_device_bulk(QEMUFile *f, BlkMigDevState *bms)
{
    int nr_sectors;
    int64_t total_sectors, cur_sector = 0;
    BlockDriverState *bs = bms->bs;
    BlkMigBlock *blk;

    blk = qemu_malloc(sizeof(BlkMigBlock));
    blk->buf = qemu_malloc(BLOCK_SIZE);

    cur_sector = bms->cur_sector;
    total_sectors = bms->total_sectors;

    if (bms->shared_base) {
        while (cur_sector < total_sectors &&
               !bdrv_is_allocated(bms->bs, cur_sector,
                                  MAX_IS_ALLOCATED_SEARCH, &nr_sectors)) {
            cur_sector += nr_sectors;
        }
    }

    if (cur_sector >= total_sectors) {
        bms->cur_sector = total_sectors;
        qemu_free(blk->buf);
        qemu_free(blk);
        return 1;
    }

    if (cur_sector >= block_mig_state.print_completion) {
        printf("Completed %" PRId64 " %%\r", cur_sector * 100 / total_sectors);
        fflush(stdout);
        block_mig_state.print_completion +=
            (BDRV_SECTORS_PER_DIRTY_CHUNK * 10000);
    }

    /* we are going to transfer a full block even if it is not allocated */
    nr_sectors = BDRV_SECTORS_PER_DIRTY_CHUNK;

    cur_sector &= ~((int64_t)BDRV_SECTORS_PER_DIRTY_CHUNK - 1);

    if (total_sectors - cur_sector < BDRV_SECTORS_PER_DIRTY_CHUNK) {
        nr_sectors = (total_sectors - cur_sector);
    }

    bms->cur_sector = cur_sector + nr_sectors;
    blk->sector = cur_sector;
    blk->bmds = bms;

    blk->iov.iov_base = blk->buf;
    blk->iov.iov_len = nr_sectors * BDRV_SECTOR_SIZE;
    qemu_iovec_init_external(&blk->qiov, &blk->iov, 1);

    blk->aiocb = bdrv_aio_readv(bs, cur_sector, &blk->qiov,
                                nr_sectors, blk_mig_read_cb, blk);

    if (!blk->aiocb) {
        printf("Error reading sector %" PRId64 "\n", cur_sector);
        qemu_free(blk->buf);
        qemu_free(blk);
        return 0;
    }

    bdrv_reset_dirty(bms->bs, cur_sector, nr_sectors);
    block_mig_state.submitted++;

    return (bms->cur_sector >= total_sectors);
}

static int mig_save_device_bulk(QEMUFile *f, BlkMigDevState *bmds)
{
    int len, nr_sectors;
    int64_t total_sectors = bmds->total_sectors, cur_sector = 0;
    uint8_t *tmp_buf = NULL;
    BlockDriverState *bs = bmds->bs;

    tmp_buf = qemu_malloc(BLOCK_SIZE);

    cur_sector = bmds->cur_sector;

    if (bmds->shared_base) {
        while (cur_sector < total_sectors &&
               !bdrv_is_allocated(bmds->bs, cur_sector,
                                  MAX_IS_ALLOCATED_SEARCH, &nr_sectors)) {
            cur_sector += nr_sectors;
        }
    }

    if (cur_sector >= total_sectors) {
        bmds->cur_sector = total_sectors;
        qemu_free(tmp_buf);
        return 1;
    }

    if (cur_sector >= block_mig_state.print_completion) {
        printf("Completed %" PRId64 " %%\r", cur_sector * 100 / total_sectors);
        fflush(stdout);
        block_mig_state.print_completion +=
            (BDRV_SECTORS_PER_DIRTY_CHUNK * 10000);
    }

    cur_sector &= ~((int64_t)BDRV_SECTORS_PER_DIRTY_CHUNK - 1);

    /* we are going to transfer a full block even if it is not allocated */
    nr_sectors = BDRV_SECTORS_PER_DIRTY_CHUNK;

    if (total_sectors - cur_sector < BDRV_SECTORS_PER_DIRTY_CHUNK) {
        nr_sectors = (total_sectors - cur_sector);
    }

    if (bdrv_read(bs, cur_sector, tmp_buf, nr_sectors) < 0) {
        printf("Error reading sector %" PRId64 "\n", cur_sector);
    }

    bdrv_reset_dirty(bs, cur_sector, nr_sectors);

    /* sector number and flags */
    qemu_put_be64(f, (cur_sector << BDRV_SECTOR_BITS)
                     | BLK_MIG_FLAG_DEVICE_BLOCK);

    /* device name */
    len = strlen(bs->device_name);
    qemu_put_byte(f, len);
    qemu_put_buffer(f, (uint8_t *)bs->device_name, len);

    qemu_put_buffer(f, tmp_buf, BLOCK_SIZE);

    bmds->cur_sector = cur_sector + BDRV_SECTORS_PER_DIRTY_CHUNK;

    qemu_free(tmp_buf);

    return (bmds->cur_sector >= total_sectors);
}

static void send_blk(QEMUFile *f, BlkMigBlock * blk)
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

static void set_dirty_tracking(int enable)
{
    BlkMigDevState *bmds;

    QSIMPLEQ_FOREACH(bmds, &block_mig_state.bmds_list, entry) {
        bdrv_set_dirty_tracking(bmds->bs, enable);
    }
}

static void init_blk_migration(QEMUFile *f)
{
    BlkMigDevState *bmds;
    BlockDriverState *bs;

    block_mig_state.submitted = 0;
    block_mig_state.read_done = 0;
    block_mig_state.transferred = 0;
    block_mig_state.print_completion = 0;

    for (bs = bdrv_first; bs != NULL; bs = bs->next) {
        if (bs->type == BDRV_TYPE_HD) {
            bmds = qemu_mallocz(sizeof(BlkMigDevState));
            bmds->bs = bs;
            bmds->bulk_completed = 0;
            bmds->total_sectors = bdrv_getlength(bs) >> BDRV_SECTOR_BITS;
            bmds->shared_base = block_mig_state.shared_base;

            if (bmds->shared_base) {
                printf("Start migration for %s with shared base image\n",
                       bs->device_name);
            } else {
                printf("Start full migration for %s\n", bs->device_name);
            }

            QSIMPLEQ_INSERT_TAIL(&block_mig_state.bmds_list, bmds, entry);
        }
    }
}

static int blk_mig_save_bulked_block(QEMUFile *f, int is_async)
{
    BlkMigDevState *bmds;

    QSIMPLEQ_FOREACH(bmds, &block_mig_state.bmds_list, entry) {
        if (bmds->bulk_completed == 0) {
            if (is_async) {
                if (mig_read_device_bulk(f, bmds) == 1) {
                    /* completed bulk section for this device */
                    bmds->bulk_completed = 1;
                }
            } else {
                if (mig_save_device_bulk(f, bmds) == 1) {
                    /* completed bulk section for this device */
                    bmds->bulk_completed = 1;
                }
            }
            return 1;
        }
    }

    /* we reached here means bulk is completed */
    return 0;
}

#define MAX_NUM_BLOCKS 4

static void blk_mig_save_dirty_blocks(QEMUFile *f)
{
    BlkMigDevState *bmds;
    uint8_t *buf;
    int64_t sector;
    int len;

    buf = qemu_malloc(BLOCK_SIZE);

    QSIMPLEQ_FOREACH(bmds, &block_mig_state.bmds_list, entry) {
        for (sector = 0; sector < bmds->cur_sector;) {
            if (bdrv_get_dirty(bmds->bs, sector)) {
                if (bdrv_read(bmds->bs, sector, buf,
                              BDRV_SECTORS_PER_DIRTY_CHUNK) < 0) {
                    /* FIXME: add error handling */
                }

                /* sector number and flags */
                qemu_put_be64(f, (sector << BDRV_SECTOR_BITS)
                                 | BLK_MIG_FLAG_DEVICE_BLOCK);

                /* device name */
                len = strlen(bmds->bs->device_name);
                qemu_put_byte(f, len);
                qemu_put_buffer(f, (uint8_t *)bmds->bs->device_name, len);

                qemu_put_buffer(f, buf, BLOCK_SIZE);

                bdrv_reset_dirty(bmds->bs, sector,
                                 BDRV_SECTORS_PER_DIRTY_CHUNK);
            }
            sector += BDRV_SECTORS_PER_DIRTY_CHUNK;
        }
    }

    qemu_free(buf);
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
        send_blk(f, blk);

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

static int block_save_live(QEMUFile *f, int stage, void *opaque)
{
    dprintf("Enter save live stage %d submitted %d transferred %d\n",
            stage, block_mig_state.submitted, block_mig_state.transferred);

    if (block_mig_state.blk_enable != 1) {
        /* no need to migrate storage */
        qemu_put_be64(f, BLK_MIG_FLAG_EOS);
        return 1;
    }

    if (stage == 1) {
        init_blk_migration(f);

        /* start track dirty blocks */
        set_dirty_tracking(1);
    }

    flush_blks(f);

    /* control the rate of transfer */
    while ((block_mig_state.submitted +
            block_mig_state.read_done) * BLOCK_SIZE <
           qemu_file_get_rate_limit(f)) {
        if (blk_mig_save_bulked_block(f, 1) == 0) {
            /* no more bulk blocks for now */
            break;
        }
    }

    flush_blks(f);

    if (stage == 3) {
        while (blk_mig_save_bulked_block(f, 0) != 0) {
            /* empty */
        }

        blk_mig_save_dirty_blocks(f);

        /* stop track dirty blocks */
        set_dirty_tracking(0);

        printf("\nBlock migration completed\n");
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

            buf = qemu_malloc(BLOCK_SIZE);

            qemu_get_buffer(f, buf, BLOCK_SIZE);
            if (bs != NULL) {
                bdrv_write(bs, addr, buf, BDRV_SECTORS_PER_DIRTY_CHUNK);
            } else {
                printf("Error unknown block device %s\n", device_name);
                /* FIXME: add error handling */
            }

            qemu_free(buf);
        } else if (!(flags & BLK_MIG_FLAG_EOS)) {
            printf("Unknown flags\n");
            /* FIXME: add error handling */
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

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
#include "block-migration.h"
#include <assert.h>

#define SECTOR_BITS 9
#define SECTOR_SIZE (1 << SECTOR_BITS)
#define SECTOR_MASK ~(SECTOR_SIZE - 1);

#define BLOCK_SIZE (block_mig_state->sectors_per_block << SECTOR_BITS) 

#define BLK_MIG_FLAG_DEVICE_BLOCK       0x01
#define BLK_MIG_FLAG_EOS                0x02

#define MAX_IS_ALLOCATED_SEARCH 65536
#define MAX_BLOCKS_READ 10000
#define BLOCKS_READ_CHANGE 100
#define INITIAL_BLOCKS_READ 100

//#define DEBUG_BLK_MIGRATION

#ifdef DEBUG_BLK_MIGRATION
#define dprintf(fmt, ...)						\
    do { printf("blk_migration: " fmt, ## __VA_ARGS__); } while (0)
#else
#define dprintf(fmt, ...)			\
    do { } while (0)
#endif

typedef struct BlkMigBlock {
    uint8_t *buf;
    BlkMigDevState *bmds;
    int64_t sector;
    struct iovec iov;
    QEMUIOVector qiov;
    BlockDriverAIOCB *aiocb;
    int ret;
    struct BlkMigBlock *next;
} BlkMigBlock;

typedef struct BlkMigState {
    int bulk_completed;
    int blk_enable;
    int shared_base;
    int no_dirty;
    QEMUFile *load_file;
    BlkMigDevState *bmds_first;
    int sectors_per_block;
    BlkMigBlock *first_blk;
    BlkMigBlock *last_blk;
    int submitted;
    int read_done;
    int transferred;
    int64_t print_completion;
} BlkMigState;

static BlkMigState *block_mig_state = NULL;  

static void blk_mig_read_cb(void *opaque, int ret)
{
    BlkMigBlock *blk = opaque;
  
    blk->ret = ret;
    
    /* insert at the end */
    if(block_mig_state->last_blk == NULL) {
        block_mig_state->first_blk = blk;
        block_mig_state->last_blk = blk;
    } else {
        block_mig_state->last_blk->next = blk;
        block_mig_state->last_blk = blk;
    }
    
    block_mig_state->submitted--;
    block_mig_state->read_done++;
    assert(block_mig_state->submitted >= 0);
    
    return;
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
    total_sectors = bdrv_getlength(bs) >> SECTOR_BITS;
  
    if(bms->shared_base) {
        while(cur_sector < bms->total_sectors && 
              !bdrv_is_allocated(bms->bs, cur_sector, 
                                 MAX_IS_ALLOCATED_SEARCH, &nr_sectors)) {
            cur_sector += nr_sectors;
        }
    }
    
    if(cur_sector >= total_sectors) {
        bms->cur_sector = total_sectors;
        qemu_free(blk->buf);
        qemu_free(blk);
        return 1;
    }
  
    if(cur_sector >= block_mig_state->print_completion) {
        printf("Completed %" PRId64 " %%\r", cur_sector * 100 / total_sectors);
        fflush(stdout);
        block_mig_state->print_completion += 
            (block_mig_state->sectors_per_block * 10000);
    }
  
    /* we going to transfder BLOCK_SIZE any way even if it is not allocated */
    nr_sectors = block_mig_state->sectors_per_block;

    cur_sector &= ~((int64_t)block_mig_state->sectors_per_block -1);
    
    if(total_sectors - cur_sector < block_mig_state->sectors_per_block) {
        nr_sectors = (total_sectors - cur_sector);
    }
  
    bms->cur_sector = cur_sector + nr_sectors;
    blk->sector = cur_sector;
    blk->bmds = bms;
    blk->next = NULL;
  
    blk->iov.iov_base = blk->buf;
    blk->iov.iov_len = nr_sectors * SECTOR_SIZE;
    qemu_iovec_init_external(&blk->qiov, &blk->iov, 1);
  
    blk->aiocb = bdrv_aio_readv(bs, cur_sector, &blk->qiov,
                                nr_sectors, blk_mig_read_cb, blk);
  
    if(!blk->aiocb) {
        printf("Error reading sector %" PRId64 "\n", cur_sector);
        qemu_free(blk->buf);
        qemu_free(blk);
        return 0;
    }

    bdrv_reset_dirty(bms->bs, cur_sector, nr_sectors);
    block_mig_state->submitted++;
  
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
    
    if(bmds->shared_base) {
        while(cur_sector < bmds->total_sectors && 
              !bdrv_is_allocated(bmds->bs, cur_sector, 
                                 MAX_IS_ALLOCATED_SEARCH, &nr_sectors)) {
            cur_sector += nr_sectors;
        }
    }
    
    if(cur_sector >= total_sectors) {
        bmds->cur_sector = total_sectors;
        qemu_free(tmp_buf);
        return 1;
    }
    
    if(cur_sector >= block_mig_state->print_completion) {
        printf("Completed %" PRId64 " %%\r", cur_sector * 100 / total_sectors);
        fflush(stdout);
        block_mig_state->print_completion += 
            (block_mig_state->sectors_per_block * 10000);
    }
    
    cur_sector &= ~((int64_t)block_mig_state->sectors_per_block -1);
        
    /* we going to transfer 
       BLOCK_SIZE 
       any way even if it is not allocated */
    nr_sectors = block_mig_state->sectors_per_block;
  
    if(total_sectors - cur_sector < block_mig_state->sectors_per_block) {
        nr_sectors = (total_sectors - cur_sector);
    }
  
    if(bdrv_read(bs, cur_sector, tmp_buf, nr_sectors) < 0) {
        printf("Error reading sector %" PRId64 "\n", cur_sector);
    }

    bdrv_reset_dirty(bs, cur_sector, nr_sectors);
  
    /* Device name */
    qemu_put_be64(f,(cur_sector << SECTOR_BITS) | BLK_MIG_FLAG_DEVICE_BLOCK);
  
    len = strlen(bs->device_name);
    qemu_put_byte(f, len);
    qemu_put_buffer(f, (uint8_t *)bs->device_name, len);
  
    qemu_put_buffer(f, tmp_buf, 
                    BLOCK_SIZE);
    
    bmds->cur_sector = cur_sector + block_mig_state->sectors_per_block;
  
    qemu_free(tmp_buf);
  
    return (bmds->cur_sector >= total_sectors);
}

static void send_blk(QEMUFile *f, BlkMigBlock * blk)
{
    int len;
  
    /* Device name */ 
    qemu_put_be64(f,(blk->sector << SECTOR_BITS) | BLK_MIG_FLAG_DEVICE_BLOCK);
  
    len = strlen(blk->bmds->bs->device_name);
    qemu_put_byte(f, len);
    qemu_put_buffer(f, (uint8_t *)blk->bmds->bs->device_name, len);
  
    qemu_put_buffer(f, blk->buf, 
                    BLOCK_SIZE);
  
    return;
}

static void blk_mig_save_dev_info(QEMUFile *f, BlkMigDevState *bmds)
{
}

static void set_dirty_tracking(int enable)
{
    BlkMigDevState *bmds;
    for(bmds = block_mig_state->bmds_first; bmds != NULL; bmds = bmds->next) {
        bdrv_set_dirty_tracking(bmds->bs,enable);
    }
    
    return;
}

static void init_blk_migration(QEMUFile *f)
{
    BlkMigDevState **pbmds, *bmds;
    BlockDriverState *bs;
    
    for (bs = bdrv_first; bs != NULL; bs = bs->next) {
        if(bs->type == BDRV_TYPE_HD) {
            bmds = qemu_mallocz(sizeof(BlkMigDevState));
            bmds->bs = bs;
            bmds->bulk_completed = 0;
            bmds->total_sectors = bdrv_getlength(bs) >> SECTOR_BITS;
            bmds->shared_base = block_mig_state->shared_base;
	          
            if(bmds->shared_base) {
                printf("Start migration for %s with shared base image\n", 
                       bs->device_name);
            } else {
                printf("Start full migration for %s\n", bs->device_name);
            }
      
            /* insert at the end */
            pbmds = &block_mig_state->bmds_first;
            while (*pbmds != NULL)
                pbmds = &(*pbmds)->next;
            *pbmds = bmds;
      
            blk_mig_save_dev_info(f, bmds);
	    
        }
    } 
    
    block_mig_state->sectors_per_block = bdrv_get_sectors_per_chunk();
    
    return;
}

static int blk_mig_save_bulked_block(QEMUFile *f, int is_async)
{
    BlkMigDevState *bmds;

    for (bmds = block_mig_state->bmds_first; bmds != NULL; bmds = bmds->next) {
        if(bmds->bulk_completed == 0) {
            if(is_async) {
                if(mig_read_device_bulk(f, bmds) == 1) {
                    /* completed bulk section for this device */
                    bmds->bulk_completed = 1;
                }
            } else {
                if(mig_save_device_bulk(f,bmds) == 1) {
                    /* completed bulk section for this device */
                    bmds->bulk_completed = 1;
                }
            }
            return 1;
        }
    }
  
    /* we reached here means bulk is completed */
    block_mig_state->bulk_completed = 1;
  
    return 0;
    
}

#define MAX_NUM_BLOCKS 4

static void blk_mig_save_dirty_blocks(QEMUFile *f)
{
    BlkMigDevState *bmds;
    uint8_t buf[BLOCK_SIZE];
    int64_t sector;
    int len;
    
    for(bmds = block_mig_state->bmds_first; bmds != NULL; bmds = bmds->next) {
        for(sector = 0; sector < bmds->cur_sector;) {
	    
            if(bdrv_get_dirty(bmds->bs,sector)) {
		
                if(bdrv_read(bmds->bs, sector, buf, 
                             block_mig_state->sectors_per_block) < 0) {
                }
		
                /* device name */
                qemu_put_be64(f,(sector << SECTOR_BITS) 
                              | BLK_MIG_FLAG_DEVICE_BLOCK);
	
                len = strlen(bmds->bs->device_name);
	
                qemu_put_byte(f, len);
                qemu_put_buffer(f, (uint8_t *)bmds->bs->device_name, len);
	
                qemu_put_buffer(f, buf, 
                                (block_mig_state->sectors_per_block * 
                                 SECTOR_SIZE));
		
                bdrv_reset_dirty(bmds->bs, sector, 
                                 block_mig_state->sectors_per_block);
	
                sector += block_mig_state->sectors_per_block;
            } else {
                /* sector is clean */
                sector += block_mig_state->sectors_per_block;
            }  
        }
    }
    
    return;
}

static void flush_blks(QEMUFile* f)
{
    BlkMigBlock *blk, *tmp;
    
    dprintf("%s Enter submitted %d read_done %d transfered\n", __FUNCTION__, 
            submitted, read_done, transfered);
  
    for(blk = block_mig_state->first_blk; 
        blk != NULL && !qemu_file_rate_limit(f); blk = tmp) {
        send_blk(f, blk);
    
        tmp = blk->next;
        qemu_free(blk->buf);
        qemu_free(blk);
    
        block_mig_state->read_done--;
        block_mig_state->transferred++;
        assert(block_mig_state->read_done >= 0);
    }
    block_mig_state->first_blk = blk;
  
    if(block_mig_state->first_blk == NULL) {
        block_mig_state->last_blk = NULL;
    }

    dprintf("%s Exit submitted %d read_done %d transferred%d\n", __FUNCTION__, 
            block_mig_state->submitted, block_mig_state->read_done, 
            block_mig_state->transferred);

    return;
}

static int is_stage2_completed(void)
{
    BlkMigDevState *bmds;
  
    if(block_mig_state->submitted > 0) {
        return 0;
    }
  
    for (bmds = block_mig_state->bmds_first; bmds != NULL; bmds = bmds->next) {
        if(bmds->bulk_completed == 0) {
            return 0;
        }
    }
    
    return 1;
}

static int block_save_live(QEMUFile *f, int stage, void *opaque)
{
    int ret = 1;
    
    dprintf("Enter save live stage %d submitted %d transferred %d\n", stage, 
            submitted, transferred);
  
    if(block_mig_state->blk_enable != 1) {
        /* no need to migrate storage */
    
        qemu_put_be64(f,BLK_MIG_FLAG_EOS);
        return 1;
    }
  
    if(stage == 1) {
        init_blk_migration(f);
	
        /* start track dirty blocks */
        set_dirty_tracking(1);
	
    }

    flush_blks(f);
  
    /* control the rate of transfer */
    while ((block_mig_state->submitted + block_mig_state->read_done) * 
           (BLOCK_SIZE) < 
           (qemu_file_get_rate_limit(f))) {
	
        ret = blk_mig_save_bulked_block(f, 1);
	
        if (ret == 0) /* no more bulk blocks for now*/
            break;
    }
  
    flush_blks(f);
    
    if(stage == 3) {
	
        while(blk_mig_save_bulked_block(f, 0) != 0);
	
        blk_mig_save_dirty_blocks(f);
	
        /* stop track dirty blocks */
        set_dirty_tracking(0);;
	
        printf("\nBlock migration completed\n");  
    }
  
    qemu_put_be64(f,BLK_MIG_FLAG_EOS);
  
    return ((stage == 2) && is_stage2_completed());
}

static int block_load(QEMUFile *f, void *opaque, int version_id)
{
    int len, flags;
    char device_name[256];
    int64_t addr;
    BlockDriverState *bs;
    uint8_t *buf;
    
    block_mig_state->sectors_per_block = bdrv_get_sectors_per_chunk();
    buf = qemu_malloc(BLOCK_SIZE);
    
    do {
    
        addr = qemu_get_be64(f);
    
        flags = addr & ~SECTOR_MASK;
        addr &= SECTOR_MASK;
    
        if(flags & BLK_MIG_FLAG_DEVICE_BLOCK) {
	    
            /* get device name */
            len = qemu_get_byte(f);
      
            qemu_get_buffer(f, (uint8_t *)device_name, len);
            device_name[len] = '\0';
      
            bs = bdrv_find(device_name);
      
            qemu_get_buffer(f, buf, 
                            BLOCK_SIZE);
            if(bs != NULL) {
	
                bdrv_write(bs, (addr >> SECTOR_BITS), 
                           buf, block_mig_state->sectors_per_block);
            } else {
                printf("Error unknown block device %s\n", device_name);
            }
        } else if(flags & BLK_MIG_FLAG_EOS) {
	    
        } else {
            printf("Unknown flags\n");
        }
    } while(!(flags & BLK_MIG_FLAG_EOS));
  
    qemu_free(buf);

    return 0;
}

static void block_set_params(int blk_enable, int shared_base, void *opaque)
{
    assert(opaque == block_mig_state);

    block_mig_state->blk_enable = blk_enable;
    block_mig_state->shared_base = shared_base;
  
    /* shared base means that blk_enable = 1 */
    block_mig_state->blk_enable |= shared_base;
  
    return;
}

void blk_mig_info(void)
{
    BlockDriverState *bs;
  
    for (bs = bdrv_first; bs != NULL; bs = bs->next) {
        printf("Device %s\n", bs->device_name);
        if(bs->type == BDRV_TYPE_HD) {
            printf("device %s format %s\n", 
                   bs->device_name, bs->drv->format_name);
        }
    }
}

void blk_mig_init(void)
{ 
    
    block_mig_state = qemu_mallocz(sizeof(BlkMigState));
    
    register_savevm_live("block", 0, 1, block_set_params, block_save_live, 
                         NULL, block_load, block_mig_state);

 
}

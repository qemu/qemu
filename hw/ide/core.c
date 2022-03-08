/*
 * QEMU IDE disk and CD/DVD-ROM Emulator
 *
 * Copyright (c) 2003 Fabrice Bellard
 * Copyright (c) 2006 Openedhand Ltd.
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
#include "hw/isa/isa.h"
#include "migration/vmstate.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include "qemu/timer.h"
#include "qemu/hw-version.h"
#include "qemu/memalign.h"
#include "sysemu/sysemu.h"
#include "sysemu/blockdev.h"
#include "sysemu/dma.h"
#include "hw/block/block.h"
#include "sysemu/block-backend.h"
#include "qapi/error.h"
#include "qemu/cutils.h"
#include "sysemu/replay.h"
#include "sysemu/runstate.h"
#include "hw/ide/internal.h"
#include "trace.h"

/* These values were based on a Seagate ST3500418AS but have been modified
   to make more sense in QEMU */
static const int smart_attributes[][12] = {
    /* id,  flags, hflags, val, wrst, raw (6 bytes), threshold */
    /* raw read error rate*/
    { 0x01, 0x03, 0x00, 0x64, 0x64, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06},
    /* spin up */
    { 0x03, 0x03, 0x00, 0x64, 0x64, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    /* start stop count */
    { 0x04, 0x02, 0x00, 0x64, 0x64, 0x64, 0x00, 0x00, 0x00, 0x00, 0x00, 0x14},
    /* remapped sectors */
    { 0x05, 0x03, 0x00, 0x64, 0x64, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x24},
    /* power on hours */
    { 0x09, 0x03, 0x00, 0x64, 0x64, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    /* power cycle count */
    { 0x0c, 0x03, 0x00, 0x64, 0x64, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    /* airflow-temperature-celsius */
    { 190,  0x03, 0x00, 0x45, 0x45, 0x1f, 0x00, 0x1f, 0x1f, 0x00, 0x00, 0x32},
};

const char *IDE_DMA_CMD_lookup[IDE_DMA__COUNT] = {
    [IDE_DMA_READ] = "DMA READ",
    [IDE_DMA_WRITE] = "DMA WRITE",
    [IDE_DMA_TRIM] = "DMA TRIM",
    [IDE_DMA_ATAPI] = "DMA ATAPI"
};

static const char *IDE_DMA_CMD_str(enum ide_dma_cmd enval)
{
    if ((unsigned)enval < IDE_DMA__COUNT) {
        return IDE_DMA_CMD_lookup[enval];
    }
    return "DMA UNKNOWN CMD";
}

static void ide_dummy_transfer_stop(IDEState *s);

static void padstr(char *str, const char *src, int len)
{
    int i, v;
    for(i = 0; i < len; i++) {
        if (*src)
            v = *src++;
        else
            v = ' ';
        str[i^1] = v;
    }
}

static void put_le16(uint16_t *p, unsigned int v)
{
    *p = cpu_to_le16(v);
}

static void ide_identify_size(IDEState *s)
{
    uint16_t *p = (uint16_t *)s->identify_data;
    int64_t nb_sectors_lba28 = s->nb_sectors;
    if (nb_sectors_lba28 >= 1 << 28) {
        nb_sectors_lba28 = (1 << 28) - 1;
    }
    put_le16(p + 60, nb_sectors_lba28);
    put_le16(p + 61, nb_sectors_lba28 >> 16);
    put_le16(p + 100, s->nb_sectors);
    put_le16(p + 101, s->nb_sectors >> 16);
    put_le16(p + 102, s->nb_sectors >> 32);
    put_le16(p + 103, s->nb_sectors >> 48);
}

static void ide_identify(IDEState *s)
{
    uint16_t *p;
    unsigned int oldsize;
    IDEDevice *dev = s->unit ? s->bus->slave : s->bus->master;

    p = (uint16_t *)s->identify_data;
    if (s->identify_set) {
        goto fill_buffer;
    }
    memset(p, 0, sizeof(s->identify_data));

    put_le16(p + 0, 0x0040);
    put_le16(p + 1, s->cylinders);
    put_le16(p + 3, s->heads);
    put_le16(p + 4, 512 * s->sectors); /* XXX: retired, remove ? */
    put_le16(p + 5, 512); /* XXX: retired, remove ? */
    put_le16(p + 6, s->sectors);
    padstr((char *)(p + 10), s->drive_serial_str, 20); /* serial number */
    put_le16(p + 20, 3); /* XXX: retired, remove ? */
    put_le16(p + 21, 512); /* cache size in sectors */
    put_le16(p + 22, 4); /* ecc bytes */
    padstr((char *)(p + 23), s->version, 8); /* firmware version */
    padstr((char *)(p + 27), s->drive_model_str, 40); /* model */
#if MAX_MULT_SECTORS > 1
    put_le16(p + 47, 0x8000 | MAX_MULT_SECTORS);
#endif
    put_le16(p + 48, 1); /* dword I/O */
    put_le16(p + 49, (1 << 11) | (1 << 9) | (1 << 8)); /* DMA and LBA supported */
    put_le16(p + 51, 0x200); /* PIO transfer cycle */
    put_le16(p + 52, 0x200); /* DMA transfer cycle */
    put_le16(p + 53, 1 | (1 << 1) | (1 << 2)); /* words 54-58,64-70,88 are valid */
    put_le16(p + 54, s->cylinders);
    put_le16(p + 55, s->heads);
    put_le16(p + 56, s->sectors);
    oldsize = s->cylinders * s->heads * s->sectors;
    put_le16(p + 57, oldsize);
    put_le16(p + 58, oldsize >> 16);
    if (s->mult_sectors)
        put_le16(p + 59, 0x100 | s->mult_sectors);
    /* *(p + 60) := nb_sectors       -- see ide_identify_size */
    /* *(p + 61) := nb_sectors >> 16 -- see ide_identify_size */
    put_le16(p + 62, 0x07); /* single word dma0-2 supported */
    put_le16(p + 63, 0x07); /* mdma0-2 supported */
    put_le16(p + 64, 0x03); /* pio3-4 supported */
    put_le16(p + 65, 120);
    put_le16(p + 66, 120);
    put_le16(p + 67, 120);
    put_le16(p + 68, 120);
    if (dev && dev->conf.discard_granularity) {
        put_le16(p + 69, (1 << 14)); /* determinate TRIM behavior */
    }

    if (s->ncq_queues) {
        put_le16(p + 75, s->ncq_queues - 1);
        /* NCQ supported */
        put_le16(p + 76, (1 << 8));
    }

    put_le16(p + 80, 0xf0); /* ata3 -> ata6 supported */
    put_le16(p + 81, 0x16); /* conforms to ata5 */
    /* 14=NOP supported, 5=WCACHE supported, 0=SMART supported */
    put_le16(p + 82, (1 << 14) | (1 << 5) | 1);
    /* 13=flush_cache_ext,12=flush_cache,10=lba48 */
    put_le16(p + 83, (1 << 14) | (1 << 13) | (1 <<12) | (1 << 10));
    /* 14=set to 1, 8=has WWN, 1=SMART self test, 0=SMART error logging */
    if (s->wwn) {
        put_le16(p + 84, (1 << 14) | (1 << 8) | 0);
    } else {
        put_le16(p + 84, (1 << 14) | 0);
    }
    /* 14 = NOP supported, 5=WCACHE enabled, 0=SMART feature set enabled */
    if (blk_enable_write_cache(s->blk)) {
        put_le16(p + 85, (1 << 14) | (1 << 5) | 1);
    } else {
        put_le16(p + 85, (1 << 14) | 1);
    }
    /* 13=flush_cache_ext,12=flush_cache,10=lba48 */
    put_le16(p + 86, (1 << 13) | (1 <<12) | (1 << 10));
    /* 14=set to 1, 8=has WWN, 1=SMART self test, 0=SMART error logging */
    if (s->wwn) {
        put_le16(p + 87, (1 << 14) | (1 << 8) | 0);
    } else {
        put_le16(p + 87, (1 << 14) | 0);
    }
    put_le16(p + 88, 0x3f | (1 << 13)); /* udma5 set and supported */
    put_le16(p + 93, 1 | (1 << 14) | 0x2000);
    /* *(p + 100) := nb_sectors       -- see ide_identify_size */
    /* *(p + 101) := nb_sectors >> 16 -- see ide_identify_size */
    /* *(p + 102) := nb_sectors >> 32 -- see ide_identify_size */
    /* *(p + 103) := nb_sectors >> 48 -- see ide_identify_size */

    if (dev && dev->conf.physical_block_size)
        put_le16(p + 106, 0x6000 | get_physical_block_exp(&dev->conf));
    if (s->wwn) {
        /* LE 16-bit words 111-108 contain 64-bit World Wide Name */
        put_le16(p + 108, s->wwn >> 48);
        put_le16(p + 109, s->wwn >> 32);
        put_le16(p + 110, s->wwn >> 16);
        put_le16(p + 111, s->wwn);
    }
    if (dev && dev->conf.discard_granularity) {
        put_le16(p + 169, 1); /* TRIM support */
    }
    if (dev) {
        put_le16(p + 217, dev->rotation_rate); /* Nominal media rotation rate */
    }

    ide_identify_size(s);
    s->identify_set = 1;

fill_buffer:
    memcpy(s->io_buffer, p, sizeof(s->identify_data));
}

static void ide_atapi_identify(IDEState *s)
{
    uint16_t *p;

    p = (uint16_t *)s->identify_data;
    if (s->identify_set) {
        goto fill_buffer;
    }
    memset(p, 0, sizeof(s->identify_data));

    /* Removable CDROM, 50us response, 12 byte packets */
    put_le16(p + 0, (2 << 14) | (5 << 8) | (1 << 7) | (2 << 5) | (0 << 0));
    padstr((char *)(p + 10), s->drive_serial_str, 20); /* serial number */
    put_le16(p + 20, 3); /* buffer type */
    put_le16(p + 21, 512); /* cache size in sectors */
    put_le16(p + 22, 4); /* ecc bytes */
    padstr((char *)(p + 23), s->version, 8); /* firmware version */
    padstr((char *)(p + 27), s->drive_model_str, 40); /* model */
    put_le16(p + 48, 1); /* dword I/O (XXX: should not be set on CDROM) */
#ifdef USE_DMA_CDROM
    put_le16(p + 49, 1 << 9 | 1 << 8); /* DMA and LBA supported */
    put_le16(p + 53, 7); /* words 64-70, 54-58, 88 valid */
    put_le16(p + 62, 7);  /* single word dma0-2 supported */
    put_le16(p + 63, 7);  /* mdma0-2 supported */
#else
    put_le16(p + 49, 1 << 9); /* LBA supported, no DMA */
    put_le16(p + 53, 3); /* words 64-70, 54-58 valid */
    put_le16(p + 63, 0x103); /* DMA modes XXX: may be incorrect */
#endif
    put_le16(p + 64, 3); /* pio3-4 supported */
    put_le16(p + 65, 0xb4); /* minimum DMA multiword tx cycle time */
    put_le16(p + 66, 0xb4); /* recommended DMA multiword tx cycle time */
    put_le16(p + 67, 0x12c); /* minimum PIO cycle time without flow control */
    put_le16(p + 68, 0xb4); /* minimum PIO cycle time with IORDY flow control */

    put_le16(p + 71, 30); /* in ns */
    put_le16(p + 72, 30); /* in ns */

    if (s->ncq_queues) {
        put_le16(p + 75, s->ncq_queues - 1);
        /* NCQ supported */
        put_le16(p + 76, (1 << 8));
    }

    put_le16(p + 80, 0x1e); /* support up to ATA/ATAPI-4 */
    if (s->wwn) {
        put_le16(p + 84, (1 << 8)); /* supports WWN for words 108-111 */
        put_le16(p + 87, (1 << 8)); /* WWN enabled */
    }

#ifdef USE_DMA_CDROM
    put_le16(p + 88, 0x3f | (1 << 13)); /* udma5 set and supported */
#endif

    if (s->wwn) {
        /* LE 16-bit words 111-108 contain 64-bit World Wide Name */
        put_le16(p + 108, s->wwn >> 48);
        put_le16(p + 109, s->wwn >> 32);
        put_le16(p + 110, s->wwn >> 16);
        put_le16(p + 111, s->wwn);
    }

    s->identify_set = 1;

fill_buffer:
    memcpy(s->io_buffer, p, sizeof(s->identify_data));
}

static void ide_cfata_identify_size(IDEState *s)
{
    uint16_t *p = (uint16_t *)s->identify_data;
    put_le16(p + 7, s->nb_sectors >> 16);  /* Sectors per card */
    put_le16(p + 8, s->nb_sectors);        /* Sectors per card */
    put_le16(p + 60, s->nb_sectors);       /* Total LBA sectors */
    put_le16(p + 61, s->nb_sectors >> 16); /* Total LBA sectors */
}

static void ide_cfata_identify(IDEState *s)
{
    uint16_t *p;
    uint32_t cur_sec;

    p = (uint16_t *)s->identify_data;
    if (s->identify_set) {
        goto fill_buffer;
    }
    memset(p, 0, sizeof(s->identify_data));

    cur_sec = s->cylinders * s->heads * s->sectors;

    put_le16(p + 0, 0x848a);			/* CF Storage Card signature */
    put_le16(p + 1, s->cylinders);		/* Default cylinders */
    put_le16(p + 3, s->heads);			/* Default heads */
    put_le16(p + 6, s->sectors);		/* Default sectors per track */
    /* *(p + 7) := nb_sectors >> 16 -- see ide_cfata_identify_size */
    /* *(p + 8) := nb_sectors       -- see ide_cfata_identify_size */
    padstr((char *)(p + 10), s->drive_serial_str, 20); /* serial number */
    put_le16(p + 22, 0x0004);			/* ECC bytes */
    padstr((char *) (p + 23), s->version, 8);	/* Firmware Revision */
    padstr((char *) (p + 27), s->drive_model_str, 40);/* Model number */
#if MAX_MULT_SECTORS > 1
    put_le16(p + 47, 0x8000 | MAX_MULT_SECTORS);
#else
    put_le16(p + 47, 0x0000);
#endif
    put_le16(p + 49, 0x0f00);			/* Capabilities */
    put_le16(p + 51, 0x0002);			/* PIO cycle timing mode */
    put_le16(p + 52, 0x0001);			/* DMA cycle timing mode */
    put_le16(p + 53, 0x0003);			/* Translation params valid */
    put_le16(p + 54, s->cylinders);		/* Current cylinders */
    put_le16(p + 55, s->heads);			/* Current heads */
    put_le16(p + 56, s->sectors);		/* Current sectors */
    put_le16(p + 57, cur_sec);			/* Current capacity */
    put_le16(p + 58, cur_sec >> 16);		/* Current capacity */
    if (s->mult_sectors)			/* Multiple sector setting */
        put_le16(p + 59, 0x100 | s->mult_sectors);
    /* *(p + 60) := nb_sectors       -- see ide_cfata_identify_size */
    /* *(p + 61) := nb_sectors >> 16 -- see ide_cfata_identify_size */
    put_le16(p + 63, 0x0203);			/* Multiword DMA capability */
    put_le16(p + 64, 0x0001);			/* Flow Control PIO support */
    put_le16(p + 65, 0x0096);			/* Min. Multiword DMA cycle */
    put_le16(p + 66, 0x0096);			/* Rec. Multiword DMA cycle */
    put_le16(p + 68, 0x00b4);			/* Min. PIO cycle time */
    put_le16(p + 82, 0x400c);			/* Command Set supported */
    put_le16(p + 83, 0x7068);			/* Command Set supported */
    put_le16(p + 84, 0x4000);			/* Features supported */
    put_le16(p + 85, 0x000c);			/* Command Set enabled */
    put_le16(p + 86, 0x7044);			/* Command Set enabled */
    put_le16(p + 87, 0x4000);			/* Features enabled */
    put_le16(p + 91, 0x4060);			/* Current APM level */
    put_le16(p + 129, 0x0002);			/* Current features option */
    put_le16(p + 130, 0x0005);			/* Reassigned sectors */
    put_le16(p + 131, 0x0001);			/* Initial power mode */
    put_le16(p + 132, 0x0000);			/* User signature */
    put_le16(p + 160, 0x8100);			/* Power requirement */
    put_le16(p + 161, 0x8001);			/* CF command set */

    ide_cfata_identify_size(s);
    s->identify_set = 1;

fill_buffer:
    memcpy(s->io_buffer, p, sizeof(s->identify_data));
}

static void ide_set_signature(IDEState *s)
{
    s->select &= ~(ATA_DEV_HS); /* clear head */
    /* put signature */
    s->nsector = 1;
    s->sector = 1;
    if (s->drive_kind == IDE_CD) {
        s->lcyl = 0x14;
        s->hcyl = 0xeb;
    } else if (s->blk) {
        s->lcyl = 0;
        s->hcyl = 0;
    } else {
        s->lcyl = 0xff;
        s->hcyl = 0xff;
    }
}

static bool ide_sect_range_ok(IDEState *s,
                              uint64_t sector, uint64_t nb_sectors)
{
    uint64_t total_sectors;

    blk_get_geometry(s->blk, &total_sectors);
    if (sector > total_sectors || nb_sectors > total_sectors - sector) {
        return false;
    }
    return true;
}

typedef struct TrimAIOCB {
    BlockAIOCB common;
    IDEState *s;
    QEMUBH *bh;
    int ret;
    QEMUIOVector *qiov;
    BlockAIOCB *aiocb;
    int i, j;
} TrimAIOCB;

static void trim_aio_cancel(BlockAIOCB *acb)
{
    TrimAIOCB *iocb = container_of(acb, TrimAIOCB, common);

    /* Exit the loop so ide_issue_trim_cb will not continue  */
    iocb->j = iocb->qiov->niov - 1;
    iocb->i = (iocb->qiov->iov[iocb->j].iov_len / 8) - 1;

    iocb->ret = -ECANCELED;

    if (iocb->aiocb) {
        blk_aio_cancel_async(iocb->aiocb);
        iocb->aiocb = NULL;
    }
}

static const AIOCBInfo trim_aiocb_info = {
    .aiocb_size         = sizeof(TrimAIOCB),
    .cancel_async       = trim_aio_cancel,
};

static void ide_trim_bh_cb(void *opaque)
{
    TrimAIOCB *iocb = opaque;
    BlockBackend *blk = iocb->s->blk;

    iocb->common.cb(iocb->common.opaque, iocb->ret);

    qemu_bh_delete(iocb->bh);
    iocb->bh = NULL;
    qemu_aio_unref(iocb);

    /* Paired with an increment in ide_issue_trim() */
    blk_dec_in_flight(blk);
}

static void ide_issue_trim_cb(void *opaque, int ret)
{
    TrimAIOCB *iocb = opaque;
    IDEState *s = iocb->s;

    if (iocb->i >= 0) {
        if (ret >= 0) {
            block_acct_done(blk_get_stats(s->blk), &s->acct);
        } else {
            block_acct_failed(blk_get_stats(s->blk), &s->acct);
        }
    }

    if (ret >= 0) {
        while (iocb->j < iocb->qiov->niov) {
            int j = iocb->j;
            while (++iocb->i < iocb->qiov->iov[j].iov_len / 8) {
                int i = iocb->i;
                uint64_t *buffer = iocb->qiov->iov[j].iov_base;

                /* 6-byte LBA + 2-byte range per entry */
                uint64_t entry = le64_to_cpu(buffer[i]);
                uint64_t sector = entry & 0x0000ffffffffffffULL;
                uint16_t count = entry >> 48;

                if (count == 0) {
                    continue;
                }

                if (!ide_sect_range_ok(s, sector, count)) {
                    block_acct_invalid(blk_get_stats(s->blk), BLOCK_ACCT_UNMAP);
                    iocb->ret = -EINVAL;
                    goto done;
                }

                block_acct_start(blk_get_stats(s->blk), &s->acct,
                                 count << BDRV_SECTOR_BITS, BLOCK_ACCT_UNMAP);

                /* Got an entry! Submit and exit.  */
                iocb->aiocb = blk_aio_pdiscard(s->blk,
                                               sector << BDRV_SECTOR_BITS,
                                               count << BDRV_SECTOR_BITS,
                                               ide_issue_trim_cb, opaque);
                return;
            }

            iocb->j++;
            iocb->i = -1;
        }
    } else {
        iocb->ret = ret;
    }

done:
    iocb->aiocb = NULL;
    if (iocb->bh) {
        replay_bh_schedule_event(iocb->bh);
    }
}

BlockAIOCB *ide_issue_trim(
        int64_t offset, QEMUIOVector *qiov,
        BlockCompletionFunc *cb, void *cb_opaque, void *opaque)
{
    IDEState *s = opaque;
    TrimAIOCB *iocb;

    /* Paired with a decrement in ide_trim_bh_cb() */
    blk_inc_in_flight(s->blk);

    iocb = blk_aio_get(&trim_aiocb_info, s->blk, cb, cb_opaque);
    iocb->s = s;
    iocb->bh = qemu_bh_new(ide_trim_bh_cb, iocb);
    iocb->ret = 0;
    iocb->qiov = qiov;
    iocb->i = -1;
    iocb->j = 0;
    ide_issue_trim_cb(iocb, 0);
    return &iocb->common;
}

void ide_abort_command(IDEState *s)
{
    ide_transfer_stop(s);
    s->status = READY_STAT | ERR_STAT;
    s->error = ABRT_ERR;
}

static void ide_set_retry(IDEState *s)
{
    s->bus->retry_unit = s->unit;
    s->bus->retry_sector_num = ide_get_sector(s);
    s->bus->retry_nsector = s->nsector;
}

static void ide_clear_retry(IDEState *s)
{
    s->bus->retry_unit = -1;
    s->bus->retry_sector_num = 0;
    s->bus->retry_nsector = 0;
}

/* prepare data transfer and tell what to do after */
bool ide_transfer_start_norecurse(IDEState *s, uint8_t *buf, int size,
                                  EndTransferFunc *end_transfer_func)
{
    s->data_ptr = buf;
    s->data_end = buf + size;
    ide_set_retry(s);
    if (!(s->status & ERR_STAT)) {
        s->status |= DRQ_STAT;
    }
    if (!s->bus->dma->ops->pio_transfer) {
        s->end_transfer_func = end_transfer_func;
        return false;
    }
    s->bus->dma->ops->pio_transfer(s->bus->dma);
    return true;
}

void ide_transfer_start(IDEState *s, uint8_t *buf, int size,
                        EndTransferFunc *end_transfer_func)
{
    if (ide_transfer_start_norecurse(s, buf, size, end_transfer_func)) {
        end_transfer_func(s);
    }
}

static void ide_cmd_done(IDEState *s)
{
    if (s->bus->dma->ops->cmd_done) {
        s->bus->dma->ops->cmd_done(s->bus->dma);
    }
}

static void ide_transfer_halt(IDEState *s)
{
    s->end_transfer_func = ide_transfer_stop;
    s->data_ptr = s->io_buffer;
    s->data_end = s->io_buffer;
    s->status &= ~DRQ_STAT;
}

void ide_transfer_stop(IDEState *s)
{
    ide_transfer_halt(s);
    ide_cmd_done(s);
}

int64_t ide_get_sector(IDEState *s)
{
    int64_t sector_num;
    if (s->select & (ATA_DEV_LBA)) {
        if (s->lba48) {
            sector_num = ((int64_t)s->hob_hcyl << 40) |
                ((int64_t) s->hob_lcyl << 32) |
                ((int64_t) s->hob_sector << 24) |
                ((int64_t) s->hcyl << 16) |
                ((int64_t) s->lcyl << 8) | s->sector;
        } else {
            /* LBA28 */
            sector_num = ((s->select & (ATA_DEV_LBA_MSB)) << 24) |
                (s->hcyl << 16) | (s->lcyl << 8) | s->sector;
        }
    } else {
        /* CHS */
        sector_num = ((s->hcyl << 8) | s->lcyl) * s->heads * s->sectors +
            (s->select & (ATA_DEV_HS)) * s->sectors + (s->sector - 1);
    }

    return sector_num;
}

void ide_set_sector(IDEState *s, int64_t sector_num)
{
    unsigned int cyl, r;
    if (s->select & (ATA_DEV_LBA)) {
        if (s->lba48) {
            s->sector = sector_num;
            s->lcyl = sector_num >> 8;
            s->hcyl = sector_num >> 16;
            s->hob_sector = sector_num >> 24;
            s->hob_lcyl = sector_num >> 32;
            s->hob_hcyl = sector_num >> 40;
        } else {
            /* LBA28 */
            s->select = (s->select & ~(ATA_DEV_LBA_MSB)) |
                ((sector_num >> 24) & (ATA_DEV_LBA_MSB));
            s->hcyl = (sector_num >> 16);
            s->lcyl = (sector_num >> 8);
            s->sector = (sector_num);
        }
    } else {
        /* CHS */
        cyl = sector_num / (s->heads * s->sectors);
        r = sector_num % (s->heads * s->sectors);
        s->hcyl = cyl >> 8;
        s->lcyl = cyl;
        s->select = (s->select & ~(ATA_DEV_HS)) |
            ((r / s->sectors) & (ATA_DEV_HS));
        s->sector = (r % s->sectors) + 1;
    }
}

static void ide_rw_error(IDEState *s) {
    ide_abort_command(s);
    ide_set_irq(s->bus);
}

static void ide_buffered_readv_cb(void *opaque, int ret)
{
    IDEBufferedRequest *req = opaque;
    if (!req->orphaned) {
        if (!ret) {
            assert(req->qiov.size == req->original_qiov->size);
            qemu_iovec_from_buf(req->original_qiov, 0,
                                req->qiov.local_iov.iov_base,
                                req->original_qiov->size);
        }
        req->original_cb(req->original_opaque, ret);
    }
    QLIST_REMOVE(req, list);
    qemu_vfree(qemu_iovec_buf(&req->qiov));
    g_free(req);
}

#define MAX_BUFFERED_REQS 16

BlockAIOCB *ide_buffered_readv(IDEState *s, int64_t sector_num,
                               QEMUIOVector *iov, int nb_sectors,
                               BlockCompletionFunc *cb, void *opaque)
{
    BlockAIOCB *aioreq;
    IDEBufferedRequest *req;
    int c = 0;

    QLIST_FOREACH(req, &s->buffered_requests, list) {
        c++;
    }
    if (c > MAX_BUFFERED_REQS) {
        return blk_abort_aio_request(s->blk, cb, opaque, -EIO);
    }

    req = g_new0(IDEBufferedRequest, 1);
    req->original_qiov = iov;
    req->original_cb = cb;
    req->original_opaque = opaque;
    qemu_iovec_init_buf(&req->qiov, blk_blockalign(s->blk, iov->size),
                        iov->size);

    aioreq = blk_aio_preadv(s->blk, sector_num << BDRV_SECTOR_BITS,
                            &req->qiov, 0, ide_buffered_readv_cb, req);

    QLIST_INSERT_HEAD(&s->buffered_requests, req, list);
    return aioreq;
}

/**
 * Cancel all pending DMA requests.
 * Any buffered DMA requests are instantly canceled,
 * but any pending unbuffered DMA requests must be waited on.
 */
void ide_cancel_dma_sync(IDEState *s)
{
    IDEBufferedRequest *req;

    /* First invoke the callbacks of all buffered requests
     * and flag those requests as orphaned. Ideally there
     * are no unbuffered (Scatter Gather DMA Requests or
     * write requests) pending and we can avoid to drain. */
    QLIST_FOREACH(req, &s->buffered_requests, list) {
        if (!req->orphaned) {
            trace_ide_cancel_dma_sync_buffered(req->original_cb, req);
            req->original_cb(req->original_opaque, -ECANCELED);
        }
        req->orphaned = true;
    }

    /*
     * We can't cancel Scatter Gather DMA in the middle of the
     * operation or a partial (not full) DMA transfer would reach
     * the storage so we wait for completion instead (we behave
     * like if the DMA was completed by the time the guest trying
     * to cancel dma with bmdma_cmd_writeb with BM_CMD_START not
     * set).
     *
     * In the future we'll be able to safely cancel the I/O if the
     * whole DMA operation will be submitted to disk with a single
     * aio operation with preadv/pwritev.
     */
    if (s->bus->dma->aiocb) {
        trace_ide_cancel_dma_sync_remaining();
        blk_drain(s->blk);
        assert(s->bus->dma->aiocb == NULL);
    }
}

static void ide_sector_read(IDEState *s);

static void ide_sector_read_cb(void *opaque, int ret)
{
    IDEState *s = opaque;
    int n;

    s->pio_aiocb = NULL;
    s->status &= ~BUSY_STAT;

    if (ret != 0) {
        if (ide_handle_rw_error(s, -ret, IDE_RETRY_PIO |
                                IDE_RETRY_READ)) {
            return;
        }
    }

    block_acct_done(blk_get_stats(s->blk), &s->acct);

    n = s->nsector;
    if (n > s->req_nb_sectors) {
        n = s->req_nb_sectors;
    }

    ide_set_sector(s, ide_get_sector(s) + n);
    s->nsector -= n;
    /* Allow the guest to read the io_buffer */
    ide_transfer_start(s, s->io_buffer, n * BDRV_SECTOR_SIZE, ide_sector_read);
    ide_set_irq(s->bus);
}

static void ide_sector_read(IDEState *s)
{
    int64_t sector_num;
    int n;

    s->status = READY_STAT | SEEK_STAT;
    s->error = 0; /* not needed by IDE spec, but needed by Windows */
    sector_num = ide_get_sector(s);
    n = s->nsector;

    if (n == 0) {
        ide_transfer_stop(s);
        return;
    }

    s->status |= BUSY_STAT;

    if (n > s->req_nb_sectors) {
        n = s->req_nb_sectors;
    }

    trace_ide_sector_read(sector_num, n);

    if (!ide_sect_range_ok(s, sector_num, n)) {
        ide_rw_error(s);
        block_acct_invalid(blk_get_stats(s->blk), BLOCK_ACCT_READ);
        return;
    }

    qemu_iovec_init_buf(&s->qiov, s->io_buffer, n * BDRV_SECTOR_SIZE);

    block_acct_start(blk_get_stats(s->blk), &s->acct,
                     n * BDRV_SECTOR_SIZE, BLOCK_ACCT_READ);
    s->pio_aiocb = ide_buffered_readv(s, sector_num, &s->qiov, n,
                                      ide_sector_read_cb, s);
}

void dma_buf_commit(IDEState *s, uint32_t tx_bytes)
{
    if (s->bus->dma->ops->commit_buf) {
        s->bus->dma->ops->commit_buf(s->bus->dma, tx_bytes);
    }
    s->io_buffer_offset += tx_bytes;
    qemu_sglist_destroy(&s->sg);
}

void ide_set_inactive(IDEState *s, bool more)
{
    s->bus->dma->aiocb = NULL;
    ide_clear_retry(s);
    if (s->bus->dma->ops->set_inactive) {
        s->bus->dma->ops->set_inactive(s->bus->dma, more);
    }
    ide_cmd_done(s);
}

void ide_dma_error(IDEState *s)
{
    dma_buf_commit(s, 0);
    ide_abort_command(s);
    ide_set_inactive(s, false);
    ide_set_irq(s->bus);
}

int ide_handle_rw_error(IDEState *s, int error, int op)
{
    bool is_read = (op & IDE_RETRY_READ) != 0;
    BlockErrorAction action = blk_get_error_action(s->blk, is_read, error);

    if (action == BLOCK_ERROR_ACTION_STOP) {
        assert(s->bus->retry_unit == s->unit);
        s->bus->error_status = op;
    } else if (action == BLOCK_ERROR_ACTION_REPORT) {
        block_acct_failed(blk_get_stats(s->blk), &s->acct);
        if (IS_IDE_RETRY_DMA(op)) {
            ide_dma_error(s);
        } else if (IS_IDE_RETRY_ATAPI(op)) {
            ide_atapi_io_error(s, -error);
        } else {
            ide_rw_error(s);
        }
    }
    blk_error_action(s->blk, action, is_read, error);
    return action != BLOCK_ERROR_ACTION_IGNORE;
}

static void ide_dma_cb(void *opaque, int ret)
{
    IDEState *s = opaque;
    int n;
    int64_t sector_num;
    uint64_t offset;
    bool stay_active = false;
    int32_t prep_size = 0;

    if (ret == -EINVAL) {
        ide_dma_error(s);
        return;
    }

    if (ret < 0) {
        if (ide_handle_rw_error(s, -ret, ide_dma_cmd_to_retry(s->dma_cmd))) {
            s->bus->dma->aiocb = NULL;
            dma_buf_commit(s, 0);
            return;
        }
    }

    if (s->io_buffer_size > s->nsector * 512) {
        /*
         * The PRDs were longer than needed for this request.
         * The Active bit must remain set after the request completes.
         */
        n = s->nsector;
        stay_active = true;
    } else {
        n = s->io_buffer_size >> 9;
    }

    sector_num = ide_get_sector(s);
    if (n > 0) {
        assert(n * 512 == s->sg.size);
        dma_buf_commit(s, s->sg.size);
        sector_num += n;
        ide_set_sector(s, sector_num);
        s->nsector -= n;
    }

    /* end of transfer ? */
    if (s->nsector == 0) {
        s->status = READY_STAT | SEEK_STAT;
        ide_set_irq(s->bus);
        goto eot;
    }

    /* launch next transfer */
    n = s->nsector;
    s->io_buffer_index = 0;
    s->io_buffer_size = n * 512;
    prep_size = s->bus->dma->ops->prepare_buf(s->bus->dma, s->io_buffer_size);
    /* prepare_buf() must succeed and respect the limit */
    assert(prep_size >= 0 && prep_size <= n * 512);

    /*
     * Now prep_size stores the number of bytes in the sglist, and
     * s->io_buffer_size stores the number of bytes described by the PRDs.
     */

    if (prep_size < n * 512) {
        /*
         * The PRDs are too short for this request. Error condition!
         * Reset the Active bit and don't raise the interrupt.
         */
        s->status = READY_STAT | SEEK_STAT;
        dma_buf_commit(s, 0);
        goto eot;
    }

    trace_ide_dma_cb(s, sector_num, n, IDE_DMA_CMD_str(s->dma_cmd));

    if ((s->dma_cmd == IDE_DMA_READ || s->dma_cmd == IDE_DMA_WRITE) &&
        !ide_sect_range_ok(s, sector_num, n)) {
        ide_dma_error(s);
        block_acct_invalid(blk_get_stats(s->blk), s->acct.type);
        return;
    }

    offset = sector_num << BDRV_SECTOR_BITS;
    switch (s->dma_cmd) {
    case IDE_DMA_READ:
        s->bus->dma->aiocb = dma_blk_read(s->blk, &s->sg, offset,
                                          BDRV_SECTOR_SIZE, ide_dma_cb, s);
        break;
    case IDE_DMA_WRITE:
        s->bus->dma->aiocb = dma_blk_write(s->blk, &s->sg, offset,
                                           BDRV_SECTOR_SIZE, ide_dma_cb, s);
        break;
    case IDE_DMA_TRIM:
        s->bus->dma->aiocb = dma_blk_io(blk_get_aio_context(s->blk),
                                        &s->sg, offset, BDRV_SECTOR_SIZE,
                                        ide_issue_trim, s, ide_dma_cb, s,
                                        DMA_DIRECTION_TO_DEVICE);
        break;
    default:
        abort();
    }
    return;

eot:
    if (s->dma_cmd == IDE_DMA_READ || s->dma_cmd == IDE_DMA_WRITE) {
        block_acct_done(blk_get_stats(s->blk), &s->acct);
    }
    ide_set_inactive(s, stay_active);
}

static void ide_sector_start_dma(IDEState *s, enum ide_dma_cmd dma_cmd)
{
    s->status = READY_STAT | SEEK_STAT | DRQ_STAT;
    s->io_buffer_size = 0;
    s->dma_cmd = dma_cmd;

    switch (dma_cmd) {
    case IDE_DMA_READ:
        block_acct_start(blk_get_stats(s->blk), &s->acct,
                         s->nsector * BDRV_SECTOR_SIZE, BLOCK_ACCT_READ);
        break;
    case IDE_DMA_WRITE:
        block_acct_start(blk_get_stats(s->blk), &s->acct,
                         s->nsector * BDRV_SECTOR_SIZE, BLOCK_ACCT_WRITE);
        break;
    default:
        break;
    }

    ide_start_dma(s, ide_dma_cb);
}

void ide_start_dma(IDEState *s, BlockCompletionFunc *cb)
{
    s->io_buffer_index = 0;
    ide_set_retry(s);
    if (s->bus->dma->ops->start_dma) {
        s->bus->dma->ops->start_dma(s->bus->dma, s, cb);
    }
}

static void ide_sector_write(IDEState *s);

static void ide_sector_write_timer_cb(void *opaque)
{
    IDEState *s = opaque;
    ide_set_irq(s->bus);
}

static void ide_sector_write_cb(void *opaque, int ret)
{
    IDEState *s = opaque;
    int n;

    s->pio_aiocb = NULL;
    s->status &= ~BUSY_STAT;

    if (ret != 0) {
        if (ide_handle_rw_error(s, -ret, IDE_RETRY_PIO)) {
            return;
        }
    }

    block_acct_done(blk_get_stats(s->blk), &s->acct);

    n = s->nsector;
    if (n > s->req_nb_sectors) {
        n = s->req_nb_sectors;
    }
    s->nsector -= n;

    ide_set_sector(s, ide_get_sector(s) + n);
    if (s->nsector == 0) {
        /* no more sectors to write */
        ide_transfer_stop(s);
    } else {
        int n1 = s->nsector;
        if (n1 > s->req_nb_sectors) {
            n1 = s->req_nb_sectors;
        }
        ide_transfer_start(s, s->io_buffer, n1 * BDRV_SECTOR_SIZE,
                           ide_sector_write);
    }

    if (win2k_install_hack && ((++s->irq_count % 16) == 0)) {
        /* It seems there is a bug in the Windows 2000 installer HDD
           IDE driver which fills the disk with empty logs when the
           IDE write IRQ comes too early. This hack tries to correct
           that at the expense of slower write performances. Use this
           option _only_ to install Windows 2000. You must disable it
           for normal use. */
        timer_mod(s->sector_write_timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                  (NANOSECONDS_PER_SECOND / 1000));
    } else {
        ide_set_irq(s->bus);
    }
}

static void ide_sector_write(IDEState *s)
{
    int64_t sector_num;
    int n;

    s->status = READY_STAT | SEEK_STAT | BUSY_STAT;
    sector_num = ide_get_sector(s);

    n = s->nsector;
    if (n > s->req_nb_sectors) {
        n = s->req_nb_sectors;
    }

    trace_ide_sector_write(sector_num, n);

    if (!ide_sect_range_ok(s, sector_num, n)) {
        ide_rw_error(s);
        block_acct_invalid(blk_get_stats(s->blk), BLOCK_ACCT_WRITE);
        return;
    }

    qemu_iovec_init_buf(&s->qiov, s->io_buffer, n * BDRV_SECTOR_SIZE);

    block_acct_start(blk_get_stats(s->blk), &s->acct,
                     n * BDRV_SECTOR_SIZE, BLOCK_ACCT_WRITE);
    s->pio_aiocb = blk_aio_pwritev(s->blk, sector_num << BDRV_SECTOR_BITS,
                                   &s->qiov, 0, ide_sector_write_cb, s);
}

static void ide_flush_cb(void *opaque, int ret)
{
    IDEState *s = opaque;

    s->pio_aiocb = NULL;

    if (ret < 0) {
        /* XXX: What sector number to set here? */
        if (ide_handle_rw_error(s, -ret, IDE_RETRY_FLUSH)) {
            return;
        }
    }

    if (s->blk) {
        block_acct_done(blk_get_stats(s->blk), &s->acct);
    }
    s->status = READY_STAT | SEEK_STAT;
    ide_cmd_done(s);
    ide_set_irq(s->bus);
}

static void ide_flush_cache(IDEState *s)
{
    if (s->blk == NULL) {
        ide_flush_cb(s, 0);
        return;
    }

    s->status |= BUSY_STAT;
    ide_set_retry(s);
    block_acct_start(blk_get_stats(s->blk), &s->acct, 0, BLOCK_ACCT_FLUSH);
    s->pio_aiocb = blk_aio_flush(s->blk, ide_flush_cb, s);
}

static void ide_cfata_metadata_inquiry(IDEState *s)
{
    uint16_t *p;
    uint32_t spd;

    p = (uint16_t *) s->io_buffer;
    memset(p, 0, 0x200);
    spd = ((s->mdata_size - 1) >> 9) + 1;

    put_le16(p + 0, 0x0001);			/* Data format revision */
    put_le16(p + 1, 0x0000);			/* Media property: silicon */
    put_le16(p + 2, s->media_changed);		/* Media status */
    put_le16(p + 3, s->mdata_size & 0xffff);	/* Capacity in bytes (low) */
    put_le16(p + 4, s->mdata_size >> 16);	/* Capacity in bytes (high) */
    put_le16(p + 5, spd & 0xffff);		/* Sectors per device (low) */
    put_le16(p + 6, spd >> 16);			/* Sectors per device (high) */
}

static void ide_cfata_metadata_read(IDEState *s)
{
    uint16_t *p;

    if (((s->hcyl << 16) | s->lcyl) << 9 > s->mdata_size + 2) {
        s->status = ERR_STAT;
        s->error = ABRT_ERR;
        return;
    }

    p = (uint16_t *) s->io_buffer;
    memset(p, 0, 0x200);

    put_le16(p + 0, s->media_changed);		/* Media status */
    memcpy(p + 1, s->mdata_storage + (((s->hcyl << 16) | s->lcyl) << 9),
                    MIN(MIN(s->mdata_size - (((s->hcyl << 16) | s->lcyl) << 9),
                                    s->nsector << 9), 0x200 - 2));
}

static void ide_cfata_metadata_write(IDEState *s)
{
    if (((s->hcyl << 16) | s->lcyl) << 9 > s->mdata_size + 2) {
        s->status = ERR_STAT;
        s->error = ABRT_ERR;
        return;
    }

    s->media_changed = 0;

    memcpy(s->mdata_storage + (((s->hcyl << 16) | s->lcyl) << 9),
                    s->io_buffer + 2,
                    MIN(MIN(s->mdata_size - (((s->hcyl << 16) | s->lcyl) << 9),
                                    s->nsector << 9), 0x200 - 2));
}

/* called when the inserted state of the media has changed */
static void ide_cd_change_cb(void *opaque, bool load, Error **errp)
{
    IDEState *s = opaque;
    uint64_t nb_sectors;

    s->tray_open = !load;
    blk_get_geometry(s->blk, &nb_sectors);
    s->nb_sectors = nb_sectors;

    /*
     * First indicate to the guest that a CD has been removed.  That's
     * done on the next command the guest sends us.
     *
     * Then we set UNIT_ATTENTION, by which the guest will
     * detect a new CD in the drive.  See ide_atapi_cmd() for details.
     */
    s->cdrom_changed = 1;
    s->events.new_media = true;
    s->events.eject_request = false;
    ide_set_irq(s->bus);
}

static void ide_cd_eject_request_cb(void *opaque, bool force)
{
    IDEState *s = opaque;

    s->events.eject_request = true;
    if (force) {
        s->tray_locked = false;
    }
    ide_set_irq(s->bus);
}

static void ide_cmd_lba48_transform(IDEState *s, int lba48)
{
    s->lba48 = lba48;

    /* handle the 'magic' 0 nsector count conversion here. to avoid
     * fiddling with the rest of the read logic, we just store the
     * full sector count in ->nsector and ignore ->hob_nsector from now
     */
    if (!s->lba48) {
        if (!s->nsector)
            s->nsector = 256;
    } else {
        if (!s->nsector && !s->hob_nsector)
            s->nsector = 65536;
        else {
            int lo = s->nsector;
            int hi = s->hob_nsector;

            s->nsector = (hi << 8) | lo;
        }
    }
}

static void ide_clear_hob(IDEBus *bus)
{
    /* any write clears HOB high bit of device control register */
    bus->cmd &= ~(IDE_CTRL_HOB);
}

/* IOport [W]rite [R]egisters */
enum ATA_IOPORT_WR {
    ATA_IOPORT_WR_DATA = 0,
    ATA_IOPORT_WR_FEATURES = 1,
    ATA_IOPORT_WR_SECTOR_COUNT = 2,
    ATA_IOPORT_WR_SECTOR_NUMBER = 3,
    ATA_IOPORT_WR_CYLINDER_LOW = 4,
    ATA_IOPORT_WR_CYLINDER_HIGH = 5,
    ATA_IOPORT_WR_DEVICE_HEAD = 6,
    ATA_IOPORT_WR_COMMAND = 7,
    ATA_IOPORT_WR_NUM_REGISTERS,
};

const char *ATA_IOPORT_WR_lookup[ATA_IOPORT_WR_NUM_REGISTERS] = {
    [ATA_IOPORT_WR_DATA] = "Data",
    [ATA_IOPORT_WR_FEATURES] = "Features",
    [ATA_IOPORT_WR_SECTOR_COUNT] = "Sector Count",
    [ATA_IOPORT_WR_SECTOR_NUMBER] = "Sector Number",
    [ATA_IOPORT_WR_CYLINDER_LOW] = "Cylinder Low",
    [ATA_IOPORT_WR_CYLINDER_HIGH] = "Cylinder High",
    [ATA_IOPORT_WR_DEVICE_HEAD] = "Device/Head",
    [ATA_IOPORT_WR_COMMAND] = "Command"
};

void ide_ioport_write(void *opaque, uint32_t addr, uint32_t val)
{
    IDEBus *bus = opaque;
    IDEState *s = idebus_active_if(bus);
    int reg_num = addr & 7;

    trace_ide_ioport_write(addr, ATA_IOPORT_WR_lookup[reg_num], val, bus, s);

    /* ignore writes to command block while busy with previous command */
    if (reg_num != 7 && (s->status & (BUSY_STAT|DRQ_STAT))) {
        return;
    }

    /* NOTE: Device0 and Device1 both receive incoming register writes.
     * (They're on the same bus! They have to!) */

    switch (reg_num) {
    case 0:
        break;
    case ATA_IOPORT_WR_FEATURES:
        ide_clear_hob(bus);
        bus->ifs[0].hob_feature = bus->ifs[0].feature;
        bus->ifs[1].hob_feature = bus->ifs[1].feature;
        bus->ifs[0].feature = val;
        bus->ifs[1].feature = val;
        break;
    case ATA_IOPORT_WR_SECTOR_COUNT:
        ide_clear_hob(bus);
        bus->ifs[0].hob_nsector = bus->ifs[0].nsector;
        bus->ifs[1].hob_nsector = bus->ifs[1].nsector;
        bus->ifs[0].nsector = val;
        bus->ifs[1].nsector = val;
        break;
    case ATA_IOPORT_WR_SECTOR_NUMBER:
        ide_clear_hob(bus);
        bus->ifs[0].hob_sector = bus->ifs[0].sector;
        bus->ifs[1].hob_sector = bus->ifs[1].sector;
        bus->ifs[0].sector = val;
        bus->ifs[1].sector = val;
        break;
    case ATA_IOPORT_WR_CYLINDER_LOW:
        ide_clear_hob(bus);
        bus->ifs[0].hob_lcyl = bus->ifs[0].lcyl;
        bus->ifs[1].hob_lcyl = bus->ifs[1].lcyl;
        bus->ifs[0].lcyl = val;
        bus->ifs[1].lcyl = val;
        break;
    case ATA_IOPORT_WR_CYLINDER_HIGH:
        ide_clear_hob(bus);
        bus->ifs[0].hob_hcyl = bus->ifs[0].hcyl;
        bus->ifs[1].hob_hcyl = bus->ifs[1].hcyl;
        bus->ifs[0].hcyl = val;
        bus->ifs[1].hcyl = val;
        break;
    case ATA_IOPORT_WR_DEVICE_HEAD:
        ide_clear_hob(bus);
        bus->ifs[0].select = val | (ATA_DEV_ALWAYS_ON);
        bus->ifs[1].select = val | (ATA_DEV_ALWAYS_ON);
        /* select drive */
        bus->unit = (val & (ATA_DEV_SELECT)) ? 1 : 0;
        break;
    default:
    case ATA_IOPORT_WR_COMMAND:
        ide_clear_hob(bus);
        qemu_irq_lower(bus->irq);
        ide_exec_cmd(bus, val);
        break;
    }
}

static void ide_reset(IDEState *s)
{
    trace_ide_reset(s);

    if (s->pio_aiocb) {
        blk_aio_cancel(s->pio_aiocb);
        s->pio_aiocb = NULL;
    }

    if (s->drive_kind == IDE_CFATA)
        s->mult_sectors = 0;
    else
        s->mult_sectors = MAX_MULT_SECTORS;
    /* ide regs */
    s->feature = 0;
    s->error = 0;
    s->nsector = 0;
    s->sector = 0;
    s->lcyl = 0;
    s->hcyl = 0;

    /* lba48 */
    s->hob_feature = 0;
    s->hob_sector = 0;
    s->hob_nsector = 0;
    s->hob_lcyl = 0;
    s->hob_hcyl = 0;

    s->select = (ATA_DEV_ALWAYS_ON);
    s->status = READY_STAT | SEEK_STAT;

    s->lba48 = 0;

    /* ATAPI specific */
    s->sense_key = 0;
    s->asc = 0;
    s->cdrom_changed = 0;
    s->packet_transfer_size = 0;
    s->elementary_transfer_size = 0;
    s->io_buffer_index = 0;
    s->cd_sector_size = 0;
    s->atapi_dma = 0;
    s->tray_locked = 0;
    s->tray_open = 0;
    /* ATA DMA state */
    s->io_buffer_size = 0;
    s->req_nb_sectors = 0;

    ide_set_signature(s);
    /* init the transfer handler so that 0xffff is returned on data
       accesses */
    s->end_transfer_func = ide_dummy_transfer_stop;
    ide_dummy_transfer_stop(s);
    s->media_changed = 0;
}

static bool cmd_nop(IDEState *s, uint8_t cmd)
{
    return true;
}

static bool cmd_device_reset(IDEState *s, uint8_t cmd)
{
    /* Halt PIO (in the DRQ phase), then DMA */
    ide_transfer_halt(s);
    ide_cancel_dma_sync(s);

    /* Reset any PIO commands, reset signature, etc */
    ide_reset(s);

    /* RESET: ATA8-ACS3 7.10.4 "Normal Outputs";
     * ATA8-ACS3 Table 184 "Device Signatures for Normal Output" */
    s->status = 0x00;

    /* Do not overwrite status register */
    return false;
}

static bool cmd_data_set_management(IDEState *s, uint8_t cmd)
{
    switch (s->feature) {
    case DSM_TRIM:
        if (s->blk) {
            ide_sector_start_dma(s, IDE_DMA_TRIM);
            return false;
        }
        break;
    }

    ide_abort_command(s);
    return true;
}

static bool cmd_identify(IDEState *s, uint8_t cmd)
{
    if (s->blk && s->drive_kind != IDE_CD) {
        if (s->drive_kind != IDE_CFATA) {
            ide_identify(s);
        } else {
            ide_cfata_identify(s);
        }
        s->status = READY_STAT | SEEK_STAT;
        ide_transfer_start(s, s->io_buffer, 512, ide_transfer_stop);
        ide_set_irq(s->bus);
        return false;
    } else {
        if (s->drive_kind == IDE_CD) {
            ide_set_signature(s);
        }
        ide_abort_command(s);
    }

    return true;
}

static bool cmd_verify(IDEState *s, uint8_t cmd)
{
    bool lba48 = (cmd == WIN_VERIFY_EXT);

    /* do sector number check ? */
    ide_cmd_lba48_transform(s, lba48);

    return true;
}

static bool cmd_set_multiple_mode(IDEState *s, uint8_t cmd)
{
    if (s->drive_kind == IDE_CFATA && s->nsector == 0) {
        /* Disable Read and Write Multiple */
        s->mult_sectors = 0;
    } else if ((s->nsector & 0xff) != 0 &&
        ((s->nsector & 0xff) > MAX_MULT_SECTORS ||
         (s->nsector & (s->nsector - 1)) != 0)) {
        ide_abort_command(s);
    } else {
        s->mult_sectors = s->nsector & 0xff;
    }

    return true;
}

static bool cmd_read_multiple(IDEState *s, uint8_t cmd)
{
    bool lba48 = (cmd == WIN_MULTREAD_EXT);

    if (!s->blk || !s->mult_sectors) {
        ide_abort_command(s);
        return true;
    }

    ide_cmd_lba48_transform(s, lba48);
    s->req_nb_sectors = s->mult_sectors;
    ide_sector_read(s);
    return false;
}

static bool cmd_write_multiple(IDEState *s, uint8_t cmd)
{
    bool lba48 = (cmd == WIN_MULTWRITE_EXT);
    int n;

    if (!s->blk || !s->mult_sectors) {
        ide_abort_command(s);
        return true;
    }

    ide_cmd_lba48_transform(s, lba48);

    s->req_nb_sectors = s->mult_sectors;
    n = MIN(s->nsector, s->req_nb_sectors);

    s->status = SEEK_STAT | READY_STAT;
    ide_transfer_start(s, s->io_buffer, 512 * n, ide_sector_write);

    s->media_changed = 1;

    return false;
}

static bool cmd_read_pio(IDEState *s, uint8_t cmd)
{
    bool lba48 = (cmd == WIN_READ_EXT);

    if (s->drive_kind == IDE_CD) {
        ide_set_signature(s); /* odd, but ATA4 8.27.5.2 requires it */
        ide_abort_command(s);
        return true;
    }

    if (!s->blk) {
        ide_abort_command(s);
        return true;
    }

    ide_cmd_lba48_transform(s, lba48);
    s->req_nb_sectors = 1;
    ide_sector_read(s);

    return false;
}

static bool cmd_write_pio(IDEState *s, uint8_t cmd)
{
    bool lba48 = (cmd == WIN_WRITE_EXT);

    if (!s->blk) {
        ide_abort_command(s);
        return true;
    }

    ide_cmd_lba48_transform(s, lba48);

    s->req_nb_sectors = 1;
    s->status = SEEK_STAT | READY_STAT;
    ide_transfer_start(s, s->io_buffer, 512, ide_sector_write);

    s->media_changed = 1;

    return false;
}

static bool cmd_read_dma(IDEState *s, uint8_t cmd)
{
    bool lba48 = (cmd == WIN_READDMA_EXT);

    if (!s->blk) {
        ide_abort_command(s);
        return true;
    }

    ide_cmd_lba48_transform(s, lba48);
    ide_sector_start_dma(s, IDE_DMA_READ);

    return false;
}

static bool cmd_write_dma(IDEState *s, uint8_t cmd)
{
    bool lba48 = (cmd == WIN_WRITEDMA_EXT);

    if (!s->blk) {
        ide_abort_command(s);
        return true;
    }

    ide_cmd_lba48_transform(s, lba48);
    ide_sector_start_dma(s, IDE_DMA_WRITE);

    s->media_changed = 1;

    return false;
}

static bool cmd_flush_cache(IDEState *s, uint8_t cmd)
{
    ide_flush_cache(s);
    return false;
}

static bool cmd_seek(IDEState *s, uint8_t cmd)
{
    /* XXX: Check that seek is within bounds */
    return true;
}

static bool cmd_read_native_max(IDEState *s, uint8_t cmd)
{
    bool lba48 = (cmd == WIN_READ_NATIVE_MAX_EXT);

    /* Refuse if no sectors are addressable (e.g. medium not inserted) */
    if (s->nb_sectors == 0) {
        ide_abort_command(s);
        return true;
    }

    ide_cmd_lba48_transform(s, lba48);
    ide_set_sector(s, s->nb_sectors - 1);

    return true;
}

static bool cmd_check_power_mode(IDEState *s, uint8_t cmd)
{
    s->nsector = 0xff; /* device active or idle */
    return true;
}

static bool cmd_set_features(IDEState *s, uint8_t cmd)
{
    uint16_t *identify_data;

    if (!s->blk) {
        ide_abort_command(s);
        return true;
    }

    /* XXX: valid for CDROM ? */
    switch (s->feature) {
    case 0x02: /* write cache enable */
        blk_set_enable_write_cache(s->blk, true);
        identify_data = (uint16_t *)s->identify_data;
        put_le16(identify_data + 85, (1 << 14) | (1 << 5) | 1);
        return true;
    case 0x82: /* write cache disable */
        blk_set_enable_write_cache(s->blk, false);
        identify_data = (uint16_t *)s->identify_data;
        put_le16(identify_data + 85, (1 << 14) | 1);
        ide_flush_cache(s);
        return false;
    case 0xcc: /* reverting to power-on defaults enable */
    case 0x66: /* reverting to power-on defaults disable */
    case 0xaa: /* read look-ahead enable */
    case 0x55: /* read look-ahead disable */
    case 0x05: /* set advanced power management mode */
    case 0x85: /* disable advanced power management mode */
    case 0x69: /* NOP */
    case 0x67: /* NOP */
    case 0x96: /* NOP */
    case 0x9a: /* NOP */
    case 0x42: /* enable Automatic Acoustic Mode */
    case 0xc2: /* disable Automatic Acoustic Mode */
        return true;
    case 0x03: /* set transfer mode */
        {
            uint8_t val = s->nsector & 0x07;
            identify_data = (uint16_t *)s->identify_data;

            switch (s->nsector >> 3) {
            case 0x00: /* pio default */
            case 0x01: /* pio mode */
                put_le16(identify_data + 62, 0x07);
                put_le16(identify_data + 63, 0x07);
                put_le16(identify_data + 88, 0x3f);
                break;
            case 0x02: /* sigle word dma mode*/
                put_le16(identify_data + 62, 0x07 | (1 << (val + 8)));
                put_le16(identify_data + 63, 0x07);
                put_le16(identify_data + 88, 0x3f);
                break;
            case 0x04: /* mdma mode */
                put_le16(identify_data + 62, 0x07);
                put_le16(identify_data + 63, 0x07 | (1 << (val + 8)));
                put_le16(identify_data + 88, 0x3f);
                break;
            case 0x08: /* udma mode */
                put_le16(identify_data + 62, 0x07);
                put_le16(identify_data + 63, 0x07);
                put_le16(identify_data + 88, 0x3f | (1 << (val + 8)));
                break;
            default:
                goto abort_cmd;
            }
            return true;
        }
    }

abort_cmd:
    ide_abort_command(s);
    return true;
}


/*** ATAPI commands ***/

static bool cmd_identify_packet(IDEState *s, uint8_t cmd)
{
    ide_atapi_identify(s);
    s->status = READY_STAT | SEEK_STAT;
    ide_transfer_start(s, s->io_buffer, 512, ide_transfer_stop);
    ide_set_irq(s->bus);
    return false;
}

static bool cmd_exec_dev_diagnostic(IDEState *s, uint8_t cmd)
{
    ide_set_signature(s);

    if (s->drive_kind == IDE_CD) {
        s->status = 0; /* ATAPI spec (v6) section 9.10 defines packet
                        * devices to return a clear status register
                        * with READY_STAT *not* set. */
        s->error = 0x01;
    } else {
        s->status = READY_STAT | SEEK_STAT;
        /* The bits of the error register are not as usual for this command!
         * They are part of the regular output (this is why ERR_STAT isn't set)
         * Device 0 passed, Device 1 passed or not present. */
        s->error = 0x01;
        ide_set_irq(s->bus);
    }

    return false;
}

static bool cmd_packet(IDEState *s, uint8_t cmd)
{
    /* overlapping commands not supported */
    if (s->feature & 0x02) {
        ide_abort_command(s);
        return true;
    }

    s->status = READY_STAT | SEEK_STAT;
    s->atapi_dma = s->feature & 1;
    if (s->atapi_dma) {
        s->dma_cmd = IDE_DMA_ATAPI;
    }
    s->nsector = 1;
    ide_transfer_start(s, s->io_buffer, ATAPI_PACKET_SIZE,
                       ide_atapi_cmd);
    return false;
}


/*** CF-ATA commands ***/

static bool cmd_cfa_req_ext_error_code(IDEState *s, uint8_t cmd)
{
    s->error = 0x09;    /* miscellaneous error */
    s->status = READY_STAT | SEEK_STAT;
    ide_set_irq(s->bus);

    return false;
}

static bool cmd_cfa_erase_sectors(IDEState *s, uint8_t cmd)
{
    /* WIN_SECURITY_FREEZE_LOCK has the same ID as CFA_WEAR_LEVEL and is
     * required for Windows 8 to work with AHCI */

    if (cmd == CFA_WEAR_LEVEL) {
        s->nsector = 0;
    }

    if (cmd == CFA_ERASE_SECTORS) {
        s->media_changed = 1;
    }

    return true;
}

static bool cmd_cfa_translate_sector(IDEState *s, uint8_t cmd)
{
    s->status = READY_STAT | SEEK_STAT;

    memset(s->io_buffer, 0, 0x200);
    s->io_buffer[0x00] = s->hcyl;                   /* Cyl MSB */
    s->io_buffer[0x01] = s->lcyl;                   /* Cyl LSB */
    s->io_buffer[0x02] = s->select;                 /* Head */
    s->io_buffer[0x03] = s->sector;                 /* Sector */
    s->io_buffer[0x04] = ide_get_sector(s) >> 16;   /* LBA MSB */
    s->io_buffer[0x05] = ide_get_sector(s) >> 8;    /* LBA */
    s->io_buffer[0x06] = ide_get_sector(s) >> 0;    /* LBA LSB */
    s->io_buffer[0x13] = 0x00;                      /* Erase flag */
    s->io_buffer[0x18] = 0x00;                      /* Hot count */
    s->io_buffer[0x19] = 0x00;                      /* Hot count */
    s->io_buffer[0x1a] = 0x01;                      /* Hot count */

    ide_transfer_start(s, s->io_buffer, 0x200, ide_transfer_stop);
    ide_set_irq(s->bus);

    return false;
}

static bool cmd_cfa_access_metadata_storage(IDEState *s, uint8_t cmd)
{
    switch (s->feature) {
    case 0x02:  /* Inquiry Metadata Storage */
        ide_cfata_metadata_inquiry(s);
        break;
    case 0x03:  /* Read Metadata Storage */
        ide_cfata_metadata_read(s);
        break;
    case 0x04:  /* Write Metadata Storage */
        ide_cfata_metadata_write(s);
        break;
    default:
        ide_abort_command(s);
        return true;
    }

    ide_transfer_start(s, s->io_buffer, 0x200, ide_transfer_stop);
    s->status = 0x00; /* NOTE: READY is _not_ set */
    ide_set_irq(s->bus);

    return false;
}

static bool cmd_ibm_sense_condition(IDEState *s, uint8_t cmd)
{
    switch (s->feature) {
    case 0x01:  /* sense temperature in device */
        s->nsector = 0x50;      /* +20 C */
        break;
    default:
        ide_abort_command(s);
        return true;
    }

    return true;
}


/*** SMART commands ***/

static bool cmd_smart(IDEState *s, uint8_t cmd)
{
    int n;

    if (s->hcyl != 0xc2 || s->lcyl != 0x4f) {
        goto abort_cmd;
    }

    if (!s->smart_enabled && s->feature != SMART_ENABLE) {
        goto abort_cmd;
    }

    switch (s->feature) {
    case SMART_DISABLE:
        s->smart_enabled = 0;
        return true;

    case SMART_ENABLE:
        s->smart_enabled = 1;
        return true;

    case SMART_ATTR_AUTOSAVE:
        switch (s->sector) {
        case 0x00:
            s->smart_autosave = 0;
            break;
        case 0xf1:
            s->smart_autosave = 1;
            break;
        default:
            goto abort_cmd;
        }
        return true;

    case SMART_STATUS:
        if (!s->smart_errors) {
            s->hcyl = 0xc2;
            s->lcyl = 0x4f;
        } else {
            s->hcyl = 0x2c;
            s->lcyl = 0xf4;
        }
        return true;

    case SMART_READ_THRESH:
        memset(s->io_buffer, 0, 0x200);
        s->io_buffer[0] = 0x01; /* smart struct version */

        for (n = 0; n < ARRAY_SIZE(smart_attributes); n++) {
            s->io_buffer[2 + 0 + (n * 12)] = smart_attributes[n][0];
            s->io_buffer[2 + 1 + (n * 12)] = smart_attributes[n][11];
        }

        /* checksum */
        for (n = 0; n < 511; n++) {
            s->io_buffer[511] += s->io_buffer[n];
        }
        s->io_buffer[511] = 0x100 - s->io_buffer[511];

        s->status = READY_STAT | SEEK_STAT;
        ide_transfer_start(s, s->io_buffer, 0x200, ide_transfer_stop);
        ide_set_irq(s->bus);
        return false;

    case SMART_READ_DATA:
        memset(s->io_buffer, 0, 0x200);
        s->io_buffer[0] = 0x01; /* smart struct version */

        for (n = 0; n < ARRAY_SIZE(smart_attributes); n++) {
            int i;
            for (i = 0; i < 11; i++) {
                s->io_buffer[2 + i + (n * 12)] = smart_attributes[n][i];
            }
        }

        s->io_buffer[362] = 0x02 | (s->smart_autosave ? 0x80 : 0x00);
        if (s->smart_selftest_count == 0) {
            s->io_buffer[363] = 0;
        } else {
            s->io_buffer[363] =
                s->smart_selftest_data[3 +
                           (s->smart_selftest_count - 1) *
                           24];
        }
        s->io_buffer[364] = 0x20;
        s->io_buffer[365] = 0x01;
        /* offline data collection capacity: execute + self-test*/
        s->io_buffer[367] = (1 << 4 | 1 << 3 | 1);
        s->io_buffer[368] = 0x03; /* smart capability (1) */
        s->io_buffer[369] = 0x00; /* smart capability (2) */
        s->io_buffer[370] = 0x01; /* error logging supported */
        s->io_buffer[372] = 0x02; /* minutes for poll short test */
        s->io_buffer[373] = 0x36; /* minutes for poll ext test */
        s->io_buffer[374] = 0x01; /* minutes for poll conveyance */

        for (n = 0; n < 511; n++) {
            s->io_buffer[511] += s->io_buffer[n];
        }
        s->io_buffer[511] = 0x100 - s->io_buffer[511];

        s->status = READY_STAT | SEEK_STAT;
        ide_transfer_start(s, s->io_buffer, 0x200, ide_transfer_stop);
        ide_set_irq(s->bus);
        return false;

    case SMART_READ_LOG:
        switch (s->sector) {
        case 0x01: /* summary smart error log */
            memset(s->io_buffer, 0, 0x200);
            s->io_buffer[0] = 0x01;
            s->io_buffer[1] = 0x00; /* no error entries */
            s->io_buffer[452] = s->smart_errors & 0xff;
            s->io_buffer[453] = (s->smart_errors & 0xff00) >> 8;

            for (n = 0; n < 511; n++) {
                s->io_buffer[511] += s->io_buffer[n];
            }
            s->io_buffer[511] = 0x100 - s->io_buffer[511];
            break;
        case 0x06: /* smart self test log */
            memset(s->io_buffer, 0, 0x200);
            s->io_buffer[0] = 0x01;
            if (s->smart_selftest_count == 0) {
                s->io_buffer[508] = 0;
            } else {
                s->io_buffer[508] = s->smart_selftest_count;
                for (n = 2; n < 506; n++)  {
                    s->io_buffer[n] = s->smart_selftest_data[n];
                }
            }

            for (n = 0; n < 511; n++) {
                s->io_buffer[511] += s->io_buffer[n];
            }
            s->io_buffer[511] = 0x100 - s->io_buffer[511];
            break;
        default:
            goto abort_cmd;
        }
        s->status = READY_STAT | SEEK_STAT;
        ide_transfer_start(s, s->io_buffer, 0x200, ide_transfer_stop);
        ide_set_irq(s->bus);
        return false;

    case SMART_EXECUTE_OFFLINE:
        switch (s->sector) {
        case 0: /* off-line routine */
        case 1: /* short self test */
        case 2: /* extended self test */
            s->smart_selftest_count++;
            if (s->smart_selftest_count > 21) {
                s->smart_selftest_count = 1;
            }
            n = 2 + (s->smart_selftest_count - 1) * 24;
            s->smart_selftest_data[n] = s->sector;
            s->smart_selftest_data[n + 1] = 0x00; /* OK and finished */
            s->smart_selftest_data[n + 2] = 0x34; /* hour count lsb */
            s->smart_selftest_data[n + 3] = 0x12; /* hour count msb */
            break;
        default:
            goto abort_cmd;
        }
        return true;
    }

abort_cmd:
    ide_abort_command(s);
    return true;
}

#define HD_OK (1u << IDE_HD)
#define CD_OK (1u << IDE_CD)
#define CFA_OK (1u << IDE_CFATA)
#define HD_CFA_OK (HD_OK | CFA_OK)
#define ALL_OK (HD_OK | CD_OK | CFA_OK)

/* Set the Disk Seek Completed status bit during completion */
#define SET_DSC (1u << 8)

/* See ACS-2 T13/2015-D Table B.2 Command codes */
static const struct {
    /* Returns true if the completion code should be run */
    bool (*handler)(IDEState *s, uint8_t cmd);
    int flags;
} ide_cmd_table[0x100] = {
    /* NOP not implemented, mandatory for CD */
    [CFA_REQ_EXT_ERROR_CODE]      = { cmd_cfa_req_ext_error_code, CFA_OK },
    [WIN_DSM]                     = { cmd_data_set_management, HD_CFA_OK },
    [WIN_DEVICE_RESET]            = { cmd_device_reset, CD_OK },
    [WIN_RECAL]                   = { cmd_nop, HD_CFA_OK | SET_DSC},
    [WIN_READ]                    = { cmd_read_pio, ALL_OK },
    [WIN_READ_ONCE]               = { cmd_read_pio, HD_CFA_OK },
    [WIN_READ_EXT]                = { cmd_read_pio, HD_CFA_OK },
    [WIN_READDMA_EXT]             = { cmd_read_dma, HD_CFA_OK },
    [WIN_READ_NATIVE_MAX_EXT]     = { cmd_read_native_max, HD_CFA_OK | SET_DSC },
    [WIN_MULTREAD_EXT]            = { cmd_read_multiple, HD_CFA_OK },
    [WIN_WRITE]                   = { cmd_write_pio, HD_CFA_OK },
    [WIN_WRITE_ONCE]              = { cmd_write_pio, HD_CFA_OK },
    [WIN_WRITE_EXT]               = { cmd_write_pio, HD_CFA_OK },
    [WIN_WRITEDMA_EXT]            = { cmd_write_dma, HD_CFA_OK },
    [CFA_WRITE_SECT_WO_ERASE]     = { cmd_write_pio, CFA_OK },
    [WIN_MULTWRITE_EXT]           = { cmd_write_multiple, HD_CFA_OK },
    [WIN_WRITE_VERIFY]            = { cmd_write_pio, HD_CFA_OK },
    [WIN_VERIFY]                  = { cmd_verify, HD_CFA_OK | SET_DSC },
    [WIN_VERIFY_ONCE]             = { cmd_verify, HD_CFA_OK | SET_DSC },
    [WIN_VERIFY_EXT]              = { cmd_verify, HD_CFA_OK | SET_DSC },
    [WIN_SEEK]                    = { cmd_seek, HD_CFA_OK | SET_DSC },
    [CFA_TRANSLATE_SECTOR]        = { cmd_cfa_translate_sector, CFA_OK },
    [WIN_DIAGNOSE]                = { cmd_exec_dev_diagnostic, ALL_OK },
    [WIN_SPECIFY]                 = { cmd_nop, HD_CFA_OK | SET_DSC },
    [WIN_STANDBYNOW2]             = { cmd_nop, HD_CFA_OK },
    [WIN_IDLEIMMEDIATE2]          = { cmd_nop, HD_CFA_OK },
    [WIN_STANDBY2]                = { cmd_nop, HD_CFA_OK },
    [WIN_SETIDLE2]                = { cmd_nop, HD_CFA_OK },
    [WIN_CHECKPOWERMODE2]         = { cmd_check_power_mode, HD_CFA_OK | SET_DSC },
    [WIN_SLEEPNOW2]               = { cmd_nop, HD_CFA_OK },
    [WIN_PACKETCMD]               = { cmd_packet, CD_OK },
    [WIN_PIDENTIFY]               = { cmd_identify_packet, CD_OK },
    [WIN_SMART]                   = { cmd_smart, HD_CFA_OK | SET_DSC },
    [CFA_ACCESS_METADATA_STORAGE] = { cmd_cfa_access_metadata_storage, CFA_OK },
    [CFA_ERASE_SECTORS]           = { cmd_cfa_erase_sectors, CFA_OK | SET_DSC },
    [WIN_MULTREAD]                = { cmd_read_multiple, HD_CFA_OK },
    [WIN_MULTWRITE]               = { cmd_write_multiple, HD_CFA_OK },
    [WIN_SETMULT]                 = { cmd_set_multiple_mode, HD_CFA_OK | SET_DSC },
    [WIN_READDMA]                 = { cmd_read_dma, HD_CFA_OK },
    [WIN_READDMA_ONCE]            = { cmd_read_dma, HD_CFA_OK },
    [WIN_WRITEDMA]                = { cmd_write_dma, HD_CFA_OK },
    [WIN_WRITEDMA_ONCE]           = { cmd_write_dma, HD_CFA_OK },
    [CFA_WRITE_MULTI_WO_ERASE]    = { cmd_write_multiple, CFA_OK },
    [WIN_STANDBYNOW1]             = { cmd_nop, HD_CFA_OK },
    [WIN_IDLEIMMEDIATE]           = { cmd_nop, HD_CFA_OK },
    [WIN_STANDBY]                 = { cmd_nop, HD_CFA_OK },
    [WIN_SETIDLE1]                = { cmd_nop, HD_CFA_OK },
    [WIN_CHECKPOWERMODE1]         = { cmd_check_power_mode, HD_CFA_OK | SET_DSC },
    [WIN_SLEEPNOW1]               = { cmd_nop, HD_CFA_OK },
    [WIN_FLUSH_CACHE]             = { cmd_flush_cache, ALL_OK },
    [WIN_FLUSH_CACHE_EXT]         = { cmd_flush_cache, HD_CFA_OK },
    [WIN_IDENTIFY]                = { cmd_identify, ALL_OK },
    [WIN_SETFEATURES]             = { cmd_set_features, ALL_OK | SET_DSC },
    [IBM_SENSE_CONDITION]         = { cmd_ibm_sense_condition, CFA_OK | SET_DSC },
    [CFA_WEAR_LEVEL]              = { cmd_cfa_erase_sectors, HD_CFA_OK | SET_DSC },
    [WIN_READ_NATIVE_MAX]         = { cmd_read_native_max, HD_CFA_OK | SET_DSC },
};

static bool ide_cmd_permitted(IDEState *s, uint32_t cmd)
{
    return cmd < ARRAY_SIZE(ide_cmd_table)
        && (ide_cmd_table[cmd].flags & (1u << s->drive_kind));
}

void ide_exec_cmd(IDEBus *bus, uint32_t val)
{
    IDEState *s;
    bool complete;

    s = idebus_active_if(bus);
    trace_ide_exec_cmd(bus, s, val);

    /* ignore commands to non existent slave */
    if (s != bus->ifs && !s->blk) {
        return;
    }

    /* Only RESET is allowed while BSY and/or DRQ are set,
     * and only to ATAPI devices. */
    if (s->status & (BUSY_STAT|DRQ_STAT)) {
        if (val != WIN_DEVICE_RESET || s->drive_kind != IDE_CD) {
            return;
        }
    }

    if (!ide_cmd_permitted(s, val)) {
        ide_abort_command(s);
        ide_set_irq(s->bus);
        return;
    }

    s->status = READY_STAT | BUSY_STAT;
    s->error = 0;
    s->io_buffer_offset = 0;

    complete = ide_cmd_table[val].handler(s, val);
    if (complete) {
        s->status &= ~BUSY_STAT;
        assert(!!s->error == !!(s->status & ERR_STAT));

        if ((ide_cmd_table[val].flags & SET_DSC) && !s->error) {
            s->status |= SEEK_STAT;
        }

        ide_cmd_done(s);
        ide_set_irq(s->bus);
    }
}

/* IOport [R]ead [R]egisters */
enum ATA_IOPORT_RR {
    ATA_IOPORT_RR_DATA = 0,
    ATA_IOPORT_RR_ERROR = 1,
    ATA_IOPORT_RR_SECTOR_COUNT = 2,
    ATA_IOPORT_RR_SECTOR_NUMBER = 3,
    ATA_IOPORT_RR_CYLINDER_LOW = 4,
    ATA_IOPORT_RR_CYLINDER_HIGH = 5,
    ATA_IOPORT_RR_DEVICE_HEAD = 6,
    ATA_IOPORT_RR_STATUS = 7,
    ATA_IOPORT_RR_NUM_REGISTERS,
};

const char *ATA_IOPORT_RR_lookup[ATA_IOPORT_RR_NUM_REGISTERS] = {
    [ATA_IOPORT_RR_DATA] = "Data",
    [ATA_IOPORT_RR_ERROR] = "Error",
    [ATA_IOPORT_RR_SECTOR_COUNT] = "Sector Count",
    [ATA_IOPORT_RR_SECTOR_NUMBER] = "Sector Number",
    [ATA_IOPORT_RR_CYLINDER_LOW] = "Cylinder Low",
    [ATA_IOPORT_RR_CYLINDER_HIGH] = "Cylinder High",
    [ATA_IOPORT_RR_DEVICE_HEAD] = "Device/Head",
    [ATA_IOPORT_RR_STATUS] = "Status"
};

uint32_t ide_ioport_read(void *opaque, uint32_t addr)
{
    IDEBus *bus = opaque;
    IDEState *s = idebus_active_if(bus);
    uint32_t reg_num;
    int ret, hob;

    reg_num = addr & 7;
    hob = bus->cmd & (IDE_CTRL_HOB);
    switch (reg_num) {
    case ATA_IOPORT_RR_DATA:
        ret = 0xff;
        break;
    case ATA_IOPORT_RR_ERROR:
        if ((!bus->ifs[0].blk && !bus->ifs[1].blk) ||
            (s != bus->ifs && !s->blk)) {
            ret = 0;
        } else if (!hob) {
            ret = s->error;
        } else {
            ret = s->hob_feature;
        }
        break;
    case ATA_IOPORT_RR_SECTOR_COUNT:
        if (!bus->ifs[0].blk && !bus->ifs[1].blk) {
            ret = 0;
        } else if (!hob) {
            ret = s->nsector & 0xff;
        } else {
            ret = s->hob_nsector;
        }
        break;
    case ATA_IOPORT_RR_SECTOR_NUMBER:
        if (!bus->ifs[0].blk && !bus->ifs[1].blk) {
            ret = 0;
        } else if (!hob) {
            ret = s->sector;
        } else {
            ret = s->hob_sector;
        }
        break;
    case ATA_IOPORT_RR_CYLINDER_LOW:
        if (!bus->ifs[0].blk && !bus->ifs[1].blk) {
            ret = 0;
        } else if (!hob) {
            ret = s->lcyl;
        } else {
            ret = s->hob_lcyl;
        }
        break;
    case ATA_IOPORT_RR_CYLINDER_HIGH:
        if (!bus->ifs[0].blk && !bus->ifs[1].blk) {
            ret = 0;
        } else if (!hob) {
            ret = s->hcyl;
        } else {
            ret = s->hob_hcyl;
        }
        break;
    case ATA_IOPORT_RR_DEVICE_HEAD:
        if (!bus->ifs[0].blk && !bus->ifs[1].blk) {
            ret = 0;
        } else {
            ret = s->select;
        }
        break;
    default:
    case ATA_IOPORT_RR_STATUS:
        if ((!bus->ifs[0].blk && !bus->ifs[1].blk) ||
            (s != bus->ifs && !s->blk)) {
            ret = 0;
        } else {
            ret = s->status;
        }
        qemu_irq_lower(bus->irq);
        break;
    }

    trace_ide_ioport_read(addr, ATA_IOPORT_RR_lookup[reg_num], ret, bus, s);
    return ret;
}

uint32_t ide_status_read(void *opaque, uint32_t addr)
{
    IDEBus *bus = opaque;
    IDEState *s = idebus_active_if(bus);
    int ret;

    if ((!bus->ifs[0].blk && !bus->ifs[1].blk) ||
        (s != bus->ifs && !s->blk)) {
        ret = 0;
    } else {
        ret = s->status;
    }

    trace_ide_status_read(addr, ret, bus, s);
    return ret;
}

static void ide_perform_srst(IDEState *s)
{
    s->status |= BUSY_STAT;

    /* Halt PIO (Via register state); PIO BH remains scheduled. */
    ide_transfer_halt(s);

    /* Cancel DMA -- may drain block device and invoke callbacks */
    ide_cancel_dma_sync(s);

    /* Cancel PIO callback, reset registers/signature, etc */
    ide_reset(s);

    /* perform diagnostic */
    cmd_exec_dev_diagnostic(s, WIN_DIAGNOSE);
}

static void ide_bus_perform_srst(void *opaque)
{
    IDEBus *bus = opaque;
    IDEState *s;
    int i;

    for (i = 0; i < 2; i++) {
        s = &bus->ifs[i];
        ide_perform_srst(s);
    }

    bus->cmd &= ~IDE_CTRL_RESET;
}

void ide_ctrl_write(void *opaque, uint32_t addr, uint32_t val)
{
    IDEBus *bus = opaque;
    IDEState *s;
    int i;

    trace_ide_ctrl_write(addr, val, bus);

    /* Device0 and Device1 each have their own control register,
     * but QEMU models it as just one register in the controller. */
    if (!(bus->cmd & IDE_CTRL_RESET) && (val & IDE_CTRL_RESET)) {
        for (i = 0; i < 2; i++) {
            s = &bus->ifs[i];
            s->status |= BUSY_STAT;
        }
        replay_bh_schedule_oneshot_event(qemu_get_aio_context(),
                                         ide_bus_perform_srst, bus);
    }

    bus->cmd = val;
}

/*
 * Returns true if the running PIO transfer is a PIO out (i.e. data is
 * transferred from the device to the guest), false if it's a PIO in
 */
static bool ide_is_pio_out(IDEState *s)
{
    if (s->end_transfer_func == ide_sector_write ||
        s->end_transfer_func == ide_atapi_cmd) {
        return false;
    } else if (s->end_transfer_func == ide_sector_read ||
               s->end_transfer_func == ide_transfer_stop ||
               s->end_transfer_func == ide_atapi_cmd_reply_end ||
               s->end_transfer_func == ide_dummy_transfer_stop) {
        return true;
    }

    abort();
}

void ide_data_writew(void *opaque, uint32_t addr, uint32_t val)
{
    IDEBus *bus = opaque;
    IDEState *s = idebus_active_if(bus);
    uint8_t *p;

    trace_ide_data_writew(addr, val, bus, s);

    /* PIO data access allowed only when DRQ bit is set. The result of a write
     * during PIO out is indeterminate, just ignore it. */
    if (!(s->status & DRQ_STAT) || ide_is_pio_out(s)) {
        return;
    }

    p = s->data_ptr;
    if (p + 2 > s->data_end) {
        return;
    }

    *(uint16_t *)p = le16_to_cpu(val);
    p += 2;
    s->data_ptr = p;
    if (p >= s->data_end) {
        s->status &= ~DRQ_STAT;
        s->end_transfer_func(s);
    }
}

uint32_t ide_data_readw(void *opaque, uint32_t addr)
{
    IDEBus *bus = opaque;
    IDEState *s = idebus_active_if(bus);
    uint8_t *p;
    int ret;

    /* PIO data access allowed only when DRQ bit is set. The result of a read
     * during PIO in is indeterminate, return 0 and don't move forward. */
    if (!(s->status & DRQ_STAT) || !ide_is_pio_out(s)) {
        return 0;
    }

    p = s->data_ptr;
    if (p + 2 > s->data_end) {
        return 0;
    }

    ret = cpu_to_le16(*(uint16_t *)p);
    p += 2;
    s->data_ptr = p;
    if (p >= s->data_end) {
        s->status &= ~DRQ_STAT;
        s->end_transfer_func(s);
    }

    trace_ide_data_readw(addr, ret, bus, s);
    return ret;
}

void ide_data_writel(void *opaque, uint32_t addr, uint32_t val)
{
    IDEBus *bus = opaque;
    IDEState *s = idebus_active_if(bus);
    uint8_t *p;

    trace_ide_data_writel(addr, val, bus, s);

    /* PIO data access allowed only when DRQ bit is set. The result of a write
     * during PIO out is indeterminate, just ignore it. */
    if (!(s->status & DRQ_STAT) || ide_is_pio_out(s)) {
        return;
    }

    p = s->data_ptr;
    if (p + 4 > s->data_end) {
        return;
    }

    *(uint32_t *)p = le32_to_cpu(val);
    p += 4;
    s->data_ptr = p;
    if (p >= s->data_end) {
        s->status &= ~DRQ_STAT;
        s->end_transfer_func(s);
    }
}

uint32_t ide_data_readl(void *opaque, uint32_t addr)
{
    IDEBus *bus = opaque;
    IDEState *s = idebus_active_if(bus);
    uint8_t *p;
    int ret;

    /* PIO data access allowed only when DRQ bit is set. The result of a read
     * during PIO in is indeterminate, return 0 and don't move forward. */
    if (!(s->status & DRQ_STAT) || !ide_is_pio_out(s)) {
        ret = 0;
        goto out;
    }

    p = s->data_ptr;
    if (p + 4 > s->data_end) {
        return 0;
    }

    ret = cpu_to_le32(*(uint32_t *)p);
    p += 4;
    s->data_ptr = p;
    if (p >= s->data_end) {
        s->status &= ~DRQ_STAT;
        s->end_transfer_func(s);
    }

out:
    trace_ide_data_readl(addr, ret, bus, s);
    return ret;
}

static void ide_dummy_transfer_stop(IDEState *s)
{
    s->data_ptr = s->io_buffer;
    s->data_end = s->io_buffer;
    s->io_buffer[0] = 0xff;
    s->io_buffer[1] = 0xff;
    s->io_buffer[2] = 0xff;
    s->io_buffer[3] = 0xff;
}

void ide_bus_reset(IDEBus *bus)
{
    bus->unit = 0;
    bus->cmd = 0;
    ide_reset(&bus->ifs[0]);
    ide_reset(&bus->ifs[1]);
    ide_clear_hob(bus);

    /* pending async DMA */
    if (bus->dma->aiocb) {
        trace_ide_bus_reset_aio();
        blk_aio_cancel(bus->dma->aiocb);
        bus->dma->aiocb = NULL;
    }

    /* reset dma provider too */
    if (bus->dma->ops->reset) {
        bus->dma->ops->reset(bus->dma);
    }
}

static bool ide_cd_is_tray_open(void *opaque)
{
    return ((IDEState *)opaque)->tray_open;
}

static bool ide_cd_is_medium_locked(void *opaque)
{
    return ((IDEState *)opaque)->tray_locked;
}

static void ide_resize_cb(void *opaque)
{
    IDEState *s = opaque;
    uint64_t nb_sectors;

    if (!s->identify_set) {
        return;
    }

    blk_get_geometry(s->blk, &nb_sectors);
    s->nb_sectors = nb_sectors;

    /* Update the identify data buffer. */
    if (s->drive_kind == IDE_CFATA) {
        ide_cfata_identify_size(s);
    } else {
        /* IDE_CD uses a different set of callbacks entirely. */
        assert(s->drive_kind != IDE_CD);
        ide_identify_size(s);
    }
}

static const BlockDevOps ide_cd_block_ops = {
    .change_media_cb = ide_cd_change_cb,
    .eject_request_cb = ide_cd_eject_request_cb,
    .is_tray_open = ide_cd_is_tray_open,
    .is_medium_locked = ide_cd_is_medium_locked,
};

static const BlockDevOps ide_hd_block_ops = {
    .resize_cb = ide_resize_cb,
};

int ide_init_drive(IDEState *s, BlockBackend *blk, IDEDriveKind kind,
                   const char *version, const char *serial, const char *model,
                   uint64_t wwn,
                   uint32_t cylinders, uint32_t heads, uint32_t secs,
                   int chs_trans, Error **errp)
{
    uint64_t nb_sectors;

    s->blk = blk;
    s->drive_kind = kind;

    blk_get_geometry(blk, &nb_sectors);
    s->cylinders = cylinders;
    s->heads = heads;
    s->sectors = secs;
    s->chs_trans = chs_trans;
    s->nb_sectors = nb_sectors;
    s->wwn = wwn;
    /* The SMART values should be preserved across power cycles
       but they aren't.  */
    s->smart_enabled = 1;
    s->smart_autosave = 1;
    s->smart_errors = 0;
    s->smart_selftest_count = 0;
    if (kind == IDE_CD) {
        blk_set_dev_ops(blk, &ide_cd_block_ops, s);
        blk_set_guest_block_size(blk, 2048);
    } else {
        if (!blk_is_inserted(s->blk)) {
            error_setg(errp, "Device needs media, but drive is empty");
            return -1;
        }
        if (!blk_is_writable(blk)) {
            error_setg(errp, "Can't use a read-only drive");
            return -1;
        }
        blk_set_dev_ops(blk, &ide_hd_block_ops, s);
    }
    if (serial) {
        pstrcpy(s->drive_serial_str, sizeof(s->drive_serial_str), serial);
    } else {
        snprintf(s->drive_serial_str, sizeof(s->drive_serial_str),
                 "QM%05d", s->drive_serial);
    }
    if (model) {
        pstrcpy(s->drive_model_str, sizeof(s->drive_model_str), model);
    } else {
        switch (kind) {
        case IDE_CD:
            strcpy(s->drive_model_str, "QEMU DVD-ROM");
            break;
        case IDE_CFATA:
            strcpy(s->drive_model_str, "QEMU MICRODRIVE");
            break;
        default:
            strcpy(s->drive_model_str, "QEMU HARDDISK");
            break;
        }
    }

    if (version) {
        pstrcpy(s->version, sizeof(s->version), version);
    } else {
        pstrcpy(s->version, sizeof(s->version), qemu_hw_version());
    }

    ide_reset(s);
    blk_iostatus_enable(blk);
    return 0;
}

static void ide_init1(IDEBus *bus, int unit)
{
    static int drive_serial = 1;
    IDEState *s = &bus->ifs[unit];

    s->bus = bus;
    s->unit = unit;
    s->drive_serial = drive_serial++;
    /* we need at least 2k alignment for accessing CDROMs using O_DIRECT */
    s->io_buffer_total_len = IDE_DMA_BUF_SECTORS*512 + 4;
    s->io_buffer = qemu_memalign(2048, s->io_buffer_total_len);
    memset(s->io_buffer, 0, s->io_buffer_total_len);

    s->smart_selftest_data = blk_blockalign(s->blk, 512);
    memset(s->smart_selftest_data, 0, 512);

    s->sector_write_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                           ide_sector_write_timer_cb, s);
}

static int ide_nop_int(const IDEDMA *dma, bool is_write)
{
    return 0;
}

static void ide_nop(const IDEDMA *dma)
{
}

static int32_t ide_nop_int32(const IDEDMA *dma, int32_t l)
{
    return 0;
}

static const IDEDMAOps ide_dma_nop_ops = {
    .prepare_buf    = ide_nop_int32,
    .restart_dma    = ide_nop,
    .rw_buf         = ide_nop_int,
};

static void ide_restart_dma(IDEState *s, enum ide_dma_cmd dma_cmd)
{
    s->unit = s->bus->retry_unit;
    ide_set_sector(s, s->bus->retry_sector_num);
    s->nsector = s->bus->retry_nsector;
    s->bus->dma->ops->restart_dma(s->bus->dma);
    s->io_buffer_size = 0;
    s->dma_cmd = dma_cmd;
    ide_start_dma(s, ide_dma_cb);
}

static void ide_restart_bh(void *opaque)
{
    IDEBus *bus = opaque;
    IDEState *s;
    bool is_read;
    int error_status;

    qemu_bh_delete(bus->bh);
    bus->bh = NULL;

    error_status = bus->error_status;
    if (bus->error_status == 0) {
        return;
    }

    s = idebus_active_if(bus);
    is_read = (bus->error_status & IDE_RETRY_READ) != 0;

    /* The error status must be cleared before resubmitting the request: The
     * request may fail again, and this case can only be distinguished if the
     * called function can set a new error status. */
    bus->error_status = 0;

    /* The HBA has generically asked to be kicked on retry */
    if (error_status & IDE_RETRY_HBA) {
        if (s->bus->dma->ops->restart) {
            s->bus->dma->ops->restart(s->bus->dma);
        }
    } else if (IS_IDE_RETRY_DMA(error_status)) {
        if (error_status & IDE_RETRY_TRIM) {
            ide_restart_dma(s, IDE_DMA_TRIM);
        } else {
            ide_restart_dma(s, is_read ? IDE_DMA_READ : IDE_DMA_WRITE);
        }
    } else if (IS_IDE_RETRY_PIO(error_status)) {
        if (is_read) {
            ide_sector_read(s);
        } else {
            ide_sector_write(s);
        }
    } else if (error_status & IDE_RETRY_FLUSH) {
        ide_flush_cache(s);
    } else if (IS_IDE_RETRY_ATAPI(error_status)) {
        assert(s->end_transfer_func == ide_atapi_cmd);
        ide_atapi_dma_restart(s);
    } else {
        abort();
    }
}

static void ide_restart_cb(void *opaque, bool running, RunState state)
{
    IDEBus *bus = opaque;

    if (!running)
        return;

    if (!bus->bh) {
        bus->bh = qemu_bh_new(ide_restart_bh, bus);
        qemu_bh_schedule(bus->bh);
    }
}

void ide_register_restart_cb(IDEBus *bus)
{
    if (bus->dma->ops->restart_dma) {
        bus->vmstate = qemu_add_vm_change_state_handler(ide_restart_cb, bus);
    }
}

static IDEDMA ide_dma_nop = {
    .ops = &ide_dma_nop_ops,
    .aiocb = NULL,
};

void ide_init2(IDEBus *bus, qemu_irq irq)
{
    int i;

    for(i = 0; i < 2; i++) {
        ide_init1(bus, i);
        ide_reset(&bus->ifs[i]);
    }
    bus->irq = irq;
    bus->dma = &ide_dma_nop;
}

void ide_exit(IDEState *s)
{
    timer_free(s->sector_write_timer);
    qemu_vfree(s->smart_selftest_data);
    qemu_vfree(s->io_buffer);
}

static bool is_identify_set(void *opaque, int version_id)
{
    IDEState *s = opaque;

    return s->identify_set != 0;
}

static EndTransferFunc* transfer_end_table[] = {
        ide_sector_read,
        ide_sector_write,
        ide_transfer_stop,
        ide_atapi_cmd_reply_end,
        ide_atapi_cmd,
        ide_dummy_transfer_stop,
};

static int transfer_end_table_idx(EndTransferFunc *fn)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(transfer_end_table); i++)
        if (transfer_end_table[i] == fn)
            return i;

    return -1;
}

static int ide_drive_post_load(void *opaque, int version_id)
{
    IDEState *s = opaque;

    if (s->blk && s->identify_set) {
        blk_set_enable_write_cache(s->blk, !!(s->identify_data[85] & (1 << 5)));
    }
    return 0;
}

static int ide_drive_pio_post_load(void *opaque, int version_id)
{
    IDEState *s = opaque;

    if (s->end_transfer_fn_idx >= ARRAY_SIZE(transfer_end_table)) {
        return -EINVAL;
    }
    s->end_transfer_func = transfer_end_table[s->end_transfer_fn_idx];
    s->data_ptr = s->io_buffer + s->cur_io_buffer_offset;
    s->data_end = s->data_ptr + s->cur_io_buffer_len;
    s->atapi_dma = s->feature & 1; /* as per cmd_packet */

    return 0;
}

static int ide_drive_pio_pre_save(void *opaque)
{
    IDEState *s = opaque;
    int idx;

    s->cur_io_buffer_offset = s->data_ptr - s->io_buffer;
    s->cur_io_buffer_len = s->data_end - s->data_ptr;

    idx = transfer_end_table_idx(s->end_transfer_func);
    if (idx == -1) {
        fprintf(stderr, "%s: invalid end_transfer_func for DRQ_STAT\n",
                        __func__);
        s->end_transfer_fn_idx = 2;
    } else {
        s->end_transfer_fn_idx = idx;
    }

    return 0;
}

static bool ide_drive_pio_state_needed(void *opaque)
{
    IDEState *s = opaque;

    return ((s->status & DRQ_STAT) != 0)
        || (s->bus->error_status & IDE_RETRY_PIO);
}

static bool ide_tray_state_needed(void *opaque)
{
    IDEState *s = opaque;

    return s->tray_open || s->tray_locked;
}

static bool ide_atapi_gesn_needed(void *opaque)
{
    IDEState *s = opaque;

    return s->events.new_media || s->events.eject_request;
}

static bool ide_error_needed(void *opaque)
{
    IDEBus *bus = opaque;

    return (bus->error_status != 0);
}

/* Fields for GET_EVENT_STATUS_NOTIFICATION ATAPI command */
static const VMStateDescription vmstate_ide_atapi_gesn_state = {
    .name ="ide_drive/atapi/gesn_state",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = ide_atapi_gesn_needed,
    .fields = (VMStateField[]) {
        VMSTATE_BOOL(events.new_media, IDEState),
        VMSTATE_BOOL(events.eject_request, IDEState),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_ide_tray_state = {
    .name = "ide_drive/tray_state",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = ide_tray_state_needed,
    .fields = (VMStateField[]) {
        VMSTATE_BOOL(tray_open, IDEState),
        VMSTATE_BOOL(tray_locked, IDEState),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_ide_drive_pio_state = {
    .name = "ide_drive/pio_state",
    .version_id = 1,
    .minimum_version_id = 1,
    .pre_save = ide_drive_pio_pre_save,
    .post_load = ide_drive_pio_post_load,
    .needed = ide_drive_pio_state_needed,
    .fields = (VMStateField[]) {
        VMSTATE_INT32(req_nb_sectors, IDEState),
        VMSTATE_VARRAY_INT32(io_buffer, IDEState, io_buffer_total_len, 1,
                             vmstate_info_uint8, uint8_t),
        VMSTATE_INT32(cur_io_buffer_offset, IDEState),
        VMSTATE_INT32(cur_io_buffer_len, IDEState),
        VMSTATE_UINT8(end_transfer_fn_idx, IDEState),
        VMSTATE_INT32(elementary_transfer_size, IDEState),
        VMSTATE_INT32(packet_transfer_size, IDEState),
        VMSTATE_END_OF_LIST()
    }
};

const VMStateDescription vmstate_ide_drive = {
    .name = "ide_drive",
    .version_id = 3,
    .minimum_version_id = 0,
    .post_load = ide_drive_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_INT32(mult_sectors, IDEState),
        VMSTATE_INT32(identify_set, IDEState),
        VMSTATE_BUFFER_TEST(identify_data, IDEState, is_identify_set),
        VMSTATE_UINT8(feature, IDEState),
        VMSTATE_UINT8(error, IDEState),
        VMSTATE_UINT32(nsector, IDEState),
        VMSTATE_UINT8(sector, IDEState),
        VMSTATE_UINT8(lcyl, IDEState),
        VMSTATE_UINT8(hcyl, IDEState),
        VMSTATE_UINT8(hob_feature, IDEState),
        VMSTATE_UINT8(hob_sector, IDEState),
        VMSTATE_UINT8(hob_nsector, IDEState),
        VMSTATE_UINT8(hob_lcyl, IDEState),
        VMSTATE_UINT8(hob_hcyl, IDEState),
        VMSTATE_UINT8(select, IDEState),
        VMSTATE_UINT8(status, IDEState),
        VMSTATE_UINT8(lba48, IDEState),
        VMSTATE_UINT8(sense_key, IDEState),
        VMSTATE_UINT8(asc, IDEState),
        VMSTATE_UINT8_V(cdrom_changed, IDEState, 3),
        VMSTATE_END_OF_LIST()
    },
    .subsections = (const VMStateDescription*[]) {
        &vmstate_ide_drive_pio_state,
        &vmstate_ide_tray_state,
        &vmstate_ide_atapi_gesn_state,
        NULL
    }
};

static const VMStateDescription vmstate_ide_error_status = {
    .name ="ide_bus/error",
    .version_id = 2,
    .minimum_version_id = 1,
    .needed = ide_error_needed,
    .fields = (VMStateField[]) {
        VMSTATE_INT32(error_status, IDEBus),
        VMSTATE_INT64_V(retry_sector_num, IDEBus, 2),
        VMSTATE_UINT32_V(retry_nsector, IDEBus, 2),
        VMSTATE_UINT8_V(retry_unit, IDEBus, 2),
        VMSTATE_END_OF_LIST()
    }
};

const VMStateDescription vmstate_ide_bus = {
    .name = "ide_bus",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8(cmd, IDEBus),
        VMSTATE_UINT8(unit, IDEBus),
        VMSTATE_END_OF_LIST()
    },
    .subsections = (const VMStateDescription*[]) {
        &vmstate_ide_error_status,
        NULL
    }
};

void ide_drive_get(DriveInfo **hd, int n)
{
    int i;

    for (i = 0; i < n; i++) {
        hd[i] = drive_get_by_index(IF_IDE, i);
    }
}

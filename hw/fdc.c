/*
 * QEMU Floppy disk emulator (Intel 82078)
 *
 * Copyright (c) 2003, 2007 Jocelyn Mayer
 * Copyright (c) 2008 Hervé Poussineau
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
/*
 * The controller is used in Sun4m systems in a slightly different
 * way. There are changes in DOR register and DMA is not available.
 */
#include "hw.h"
#include "fdc.h"
#include "block.h"
#include "qemu-timer.h"
#include "isa.h"

/********************************************************/
/* debug Floppy devices */
//#define DEBUG_FLOPPY

#ifdef DEBUG_FLOPPY
#define FLOPPY_DPRINTF(fmt, args...) \
do { printf("FLOPPY: " fmt , ##args); } while (0)
#else
#define FLOPPY_DPRINTF(fmt, args...)
#endif

#define FLOPPY_ERROR(fmt, args...) \
do { printf("FLOPPY ERROR: %s: " fmt, __func__ , ##args); } while (0)

/********************************************************/
/* Floppy drive emulation                               */

#define GET_CUR_DRV(fdctrl) ((fdctrl)->cur_drv)
#define SET_CUR_DRV(fdctrl, drive) ((fdctrl)->cur_drv = (drive))

/* Will always be a fixed parameter for us */
#define FD_SECTOR_LEN 512
#define FD_SECTOR_SC  2   /* Sector size code */

/* Floppy disk drive emulation */
typedef enum fdisk_type_t {
    FDRIVE_DISK_288   = 0x01, /* 2.88 MB disk           */
    FDRIVE_DISK_144   = 0x02, /* 1.44 MB disk           */
    FDRIVE_DISK_720   = 0x03, /* 720 kB disk            */
    FDRIVE_DISK_USER  = 0x04, /* User defined geometry  */
    FDRIVE_DISK_NONE  = 0x05, /* No disk                */
} fdisk_type_t;

typedef enum fdrive_type_t {
    FDRIVE_DRV_144  = 0x00,   /* 1.44 MB 3"5 drive      */
    FDRIVE_DRV_288  = 0x01,   /* 2.88 MB 3"5 drive      */
    FDRIVE_DRV_120  = 0x02,   /* 1.2  MB 5"25 drive     */
    FDRIVE_DRV_NONE = 0x03,   /* No drive connected     */
} fdrive_type_t;

typedef enum fdisk_flags_t {
    FDISK_DBL_SIDES  = 0x01,
} fdisk_flags_t;

typedef struct fdrive_t {
    BlockDriverState *bs;
    /* Drive status */
    fdrive_type_t drive;
    uint8_t perpendicular;    /* 2.88 MB access mode    */
    /* Position */
    uint8_t head;
    uint8_t track;
    uint8_t sect;
    /* Media */
    fdisk_flags_t flags;
    uint8_t last_sect;        /* Nb sector per track    */
    uint8_t max_track;        /* Nb of tracks           */
    uint16_t bps;             /* Bytes per sector       */
    uint8_t ro;               /* Is read-only           */
} fdrive_t;

static void fd_init (fdrive_t *drv, BlockDriverState *bs)
{
    /* Drive */
    drv->bs = bs;
    drv->drive = FDRIVE_DRV_NONE;
    drv->perpendicular = 0;
    /* Disk */
    drv->last_sect = 0;
    drv->max_track = 0;
}

static int _fd_sector (uint8_t head, uint8_t track,
                       uint8_t sect, uint8_t last_sect)
{
    return (((track * 2) + head) * last_sect) + sect - 1;
}

/* Returns current position, in sectors, for given drive */
static int fd_sector (fdrive_t *drv)
{
    return _fd_sector(drv->head, drv->track, drv->sect, drv->last_sect);
}

/* Seek to a new position:
 * returns 0 if already on right track
 * returns 1 if track changed
 * returns 2 if track is invalid
 * returns 3 if sector is invalid
 * returns 4 if seek is disabled
 */
static int fd_seek (fdrive_t *drv, uint8_t head, uint8_t track, uint8_t sect,
                    int enable_seek)
{
    uint32_t sector;
    int ret;

    if (track > drv->max_track ||
        (head != 0 && (drv->flags & FDISK_DBL_SIDES) == 0)) {
        FLOPPY_DPRINTF("try to read %d %02x %02x (max=%d %d %02x %02x)\n",
                       head, track, sect, 1,
                       (drv->flags & FDISK_DBL_SIDES) == 0 ? 0 : 1,
                       drv->max_track, drv->last_sect);
        return 2;
    }
    if (sect > drv->last_sect) {
        FLOPPY_DPRINTF("try to read %d %02x %02x (max=%d %d %02x %02x)\n",
                       head, track, sect, 1,
                       (drv->flags & FDISK_DBL_SIDES) == 0 ? 0 : 1,
                       drv->max_track, drv->last_sect);
        return 3;
    }
    sector = _fd_sector(head, track, sect, drv->last_sect);
    ret = 0;
    if (sector != fd_sector(drv)) {
#if 0
        if (!enable_seek) {
            FLOPPY_ERROR("no implicit seek %d %02x %02x (max=%d %02x %02x)\n",
                         head, track, sect, 1, drv->max_track, drv->last_sect);
            return 4;
        }
#endif
        drv->head = head;
        if (drv->track != track)
            ret = 1;
        drv->track = track;
        drv->sect = sect;
    }

    return ret;
}

/* Set drive back to track 0 */
static void fd_recalibrate (fdrive_t *drv)
{
    FLOPPY_DPRINTF("recalibrate\n");
    drv->head = 0;
    drv->track = 0;
    drv->sect = 1;
}

/* Recognize floppy formats */
typedef struct fd_format_t {
    fdrive_type_t drive;
    fdisk_type_t  disk;
    uint8_t last_sect;
    uint8_t max_track;
    uint8_t max_head;
    const char *str;
} fd_format_t;

static const fd_format_t fd_formats[] = {
    /* First entry is default format */
    /* 1.44 MB 3"1/2 floppy disks */
    { FDRIVE_DRV_144, FDRIVE_DISK_144, 18, 80, 1, "1.44 MB 3\"1/2", },
    { FDRIVE_DRV_144, FDRIVE_DISK_144, 20, 80, 1,  "1.6 MB 3\"1/2", },
    { FDRIVE_DRV_144, FDRIVE_DISK_144, 21, 80, 1, "1.68 MB 3\"1/2", },
    { FDRIVE_DRV_144, FDRIVE_DISK_144, 21, 82, 1, "1.72 MB 3\"1/2", },
    { FDRIVE_DRV_144, FDRIVE_DISK_144, 21, 83, 1, "1.74 MB 3\"1/2", },
    { FDRIVE_DRV_144, FDRIVE_DISK_144, 22, 80, 1, "1.76 MB 3\"1/2", },
    { FDRIVE_DRV_144, FDRIVE_DISK_144, 23, 80, 1, "1.84 MB 3\"1/2", },
    { FDRIVE_DRV_144, FDRIVE_DISK_144, 24, 80, 1, "1.92 MB 3\"1/2", },
    /* 2.88 MB 3"1/2 floppy disks */
    { FDRIVE_DRV_288, FDRIVE_DISK_288, 36, 80, 1, "2.88 MB 3\"1/2", },
    { FDRIVE_DRV_288, FDRIVE_DISK_288, 39, 80, 1, "3.12 MB 3\"1/2", },
    { FDRIVE_DRV_288, FDRIVE_DISK_288, 40, 80, 1,  "3.2 MB 3\"1/2", },
    { FDRIVE_DRV_288, FDRIVE_DISK_288, 44, 80, 1, "3.52 MB 3\"1/2", },
    { FDRIVE_DRV_288, FDRIVE_DISK_288, 48, 80, 1, "3.84 MB 3\"1/2", },
    /* 720 kB 3"1/2 floppy disks */
    { FDRIVE_DRV_144, FDRIVE_DISK_720,  9, 80, 1,  "720 kB 3\"1/2", },
    { FDRIVE_DRV_144, FDRIVE_DISK_720, 10, 80, 1,  "800 kB 3\"1/2", },
    { FDRIVE_DRV_144, FDRIVE_DISK_720, 10, 82, 1,  "820 kB 3\"1/2", },
    { FDRIVE_DRV_144, FDRIVE_DISK_720, 10, 83, 1,  "830 kB 3\"1/2", },
    { FDRIVE_DRV_144, FDRIVE_DISK_720, 13, 80, 1, "1.04 MB 3\"1/2", },
    { FDRIVE_DRV_144, FDRIVE_DISK_720, 14, 80, 1, "1.12 MB 3\"1/2", },
    /* 1.2 MB 5"1/4 floppy disks */
    { FDRIVE_DRV_120, FDRIVE_DISK_288, 15, 80, 1,  "1.2 kB 5\"1/4", },
    { FDRIVE_DRV_120, FDRIVE_DISK_288, 18, 80, 1, "1.44 MB 5\"1/4", },
    { FDRIVE_DRV_120, FDRIVE_DISK_288, 18, 82, 1, "1.48 MB 5\"1/4", },
    { FDRIVE_DRV_120, FDRIVE_DISK_288, 18, 83, 1, "1.49 MB 5\"1/4", },
    { FDRIVE_DRV_120, FDRIVE_DISK_288, 20, 80, 1,  "1.6 MB 5\"1/4", },
    /* 720 kB 5"1/4 floppy disks */
    { FDRIVE_DRV_120, FDRIVE_DISK_288,  9, 80, 1,  "720 kB 5\"1/4", },
    { FDRIVE_DRV_120, FDRIVE_DISK_288, 11, 80, 1,  "880 kB 5\"1/4", },
    /* 360 kB 5"1/4 floppy disks */
    { FDRIVE_DRV_120, FDRIVE_DISK_288,  9, 40, 1,  "360 kB 5\"1/4", },
    { FDRIVE_DRV_120, FDRIVE_DISK_288,  9, 40, 0,  "180 kB 5\"1/4", },
    { FDRIVE_DRV_120, FDRIVE_DISK_288, 10, 41, 1,  "410 kB 5\"1/4", },
    { FDRIVE_DRV_120, FDRIVE_DISK_288, 10, 42, 1,  "420 kB 5\"1/4", },
    /* 320 kB 5"1/4 floppy disks */
    { FDRIVE_DRV_120, FDRIVE_DISK_288,  8, 40, 1,  "320 kB 5\"1/4", },
    { FDRIVE_DRV_120, FDRIVE_DISK_288,  8, 40, 0,  "160 kB 5\"1/4", },
    /* 360 kB must match 5"1/4 better than 3"1/2... */
    { FDRIVE_DRV_144, FDRIVE_DISK_720,  9, 80, 0,  "360 kB 3\"1/2", },
    /* end */
    { FDRIVE_DRV_NONE, FDRIVE_DISK_NONE, -1, -1, 0, NULL, },
};

/* Revalidate a disk drive after a disk change */
static void fd_revalidate (fdrive_t *drv)
{
    const fd_format_t *parse;
    uint64_t nb_sectors, size;
    int i, first_match, match;
    int nb_heads, max_track, last_sect, ro;

    FLOPPY_DPRINTF("revalidate\n");
    if (drv->bs != NULL && bdrv_is_inserted(drv->bs)) {
        ro = bdrv_is_read_only(drv->bs);
        bdrv_get_geometry_hint(drv->bs, &nb_heads, &max_track, &last_sect);
        if (nb_heads != 0 && max_track != 0 && last_sect != 0) {
            FLOPPY_DPRINTF("User defined disk (%d %d %d)",
                           nb_heads - 1, max_track, last_sect);
        } else {
            bdrv_get_geometry(drv->bs, &nb_sectors);
            match = -1;
            first_match = -1;
            for (i = 0;; i++) {
                parse = &fd_formats[i];
                if (parse->drive == FDRIVE_DRV_NONE)
                    break;
                if (drv->drive == parse->drive ||
                    drv->drive == FDRIVE_DRV_NONE) {
                    size = (parse->max_head + 1) * parse->max_track *
                        parse->last_sect;
                    if (nb_sectors == size) {
                        match = i;
                        break;
                    }
                    if (first_match == -1)
                        first_match = i;
                }
            }
            if (match == -1) {
                if (first_match == -1)
                    match = 1;
                else
                    match = first_match;
                parse = &fd_formats[match];
            }
            nb_heads = parse->max_head + 1;
            max_track = parse->max_track;
            last_sect = parse->last_sect;
            drv->drive = parse->drive;
            FLOPPY_DPRINTF("%s floppy disk (%d h %d t %d s) %s\n", parse->str,
                           nb_heads, max_track, last_sect, ro ? "ro" : "rw");
        }
        if (nb_heads == 1) {
            drv->flags &= ~FDISK_DBL_SIDES;
        } else {
            drv->flags |= FDISK_DBL_SIDES;
        }
        drv->max_track = max_track;
        drv->last_sect = last_sect;
        drv->ro = ro;
    } else {
        FLOPPY_DPRINTF("No disk in drive\n");
        drv->last_sect = 0;
        drv->max_track = 0;
        drv->flags &= ~FDISK_DBL_SIDES;
    }
}

/********************************************************/
/* Intel 82078 floppy disk controller emulation          */

static void fdctrl_reset (fdctrl_t *fdctrl, int do_irq);
static void fdctrl_reset_fifo (fdctrl_t *fdctrl);
static int fdctrl_transfer_handler (void *opaque, int nchan,
                                    int dma_pos, int dma_len);
static void fdctrl_raise_irq (fdctrl_t *fdctrl, uint8_t status0);

static uint32_t fdctrl_read_statusA (fdctrl_t *fdctrl);
static uint32_t fdctrl_read_statusB (fdctrl_t *fdctrl);
static uint32_t fdctrl_read_dor (fdctrl_t *fdctrl);
static void fdctrl_write_dor (fdctrl_t *fdctrl, uint32_t value);
static uint32_t fdctrl_read_tape (fdctrl_t *fdctrl);
static void fdctrl_write_tape (fdctrl_t *fdctrl, uint32_t value);
static uint32_t fdctrl_read_main_status (fdctrl_t *fdctrl);
static void fdctrl_write_rate (fdctrl_t *fdctrl, uint32_t value);
static uint32_t fdctrl_read_data (fdctrl_t *fdctrl);
static void fdctrl_write_data (fdctrl_t *fdctrl, uint32_t value);
static uint32_t fdctrl_read_dir (fdctrl_t *fdctrl);

enum {
    FD_DIR_WRITE   = 0,
    FD_DIR_READ    = 1,
    FD_DIR_SCANE   = 2,
    FD_DIR_SCANL   = 3,
    FD_DIR_SCANH   = 4,
};

enum {
    FD_STATE_MULTI  = 0x01,	/* multi track flag */
    FD_STATE_FORMAT = 0x02,	/* format flag */
    FD_STATE_SEEK   = 0x04,	/* seek flag */
};

enum {
    FD_REG_SRA = 0x00,
    FD_REG_SRB = 0x01,
    FD_REG_DOR = 0x02,
    FD_REG_TDR = 0x03,
    FD_REG_MSR = 0x04,
    FD_REG_DSR = 0x04,
    FD_REG_FIFO = 0x05,
    FD_REG_DIR = 0x07,
};

enum {
    FD_CMD_READ_TRACK = 0x02,
    FD_CMD_SPECIFY = 0x03,
    FD_CMD_SENSE_DRIVE_STATUS = 0x04,
    FD_CMD_WRITE = 0x05,
    FD_CMD_READ = 0x06,
    FD_CMD_RECALIBRATE = 0x07,
    FD_CMD_SENSE_INTERRUPT_STATUS = 0x08,
    FD_CMD_WRITE_DELETED = 0x09,
    FD_CMD_READ_ID = 0x0a,
    FD_CMD_READ_DELETED = 0x0c,
    FD_CMD_FORMAT_TRACK = 0x0d,
    FD_CMD_DUMPREG = 0x0e,
    FD_CMD_SEEK = 0x0f,
    FD_CMD_VERSION = 0x10,
    FD_CMD_SCAN_EQUAL = 0x11,
    FD_CMD_PERPENDICULAR_MODE = 0x12,
    FD_CMD_CONFIGURE = 0x13,
    FD_CMD_LOCK = 0x14,
    FD_CMD_VERIFY = 0x16,
    FD_CMD_POWERDOWN_MODE = 0x17,
    FD_CMD_PART_ID = 0x18,
    FD_CMD_SCAN_LOW_OR_EQUAL = 0x19,
    FD_CMD_SCAN_HIGH_OR_EQUAL = 0x1d,
    FD_CMD_SAVE = 0x2c,
    FD_CMD_OPTION = 0x33,
    FD_CMD_RESTORE = 0x4c,
    FD_CMD_DRIVE_SPECIFICATION_COMMAND = 0x8e,
    FD_CMD_RELATIVE_SEEK_OUT = 0x8f,
    FD_CMD_FORMAT_AND_WRITE = 0xcd,
    FD_CMD_RELATIVE_SEEK_IN = 0xcf,
};

enum {
    FD_CONFIG_PRETRK = 0xff, /* Pre-compensation set to track 0 */
    FD_CONFIG_FIFOTHR = 0x0f, /* FIFO threshold set to 1 byte */
    FD_CONFIG_POLL  = 0x10, /* Poll enabled */
    FD_CONFIG_EFIFO = 0x20, /* FIFO disabled */
    FD_CONFIG_EIS   = 0x40, /* No implied seeks */
};

enum {
    FD_SR0_EQPMT    = 0x10,
    FD_SR0_SEEK     = 0x20,
    FD_SR0_ABNTERM  = 0x40,
    FD_SR0_INVCMD   = 0x80,
    FD_SR0_RDYCHG   = 0xc0,
};

enum {
    FD_SR1_EC       = 0x80, /* End of cylinder */
};

enum {
    FD_SR2_SNS      = 0x04, /* Scan not satisfied */
    FD_SR2_SEH      = 0x08, /* Scan equal hit */
};

enum {
    FD_SRA_DIR      = 0x01,
    FD_SRA_nWP      = 0x02,
    FD_SRA_nINDX    = 0x04,
    FD_SRA_HDSEL    = 0x08,
    FD_SRA_nTRK0    = 0x10,
    FD_SRA_STEP     = 0x20,
    FD_SRA_nDRV2    = 0x40,
    FD_SRA_INTPEND  = 0x80,
};

enum {
    FD_SRB_MTR0     = 0x01,
    FD_SRB_MTR1     = 0x02,
    FD_SRB_WGATE    = 0x04,
    FD_SRB_RDATA    = 0x08,
    FD_SRB_WDATA    = 0x10,
    FD_SRB_DR0      = 0x20,
};

enum {
#if MAX_FD == 4
    FD_DOR_SELMASK  = 0x03,
#else
    FD_DOR_SELMASK  = 0x01,
#endif
    FD_DOR_nRESET   = 0x04,
    FD_DOR_DMAEN    = 0x08,
    FD_DOR_MOTEN0   = 0x10,
    FD_DOR_MOTEN1   = 0x20,
    FD_DOR_MOTEN2   = 0x40,
    FD_DOR_MOTEN3   = 0x80,
};

enum {
#if MAX_FD == 4
    FD_TDR_BOOTSEL  = 0x0c,
#else
    FD_TDR_BOOTSEL  = 0x04,
#endif
};

enum {
    FD_DSR_DRATEMASK= 0x03,
    FD_DSR_PWRDOWN  = 0x40,
    FD_DSR_SWRESET  = 0x80,
};

enum {
    FD_MSR_DRV0BUSY = 0x01,
    FD_MSR_DRV1BUSY = 0x02,
    FD_MSR_DRV2BUSY = 0x04,
    FD_MSR_DRV3BUSY = 0x08,
    FD_MSR_CMDBUSY  = 0x10,
    FD_MSR_NONDMA   = 0x20,
    FD_MSR_DIO      = 0x40,
    FD_MSR_RQM      = 0x80,
};

enum {
    FD_DIR_DSKCHG   = 0x80,
};

#define FD_MULTI_TRACK(state) ((state) & FD_STATE_MULTI)
#define FD_DID_SEEK(state) ((state) & FD_STATE_SEEK)
#define FD_FORMAT_CMD(state) ((state) & FD_STATE_FORMAT)

struct fdctrl_t {
    /* Controller's identification */
    uint8_t version;
    /* HW */
    qemu_irq irq;
    int dma_chann;
    target_phys_addr_t io_base;
    /* Controller state */
    QEMUTimer *result_timer;
    uint8_t sra;
    uint8_t srb;
    uint8_t dor;
    uint8_t tdr;
    uint8_t dsr;
    uint8_t msr;
    uint8_t cur_drv;
    uint8_t status0;
    uint8_t status1;
    uint8_t status2;
    /* Command FIFO */
    uint8_t *fifo;
    uint32_t data_pos;
    uint32_t data_len;
    uint8_t data_state;
    uint8_t data_dir;
    uint8_t eot; /* last wanted sector */
    /* States kept only to be returned back */
    /* Timers state */
    uint8_t timer0;
    uint8_t timer1;
    /* precompensation */
    uint8_t precomp_trk;
    uint8_t config;
    uint8_t lock;
    /* Power down config (also with status regB access mode */
    uint8_t pwrd;
    /* Sun4m quirks? */
    int sun4m;
    /* Floppy drives */
    fdrive_t drives[MAX_FD];
};

static uint32_t fdctrl_read (void *opaque, uint32_t reg)
{
    fdctrl_t *fdctrl = opaque;
    uint32_t retval;

    switch (reg) {
    case FD_REG_SRA:
        retval = fdctrl_read_statusA(fdctrl);
        break;
    case FD_REG_SRB:
        retval = fdctrl_read_statusB(fdctrl);
        break;
    case FD_REG_DOR:
        retval = fdctrl_read_dor(fdctrl);
        break;
    case FD_REG_TDR:
        retval = fdctrl_read_tape(fdctrl);
        break;
    case FD_REG_MSR:
        retval = fdctrl_read_main_status(fdctrl);
        break;
    case FD_REG_FIFO:
        retval = fdctrl_read_data(fdctrl);
        break;
    case FD_REG_DIR:
        retval = fdctrl_read_dir(fdctrl);
        break;
    default:
        retval = (uint32_t)(-1);
        break;
    }
    FLOPPY_DPRINTF("read reg%d: 0x%02x\n", reg & 7, retval);

    return retval;
}

static void fdctrl_write (void *opaque, uint32_t reg, uint32_t value)
{
    fdctrl_t *fdctrl = opaque;

    FLOPPY_DPRINTF("write reg%d: 0x%02x\n", reg & 7, value);

    switch (reg) {
    case FD_REG_DOR:
        fdctrl_write_dor(fdctrl, value);
        break;
    case FD_REG_TDR:
        fdctrl_write_tape(fdctrl, value);
        break;
    case FD_REG_DSR:
        fdctrl_write_rate(fdctrl, value);
        break;
    case FD_REG_FIFO:
        fdctrl_write_data(fdctrl, value);
        break;
    default:
        break;
    }
}

static uint32_t fdctrl_read_port (void *opaque, uint32_t reg)
{
    return fdctrl_read(opaque, reg & 7);
}

static void fdctrl_write_port (void *opaque, uint32_t reg, uint32_t value)
{
    fdctrl_write(opaque, reg & 7, value);
}

static uint32_t fdctrl_read_mem (void *opaque, target_phys_addr_t reg)
{
    return fdctrl_read(opaque, (uint32_t)reg);
}

static void fdctrl_write_mem (void *opaque,
                              target_phys_addr_t reg, uint32_t value)
{
    fdctrl_write(opaque, (uint32_t)reg, value);
}

static CPUReadMemoryFunc *fdctrl_mem_read[3] = {
    fdctrl_read_mem,
    fdctrl_read_mem,
    fdctrl_read_mem,
};

static CPUWriteMemoryFunc *fdctrl_mem_write[3] = {
    fdctrl_write_mem,
    fdctrl_write_mem,
    fdctrl_write_mem,
};

static CPUReadMemoryFunc *fdctrl_mem_read_strict[3] = {
    fdctrl_read_mem,
    NULL,
    NULL,
};

static CPUWriteMemoryFunc *fdctrl_mem_write_strict[3] = {
    fdctrl_write_mem,
    NULL,
    NULL,
};

static void fd_save (QEMUFile *f, fdrive_t *fd)
{
    qemu_put_8s(f, &fd->head);
    qemu_put_8s(f, &fd->track);
    qemu_put_8s(f, &fd->sect);
}

static void fdc_save (QEMUFile *f, void *opaque)
{
    fdctrl_t *s = opaque;
    uint8_t tmp;
    int i;
    uint8_t dor = s->dor | GET_CUR_DRV(s);

    /* Controller state */
    qemu_put_8s(f, &s->sra);
    qemu_put_8s(f, &s->srb);
    qemu_put_8s(f, &dor);
    qemu_put_8s(f, &s->tdr);
    qemu_put_8s(f, &s->dsr);
    qemu_put_8s(f, &s->msr);
    qemu_put_8s(f, &s->status0);
    qemu_put_8s(f, &s->status1);
    qemu_put_8s(f, &s->status2);
    /* Command FIFO */
    qemu_put_buffer(f, s->fifo, FD_SECTOR_LEN);
    qemu_put_be32s(f, &s->data_pos);
    qemu_put_be32s(f, &s->data_len);
    qemu_put_8s(f, &s->data_state);
    qemu_put_8s(f, &s->data_dir);
    qemu_put_8s(f, &s->eot);
    /* States kept only to be returned back */
    qemu_put_8s(f, &s->timer0);
    qemu_put_8s(f, &s->timer1);
    qemu_put_8s(f, &s->precomp_trk);
    qemu_put_8s(f, &s->config);
    qemu_put_8s(f, &s->lock);
    qemu_put_8s(f, &s->pwrd);

    tmp = MAX_FD;
    qemu_put_8s(f, &tmp);
    for (i = 0; i < MAX_FD; i++)
        fd_save(f, &s->drives[i]);
}

static int fd_load (QEMUFile *f, fdrive_t *fd)
{
    qemu_get_8s(f, &fd->head);
    qemu_get_8s(f, &fd->track);
    qemu_get_8s(f, &fd->sect);

    return 0;
}

static int fdc_load (QEMUFile *f, void *opaque, int version_id)
{
    fdctrl_t *s = opaque;
    int i, ret = 0;
    uint8_t n;

    if (version_id != 2)
        return -EINVAL;

    /* Controller state */
    qemu_get_8s(f, &s->sra);
    qemu_get_8s(f, &s->srb);
    qemu_get_8s(f, &s->dor);
    SET_CUR_DRV(s, s->dor & FD_DOR_SELMASK);
    s->dor &= ~FD_DOR_SELMASK;
    qemu_get_8s(f, &s->tdr);
    qemu_get_8s(f, &s->dsr);
    qemu_get_8s(f, &s->msr);
    qemu_get_8s(f, &s->status0);
    qemu_get_8s(f, &s->status1);
    qemu_get_8s(f, &s->status2);
    /* Command FIFO */
    qemu_get_buffer(f, s->fifo, FD_SECTOR_LEN);
    qemu_get_be32s(f, &s->data_pos);
    qemu_get_be32s(f, &s->data_len);
    qemu_get_8s(f, &s->data_state);
    qemu_get_8s(f, &s->data_dir);
    qemu_get_8s(f, &s->eot);
    /* States kept only to be returned back */
    qemu_get_8s(f, &s->timer0);
    qemu_get_8s(f, &s->timer1);
    qemu_get_8s(f, &s->precomp_trk);
    qemu_get_8s(f, &s->config);
    qemu_get_8s(f, &s->lock);
    qemu_get_8s(f, &s->pwrd);
    qemu_get_8s(f, &n);

    if (n > MAX_FD)
        return -EINVAL;

    for (i = 0; i < n; i++) {
        ret = fd_load(f, &s->drives[i]);
        if (ret != 0)
            break;
    }

    return ret;
}

static void fdctrl_external_reset(void *opaque)
{
    fdctrl_t *s = opaque;

    fdctrl_reset(s, 0);
}

static void fdctrl_handle_tc(void *opaque, int irq, int level)
{
    //fdctrl_t *s = opaque;

    if (level) {
        // XXX
        FLOPPY_DPRINTF("TC pulsed\n");
    }
}

/* XXX: may change if moved to bdrv */
int fdctrl_get_drive_type(fdctrl_t *fdctrl, int drive_num)
{
    return fdctrl->drives[drive_num].drive;
}

/* Change IRQ state */
static void fdctrl_reset_irq (fdctrl_t *fdctrl)
{
    if (!(fdctrl->sra & FD_SRA_INTPEND))
        return;
    FLOPPY_DPRINTF("Reset interrupt\n");
    qemu_set_irq(fdctrl->irq, 0);
    fdctrl->sra &= ~FD_SRA_INTPEND;
}

static void fdctrl_raise_irq (fdctrl_t *fdctrl, uint8_t status0)
{
    /* Sparc mutation */
    if (fdctrl->sun4m && (fdctrl->msr & FD_MSR_CMDBUSY)) {
        /* XXX: not sure */
        fdctrl->msr &= ~FD_MSR_CMDBUSY;
        fdctrl->msr |= FD_MSR_RQM | FD_MSR_DIO;
        fdctrl->status0 = status0;
        return;
    }
    if (!(fdctrl->sra & FD_SRA_INTPEND)) {
        qemu_set_irq(fdctrl->irq, 1);
        fdctrl->sra |= FD_SRA_INTPEND;
    }
    fdctrl->status0 = status0;
    FLOPPY_DPRINTF("Set interrupt status to 0x%02x\n", fdctrl->status0);
}

/* Reset controller */
static void fdctrl_reset (fdctrl_t *fdctrl, int do_irq)
{
    int i;

    FLOPPY_DPRINTF("reset controller\n");
    fdctrl_reset_irq(fdctrl);
    /* Initialise controller */
    fdctrl->sra = 0;
    fdctrl->srb = 0xc0;
    if (!fdctrl->drives[1].bs)
        fdctrl->sra |= FD_SRA_nDRV2;
    fdctrl->cur_drv = 0;
    fdctrl->dor = FD_DOR_nRESET;
    fdctrl->dor |= (fdctrl->dma_chann != -1) ? FD_DOR_DMAEN : 0;
    fdctrl->msr = FD_MSR_RQM;
    /* FIFO state */
    fdctrl->data_pos = 0;
    fdctrl->data_len = 0;
    fdctrl->data_state = 0;
    fdctrl->data_dir = FD_DIR_WRITE;
    for (i = 0; i < MAX_FD; i++)
        fd_recalibrate(&fdctrl->drives[i]);
    fdctrl_reset_fifo(fdctrl);
    if (do_irq) {
        fdctrl_raise_irq(fdctrl, FD_SR0_RDYCHG);
    }
}

static inline fdrive_t *drv0 (fdctrl_t *fdctrl)
{
    return &fdctrl->drives[(fdctrl->tdr & FD_TDR_BOOTSEL) >> 2];
}

static inline fdrive_t *drv1 (fdctrl_t *fdctrl)
{
    if ((fdctrl->tdr & FD_TDR_BOOTSEL) < (1 << 2))
        return &fdctrl->drives[1];
    else
        return &fdctrl->drives[0];
}

#if MAX_FD == 4
static inline fdrive_t *drv2 (fdctrl_t *fdctrl)
{
    if ((fdctrl->tdr & FD_TDR_BOOTSEL) < (2 << 2))
        return &fdctrl->drives[2];
    else
        return &fdctrl->drives[1];
}

static inline fdrive_t *drv3 (fdctrl_t *fdctrl)
{
    if ((fdctrl->tdr & FD_TDR_BOOTSEL) < (3 << 2))
        return &fdctrl->drives[3];
    else
        return &fdctrl->drives[2];
}
#endif

static fdrive_t *get_cur_drv (fdctrl_t *fdctrl)
{
    switch (fdctrl->cur_drv) {
        case 0: return drv0(fdctrl);
        case 1: return drv1(fdctrl);
#if MAX_FD == 4
        case 2: return drv2(fdctrl);
        case 3: return drv3(fdctrl);
#endif
        default: return NULL;
    }
}

/* Status A register : 0x00 (read-only) */
static uint32_t fdctrl_read_statusA (fdctrl_t *fdctrl)
{
    uint32_t retval = fdctrl->sra;

    FLOPPY_DPRINTF("status register A: 0x%02x\n", retval);

    return retval;
}

/* Status B register : 0x01 (read-only) */
static uint32_t fdctrl_read_statusB (fdctrl_t *fdctrl)
{
    uint32_t retval = fdctrl->srb;

    FLOPPY_DPRINTF("status register B: 0x%02x\n", retval);

    return retval;
}

/* Digital output register : 0x02 */
static uint32_t fdctrl_read_dor (fdctrl_t *fdctrl)
{
    uint32_t retval = fdctrl->dor;

    /* Selected drive */
    retval |= fdctrl->cur_drv;
    FLOPPY_DPRINTF("digital output register: 0x%02x\n", retval);

    return retval;
}

static void fdctrl_write_dor (fdctrl_t *fdctrl, uint32_t value)
{
    FLOPPY_DPRINTF("digital output register set to 0x%02x\n", value);

    /* Motors */
    if (value & FD_DOR_MOTEN0)
        fdctrl->srb |= FD_SRB_MTR0;
    else
        fdctrl->srb &= ~FD_SRB_MTR0;
    if (value & FD_DOR_MOTEN1)
        fdctrl->srb |= FD_SRB_MTR1;
    else
        fdctrl->srb &= ~FD_SRB_MTR1;

    /* Drive */
    if (value & 1)
        fdctrl->srb |= FD_SRB_DR0;
    else
        fdctrl->srb &= ~FD_SRB_DR0;

    /* Reset */
    if (!(value & FD_DOR_nRESET)) {
        if (fdctrl->dor & FD_DOR_nRESET) {
            FLOPPY_DPRINTF("controller enter RESET state\n");
        }
    } else {
        if (!(fdctrl->dor & FD_DOR_nRESET)) {
            FLOPPY_DPRINTF("controller out of RESET state\n");
            fdctrl_reset(fdctrl, 1);
            fdctrl->dsr &= ~FD_DSR_PWRDOWN;
        }
    }
    /* Selected drive */
    fdctrl->cur_drv = value & FD_DOR_SELMASK;

    fdctrl->dor = value;
}

/* Tape drive register : 0x03 */
static uint32_t fdctrl_read_tape (fdctrl_t *fdctrl)
{
    uint32_t retval = fdctrl->tdr;

    FLOPPY_DPRINTF("tape drive register: 0x%02x\n", retval);

    return retval;
}

static void fdctrl_write_tape (fdctrl_t *fdctrl, uint32_t value)
{
    /* Reset mode */
    if (!(fdctrl->dor & FD_DOR_nRESET)) {
        FLOPPY_DPRINTF("Floppy controller in RESET state !\n");
        return;
    }
    FLOPPY_DPRINTF("tape drive register set to 0x%02x\n", value);
    /* Disk boot selection indicator */
    fdctrl->tdr = value & FD_TDR_BOOTSEL;
    /* Tape indicators: never allow */
}

/* Main status register : 0x04 (read) */
static uint32_t fdctrl_read_main_status (fdctrl_t *fdctrl)
{
    uint32_t retval = fdctrl->msr;

    fdctrl->dsr &= ~FD_DSR_PWRDOWN;
    fdctrl->dor |= FD_DOR_nRESET;

    FLOPPY_DPRINTF("main status register: 0x%02x\n", retval);

    return retval;
}

/* Data select rate register : 0x04 (write) */
static void fdctrl_write_rate (fdctrl_t *fdctrl, uint32_t value)
{
    /* Reset mode */
    if (!(fdctrl->dor & FD_DOR_nRESET)) {
        FLOPPY_DPRINTF("Floppy controller in RESET state !\n");
        return;
    }
    FLOPPY_DPRINTF("select rate register set to 0x%02x\n", value);
    /* Reset: autoclear */
    if (value & FD_DSR_SWRESET) {
        fdctrl->dor &= ~FD_DOR_nRESET;
        fdctrl_reset(fdctrl, 1);
        fdctrl->dor |= FD_DOR_nRESET;
    }
    if (value & FD_DSR_PWRDOWN) {
        fdctrl_reset(fdctrl, 1);
    }
    fdctrl->dsr = value;
}

static int fdctrl_media_changed(fdrive_t *drv)
{
    int ret;

    if (!drv->bs)
        return 0;
    ret = bdrv_media_changed(drv->bs);
    if (ret) {
        fd_revalidate(drv);
    }
    return ret;
}

/* Digital input register : 0x07 (read-only) */
static uint32_t fdctrl_read_dir (fdctrl_t *fdctrl)
{
    uint32_t retval = 0;

    if (fdctrl_media_changed(drv0(fdctrl))
     || fdctrl_media_changed(drv1(fdctrl))
#if MAX_FD == 4
     || fdctrl_media_changed(drv2(fdctrl))
     || fdctrl_media_changed(drv3(fdctrl))
#endif
        )
        retval |= FD_DIR_DSKCHG;
    if (retval != 0)
        FLOPPY_DPRINTF("Floppy digital input register: 0x%02x\n", retval);

    return retval;
}

/* FIFO state control */
static void fdctrl_reset_fifo (fdctrl_t *fdctrl)
{
    fdctrl->data_dir = FD_DIR_WRITE;
    fdctrl->data_pos = 0;
    fdctrl->msr &= ~(FD_MSR_CMDBUSY | FD_MSR_DIO);
}

/* Set FIFO status for the host to read */
static void fdctrl_set_fifo (fdctrl_t *fdctrl, int fifo_len, int do_irq)
{
    fdctrl->data_dir = FD_DIR_READ;
    fdctrl->data_len = fifo_len;
    fdctrl->data_pos = 0;
    fdctrl->msr |= FD_MSR_CMDBUSY | FD_MSR_RQM | FD_MSR_DIO;
    if (do_irq)
        fdctrl_raise_irq(fdctrl, 0x00);
}

/* Set an error: unimplemented/unknown command */
static void fdctrl_unimplemented (fdctrl_t *fdctrl, int direction)
{
    FLOPPY_ERROR("unimplemented command 0x%02x\n", fdctrl->fifo[0]);
    fdctrl->fifo[0] = FD_SR0_INVCMD;
    fdctrl_set_fifo(fdctrl, 1, 0);
}

/* Seek to next sector */
static int fdctrl_seek_to_next_sect (fdctrl_t *fdctrl, fdrive_t *cur_drv)
{
    FLOPPY_DPRINTF("seek to next sector (%d %02x %02x => %d)\n",
                   cur_drv->head, cur_drv->track, cur_drv->sect,
                   fd_sector(cur_drv));
    /* XXX: cur_drv->sect >= cur_drv->last_sect should be an
       error in fact */
    if (cur_drv->sect >= cur_drv->last_sect ||
        cur_drv->sect == fdctrl->eot) {
        cur_drv->sect = 1;
        if (FD_MULTI_TRACK(fdctrl->data_state)) {
            if (cur_drv->head == 0 &&
                (cur_drv->flags & FDISK_DBL_SIDES) != 0) {
                cur_drv->head = 1;
            } else {
                cur_drv->head = 0;
                cur_drv->track++;
                if ((cur_drv->flags & FDISK_DBL_SIDES) == 0)
                    return 0;
            }
        } else {
            cur_drv->track++;
            return 0;
        }
        FLOPPY_DPRINTF("seek to next track (%d %02x %02x => %d)\n",
                       cur_drv->head, cur_drv->track,
                       cur_drv->sect, fd_sector(cur_drv));
    } else {
        cur_drv->sect++;
    }
    return 1;
}

/* Callback for transfer end (stop or abort) */
static void fdctrl_stop_transfer (fdctrl_t *fdctrl, uint8_t status0,
                                  uint8_t status1, uint8_t status2)
{
    fdrive_t *cur_drv;

    cur_drv = get_cur_drv(fdctrl);
    FLOPPY_DPRINTF("transfer status: %02x %02x %02x (%02x)\n",
                   status0, status1, status2,
                   status0 | (cur_drv->head << 2) | GET_CUR_DRV(fdctrl));
    fdctrl->fifo[0] = status0 | (cur_drv->head << 2) | GET_CUR_DRV(fdctrl);
    fdctrl->fifo[1] = status1;
    fdctrl->fifo[2] = status2;
    fdctrl->fifo[3] = cur_drv->track;
    fdctrl->fifo[4] = cur_drv->head;
    fdctrl->fifo[5] = cur_drv->sect;
    fdctrl->fifo[6] = FD_SECTOR_SC;
    fdctrl->data_dir = FD_DIR_READ;
    if (!(fdctrl->msr & FD_MSR_NONDMA)) {
        DMA_release_DREQ(fdctrl->dma_chann);
    }
    fdctrl->msr |= FD_MSR_RQM | FD_MSR_DIO;
    fdctrl->msr &= ~FD_MSR_NONDMA;
    fdctrl_set_fifo(fdctrl, 7, 1);
}

/* Prepare a data transfer (either DMA or FIFO) */
static void fdctrl_start_transfer (fdctrl_t *fdctrl, int direction)
{
    fdrive_t *cur_drv;
    uint8_t kh, kt, ks;
    int did_seek = 0;

    SET_CUR_DRV(fdctrl, fdctrl->fifo[1] & FD_DOR_SELMASK);
    cur_drv = get_cur_drv(fdctrl);
    kt = fdctrl->fifo[2];
    kh = fdctrl->fifo[3];
    ks = fdctrl->fifo[4];
    FLOPPY_DPRINTF("Start transfer at %d %d %02x %02x (%d)\n",
                   GET_CUR_DRV(fdctrl), kh, kt, ks,
                   _fd_sector(kh, kt, ks, cur_drv->last_sect));
    switch (fd_seek(cur_drv, kh, kt, ks, fdctrl->config & FD_CONFIG_EIS)) {
    case 2:
        /* sect too big */
        fdctrl_stop_transfer(fdctrl, FD_SR0_ABNTERM, 0x00, 0x00);
        fdctrl->fifo[3] = kt;
        fdctrl->fifo[4] = kh;
        fdctrl->fifo[5] = ks;
        return;
    case 3:
        /* track too big */
        fdctrl_stop_transfer(fdctrl, FD_SR0_ABNTERM, FD_SR1_EC, 0x00);
        fdctrl->fifo[3] = kt;
        fdctrl->fifo[4] = kh;
        fdctrl->fifo[5] = ks;
        return;
    case 4:
        /* No seek enabled */
        fdctrl_stop_transfer(fdctrl, FD_SR0_ABNTERM, 0x00, 0x00);
        fdctrl->fifo[3] = kt;
        fdctrl->fifo[4] = kh;
        fdctrl->fifo[5] = ks;
        return;
    case 1:
        did_seek = 1;
        break;
    default:
        break;
    }

    /* Set the FIFO state */
    fdctrl->data_dir = direction;
    fdctrl->data_pos = 0;
    fdctrl->msr |= FD_MSR_CMDBUSY;
    if (fdctrl->fifo[0] & 0x80)
        fdctrl->data_state |= FD_STATE_MULTI;
    else
        fdctrl->data_state &= ~FD_STATE_MULTI;
    if (did_seek)
        fdctrl->data_state |= FD_STATE_SEEK;
    else
        fdctrl->data_state &= ~FD_STATE_SEEK;
    if (fdctrl->fifo[5] == 00) {
        fdctrl->data_len = fdctrl->fifo[8];
    } else {
        int tmp;
        fdctrl->data_len = 128 << (fdctrl->fifo[5] > 7 ? 7 : fdctrl->fifo[5]);
        tmp = (fdctrl->fifo[6] - ks + 1);
        if (fdctrl->fifo[0] & 0x80)
            tmp += fdctrl->fifo[6];
        fdctrl->data_len *= tmp;
    }
    fdctrl->eot = fdctrl->fifo[6];
    if (fdctrl->dor & FD_DOR_DMAEN) {
        int dma_mode;
        /* DMA transfer are enabled. Check if DMA channel is well programmed */
        dma_mode = DMA_get_channel_mode(fdctrl->dma_chann);
        dma_mode = (dma_mode >> 2) & 3;
        FLOPPY_DPRINTF("dma_mode=%d direction=%d (%d - %d)\n",
                       dma_mode, direction,
                       (128 << fdctrl->fifo[5]) *
                       (cur_drv->last_sect - ks + 1), fdctrl->data_len);
        if (((direction == FD_DIR_SCANE || direction == FD_DIR_SCANL ||
              direction == FD_DIR_SCANH) && dma_mode == 0) ||
            (direction == FD_DIR_WRITE && dma_mode == 2) ||
            (direction == FD_DIR_READ && dma_mode == 1)) {
            /* No access is allowed until DMA transfer has completed */
            fdctrl->msr &= ~FD_MSR_RQM;
            /* Now, we just have to wait for the DMA controller to
             * recall us...
             */
            DMA_hold_DREQ(fdctrl->dma_chann);
            DMA_schedule(fdctrl->dma_chann);
            return;
        } else {
            FLOPPY_ERROR("dma_mode=%d direction=%d\n", dma_mode, direction);
        }
    }
    FLOPPY_DPRINTF("start non-DMA transfer\n");
    fdctrl->msr |= FD_MSR_NONDMA;
    if (direction != FD_DIR_WRITE)
        fdctrl->msr |= FD_MSR_DIO;
    /* IO based transfer: calculate len */
    fdctrl_raise_irq(fdctrl, 0x00);

    return;
}

/* Prepare a transfer of deleted data */
static void fdctrl_start_transfer_del (fdctrl_t *fdctrl, int direction)
{
    FLOPPY_ERROR("fdctrl_start_transfer_del() unimplemented\n");

    /* We don't handle deleted data,
     * so we don't return *ANYTHING*
     */
    fdctrl_stop_transfer(fdctrl, FD_SR0_ABNTERM | FD_SR0_SEEK, 0x00, 0x00);
}

/* handlers for DMA transfers */
static int fdctrl_transfer_handler (void *opaque, int nchan,
                                    int dma_pos, int dma_len)
{
    fdctrl_t *fdctrl;
    fdrive_t *cur_drv;
    int len, start_pos, rel_pos;
    uint8_t status0 = 0x00, status1 = 0x00, status2 = 0x00;

    fdctrl = opaque;
    if (fdctrl->msr & FD_MSR_RQM) {
        FLOPPY_DPRINTF("Not in DMA transfer mode !\n");
        return 0;
    }
    cur_drv = get_cur_drv(fdctrl);
    if (fdctrl->data_dir == FD_DIR_SCANE || fdctrl->data_dir == FD_DIR_SCANL ||
        fdctrl->data_dir == FD_DIR_SCANH)
        status2 = FD_SR2_SNS;
    if (dma_len > fdctrl->data_len)
        dma_len = fdctrl->data_len;
    if (cur_drv->bs == NULL) {
        if (fdctrl->data_dir == FD_DIR_WRITE)
            fdctrl_stop_transfer(fdctrl, FD_SR0_ABNTERM | FD_SR0_SEEK, 0x00, 0x00);
        else
            fdctrl_stop_transfer(fdctrl, FD_SR0_ABNTERM, 0x00, 0x00);
        len = 0;
        goto transfer_error;
    }
    rel_pos = fdctrl->data_pos % FD_SECTOR_LEN;
    for (start_pos = fdctrl->data_pos; fdctrl->data_pos < dma_len;) {
        len = dma_len - fdctrl->data_pos;
        if (len + rel_pos > FD_SECTOR_LEN)
            len = FD_SECTOR_LEN - rel_pos;
        FLOPPY_DPRINTF("copy %d bytes (%d %d %d) %d pos %d %02x "
                       "(%d-0x%08x 0x%08x)\n", len, dma_len, fdctrl->data_pos,
                       fdctrl->data_len, GET_CUR_DRV(fdctrl), cur_drv->head,
                       cur_drv->track, cur_drv->sect, fd_sector(cur_drv),
                       fd_sector(cur_drv) * FD_SECTOR_LEN);
        if (fdctrl->data_dir != FD_DIR_WRITE ||
            len < FD_SECTOR_LEN || rel_pos != 0) {
            /* READ & SCAN commands and realign to a sector for WRITE */
            if (bdrv_read(cur_drv->bs, fd_sector(cur_drv),
                          fdctrl->fifo, 1) < 0) {
                FLOPPY_DPRINTF("Floppy: error getting sector %d\n",
                               fd_sector(cur_drv));
                /* Sure, image size is too small... */
                memset(fdctrl->fifo, 0, FD_SECTOR_LEN);
            }
        }
        switch (fdctrl->data_dir) {
        case FD_DIR_READ:
            /* READ commands */
            DMA_write_memory (nchan, fdctrl->fifo + rel_pos,
                              fdctrl->data_pos, len);
            break;
        case FD_DIR_WRITE:
            /* WRITE commands */
            DMA_read_memory (nchan, fdctrl->fifo + rel_pos,
                             fdctrl->data_pos, len);
            if (bdrv_write(cur_drv->bs, fd_sector(cur_drv),
                           fdctrl->fifo, 1) < 0) {
                FLOPPY_ERROR("writing sector %d\n", fd_sector(cur_drv));
                fdctrl_stop_transfer(fdctrl, FD_SR0_ABNTERM | FD_SR0_SEEK, 0x00, 0x00);
                goto transfer_error;
            }
            break;
        default:
            /* SCAN commands */
            {
                uint8_t tmpbuf[FD_SECTOR_LEN];
                int ret;
                DMA_read_memory (nchan, tmpbuf, fdctrl->data_pos, len);
                ret = memcmp(tmpbuf, fdctrl->fifo + rel_pos, len);
                if (ret == 0) {
                    status2 = FD_SR2_SEH;
                    goto end_transfer;
                }
                if ((ret < 0 && fdctrl->data_dir == FD_DIR_SCANL) ||
                    (ret > 0 && fdctrl->data_dir == FD_DIR_SCANH)) {
                    status2 = 0x00;
                    goto end_transfer;
                }
            }
            break;
        }
        fdctrl->data_pos += len;
        rel_pos = fdctrl->data_pos % FD_SECTOR_LEN;
        if (rel_pos == 0) {
            /* Seek to next sector */
            if (!fdctrl_seek_to_next_sect(fdctrl, cur_drv))
                break;
        }
    }
 end_transfer:
    len = fdctrl->data_pos - start_pos;
    FLOPPY_DPRINTF("end transfer %d %d %d\n",
                   fdctrl->data_pos, len, fdctrl->data_len);
    if (fdctrl->data_dir == FD_DIR_SCANE ||
        fdctrl->data_dir == FD_DIR_SCANL ||
        fdctrl->data_dir == FD_DIR_SCANH)
        status2 = FD_SR2_SEH;
    if (FD_DID_SEEK(fdctrl->data_state))
        status0 |= FD_SR0_SEEK;
    fdctrl->data_len -= len;
    fdctrl_stop_transfer(fdctrl, status0, status1, status2);
 transfer_error:

    return len;
}

/* Data register : 0x05 */
static uint32_t fdctrl_read_data (fdctrl_t *fdctrl)
{
    fdrive_t *cur_drv;
    uint32_t retval = 0;
    int pos;

    cur_drv = get_cur_drv(fdctrl);
    fdctrl->dsr &= ~FD_DSR_PWRDOWN;
    if (!(fdctrl->msr & FD_MSR_RQM) || !(fdctrl->msr & FD_MSR_DIO)) {
        FLOPPY_ERROR("controller not ready for reading\n");
        return 0;
    }
    pos = fdctrl->data_pos;
    if (fdctrl->msr & FD_MSR_NONDMA) {
        pos %= FD_SECTOR_LEN;
        if (pos == 0) {
            if (fdctrl->data_pos != 0)
                if (!fdctrl_seek_to_next_sect(fdctrl, cur_drv)) {
                    FLOPPY_DPRINTF("error seeking to next sector %d\n",
                                   fd_sector(cur_drv));
                    return 0;
                }
            if (bdrv_read(cur_drv->bs, fd_sector(cur_drv), fdctrl->fifo, 1) < 0) {
                FLOPPY_DPRINTF("error getting sector %d\n",
                               fd_sector(cur_drv));
                /* Sure, image size is too small... */
                memset(fdctrl->fifo, 0, FD_SECTOR_LEN);
            }
        }
    }
    retval = fdctrl->fifo[pos];
    if (++fdctrl->data_pos == fdctrl->data_len) {
        fdctrl->data_pos = 0;
        /* Switch from transfer mode to status mode
         * then from status mode to command mode
         */
        if (fdctrl->msr & FD_MSR_NONDMA) {
            fdctrl_stop_transfer(fdctrl, FD_SR0_SEEK, 0x00, 0x00);
        } else {
            fdctrl_reset_fifo(fdctrl);
            fdctrl_reset_irq(fdctrl);
        }
    }
    FLOPPY_DPRINTF("data register: 0x%02x\n", retval);

    return retval;
}

static void fdctrl_format_sector (fdctrl_t *fdctrl)
{
    fdrive_t *cur_drv;
    uint8_t kh, kt, ks;

    SET_CUR_DRV(fdctrl, fdctrl->fifo[1] & FD_DOR_SELMASK);
    cur_drv = get_cur_drv(fdctrl);
    kt = fdctrl->fifo[6];
    kh = fdctrl->fifo[7];
    ks = fdctrl->fifo[8];
    FLOPPY_DPRINTF("format sector at %d %d %02x %02x (%d)\n",
                   GET_CUR_DRV(fdctrl), kh, kt, ks,
                   _fd_sector(kh, kt, ks, cur_drv->last_sect));
    switch (fd_seek(cur_drv, kh, kt, ks, fdctrl->config & FD_CONFIG_EIS)) {
    case 2:
        /* sect too big */
        fdctrl_stop_transfer(fdctrl, FD_SR0_ABNTERM, 0x00, 0x00);
        fdctrl->fifo[3] = kt;
        fdctrl->fifo[4] = kh;
        fdctrl->fifo[5] = ks;
        return;
    case 3:
        /* track too big */
        fdctrl_stop_transfer(fdctrl, FD_SR0_ABNTERM, FD_SR1_EC, 0x00);
        fdctrl->fifo[3] = kt;
        fdctrl->fifo[4] = kh;
        fdctrl->fifo[5] = ks;
        return;
    case 4:
        /* No seek enabled */
        fdctrl_stop_transfer(fdctrl, FD_SR0_ABNTERM, 0x00, 0x00);
        fdctrl->fifo[3] = kt;
        fdctrl->fifo[4] = kh;
        fdctrl->fifo[5] = ks;
        return;
    case 1:
        fdctrl->data_state |= FD_STATE_SEEK;
        break;
    default:
        break;
    }
    memset(fdctrl->fifo, 0, FD_SECTOR_LEN);
    if (cur_drv->bs == NULL ||
        bdrv_write(cur_drv->bs, fd_sector(cur_drv), fdctrl->fifo, 1) < 0) {
        FLOPPY_ERROR("formatting sector %d\n", fd_sector(cur_drv));
        fdctrl_stop_transfer(fdctrl, FD_SR0_ABNTERM | FD_SR0_SEEK, 0x00, 0x00);
    } else {
        if (cur_drv->sect == cur_drv->last_sect) {
            fdctrl->data_state &= ~FD_STATE_FORMAT;
            /* Last sector done */
            if (FD_DID_SEEK(fdctrl->data_state))
                fdctrl_stop_transfer(fdctrl, FD_SR0_SEEK, 0x00, 0x00);
            else
                fdctrl_stop_transfer(fdctrl, 0x00, 0x00, 0x00);
        } else {
            /* More to do */
            fdctrl->data_pos = 0;
            fdctrl->data_len = 4;
        }
    }
}

static void fdctrl_handle_lock (fdctrl_t *fdctrl, int direction)
{
    fdctrl->lock = (fdctrl->fifo[0] & 0x80) ? 1 : 0;
    fdctrl->fifo[0] = fdctrl->lock << 4;
    fdctrl_set_fifo(fdctrl, 1, fdctrl->lock);
}

static void fdctrl_handle_dumpreg (fdctrl_t *fdctrl, int direction)
{
    fdrive_t *cur_drv = get_cur_drv(fdctrl);

    /* Drives position */
    fdctrl->fifo[0] = drv0(fdctrl)->track;
    fdctrl->fifo[1] = drv1(fdctrl)->track;
#if MAX_FD == 4
    fdctrl->fifo[2] = drv2(fdctrl)->track;
    fdctrl->fifo[3] = drv3(fdctrl)->track;
#else
    fdctrl->fifo[2] = 0;
    fdctrl->fifo[3] = 0;
#endif
    /* timers */
    fdctrl->fifo[4] = fdctrl->timer0;
    fdctrl->fifo[5] = (fdctrl->timer1 << 1) | (fdctrl->dor & FD_DOR_DMAEN ? 1 : 0);
    fdctrl->fifo[6] = cur_drv->last_sect;
    fdctrl->fifo[7] = (fdctrl->lock << 7) |
        (cur_drv->perpendicular << 2);
    fdctrl->fifo[8] = fdctrl->config;
    fdctrl->fifo[9] = fdctrl->precomp_trk;
    fdctrl_set_fifo(fdctrl, 10, 0);
}

static void fdctrl_handle_version (fdctrl_t *fdctrl, int direction)
{
    /* Controller's version */
    fdctrl->fifo[0] = fdctrl->version;
    fdctrl_set_fifo(fdctrl, 1, 1);
}

static void fdctrl_handle_partid (fdctrl_t *fdctrl, int direction)
{
    fdctrl->fifo[0] = 0x41; /* Stepping 1 */
    fdctrl_set_fifo(fdctrl, 1, 0);
}

static void fdctrl_handle_restore (fdctrl_t *fdctrl, int direction)
{
    fdrive_t *cur_drv = get_cur_drv(fdctrl);

    /* Drives position */
    drv0(fdctrl)->track = fdctrl->fifo[3];
    drv1(fdctrl)->track = fdctrl->fifo[4];
#if MAX_FD == 4
    drv2(fdctrl)->track = fdctrl->fifo[5];
    drv3(fdctrl)->track = fdctrl->fifo[6];
#endif
    /* timers */
    fdctrl->timer0 = fdctrl->fifo[7];
    fdctrl->timer1 = fdctrl->fifo[8];
    cur_drv->last_sect = fdctrl->fifo[9];
    fdctrl->lock = fdctrl->fifo[10] >> 7;
    cur_drv->perpendicular = (fdctrl->fifo[10] >> 2) & 0xF;
    fdctrl->config = fdctrl->fifo[11];
    fdctrl->precomp_trk = fdctrl->fifo[12];
    fdctrl->pwrd = fdctrl->fifo[13];
    fdctrl_reset_fifo(fdctrl);
}

static void fdctrl_handle_save (fdctrl_t *fdctrl, int direction)
{
    fdrive_t *cur_drv = get_cur_drv(fdctrl);

    fdctrl->fifo[0] = 0;
    fdctrl->fifo[1] = 0;
    /* Drives position */
    fdctrl->fifo[2] = drv0(fdctrl)->track;
    fdctrl->fifo[3] = drv1(fdctrl)->track;
#if MAX_FD == 4
    fdctrl->fifo[4] = drv2(fdctrl)->track;
    fdctrl->fifo[5] = drv3(fdctrl)->track;
#else
    fdctrl->fifo[4] = 0;
    fdctrl->fifo[5] = 0;
#endif
    /* timers */
    fdctrl->fifo[6] = fdctrl->timer0;
    fdctrl->fifo[7] = fdctrl->timer1;
    fdctrl->fifo[8] = cur_drv->last_sect;
    fdctrl->fifo[9] = (fdctrl->lock << 7) |
        (cur_drv->perpendicular << 2);
    fdctrl->fifo[10] = fdctrl->config;
    fdctrl->fifo[11] = fdctrl->precomp_trk;
    fdctrl->fifo[12] = fdctrl->pwrd;
    fdctrl->fifo[13] = 0;
    fdctrl->fifo[14] = 0;
    fdctrl_set_fifo(fdctrl, 15, 1);
}

static void fdctrl_handle_readid (fdctrl_t *fdctrl, int direction)
{
    fdrive_t *cur_drv = get_cur_drv(fdctrl);

    /* XXX: should set main status register to busy */
    cur_drv->head = (fdctrl->fifo[1] >> 2) & 1;
    qemu_mod_timer(fdctrl->result_timer,
                   qemu_get_clock(vm_clock) + (ticks_per_sec / 50));
}

static void fdctrl_handle_format_track (fdctrl_t *fdctrl, int direction)
{
    fdrive_t *cur_drv;

    SET_CUR_DRV(fdctrl, fdctrl->fifo[1] & FD_DOR_SELMASK);
    cur_drv = get_cur_drv(fdctrl);
    fdctrl->data_state |= FD_STATE_FORMAT;
    if (fdctrl->fifo[0] & 0x80)
        fdctrl->data_state |= FD_STATE_MULTI;
    else
        fdctrl->data_state &= ~FD_STATE_MULTI;
    fdctrl->data_state &= ~FD_STATE_SEEK;
    cur_drv->bps =
        fdctrl->fifo[2] > 7 ? 16384 : 128 << fdctrl->fifo[2];
#if 0
    cur_drv->last_sect =
        cur_drv->flags & FDISK_DBL_SIDES ? fdctrl->fifo[3] :
        fdctrl->fifo[3] / 2;
#else
    cur_drv->last_sect = fdctrl->fifo[3];
#endif
    /* TODO: implement format using DMA expected by the Bochs BIOS
     * and Linux fdformat (read 3 bytes per sector via DMA and fill
     * the sector with the specified fill byte
     */
    fdctrl->data_state &= ~FD_STATE_FORMAT;
    fdctrl_stop_transfer(fdctrl, 0x00, 0x00, 0x00);
}

static void fdctrl_handle_specify (fdctrl_t *fdctrl, int direction)
{
    fdctrl->timer0 = (fdctrl->fifo[1] >> 4) & 0xF;
    fdctrl->timer1 = fdctrl->fifo[2] >> 1;
    if (fdctrl->fifo[2] & 1)
        fdctrl->dor &= ~FD_DOR_DMAEN;
    else
        fdctrl->dor |= FD_DOR_DMAEN;
    /* No result back */
    fdctrl_reset_fifo(fdctrl);
}

static void fdctrl_handle_sense_drive_status (fdctrl_t *fdctrl, int direction)
{
    fdrive_t *cur_drv;

    SET_CUR_DRV(fdctrl, fdctrl->fifo[1] & FD_DOR_SELMASK);
    cur_drv = get_cur_drv(fdctrl);
    cur_drv->head = (fdctrl->fifo[1] >> 2) & 1;
    /* 1 Byte status back */
    fdctrl->fifo[0] = (cur_drv->ro << 6) |
        (cur_drv->track == 0 ? 0x10 : 0x00) |
        (cur_drv->head << 2) |
        GET_CUR_DRV(fdctrl) |
        0x28;
    fdctrl_set_fifo(fdctrl, 1, 0);
}

static void fdctrl_handle_recalibrate (fdctrl_t *fdctrl, int direction)
{
    fdrive_t *cur_drv;

    SET_CUR_DRV(fdctrl, fdctrl->fifo[1] & FD_DOR_SELMASK);
    cur_drv = get_cur_drv(fdctrl);
    fd_recalibrate(cur_drv);
    fdctrl_reset_fifo(fdctrl);
    /* Raise Interrupt */
    fdctrl_raise_irq(fdctrl, FD_SR0_SEEK);
}

static void fdctrl_handle_sense_interrupt_status (fdctrl_t *fdctrl, int direction)
{
    fdrive_t *cur_drv = get_cur_drv(fdctrl);

#if 0
    fdctrl->fifo[0] =
        fdctrl->status0 | (cur_drv->head << 2) | GET_CUR_DRV(fdctrl);
#else
    /* XXX: status0 handling is broken for read/write
       commands, so we do this hack. It should be suppressed
       ASAP */
    fdctrl->fifo[0] =
        FD_SR0_SEEK | (cur_drv->head << 2) | GET_CUR_DRV(fdctrl);
#endif
    fdctrl->fifo[1] = cur_drv->track;
    fdctrl_set_fifo(fdctrl, 2, 0);
    fdctrl_reset_irq(fdctrl);
    fdctrl->status0 = FD_SR0_RDYCHG;
}

static void fdctrl_handle_seek (fdctrl_t *fdctrl, int direction)
{
    fdrive_t *cur_drv;

    SET_CUR_DRV(fdctrl, fdctrl->fifo[1] & FD_DOR_SELMASK);
    cur_drv = get_cur_drv(fdctrl);
    fdctrl_reset_fifo(fdctrl);
    if (fdctrl->fifo[2] > cur_drv->max_track) {
        fdctrl_raise_irq(fdctrl, FD_SR0_ABNTERM | FD_SR0_SEEK);
    } else {
        cur_drv->track = fdctrl->fifo[2];
        /* Raise Interrupt */
        fdctrl_raise_irq(fdctrl, FD_SR0_SEEK);
    }
}

static void fdctrl_handle_perpendicular_mode (fdctrl_t *fdctrl, int direction)
{
    fdrive_t *cur_drv = get_cur_drv(fdctrl);

    if (fdctrl->fifo[1] & 0x80)
        cur_drv->perpendicular = fdctrl->fifo[1] & 0x7;
    /* No result back */
    fdctrl_reset_fifo(fdctrl);
}

static void fdctrl_handle_configure (fdctrl_t *fdctrl, int direction)
{
    fdctrl->config = fdctrl->fifo[2];
    fdctrl->precomp_trk =  fdctrl->fifo[3];
    /* No result back */
    fdctrl_reset_fifo(fdctrl);
}

static void fdctrl_handle_powerdown_mode (fdctrl_t *fdctrl, int direction)
{
    fdctrl->pwrd = fdctrl->fifo[1];
    fdctrl->fifo[0] = fdctrl->fifo[1];
    fdctrl_set_fifo(fdctrl, 1, 1);
}

static void fdctrl_handle_option (fdctrl_t *fdctrl, int direction)
{
    /* No result back */
    fdctrl_reset_fifo(fdctrl);
}

static void fdctrl_handle_drive_specification_command (fdctrl_t *fdctrl, int direction)
{
    fdrive_t *cur_drv = get_cur_drv(fdctrl);

    if (fdctrl->fifo[fdctrl->data_pos - 1] & 0x80) {
        /* Command parameters done */
        if (fdctrl->fifo[fdctrl->data_pos - 1] & 0x40) {
            fdctrl->fifo[0] = fdctrl->fifo[1];
            fdctrl->fifo[2] = 0;
            fdctrl->fifo[3] = 0;
            fdctrl_set_fifo(fdctrl, 4, 1);
        } else {
            fdctrl_reset_fifo(fdctrl);
        }
    } else if (fdctrl->data_len > 7) {
        /* ERROR */
        fdctrl->fifo[0] = 0x80 |
            (cur_drv->head << 2) | GET_CUR_DRV(fdctrl);
        fdctrl_set_fifo(fdctrl, 1, 1);
    }
}

static void fdctrl_handle_relative_seek_out (fdctrl_t *fdctrl, int direction)
{
    fdrive_t *cur_drv;

    SET_CUR_DRV(fdctrl, fdctrl->fifo[1] & FD_DOR_SELMASK);
    cur_drv = get_cur_drv(fdctrl);
    if (fdctrl->fifo[2] + cur_drv->track >= cur_drv->max_track) {
        cur_drv->track = cur_drv->max_track - 1;
    } else {
        cur_drv->track += fdctrl->fifo[2];
    }
    fdctrl_reset_fifo(fdctrl);
    /* Raise Interrupt */
    fdctrl_raise_irq(fdctrl, FD_SR0_SEEK);
}

static void fdctrl_handle_relative_seek_in (fdctrl_t *fdctrl, int direction)
{
    fdrive_t *cur_drv;

    SET_CUR_DRV(fdctrl, fdctrl->fifo[1] & FD_DOR_SELMASK);
    cur_drv = get_cur_drv(fdctrl);
    if (fdctrl->fifo[2] > cur_drv->track) {
        cur_drv->track = 0;
    } else {
        cur_drv->track -= fdctrl->fifo[2];
    }
    fdctrl_reset_fifo(fdctrl);
    /* Raise Interrupt */
    fdctrl_raise_irq(fdctrl, FD_SR0_SEEK);
}

static const struct {
    uint8_t value;
    uint8_t mask;
    const char* name;
    int parameters;
    void (*handler)(fdctrl_t *fdctrl, int direction);
    int direction;
} handlers[] = {
    { FD_CMD_READ, 0x1f, "READ", 8, fdctrl_start_transfer, FD_DIR_READ },
    { FD_CMD_WRITE, 0x3f, "WRITE", 8, fdctrl_start_transfer, FD_DIR_WRITE },
    { FD_CMD_SEEK, 0xff, "SEEK", 2, fdctrl_handle_seek },
    { FD_CMD_SENSE_INTERRUPT_STATUS, 0xff, "SENSE INTERRUPT STATUS", 0, fdctrl_handle_sense_interrupt_status },
    { FD_CMD_RECALIBRATE, 0xff, "RECALIBRATE", 1, fdctrl_handle_recalibrate },
    { FD_CMD_FORMAT_TRACK, 0xbf, "FORMAT TRACK", 5, fdctrl_handle_format_track },
    { FD_CMD_READ_TRACK, 0xbf, "READ TRACK", 8, fdctrl_start_transfer, FD_DIR_READ },
    { FD_CMD_RESTORE, 0xff, "RESTORE", 17, fdctrl_handle_restore }, /* part of READ DELETED DATA */
    { FD_CMD_SAVE, 0xff, "SAVE", 0, fdctrl_handle_save }, /* part of READ DELETED DATA */
    { FD_CMD_READ_DELETED, 0x1f, "READ DELETED DATA", 8, fdctrl_start_transfer_del, FD_DIR_READ },
    { FD_CMD_SCAN_EQUAL, 0x1f, "SCAN EQUAL", 8, fdctrl_start_transfer, FD_DIR_SCANE },
    { FD_CMD_VERIFY, 0x1f, "VERIFY", 8, fdctrl_unimplemented },
    { FD_CMD_SCAN_LOW_OR_EQUAL, 0x1f, "SCAN LOW OR EQUAL", 8, fdctrl_start_transfer, FD_DIR_SCANL },
    { FD_CMD_SCAN_HIGH_OR_EQUAL, 0x1f, "SCAN HIGH OR EQUAL", 8, fdctrl_start_transfer, FD_DIR_SCANH },
    { FD_CMD_WRITE_DELETED, 0x3f, "WRITE DELETED DATA", 8, fdctrl_start_transfer_del, FD_DIR_WRITE },
    { FD_CMD_READ_ID, 0xbf, "READ ID", 1, fdctrl_handle_readid },
    { FD_CMD_SPECIFY, 0xff, "SPECIFY", 2, fdctrl_handle_specify },
    { FD_CMD_SENSE_DRIVE_STATUS, 0xff, "SENSE DRIVE STATUS", 1, fdctrl_handle_sense_drive_status },
    { FD_CMD_PERPENDICULAR_MODE, 0xff, "PERPENDICULAR MODE", 1, fdctrl_handle_perpendicular_mode },
    { FD_CMD_CONFIGURE, 0xff, "CONFIGURE", 3, fdctrl_handle_configure },
    { FD_CMD_POWERDOWN_MODE, 0xff, "POWERDOWN MODE", 2, fdctrl_handle_powerdown_mode },
    { FD_CMD_OPTION, 0xff, "OPTION", 1, fdctrl_handle_option },
    { FD_CMD_DRIVE_SPECIFICATION_COMMAND, 0xff, "DRIVE SPECIFICATION COMMAND", 5, fdctrl_handle_drive_specification_command },
    { FD_CMD_RELATIVE_SEEK_OUT, 0xff, "RELATIVE SEEK OUT", 2, fdctrl_handle_relative_seek_out },
    { FD_CMD_FORMAT_AND_WRITE, 0xff, "FORMAT AND WRITE", 10, fdctrl_unimplemented },
    { FD_CMD_RELATIVE_SEEK_IN, 0xff, "RELATIVE SEEK IN", 2, fdctrl_handle_relative_seek_in },
    { FD_CMD_LOCK, 0x7f, "LOCK", 0, fdctrl_handle_lock },
    { FD_CMD_DUMPREG, 0xff, "DUMPREG", 0, fdctrl_handle_dumpreg },
    { FD_CMD_VERSION, 0xff, "VERSION", 0, fdctrl_handle_version },
    { FD_CMD_PART_ID, 0xff, "PART ID", 0, fdctrl_handle_partid },
    { FD_CMD_WRITE, 0x1f, "WRITE (BeOS)", 8, fdctrl_start_transfer, FD_DIR_WRITE }, /* not in specification ; BeOS 4.5 bug */
    { 0, 0, "unknown", 0, fdctrl_unimplemented }, /* default handler */
};
/* Associate command to an index in the 'handlers' array */
static uint8_t command_to_handler[256];

static void fdctrl_write_data (fdctrl_t *fdctrl, uint32_t value)
{
    fdrive_t *cur_drv;
    int pos;

    /* Reset mode */
    if (!(fdctrl->dor & FD_DOR_nRESET)) {
        FLOPPY_DPRINTF("Floppy controller in RESET state !\n");
        return;
    }
    if (!(fdctrl->msr & FD_MSR_RQM) || (fdctrl->msr & FD_MSR_DIO)) {
        FLOPPY_ERROR("controller not ready for writing\n");
        return;
    }
    fdctrl->dsr &= ~FD_DSR_PWRDOWN;
    /* Is it write command time ? */
    if (fdctrl->msr & FD_MSR_NONDMA) {
        /* FIFO data write */
        pos = fdctrl->data_pos++;
        pos %= FD_SECTOR_LEN;
        fdctrl->fifo[pos] = value;
        if (pos == FD_SECTOR_LEN - 1 ||
            fdctrl->data_pos == fdctrl->data_len) {
            cur_drv = get_cur_drv(fdctrl);
            if (bdrv_write(cur_drv->bs, fd_sector(cur_drv), fdctrl->fifo, 1) < 0) {
                FLOPPY_ERROR("writing sector %d\n", fd_sector(cur_drv));
                return;
            }
            if (!fdctrl_seek_to_next_sect(fdctrl, cur_drv)) {
                FLOPPY_DPRINTF("error seeking to next sector %d\n",
                               fd_sector(cur_drv));
                return;
            }
        }
        /* Switch from transfer mode to status mode
         * then from status mode to command mode
         */
        if (fdctrl->data_pos == fdctrl->data_len)
            fdctrl_stop_transfer(fdctrl, FD_SR0_SEEK, 0x00, 0x00);
        return;
    }
    if (fdctrl->data_pos == 0) {
        /* Command */
        pos = command_to_handler[value & 0xff];
        FLOPPY_DPRINTF("%s command\n", handlers[pos].name);
        fdctrl->data_len = handlers[pos].parameters + 1;
    }

    FLOPPY_DPRINTF("%s: %02x\n", __func__, value);
    fdctrl->fifo[fdctrl->data_pos++] = value;
    if (fdctrl->data_pos == fdctrl->data_len) {
        /* We now have all parameters
         * and will be able to treat the command
         */
        if (fdctrl->data_state & FD_STATE_FORMAT) {
            fdctrl_format_sector(fdctrl);
            return;
        }

        pos = command_to_handler[fdctrl->fifo[0] & 0xff];
        FLOPPY_DPRINTF("treat %s command\n", handlers[pos].name);
        (*handlers[pos].handler)(fdctrl, handlers[pos].direction);
    }
}

static void fdctrl_result_timer(void *opaque)
{
    fdctrl_t *fdctrl = opaque;
    fdrive_t *cur_drv = get_cur_drv(fdctrl);

    /* Pretend we are spinning.
     * This is needed for Coherent, which uses READ ID to check for
     * sector interleaving.
     */
    if (cur_drv->last_sect != 0) {
        cur_drv->sect = (cur_drv->sect % cur_drv->last_sect) + 1;
    }
    fdctrl_stop_transfer(fdctrl, 0x00, 0x00, 0x00);
}

/* Init functions */
static fdctrl_t *fdctrl_init_common (qemu_irq irq, int dma_chann,
                                     target_phys_addr_t io_base,
                                     BlockDriverState **fds)
{
    fdctrl_t *fdctrl;
    int i, j;

    /* Fill 'command_to_handler' lookup table */
    for (i = sizeof(handlers)/sizeof(handlers[0]) - 1; i >= 0; i--) {
        for (j = 0; j < sizeof(command_to_handler); j++) {
            if ((j & handlers[i].mask) == handlers[i].value)
                command_to_handler[j] = i;
        }
    }

    FLOPPY_DPRINTF("init controller\n");
    fdctrl = qemu_mallocz(sizeof(fdctrl_t));
    if (!fdctrl)
        return NULL;
    fdctrl->fifo = qemu_memalign(512, FD_SECTOR_LEN);
    if (fdctrl->fifo == NULL) {
        qemu_free(fdctrl);
        return NULL;
    }
    fdctrl->result_timer = qemu_new_timer(vm_clock,
                                          fdctrl_result_timer, fdctrl);

    fdctrl->version = 0x90; /* Intel 82078 controller */
    fdctrl->irq = irq;
    fdctrl->dma_chann = dma_chann;
    fdctrl->io_base = io_base;
    fdctrl->config = FD_CONFIG_EIS | FD_CONFIG_EFIFO; /* Implicit seek, polling & FIFO enabled */
    if (fdctrl->dma_chann != -1) {
        DMA_register_channel(dma_chann, &fdctrl_transfer_handler, fdctrl);
    }
    for (i = 0; i < MAX_FD; i++) {
        fd_init(&fdctrl->drives[i], fds[i]);
    }
    fdctrl_external_reset(fdctrl);
    register_savevm("fdc", io_base, 2, fdc_save, fdc_load, fdctrl);
    qemu_register_reset(fdctrl_external_reset, fdctrl);
    for (i = 0; i < MAX_FD; i++) {
        fd_revalidate(&fdctrl->drives[i]);
    }

    return fdctrl;
}

fdctrl_t *fdctrl_init (qemu_irq irq, int dma_chann, int mem_mapped,
                       target_phys_addr_t io_base,
                       BlockDriverState **fds)
{
    fdctrl_t *fdctrl;
    int io_mem;

    fdctrl = fdctrl_init_common(irq, dma_chann, io_base, fds);

    fdctrl->sun4m = 0;
    if (mem_mapped) {
        io_mem = cpu_register_io_memory(0, fdctrl_mem_read, fdctrl_mem_write,
                                        fdctrl);
        cpu_register_physical_memory(io_base, 0x08, io_mem);
    } else {
        register_ioport_read((uint32_t)io_base + 0x01, 5, 1,
                             &fdctrl_read_port, fdctrl);
        register_ioport_read((uint32_t)io_base + 0x07, 1, 1,
                             &fdctrl_read_port, fdctrl);
        register_ioport_write((uint32_t)io_base + 0x01, 5, 1,
                              &fdctrl_write_port, fdctrl);
        register_ioport_write((uint32_t)io_base + 0x07, 1, 1,
                              &fdctrl_write_port, fdctrl);
    }

    return fdctrl;
}

fdctrl_t *sun4m_fdctrl_init (qemu_irq irq, target_phys_addr_t io_base,
                             BlockDriverState **fds, qemu_irq *fdc_tc)
{
    fdctrl_t *fdctrl;
    int io_mem;

    fdctrl = fdctrl_init_common(irq, -1, io_base, fds);
    fdctrl->sun4m = 1;
    io_mem = cpu_register_io_memory(0, fdctrl_mem_read_strict,
                                    fdctrl_mem_write_strict,
                                    fdctrl);
    cpu_register_physical_memory(io_base, 0x08, io_mem);
    *fdc_tc = *qemu_allocate_irqs(fdctrl_handle_tc, fdctrl, 1);

    return fdctrl;
}

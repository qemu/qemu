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

#include "qemu/osdep.h"
#include "hw/block/fdc.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/timer.h"
#include "hw/irq.h"
#include "hw/isa/isa.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "migration/vmstate.h"
#include "hw/block/block.h"
#include "sysemu/block-backend.h"
#include "sysemu/blockdev.h"
#include "sysemu/sysemu.h"
#include "qemu/log.h"
#include "qemu/main-loop.h"
#include "qemu/module.h"
#include "trace.h"
#include "qom/object.h"
#include "fdc-internal.h"

/********************************************************/
/* debug Floppy devices */

#define DEBUG_FLOPPY 0

#define FLOPPY_DPRINTF(fmt, ...)                                \
    do {                                                        \
        if (DEBUG_FLOPPY) {                                     \
            fprintf(stderr, "FLOPPY: " fmt , ## __VA_ARGS__);   \
        }                                                       \
    } while (0)


/********************************************************/
/* qdev floppy bus                                      */

#define TYPE_FLOPPY_BUS "floppy-bus"
OBJECT_DECLARE_SIMPLE_TYPE(FloppyBus, FLOPPY_BUS)

static FDrive *get_drv(FDCtrl *fdctrl, int unit);

static const TypeInfo floppy_bus_info = {
    .name = TYPE_FLOPPY_BUS,
    .parent = TYPE_BUS,
    .instance_size = sizeof(FloppyBus),
};

static void floppy_bus_create(FDCtrl *fdc, FloppyBus *bus, DeviceState *dev)
{
    qbus_init(bus, sizeof(FloppyBus), TYPE_FLOPPY_BUS, dev, NULL);
    bus->fdc = fdc;
}


/********************************************************/
/* Floppy drive emulation                               */

/* In many cases, the total sector size of a format is enough to uniquely
 * identify it. However, there are some total sector collisions between
 * formats of different physical size, and these are noted below by
 * highlighting the total sector size for entries with collisions. */
const FDFormat fd_formats[] = {
    /* First entry is default format */
    /* 1.44 MB 3"1/2 floppy disks */
    { FLOPPY_DRIVE_TYPE_144, 18, 80, 1, FDRIVE_RATE_500K, }, /* 3.5" 2880 */
    { FLOPPY_DRIVE_TYPE_144, 20, 80, 1, FDRIVE_RATE_500K, }, /* 3.5" 3200 */
    { FLOPPY_DRIVE_TYPE_144, 21, 80, 1, FDRIVE_RATE_500K, },
    { FLOPPY_DRIVE_TYPE_144, 21, 82, 1, FDRIVE_RATE_500K, },
    { FLOPPY_DRIVE_TYPE_144, 21, 83, 1, FDRIVE_RATE_500K, },
    { FLOPPY_DRIVE_TYPE_144, 22, 80, 1, FDRIVE_RATE_500K, },
    { FLOPPY_DRIVE_TYPE_144, 23, 80, 1, FDRIVE_RATE_500K, },
    { FLOPPY_DRIVE_TYPE_144, 24, 80, 1, FDRIVE_RATE_500K, },
    /* 2.88 MB 3"1/2 floppy disks */
    { FLOPPY_DRIVE_TYPE_288, 36, 80, 1, FDRIVE_RATE_1M, },
    { FLOPPY_DRIVE_TYPE_288, 39, 80, 1, FDRIVE_RATE_1M, },
    { FLOPPY_DRIVE_TYPE_288, 40, 80, 1, FDRIVE_RATE_1M, },
    { FLOPPY_DRIVE_TYPE_288, 44, 80, 1, FDRIVE_RATE_1M, },
    { FLOPPY_DRIVE_TYPE_288, 48, 80, 1, FDRIVE_RATE_1M, },
    /* 720 kB 3"1/2 floppy disks */
    { FLOPPY_DRIVE_TYPE_144,  9, 80, 1, FDRIVE_RATE_250K, }, /* 3.5" 1440 */
    { FLOPPY_DRIVE_TYPE_144, 10, 80, 1, FDRIVE_RATE_250K, },
    { FLOPPY_DRIVE_TYPE_144, 10, 82, 1, FDRIVE_RATE_250K, },
    { FLOPPY_DRIVE_TYPE_144, 10, 83, 1, FDRIVE_RATE_250K, },
    { FLOPPY_DRIVE_TYPE_144, 13, 80, 1, FDRIVE_RATE_250K, },
    { FLOPPY_DRIVE_TYPE_144, 14, 80, 1, FDRIVE_RATE_250K, },
    /* 1.2 MB 5"1/4 floppy disks */
    { FLOPPY_DRIVE_TYPE_120, 15, 80, 1, FDRIVE_RATE_500K, },
    { FLOPPY_DRIVE_TYPE_120, 18, 80, 1, FDRIVE_RATE_500K, }, /* 5.25" 2880 */
    { FLOPPY_DRIVE_TYPE_120, 18, 82, 1, FDRIVE_RATE_500K, },
    { FLOPPY_DRIVE_TYPE_120, 18, 83, 1, FDRIVE_RATE_500K, },
    { FLOPPY_DRIVE_TYPE_120, 20, 80, 1, FDRIVE_RATE_500K, }, /* 5.25" 3200 */
    /* 720 kB 5"1/4 floppy disks */
    { FLOPPY_DRIVE_TYPE_120,  9, 80, 1, FDRIVE_RATE_250K, }, /* 5.25" 1440 */
    { FLOPPY_DRIVE_TYPE_120, 11, 80, 1, FDRIVE_RATE_250K, },
    /* 360 kB 5"1/4 floppy disks */
    { FLOPPY_DRIVE_TYPE_120,  9, 40, 1, FDRIVE_RATE_300K, }, /* 5.25" 720 */
    { FLOPPY_DRIVE_TYPE_120,  9, 40, 0, FDRIVE_RATE_300K, },
    { FLOPPY_DRIVE_TYPE_120, 10, 41, 1, FDRIVE_RATE_300K, },
    { FLOPPY_DRIVE_TYPE_120, 10, 42, 1, FDRIVE_RATE_300K, },
    /* 320 kB 5"1/4 floppy disks */
    { FLOPPY_DRIVE_TYPE_120,  8, 40, 1, FDRIVE_RATE_250K, },
    { FLOPPY_DRIVE_TYPE_120,  8, 40, 0, FDRIVE_RATE_250K, },
    /* 360 kB must match 5"1/4 better than 3"1/2... */
    { FLOPPY_DRIVE_TYPE_144,  9, 80, 0, FDRIVE_RATE_250K, }, /* 3.5" 720 */
    /* end */
    { FLOPPY_DRIVE_TYPE_NONE, -1, -1, 0, 0, },
};

static FDriveSize drive_size(FloppyDriveType drive)
{
    switch (drive) {
    case FLOPPY_DRIVE_TYPE_120:
        return FDRIVE_SIZE_525;
    case FLOPPY_DRIVE_TYPE_144:
    case FLOPPY_DRIVE_TYPE_288:
        return FDRIVE_SIZE_350;
    default:
        return FDRIVE_SIZE_UNKNOWN;
    }
}

#define GET_CUR_DRV(fdctrl) ((fdctrl)->cur_drv)
#define SET_CUR_DRV(fdctrl, drive) ((fdctrl)->cur_drv = (drive))

/* Will always be a fixed parameter for us */
#define FD_SECTOR_LEN          512
#define FD_SECTOR_SC           2   /* Sector size code */
#define FD_RESET_SENSEI_COUNT  4   /* Number of sense interrupts on RESET */


static FloppyDriveType get_fallback_drive_type(FDrive *drv);

/* Hack: FD_SEEK is expected to work on empty drives. However, QEMU
 * currently goes through some pains to keep seeks within the bounds
 * established by last_sect and max_track. Correcting this is difficult,
 * as refactoring FDC code tends to expose nasty bugs in the Linux kernel.
 *
 * For now: allow empty drives to have large bounds so we can seek around,
 * with the understanding that when a diskette is inserted, the bounds will
 * properly tighten to match the geometry of that inserted medium.
 */
static void fd_empty_seek_hack(FDrive *drv)
{
    drv->last_sect = 0xFF;
    drv->max_track = 0xFF;
}

static void fd_init(FDrive *drv)
{
    /* Drive */
    drv->perpendicular = 0;
    /* Disk */
    drv->disk = FLOPPY_DRIVE_TYPE_NONE;
    drv->last_sect = 0;
    drv->max_track = 0;
    drv->ro = true;
    drv->media_changed = 1;
}

#define NUM_SIDES(drv) ((drv)->flags & FDISK_DBL_SIDES ? 2 : 1)

static int fd_sector_calc(uint8_t head, uint8_t track, uint8_t sect,
                          uint8_t last_sect, uint8_t num_sides)
{
    return (((track * num_sides) + head) * last_sect) + sect - 1;
}

/* Returns current position, in sectors, for given drive */
static int fd_sector(FDrive *drv)
{
    return fd_sector_calc(drv->head, drv->track, drv->sect, drv->last_sect,
                          NUM_SIDES(drv));
}

/* Returns current position, in bytes, for given drive */
static int fd_offset(FDrive *drv)
{
    g_assert(fd_sector(drv) < INT_MAX >> BDRV_SECTOR_BITS);
    return fd_sector(drv) << BDRV_SECTOR_BITS;
}

/* Seek to a new position:
 * returns 0 if already on right track
 * returns 1 if track changed
 * returns 2 if track is invalid
 * returns 3 if sector is invalid
 * returns 4 if seek is disabled
 */
static int fd_seek(FDrive *drv, uint8_t head, uint8_t track, uint8_t sect,
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
    sector = fd_sector_calc(head, track, sect, drv->last_sect, NUM_SIDES(drv));
    ret = 0;
    if (sector != fd_sector(drv)) {
#if 0
        if (!enable_seek) {
            FLOPPY_DPRINTF("error: no implicit seek %d %02x %02x"
                           " (max=%d %02x %02x)\n",
                           head, track, sect, 1, drv->max_track,
                           drv->last_sect);
            return 4;
        }
#endif
        drv->head = head;
        if (drv->track != track) {
            if (drv->blk != NULL && blk_is_inserted(drv->blk)) {
                drv->media_changed = 0;
            }
            ret = 1;
        }
        drv->track = track;
        drv->sect = sect;
    }

    if (drv->blk == NULL || !blk_is_inserted(drv->blk)) {
        ret = 2;
    }

    return ret;
}

/* Set drive back to track 0 */
static void fd_recalibrate(FDrive *drv)
{
    FLOPPY_DPRINTF("recalibrate\n");
    fd_seek(drv, 0, 0, 1, 1);
}

/**
 * Determine geometry based on inserted diskette.
 * Will not operate on an empty drive.
 *
 * @return: 0 on success, -1 if the drive is empty.
 */
static int pick_geometry(FDrive *drv)
{
    BlockBackend *blk = drv->blk;
    const FDFormat *parse;
    uint64_t nb_sectors, size;
    int i;
    int match, size_match, type_match;
    bool magic = drv->drive == FLOPPY_DRIVE_TYPE_AUTO;

    /* We can only pick a geometry if we have a diskette. */
    if (!drv->blk || !blk_is_inserted(drv->blk) ||
        drv->drive == FLOPPY_DRIVE_TYPE_NONE)
    {
        return -1;
    }

    /* We need to determine the likely geometry of the inserted medium.
     * In order of preference, we look for:
     * (1) The same drive type and number of sectors,
     * (2) The same diskette size and number of sectors,
     * (3) The same drive type.
     *
     * In all cases, matches that occur higher in the drive table will take
     * precedence over matches that occur later in the table.
     */
    blk_get_geometry(blk, &nb_sectors);
    match = size_match = type_match = -1;
    for (i = 0; ; i++) {
        parse = &fd_formats[i];
        if (parse->drive == FLOPPY_DRIVE_TYPE_NONE) {
            break;
        }
        size = (parse->max_head + 1) * parse->max_track * parse->last_sect;
        if (nb_sectors == size) {
            if (magic || parse->drive == drv->drive) {
                /* (1) perfect match -- nb_sectors and drive type */
                goto out;
            } else if (drive_size(parse->drive) == drive_size(drv->drive)) {
                /* (2) size match -- nb_sectors and physical medium size */
                match = (match == -1) ? i : match;
            } else {
                /* This is suspicious -- Did the user misconfigure? */
                size_match = (size_match == -1) ? i : size_match;
            }
        } else if (type_match == -1) {
            if ((parse->drive == drv->drive) ||
                (magic && (parse->drive == get_fallback_drive_type(drv)))) {
                /* (3) type match -- nb_sectors mismatch, but matches the type
                 *     specified explicitly by the user, or matches the fallback
                 *     default type when using the drive autodetect mechanism */
                type_match = i;
            }
        }
    }

    /* No exact match found */
    if (match == -1) {
        if (size_match != -1) {
            parse = &fd_formats[size_match];
            FLOPPY_DPRINTF("User requested floppy drive type '%s', "
                           "but inserted medium appears to be a "
                           "%"PRId64" sector '%s' type\n",
                           FloppyDriveType_str(drv->drive),
                           nb_sectors,
                           FloppyDriveType_str(parse->drive));
        }
        assert(type_match != -1 && "misconfigured fd_format");
        match = type_match;
    }
    parse = &(fd_formats[match]);

 out:
    if (parse->max_head == 0) {
        drv->flags &= ~FDISK_DBL_SIDES;
    } else {
        drv->flags |= FDISK_DBL_SIDES;
    }
    drv->max_track = parse->max_track;
    drv->last_sect = parse->last_sect;
    drv->disk = parse->drive;
    drv->media_rate = parse->rate;
    return 0;
}

static void pick_drive_type(FDrive *drv)
{
    if (drv->drive != FLOPPY_DRIVE_TYPE_AUTO) {
        return;
    }

    if (pick_geometry(drv) == 0) {
        drv->drive = drv->disk;
    } else {
        drv->drive = get_fallback_drive_type(drv);
    }

    g_assert(drv->drive != FLOPPY_DRIVE_TYPE_AUTO);
}

/* Revalidate a disk drive after a disk change */
static void fd_revalidate(FDrive *drv)
{
    int rc;

    FLOPPY_DPRINTF("revalidate\n");
    if (drv->blk != NULL) {
        drv->ro = !blk_is_writable(drv->blk);
        if (!blk_is_inserted(drv->blk)) {
            FLOPPY_DPRINTF("No disk in drive\n");
            drv->disk = FLOPPY_DRIVE_TYPE_NONE;
            fd_empty_seek_hack(drv);
        } else if (!drv->media_validated) {
            rc = pick_geometry(drv);
            if (rc) {
                FLOPPY_DPRINTF("Could not validate floppy drive media");
            } else {
                drv->media_validated = true;
                FLOPPY_DPRINTF("Floppy disk (%d h %d t %d s) %s\n",
                               (drv->flags & FDISK_DBL_SIDES) ? 2 : 1,
                               drv->max_track, drv->last_sect,
                               drv->ro ? "ro" : "rw");
            }
        }
    } else {
        FLOPPY_DPRINTF("No drive connected\n");
        drv->last_sect = 0;
        drv->max_track = 0;
        drv->flags &= ~FDISK_DBL_SIDES;
        drv->drive = FLOPPY_DRIVE_TYPE_NONE;
        drv->disk = FLOPPY_DRIVE_TYPE_NONE;
    }
}

static void fd_change_cb(void *opaque, bool load, Error **errp)
{
    FDrive *drive = opaque;

    if (!load) {
        blk_set_perm(drive->blk, 0, BLK_PERM_ALL, &error_abort);
    } else {
        if (!blkconf_apply_backend_options(drive->conf,
                                           !blk_supports_write_perm(drive->blk),
                                           false, errp)) {
            return;
        }
    }

    drive->media_changed = 1;
    drive->media_validated = false;
    fd_revalidate(drive);
}

static const BlockDevOps fd_block_ops = {
    .change_media_cb = fd_change_cb,
};


#define TYPE_FLOPPY_DRIVE "floppy"
OBJECT_DECLARE_SIMPLE_TYPE(FloppyDrive, FLOPPY_DRIVE)

struct FloppyDrive {
    DeviceState     qdev;
    uint32_t        unit;
    BlockConf       conf;
    FloppyDriveType type;
};

static Property floppy_drive_properties[] = {
    DEFINE_PROP_UINT32("unit", FloppyDrive, unit, -1),
    DEFINE_BLOCK_PROPERTIES(FloppyDrive, conf),
    DEFINE_PROP_SIGNED("drive-type", FloppyDrive, type,
                        FLOPPY_DRIVE_TYPE_AUTO, qdev_prop_fdc_drive_type,
                        FloppyDriveType),
    DEFINE_PROP_END_OF_LIST(),
};

static void floppy_drive_realize(DeviceState *qdev, Error **errp)
{
    FloppyDrive *dev = FLOPPY_DRIVE(qdev);
    FloppyBus *bus = FLOPPY_BUS(qdev->parent_bus);
    FDrive *drive;
    bool read_only;
    int ret;

    if (dev->unit == -1) {
        for (dev->unit = 0; dev->unit < MAX_FD; dev->unit++) {
            drive = get_drv(bus->fdc, dev->unit);
            if (!drive->blk) {
                break;
            }
        }
    }

    if (dev->unit >= MAX_FD) {
        error_setg(errp, "Can't create floppy unit %d, bus supports "
                   "only %d units", dev->unit, MAX_FD);
        return;
    }

    drive = get_drv(bus->fdc, dev->unit);
    if (drive->blk) {
        error_setg(errp, "Floppy unit %d is in use", dev->unit);
        return;
    }

    if (!dev->conf.blk) {
        /* Anonymous BlockBackend for an empty drive */
        dev->conf.blk = blk_new(qemu_get_aio_context(), 0, BLK_PERM_ALL);
        ret = blk_attach_dev(dev->conf.blk, qdev);
        assert(ret == 0);

        /* Don't take write permissions on an empty drive to allow attaching a
         * read-only node later */
        read_only = true;
    } else {
        read_only = !blk_bs(dev->conf.blk) ||
                    !blk_supports_write_perm(dev->conf.blk);
    }

    if (!blkconf_blocksizes(&dev->conf, errp)) {
        return;
    }

    if (dev->conf.logical_block_size != 512 ||
        dev->conf.physical_block_size != 512)
    {
        error_setg(errp, "Physical and logical block size must "
                   "be 512 for floppy");
        return;
    }

    /* rerror/werror aren't supported by fdc and therefore not even registered
     * with qdev. So set the defaults manually before they are used in
     * blkconf_apply_backend_options(). */
    dev->conf.rerror = BLOCKDEV_ON_ERROR_AUTO;
    dev->conf.werror = BLOCKDEV_ON_ERROR_AUTO;

    if (!blkconf_apply_backend_options(&dev->conf, read_only, false, errp)) {
        return;
    }

    /* 'enospc' is the default for -drive, 'report' is what blk_new() gives us
     * for empty drives. */
    if (blk_get_on_error(dev->conf.blk, 0) != BLOCKDEV_ON_ERROR_ENOSPC &&
        blk_get_on_error(dev->conf.blk, 0) != BLOCKDEV_ON_ERROR_REPORT) {
        error_setg(errp, "fdc doesn't support drive option werror");
        return;
    }
    if (blk_get_on_error(dev->conf.blk, 1) != BLOCKDEV_ON_ERROR_REPORT) {
        error_setg(errp, "fdc doesn't support drive option rerror");
        return;
    }

    drive->conf = &dev->conf;
    drive->blk = dev->conf.blk;
    drive->fdctrl = bus->fdc;

    fd_init(drive);
    blk_set_dev_ops(drive->blk, &fd_block_ops, drive);

    /* Keep 'type' qdev property and FDrive->drive in sync */
    drive->drive = dev->type;
    pick_drive_type(drive);
    dev->type = drive->drive;

    fd_revalidate(drive);
}

static void floppy_drive_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *k = DEVICE_CLASS(klass);
    k->realize = floppy_drive_realize;
    set_bit(DEVICE_CATEGORY_STORAGE, k->categories);
    k->bus_type = TYPE_FLOPPY_BUS;
    device_class_set_props(k, floppy_drive_properties);
    k->desc = "virtual floppy drive";
}

static const TypeInfo floppy_drive_info = {
    .name = TYPE_FLOPPY_DRIVE,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(FloppyDrive),
    .class_init = floppy_drive_class_init,
};

/********************************************************/
/* Intel 82078 floppy disk controller emulation          */

static void fdctrl_to_command_phase(FDCtrl *fdctrl);
static void fdctrl_raise_irq(FDCtrl *fdctrl);
static FDrive *get_cur_drv(FDCtrl *fdctrl);

static uint32_t fdctrl_read_statusA(FDCtrl *fdctrl);
static uint32_t fdctrl_read_statusB(FDCtrl *fdctrl);
static uint32_t fdctrl_read_dor(FDCtrl *fdctrl);
static void fdctrl_write_dor(FDCtrl *fdctrl, uint32_t value);
static uint32_t fdctrl_read_tape(FDCtrl *fdctrl);
static void fdctrl_write_tape(FDCtrl *fdctrl, uint32_t value);
static uint32_t fdctrl_read_main_status(FDCtrl *fdctrl);
static void fdctrl_write_rate(FDCtrl *fdctrl, uint32_t value);
static uint32_t fdctrl_read_data(FDCtrl *fdctrl);
static void fdctrl_write_data(FDCtrl *fdctrl, uint32_t value);
static uint32_t fdctrl_read_dir(FDCtrl *fdctrl);
static void fdctrl_write_ccr(FDCtrl *fdctrl, uint32_t value);

enum {
    FD_DIR_WRITE   = 0,
    FD_DIR_READ    = 1,
    FD_DIR_SCANE   = 2,
    FD_DIR_SCANL   = 3,
    FD_DIR_SCANH   = 4,
    FD_DIR_VERIFY  = 5,
};

enum {
    FD_STATE_MULTI  = 0x01,	/* multi track flag */
    FD_STATE_FORMAT = 0x02,	/* format flag */
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
    FD_REG_CCR = 0x07,
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
    FD_CMD_SAVE = 0x2e,
    FD_CMD_OPTION = 0x33,
    FD_CMD_RESTORE = 0x4e,
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
    FD_SR0_DS0      = 0x01,
    FD_SR0_DS1      = 0x02,
    FD_SR0_HEAD     = 0x04,
    FD_SR0_EQPMT    = 0x10,
    FD_SR0_SEEK     = 0x20,
    FD_SR0_ABNTERM  = 0x40,
    FD_SR0_INVCMD   = 0x80,
    FD_SR0_RDYCHG   = 0xc0,
};

enum {
    FD_SR1_MA       = 0x01, /* Missing address mark */
    FD_SR1_NW       = 0x02, /* Not writable */
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

/*
 * See chapter 5.0 "Controller phases" of the spec:
 *
 * Command phase:
 * The host writes a command and its parameters into the FIFO. The command
 * phase is completed when all parameters for the command have been supplied,
 * and execution phase is entered.
 *
 * Execution phase:
 * Data transfers, either DMA or non-DMA. For non-DMA transfers, the FIFO
 * contains the payload now, otherwise it's unused. When all bytes of the
 * required data have been transferred, the state is switched to either result
 * phase (if the command produces status bytes) or directly back into the
 * command phase for the next command.
 *
 * Result phase:
 * The host reads out the FIFO, which contains one or more result bytes now.
 */
enum {
    /* Only for migration: reconstruct phase from registers like qemu 2.3 */
    FD_PHASE_RECONSTRUCT    = 0,

    FD_PHASE_COMMAND        = 1,
    FD_PHASE_EXECUTION      = 2,
    FD_PHASE_RESULT         = 3,
};

#define FD_MULTI_TRACK(state) ((state) & FD_STATE_MULTI)
#define FD_FORMAT_CMD(state) ((state) & FD_STATE_FORMAT)

static FloppyDriveType get_fallback_drive_type(FDrive *drv)
{
    return drv->fdctrl->fallback;
}

uint32_t fdctrl_read(void *opaque, uint32_t reg)
{
    FDCtrl *fdctrl = opaque;
    uint32_t retval;

    reg &= 7;
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
    trace_fdc_ioport_read(reg, retval);

    return retval;
}

void fdctrl_write(void *opaque, uint32_t reg, uint32_t value)
{
    FDCtrl *fdctrl = opaque;

    reg &= 7;
    trace_fdc_ioport_write(reg, value);
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
    case FD_REG_CCR:
        fdctrl_write_ccr(fdctrl, value);
        break;
    default:
        break;
    }
}

static bool fdrive_media_changed_needed(void *opaque)
{
    FDrive *drive = opaque;

    return (drive->blk != NULL && drive->media_changed != 1);
}

static const VMStateDescription vmstate_fdrive_media_changed = {
    .name = "fdrive/media_changed",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = fdrive_media_changed_needed,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8(media_changed, FDrive),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_fdrive_media_rate = {
    .name = "fdrive/media_rate",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8(media_rate, FDrive),
        VMSTATE_END_OF_LIST()
    }
};

static bool fdrive_perpendicular_needed(void *opaque)
{
    FDrive *drive = opaque;

    return drive->perpendicular != 0;
}

static const VMStateDescription vmstate_fdrive_perpendicular = {
    .name = "fdrive/perpendicular",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = fdrive_perpendicular_needed,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8(perpendicular, FDrive),
        VMSTATE_END_OF_LIST()
    }
};

static int fdrive_post_load(void *opaque, int version_id)
{
    fd_revalidate(opaque);
    return 0;
}

static const VMStateDescription vmstate_fdrive = {
    .name = "fdrive",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = fdrive_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8(head, FDrive),
        VMSTATE_UINT8(track, FDrive),
        VMSTATE_UINT8(sect, FDrive),
        VMSTATE_END_OF_LIST()
    },
    .subsections = (const VMStateDescription*[]) {
        &vmstate_fdrive_media_changed,
        &vmstate_fdrive_media_rate,
        &vmstate_fdrive_perpendicular,
        NULL
    }
};

/*
 * Reconstructs the phase from register values according to the logic that was
 * implemented in qemu 2.3. This is the default value that is used if the phase
 * subsection is not present on migration.
 *
 * Don't change this function to reflect newer qemu versions, it is part of
 * the migration ABI.
 */
static int reconstruct_phase(FDCtrl *fdctrl)
{
    if (fdctrl->msr & FD_MSR_NONDMA) {
        return FD_PHASE_EXECUTION;
    } else if ((fdctrl->msr & FD_MSR_RQM) == 0) {
        /* qemu 2.3 disabled RQM only during DMA transfers */
        return FD_PHASE_EXECUTION;
    } else if (fdctrl->msr & FD_MSR_DIO) {
        return FD_PHASE_RESULT;
    } else {
        return FD_PHASE_COMMAND;
    }
}

static int fdc_pre_save(void *opaque)
{
    FDCtrl *s = opaque;

    s->dor_vmstate = s->dor | GET_CUR_DRV(s);

    return 0;
}

static int fdc_pre_load(void *opaque)
{
    FDCtrl *s = opaque;
    s->phase = FD_PHASE_RECONSTRUCT;
    return 0;
}

static int fdc_post_load(void *opaque, int version_id)
{
    FDCtrl *s = opaque;

    SET_CUR_DRV(s, s->dor_vmstate & FD_DOR_SELMASK);
    s->dor = s->dor_vmstate & ~FD_DOR_SELMASK;

    if (s->phase == FD_PHASE_RECONSTRUCT) {
        s->phase = reconstruct_phase(s);
    }

    return 0;
}

static bool fdc_reset_sensei_needed(void *opaque)
{
    FDCtrl *s = opaque;

    return s->reset_sensei != 0;
}

static const VMStateDescription vmstate_fdc_reset_sensei = {
    .name = "fdc/reset_sensei",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = fdc_reset_sensei_needed,
    .fields = (VMStateField[]) {
        VMSTATE_INT32(reset_sensei, FDCtrl),
        VMSTATE_END_OF_LIST()
    }
};

static bool fdc_result_timer_needed(void *opaque)
{
    FDCtrl *s = opaque;

    return timer_pending(s->result_timer);
}

static const VMStateDescription vmstate_fdc_result_timer = {
    .name = "fdc/result_timer",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = fdc_result_timer_needed,
    .fields = (VMStateField[]) {
        VMSTATE_TIMER_PTR(result_timer, FDCtrl),
        VMSTATE_END_OF_LIST()
    }
};

static bool fdc_phase_needed(void *opaque)
{
    FDCtrl *fdctrl = opaque;

    return reconstruct_phase(fdctrl) != fdctrl->phase;
}

static const VMStateDescription vmstate_fdc_phase = {
    .name = "fdc/phase",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = fdc_phase_needed,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8(phase, FDCtrl),
        VMSTATE_END_OF_LIST()
    }
};

const VMStateDescription vmstate_fdc = {
    .name = "fdc",
    .version_id = 2,
    .minimum_version_id = 2,
    .pre_save = fdc_pre_save,
    .pre_load = fdc_pre_load,
    .post_load = fdc_post_load,
    .fields = (VMStateField[]) {
        /* Controller State */
        VMSTATE_UINT8(sra, FDCtrl),
        VMSTATE_UINT8(srb, FDCtrl),
        VMSTATE_UINT8(dor_vmstate, FDCtrl),
        VMSTATE_UINT8(tdr, FDCtrl),
        VMSTATE_UINT8(dsr, FDCtrl),
        VMSTATE_UINT8(msr, FDCtrl),
        VMSTATE_UINT8(status0, FDCtrl),
        VMSTATE_UINT8(status1, FDCtrl),
        VMSTATE_UINT8(status2, FDCtrl),
        /* Command FIFO */
        VMSTATE_VARRAY_INT32(fifo, FDCtrl, fifo_size, 0, vmstate_info_uint8,
                             uint8_t),
        VMSTATE_UINT32(data_pos, FDCtrl),
        VMSTATE_UINT32(data_len, FDCtrl),
        VMSTATE_UINT8(data_state, FDCtrl),
        VMSTATE_UINT8(data_dir, FDCtrl),
        VMSTATE_UINT8(eot, FDCtrl),
        /* States kept only to be returned back */
        VMSTATE_UINT8(timer0, FDCtrl),
        VMSTATE_UINT8(timer1, FDCtrl),
        VMSTATE_UINT8(precomp_trk, FDCtrl),
        VMSTATE_UINT8(config, FDCtrl),
        VMSTATE_UINT8(lock, FDCtrl),
        VMSTATE_UINT8(pwrd, FDCtrl),
        VMSTATE_UINT8_EQUAL(num_floppies, FDCtrl, NULL),
        VMSTATE_STRUCT_ARRAY(drives, FDCtrl, MAX_FD, 1,
                             vmstate_fdrive, FDrive),
        VMSTATE_END_OF_LIST()
    },
    .subsections = (const VMStateDescription*[]) {
        &vmstate_fdc_reset_sensei,
        &vmstate_fdc_result_timer,
        &vmstate_fdc_phase,
        NULL
    }
};

/* Change IRQ state */
static void fdctrl_reset_irq(FDCtrl *fdctrl)
{
    fdctrl->status0 = 0;
    if (!(fdctrl->sra & FD_SRA_INTPEND))
        return;
    FLOPPY_DPRINTF("Reset interrupt\n");
    qemu_set_irq(fdctrl->irq, 0);
    fdctrl->sra &= ~FD_SRA_INTPEND;
}

static void fdctrl_raise_irq(FDCtrl *fdctrl)
{
    if (!(fdctrl->sra & FD_SRA_INTPEND)) {
        qemu_set_irq(fdctrl->irq, 1);
        fdctrl->sra |= FD_SRA_INTPEND;
    }

    fdctrl->reset_sensei = 0;
    FLOPPY_DPRINTF("Set interrupt status to 0x%02x\n", fdctrl->status0);
}

/* Reset controller */
void fdctrl_reset(FDCtrl *fdctrl, int do_irq)
{
    int i;

    FLOPPY_DPRINTF("reset controller\n");
    fdctrl_reset_irq(fdctrl);
    /* Initialise controller */
    fdctrl->sra = 0;
    fdctrl->srb = 0xc0;
    if (!fdctrl->drives[1].blk) {
        fdctrl->sra |= FD_SRA_nDRV2;
    }
    fdctrl->cur_drv = 0;
    fdctrl->dor = FD_DOR_nRESET;
    fdctrl->dor |= (fdctrl->dma_chann != -1) ? FD_DOR_DMAEN : 0;
    fdctrl->msr = FD_MSR_RQM;
    fdctrl->reset_sensei = 0;
    timer_del(fdctrl->result_timer);
    /* FIFO state */
    fdctrl->data_pos = 0;
    fdctrl->data_len = 0;
    fdctrl->data_state = 0;
    fdctrl->data_dir = FD_DIR_WRITE;
    for (i = 0; i < MAX_FD; i++)
        fd_recalibrate(&fdctrl->drives[i]);
    fdctrl_to_command_phase(fdctrl);
    if (do_irq) {
        fdctrl->status0 |= FD_SR0_RDYCHG;
        fdctrl_raise_irq(fdctrl);
        fdctrl->reset_sensei = FD_RESET_SENSEI_COUNT;
    }
}

static inline FDrive *drv0(FDCtrl *fdctrl)
{
    return &fdctrl->drives[(fdctrl->tdr & FD_TDR_BOOTSEL) >> 2];
}

static inline FDrive *drv1(FDCtrl *fdctrl)
{
    if ((fdctrl->tdr & FD_TDR_BOOTSEL) < (1 << 2))
        return &fdctrl->drives[1];
    else
        return &fdctrl->drives[0];
}

#if MAX_FD == 4
static inline FDrive *drv2(FDCtrl *fdctrl)
{
    if ((fdctrl->tdr & FD_TDR_BOOTSEL) < (2 << 2))
        return &fdctrl->drives[2];
    else
        return &fdctrl->drives[1];
}

static inline FDrive *drv3(FDCtrl *fdctrl)
{
    if ((fdctrl->tdr & FD_TDR_BOOTSEL) < (3 << 2))
        return &fdctrl->drives[3];
    else
        return &fdctrl->drives[2];
}
#endif

static FDrive *get_drv(FDCtrl *fdctrl, int unit)
{
    switch (unit) {
        case 0: return drv0(fdctrl);
        case 1: return drv1(fdctrl);
#if MAX_FD == 4
        case 2: return drv2(fdctrl);
        case 3: return drv3(fdctrl);
#endif
        default: return NULL;
    }
}

static FDrive *get_cur_drv(FDCtrl *fdctrl)
{
    return get_drv(fdctrl, fdctrl->cur_drv);
}

/* Status A register : 0x00 (read-only) */
static uint32_t fdctrl_read_statusA(FDCtrl *fdctrl)
{
    uint32_t retval = fdctrl->sra;

    FLOPPY_DPRINTF("status register A: 0x%02x\n", retval);

    return retval;
}

/* Status B register : 0x01 (read-only) */
static uint32_t fdctrl_read_statusB(FDCtrl *fdctrl)
{
    uint32_t retval = fdctrl->srb;

    FLOPPY_DPRINTF("status register B: 0x%02x\n", retval);

    return retval;
}

/* Digital output register : 0x02 */
static uint32_t fdctrl_read_dor(FDCtrl *fdctrl)
{
    uint32_t retval = fdctrl->dor;

    /* Selected drive */
    retval |= fdctrl->cur_drv;
    FLOPPY_DPRINTF("digital output register: 0x%02x\n", retval);

    return retval;
}

static void fdctrl_write_dor(FDCtrl *fdctrl, uint32_t value)
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
static uint32_t fdctrl_read_tape(FDCtrl *fdctrl)
{
    uint32_t retval = fdctrl->tdr;

    FLOPPY_DPRINTF("tape drive register: 0x%02x\n", retval);

    return retval;
}

static void fdctrl_write_tape(FDCtrl *fdctrl, uint32_t value)
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
static uint32_t fdctrl_read_main_status(FDCtrl *fdctrl)
{
    uint32_t retval = fdctrl->msr;

    fdctrl->dsr &= ~FD_DSR_PWRDOWN;
    fdctrl->dor |= FD_DOR_nRESET;

    FLOPPY_DPRINTF("main status register: 0x%02x\n", retval);

    return retval;
}

/* Data select rate register : 0x04 (write) */
static void fdctrl_write_rate(FDCtrl *fdctrl, uint32_t value)
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

/* Configuration control register: 0x07 (write) */
static void fdctrl_write_ccr(FDCtrl *fdctrl, uint32_t value)
{
    /* Reset mode */
    if (!(fdctrl->dor & FD_DOR_nRESET)) {
        FLOPPY_DPRINTF("Floppy controller in RESET state !\n");
        return;
    }
    FLOPPY_DPRINTF("configuration control register set to 0x%02x\n", value);

    /* Only the rate selection bits used in AT mode, and we
     * store those in the DSR.
     */
    fdctrl->dsr = (fdctrl->dsr & ~FD_DSR_DRATEMASK) |
                  (value & FD_DSR_DRATEMASK);
}

static int fdctrl_media_changed(FDrive *drv)
{
    return drv->media_changed;
}

/* Digital input register : 0x07 (read-only) */
static uint32_t fdctrl_read_dir(FDCtrl *fdctrl)
{
    uint32_t retval = 0;

    if (fdctrl_media_changed(get_cur_drv(fdctrl))) {
        retval |= FD_DIR_DSKCHG;
    }
    if (retval != 0) {
        FLOPPY_DPRINTF("Floppy digital input register: 0x%02x\n", retval);
    }

    return retval;
}

/* Clear the FIFO and update the state for receiving the next command */
static void fdctrl_to_command_phase(FDCtrl *fdctrl)
{
    fdctrl->phase = FD_PHASE_COMMAND;
    fdctrl->data_dir = FD_DIR_WRITE;
    fdctrl->data_pos = 0;
    fdctrl->data_len = 1; /* Accept command byte, adjust for params later */
    fdctrl->msr &= ~(FD_MSR_CMDBUSY | FD_MSR_DIO);
    fdctrl->msr |= FD_MSR_RQM;
}

/* Update the state to allow the guest to read out the command status.
 * @fifo_len is the number of result bytes to be read out. */
static void fdctrl_to_result_phase(FDCtrl *fdctrl, int fifo_len)
{
    fdctrl->phase = FD_PHASE_RESULT;
    fdctrl->data_dir = FD_DIR_READ;
    fdctrl->data_len = fifo_len;
    fdctrl->data_pos = 0;
    fdctrl->msr |= FD_MSR_CMDBUSY | FD_MSR_RQM | FD_MSR_DIO;
}

/* Set an error: unimplemented/unknown command */
static void fdctrl_unimplemented(FDCtrl *fdctrl, int direction)
{
    qemu_log_mask(LOG_UNIMP, "fdc: unimplemented command 0x%02x\n",
                  fdctrl->fifo[0]);
    fdctrl->fifo[0] = FD_SR0_INVCMD;
    fdctrl_to_result_phase(fdctrl, 1);
}

/* Seek to next sector
 * returns 0 when end of track reached (for DBL_SIDES on head 1)
 * otherwise returns 1
 */
static int fdctrl_seek_to_next_sect(FDCtrl *fdctrl, FDrive *cur_drv)
{
    FLOPPY_DPRINTF("seek to next sector (%d %02x %02x => %d)\n",
                   cur_drv->head, cur_drv->track, cur_drv->sect,
                   fd_sector(cur_drv));
    /* XXX: cur_drv->sect >= cur_drv->last_sect should be an
       error in fact */
    uint8_t new_head = cur_drv->head;
    uint8_t new_track = cur_drv->track;
    uint8_t new_sect = cur_drv->sect;

    int ret = 1;

    if (new_sect >= cur_drv->last_sect ||
        new_sect == fdctrl->eot) {
        new_sect = 1;
        if (FD_MULTI_TRACK(fdctrl->data_state)) {
            if (new_head == 0 &&
                (cur_drv->flags & FDISK_DBL_SIDES) != 0) {
                new_head = 1;
            } else {
                new_head = 0;
                new_track++;
                fdctrl->status0 |= FD_SR0_SEEK;
                if ((cur_drv->flags & FDISK_DBL_SIDES) == 0) {
                    ret = 0;
                }
            }
        } else {
            fdctrl->status0 |= FD_SR0_SEEK;
            new_track++;
            ret = 0;
        }
        if (ret == 1) {
            FLOPPY_DPRINTF("seek to next track (%d %02x %02x => %d)\n",
                    new_head, new_track, new_sect, fd_sector(cur_drv));
        }
    } else {
        new_sect++;
    }
    fd_seek(cur_drv, new_head, new_track, new_sect, 1);
    return ret;
}

/* Callback for transfer end (stop or abort) */
static void fdctrl_stop_transfer(FDCtrl *fdctrl, uint8_t status0,
                                 uint8_t status1, uint8_t status2)
{
    FDrive *cur_drv;
    cur_drv = get_cur_drv(fdctrl);

    fdctrl->status0 &= ~(FD_SR0_DS0 | FD_SR0_DS1 | FD_SR0_HEAD);
    fdctrl->status0 |= GET_CUR_DRV(fdctrl);
    if (cur_drv->head) {
        fdctrl->status0 |= FD_SR0_HEAD;
    }
    fdctrl->status0 |= status0;

    FLOPPY_DPRINTF("transfer status: %02x %02x %02x (%02x)\n",
                   status0, status1, status2, fdctrl->status0);
    fdctrl->fifo[0] = fdctrl->status0;
    fdctrl->fifo[1] = status1;
    fdctrl->fifo[2] = status2;
    fdctrl->fifo[3] = cur_drv->track;
    fdctrl->fifo[4] = cur_drv->head;
    fdctrl->fifo[5] = cur_drv->sect;
    fdctrl->fifo[6] = FD_SECTOR_SC;
    fdctrl->data_dir = FD_DIR_READ;
    if (fdctrl->dma_chann != -1 && !(fdctrl->msr & FD_MSR_NONDMA)) {
        IsaDmaClass *k = ISADMA_GET_CLASS(fdctrl->dma);
        k->release_DREQ(fdctrl->dma, fdctrl->dma_chann);
    }
    fdctrl->msr |= FD_MSR_RQM | FD_MSR_DIO;
    fdctrl->msr &= ~FD_MSR_NONDMA;

    fdctrl_to_result_phase(fdctrl, 7);
    fdctrl_raise_irq(fdctrl);
}

/* Prepare a data transfer (either DMA or FIFO) */
static void fdctrl_start_transfer(FDCtrl *fdctrl, int direction)
{
    FDrive *cur_drv;
    uint8_t kh, kt, ks;

    SET_CUR_DRV(fdctrl, fdctrl->fifo[1] & FD_DOR_SELMASK);
    cur_drv = get_cur_drv(fdctrl);
    kt = fdctrl->fifo[2];
    kh = fdctrl->fifo[3];
    ks = fdctrl->fifo[4];
    FLOPPY_DPRINTF("Start transfer at %d %d %02x %02x (%d)\n",
                   GET_CUR_DRV(fdctrl), kh, kt, ks,
                   fd_sector_calc(kh, kt, ks, cur_drv->last_sect,
                                  NUM_SIDES(cur_drv)));
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
        fdctrl->status0 |= FD_SR0_SEEK;
        break;
    default:
        break;
    }

    /* Check the data rate. If the programmed data rate does not match
     * the currently inserted medium, the operation has to fail. */
    if ((fdctrl->dsr & FD_DSR_DRATEMASK) != cur_drv->media_rate) {
        FLOPPY_DPRINTF("data rate mismatch (fdc=%d, media=%d)\n",
                       fdctrl->dsr & FD_DSR_DRATEMASK, cur_drv->media_rate);
        fdctrl_stop_transfer(fdctrl, FD_SR0_ABNTERM, FD_SR1_MA, 0x00);
        fdctrl->fifo[3] = kt;
        fdctrl->fifo[4] = kh;
        fdctrl->fifo[5] = ks;
        return;
    }

    /* Set the FIFO state */
    fdctrl->data_dir = direction;
    fdctrl->data_pos = 0;
    assert(fdctrl->msr & FD_MSR_CMDBUSY);
    if (fdctrl->fifo[0] & 0x80)
        fdctrl->data_state |= FD_STATE_MULTI;
    else
        fdctrl->data_state &= ~FD_STATE_MULTI;
    if (fdctrl->fifo[5] == 0) {
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
        /* DMA transfer is enabled. */
        IsaDmaClass *k = ISADMA_GET_CLASS(fdctrl->dma);

        FLOPPY_DPRINTF("direction=%d (%d - %d)\n",
                       direction, (128 << fdctrl->fifo[5]) *
                       (cur_drv->last_sect - ks + 1), fdctrl->data_len);

        /* No access is allowed until DMA transfer has completed */
        fdctrl->msr &= ~FD_MSR_RQM;
        if (direction != FD_DIR_VERIFY) {
            /*
             * Now, we just have to wait for the DMA controller to
             * recall us...
             */
            k->hold_DREQ(fdctrl->dma, fdctrl->dma_chann);
            k->schedule(fdctrl->dma);
        } else {
            /* Start transfer */
            fdctrl_transfer_handler(fdctrl, fdctrl->dma_chann, 0,
                    fdctrl->data_len);
        }
        return;
    }
    FLOPPY_DPRINTF("start non-DMA transfer\n");
    fdctrl->msr |= FD_MSR_NONDMA | FD_MSR_RQM;
    if (direction != FD_DIR_WRITE)
        fdctrl->msr |= FD_MSR_DIO;
    /* IO based transfer: calculate len */
    fdctrl_raise_irq(fdctrl);
}

/* Prepare a transfer of deleted data */
static void fdctrl_start_transfer_del(FDCtrl *fdctrl, int direction)
{
    qemu_log_mask(LOG_UNIMP, "fdctrl_start_transfer_del() unimplemented\n");

    /* We don't handle deleted data,
     * so we don't return *ANYTHING*
     */
    fdctrl_stop_transfer(fdctrl, FD_SR0_ABNTERM | FD_SR0_SEEK, 0x00, 0x00);
}

/* handlers for DMA transfers */
int fdctrl_transfer_handler(void *opaque, int nchan, int dma_pos, int dma_len)
{
    FDCtrl *fdctrl;
    FDrive *cur_drv;
    int len, start_pos, rel_pos;
    uint8_t status0 = 0x00, status1 = 0x00, status2 = 0x00;
    IsaDmaClass *k;

    fdctrl = opaque;
    if (fdctrl->msr & FD_MSR_RQM) {
        FLOPPY_DPRINTF("Not in DMA transfer mode !\n");
        return 0;
    }
    k = ISADMA_GET_CLASS(fdctrl->dma);
    cur_drv = get_cur_drv(fdctrl);
    if (fdctrl->data_dir == FD_DIR_SCANE || fdctrl->data_dir == FD_DIR_SCANL ||
        fdctrl->data_dir == FD_DIR_SCANH)
        status2 = FD_SR2_SNS;
    if (dma_len > fdctrl->data_len)
        dma_len = fdctrl->data_len;
    if (cur_drv->blk == NULL) {
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
            if (blk_pread(cur_drv->blk, fd_offset(cur_drv),
                          fdctrl->fifo, BDRV_SECTOR_SIZE) < 0) {
                FLOPPY_DPRINTF("Floppy: error getting sector %d\n",
                               fd_sector(cur_drv));
                /* Sure, image size is too small... */
                memset(fdctrl->fifo, 0, FD_SECTOR_LEN);
            }
        }
        switch (fdctrl->data_dir) {
        case FD_DIR_READ:
            /* READ commands */
            k->write_memory(fdctrl->dma, nchan, fdctrl->fifo + rel_pos,
                            fdctrl->data_pos, len);
            break;
        case FD_DIR_WRITE:
            /* WRITE commands */
            if (cur_drv->ro) {
                /* Handle readonly medium early, no need to do DMA, touch the
                 * LED or attempt any writes. A real floppy doesn't attempt
                 * to write to readonly media either. */
                fdctrl_stop_transfer(fdctrl,
                                     FD_SR0_ABNTERM | FD_SR0_SEEK, FD_SR1_NW,
                                     0x00);
                goto transfer_error;
            }

            k->read_memory(fdctrl->dma, nchan, fdctrl->fifo + rel_pos,
                           fdctrl->data_pos, len);
            if (blk_pwrite(cur_drv->blk, fd_offset(cur_drv),
                           fdctrl->fifo, BDRV_SECTOR_SIZE, 0) < 0) {
                FLOPPY_DPRINTF("error writing sector %d\n",
                               fd_sector(cur_drv));
                fdctrl_stop_transfer(fdctrl, FD_SR0_ABNTERM | FD_SR0_SEEK, 0x00, 0x00);
                goto transfer_error;
            }
            break;
        case FD_DIR_VERIFY:
            /* VERIFY commands */
            break;
        default:
            /* SCAN commands */
            {
                uint8_t tmpbuf[FD_SECTOR_LEN];
                int ret;
                k->read_memory(fdctrl->dma, nchan, tmpbuf, fdctrl->data_pos,
                               len);
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
    fdctrl->data_len -= len;
    fdctrl_stop_transfer(fdctrl, status0, status1, status2);
 transfer_error:

    return len;
}

/* Data register : 0x05 */
static uint32_t fdctrl_read_data(FDCtrl *fdctrl)
{
    FDrive *cur_drv;
    uint32_t retval = 0;
    uint32_t pos;

    cur_drv = get_cur_drv(fdctrl);
    fdctrl->dsr &= ~FD_DSR_PWRDOWN;
    if (!(fdctrl->msr & FD_MSR_RQM) || !(fdctrl->msr & FD_MSR_DIO)) {
        FLOPPY_DPRINTF("error: controller not ready for reading\n");
        return 0;
    }

    /* If data_len spans multiple sectors, the current position in the FIFO
     * wraps around while fdctrl->data_pos is the real position in the whole
     * request. */
    pos = fdctrl->data_pos;
    pos %= FD_SECTOR_LEN;

    switch (fdctrl->phase) {
    case FD_PHASE_EXECUTION:
        assert(fdctrl->msr & FD_MSR_NONDMA);
        if (pos == 0) {
            if (fdctrl->data_pos != 0)
                if (!fdctrl_seek_to_next_sect(fdctrl, cur_drv)) {
                    FLOPPY_DPRINTF("error seeking to next sector %d\n",
                                   fd_sector(cur_drv));
                    return 0;
                }
            if (blk_pread(cur_drv->blk, fd_offset(cur_drv), fdctrl->fifo,
                          BDRV_SECTOR_SIZE)
                < 0) {
                FLOPPY_DPRINTF("error getting sector %d\n",
                               fd_sector(cur_drv));
                /* Sure, image size is too small... */
                memset(fdctrl->fifo, 0, FD_SECTOR_LEN);
            }
        }

        if (++fdctrl->data_pos == fdctrl->data_len) {
            fdctrl->msr &= ~FD_MSR_RQM;
            fdctrl_stop_transfer(fdctrl, 0x00, 0x00, 0x00);
        }
        break;

    case FD_PHASE_RESULT:
        assert(!(fdctrl->msr & FD_MSR_NONDMA));
        if (++fdctrl->data_pos == fdctrl->data_len) {
            fdctrl->msr &= ~FD_MSR_RQM;
            fdctrl_to_command_phase(fdctrl);
            fdctrl_reset_irq(fdctrl);
        }
        break;

    case FD_PHASE_COMMAND:
    default:
        abort();
    }

    retval = fdctrl->fifo[pos];
    FLOPPY_DPRINTF("data register: 0x%02x\n", retval);

    return retval;
}

static void fdctrl_format_sector(FDCtrl *fdctrl)
{
    FDrive *cur_drv;
    uint8_t kh, kt, ks;

    SET_CUR_DRV(fdctrl, fdctrl->fifo[1] & FD_DOR_SELMASK);
    cur_drv = get_cur_drv(fdctrl);
    kt = fdctrl->fifo[6];
    kh = fdctrl->fifo[7];
    ks = fdctrl->fifo[8];
    FLOPPY_DPRINTF("format sector at %d %d %02x %02x (%d)\n",
                   GET_CUR_DRV(fdctrl), kh, kt, ks,
                   fd_sector_calc(kh, kt, ks, cur_drv->last_sect,
                                  NUM_SIDES(cur_drv)));
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
        fdctrl->status0 |= FD_SR0_SEEK;
        break;
    default:
        break;
    }
    memset(fdctrl->fifo, 0, FD_SECTOR_LEN);
    if (cur_drv->blk == NULL ||
        blk_pwrite(cur_drv->blk, fd_offset(cur_drv), fdctrl->fifo,
                   BDRV_SECTOR_SIZE, 0) < 0) {
        FLOPPY_DPRINTF("error formatting sector %d\n", fd_sector(cur_drv));
        fdctrl_stop_transfer(fdctrl, FD_SR0_ABNTERM | FD_SR0_SEEK, 0x00, 0x00);
    } else {
        if (cur_drv->sect == cur_drv->last_sect) {
            fdctrl->data_state &= ~FD_STATE_FORMAT;
            /* Last sector done */
            fdctrl_stop_transfer(fdctrl, 0x00, 0x00, 0x00);
        } else {
            /* More to do */
            fdctrl->data_pos = 0;
            fdctrl->data_len = 4;
        }
    }
}

static void fdctrl_handle_lock(FDCtrl *fdctrl, int direction)
{
    fdctrl->lock = (fdctrl->fifo[0] & 0x80) ? 1 : 0;
    fdctrl->fifo[0] = fdctrl->lock << 4;
    fdctrl_to_result_phase(fdctrl, 1);
}

static void fdctrl_handle_dumpreg(FDCtrl *fdctrl, int direction)
{
    FDrive *cur_drv = get_cur_drv(fdctrl);

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
    fdctrl_to_result_phase(fdctrl, 10);
}

static void fdctrl_handle_version(FDCtrl *fdctrl, int direction)
{
    /* Controller's version */
    fdctrl->fifo[0] = fdctrl->version;
    fdctrl_to_result_phase(fdctrl, 1);
}

static void fdctrl_handle_partid(FDCtrl *fdctrl, int direction)
{
    fdctrl->fifo[0] = 0x41; /* Stepping 1 */
    fdctrl_to_result_phase(fdctrl, 1);
}

static void fdctrl_handle_restore(FDCtrl *fdctrl, int direction)
{
    FDrive *cur_drv = get_cur_drv(fdctrl);

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
    fdctrl_to_command_phase(fdctrl);
}

static void fdctrl_handle_save(FDCtrl *fdctrl, int direction)
{
    FDrive *cur_drv = get_cur_drv(fdctrl);

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
    fdctrl_to_result_phase(fdctrl, 15);
}

static void fdctrl_handle_readid(FDCtrl *fdctrl, int direction)
{
    FDrive *cur_drv = get_cur_drv(fdctrl);

    cur_drv->head = (fdctrl->fifo[1] >> 2) & 1;
    timer_mod(fdctrl->result_timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
             (NANOSECONDS_PER_SECOND / 50));
}

static void fdctrl_handle_format_track(FDCtrl *fdctrl, int direction)
{
    FDrive *cur_drv;

    SET_CUR_DRV(fdctrl, fdctrl->fifo[1] & FD_DOR_SELMASK);
    cur_drv = get_cur_drv(fdctrl);
    fdctrl->data_state |= FD_STATE_FORMAT;
    if (fdctrl->fifo[0] & 0x80)
        fdctrl->data_state |= FD_STATE_MULTI;
    else
        fdctrl->data_state &= ~FD_STATE_MULTI;
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

static void fdctrl_handle_specify(FDCtrl *fdctrl, int direction)
{
    fdctrl->timer0 = (fdctrl->fifo[1] >> 4) & 0xF;
    fdctrl->timer1 = fdctrl->fifo[2] >> 1;
    if (fdctrl->fifo[2] & 1)
        fdctrl->dor &= ~FD_DOR_DMAEN;
    else
        fdctrl->dor |= FD_DOR_DMAEN;
    /* No result back */
    fdctrl_to_command_phase(fdctrl);
}

static void fdctrl_handle_sense_drive_status(FDCtrl *fdctrl, int direction)
{
    FDrive *cur_drv;

    SET_CUR_DRV(fdctrl, fdctrl->fifo[1] & FD_DOR_SELMASK);
    cur_drv = get_cur_drv(fdctrl);
    cur_drv->head = (fdctrl->fifo[1] >> 2) & 1;
    /* 1 Byte status back */
    fdctrl->fifo[0] = (cur_drv->ro << 6) |
        (cur_drv->track == 0 ? 0x10 : 0x00) |
        (cur_drv->head << 2) |
        GET_CUR_DRV(fdctrl) |
        0x28;
    fdctrl_to_result_phase(fdctrl, 1);
}

static void fdctrl_handle_recalibrate(FDCtrl *fdctrl, int direction)
{
    FDrive *cur_drv;

    SET_CUR_DRV(fdctrl, fdctrl->fifo[1] & FD_DOR_SELMASK);
    cur_drv = get_cur_drv(fdctrl);
    fd_recalibrate(cur_drv);
    fdctrl_to_command_phase(fdctrl);
    /* Raise Interrupt */
    fdctrl->status0 |= FD_SR0_SEEK;
    fdctrl_raise_irq(fdctrl);
}

static void fdctrl_handle_sense_interrupt_status(FDCtrl *fdctrl, int direction)
{
    FDrive *cur_drv = get_cur_drv(fdctrl);

    if (fdctrl->reset_sensei > 0) {
        fdctrl->fifo[0] =
            FD_SR0_RDYCHG + FD_RESET_SENSEI_COUNT - fdctrl->reset_sensei;
        fdctrl->reset_sensei--;
    } else if (!(fdctrl->sra & FD_SRA_INTPEND)) {
        fdctrl->fifo[0] = FD_SR0_INVCMD;
        fdctrl_to_result_phase(fdctrl, 1);
        return;
    } else {
        fdctrl->fifo[0] =
                (fdctrl->status0 & ~(FD_SR0_HEAD | FD_SR0_DS1 | FD_SR0_DS0))
                | GET_CUR_DRV(fdctrl);
    }

    fdctrl->fifo[1] = cur_drv->track;
    fdctrl_to_result_phase(fdctrl, 2);
    fdctrl_reset_irq(fdctrl);
    fdctrl->status0 = FD_SR0_RDYCHG;
}

static void fdctrl_handle_seek(FDCtrl *fdctrl, int direction)
{
    FDrive *cur_drv;

    SET_CUR_DRV(fdctrl, fdctrl->fifo[1] & FD_DOR_SELMASK);
    cur_drv = get_cur_drv(fdctrl);
    fdctrl_to_command_phase(fdctrl);
    /* The seek command just sends step pulses to the drive and doesn't care if
     * there is a medium inserted of if it's banging the head against the drive.
     */
    fd_seek(cur_drv, cur_drv->head, fdctrl->fifo[2], cur_drv->sect, 1);
    /* Raise Interrupt */
    fdctrl->status0 |= FD_SR0_SEEK;
    fdctrl_raise_irq(fdctrl);
}

static void fdctrl_handle_perpendicular_mode(FDCtrl *fdctrl, int direction)
{
    FDrive *cur_drv = get_cur_drv(fdctrl);

    if (fdctrl->fifo[1] & 0x80)
        cur_drv->perpendicular = fdctrl->fifo[1] & 0x7;
    /* No result back */
    fdctrl_to_command_phase(fdctrl);
}

static void fdctrl_handle_configure(FDCtrl *fdctrl, int direction)
{
    fdctrl->config = fdctrl->fifo[2];
    fdctrl->precomp_trk =  fdctrl->fifo[3];
    /* No result back */
    fdctrl_to_command_phase(fdctrl);
}

static void fdctrl_handle_powerdown_mode(FDCtrl *fdctrl, int direction)
{
    fdctrl->pwrd = fdctrl->fifo[1];
    fdctrl->fifo[0] = fdctrl->fifo[1];
    fdctrl_to_result_phase(fdctrl, 1);
}

static void fdctrl_handle_option(FDCtrl *fdctrl, int direction)
{
    /* No result back */
    fdctrl_to_command_phase(fdctrl);
}

static void fdctrl_handle_drive_specification_command(FDCtrl *fdctrl, int direction)
{
    FDrive *cur_drv = get_cur_drv(fdctrl);
    uint32_t pos;

    pos = fdctrl->data_pos - 1;
    pos %= FD_SECTOR_LEN;
    if (fdctrl->fifo[pos] & 0x80) {
        /* Command parameters done */
        if (fdctrl->fifo[pos] & 0x40) {
            fdctrl->fifo[0] = fdctrl->fifo[1];
            fdctrl->fifo[2] = 0;
            fdctrl->fifo[3] = 0;
            fdctrl_to_result_phase(fdctrl, 4);
        } else {
            fdctrl_to_command_phase(fdctrl);
        }
    } else if (fdctrl->data_len > 7) {
        /* ERROR */
        fdctrl->fifo[0] = 0x80 |
            (cur_drv->head << 2) | GET_CUR_DRV(fdctrl);
        fdctrl_to_result_phase(fdctrl, 1);
    }
}

static void fdctrl_handle_relative_seek_in(FDCtrl *fdctrl, int direction)
{
    FDrive *cur_drv;

    SET_CUR_DRV(fdctrl, fdctrl->fifo[1] & FD_DOR_SELMASK);
    cur_drv = get_cur_drv(fdctrl);
    if (fdctrl->fifo[2] + cur_drv->track >= cur_drv->max_track) {
        fd_seek(cur_drv, cur_drv->head, cur_drv->max_track - 1,
                cur_drv->sect, 1);
    } else {
        fd_seek(cur_drv, cur_drv->head,
                cur_drv->track + fdctrl->fifo[2], cur_drv->sect, 1);
    }
    fdctrl_to_command_phase(fdctrl);
    /* Raise Interrupt */
    fdctrl->status0 |= FD_SR0_SEEK;
    fdctrl_raise_irq(fdctrl);
}

static void fdctrl_handle_relative_seek_out(FDCtrl *fdctrl, int direction)
{
    FDrive *cur_drv;

    SET_CUR_DRV(fdctrl, fdctrl->fifo[1] & FD_DOR_SELMASK);
    cur_drv = get_cur_drv(fdctrl);
    if (fdctrl->fifo[2] > cur_drv->track) {
        fd_seek(cur_drv, cur_drv->head, 0, cur_drv->sect, 1);
    } else {
        fd_seek(cur_drv, cur_drv->head,
                cur_drv->track - fdctrl->fifo[2], cur_drv->sect, 1);
    }
    fdctrl_to_command_phase(fdctrl);
    /* Raise Interrupt */
    fdctrl->status0 |= FD_SR0_SEEK;
    fdctrl_raise_irq(fdctrl);
}

/*
 * Handlers for the execution phase of each command
 */
typedef struct FDCtrlCommand {
    uint8_t value;
    uint8_t mask;
    const char* name;
    int parameters;
    void (*handler)(FDCtrl *fdctrl, int direction);
    int direction;
} FDCtrlCommand;

static const FDCtrlCommand handlers[] = {
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
    { FD_CMD_VERIFY, 0x1f, "VERIFY", 8, fdctrl_start_transfer, FD_DIR_VERIFY },
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

static const FDCtrlCommand *get_command(uint8_t cmd)
{
    int idx;

    idx = command_to_handler[cmd];
    FLOPPY_DPRINTF("%s command\n", handlers[idx].name);
    return &handlers[idx];
}

static void fdctrl_write_data(FDCtrl *fdctrl, uint32_t value)
{
    FDrive *cur_drv;
    const FDCtrlCommand *cmd;
    uint32_t pos;

    /* Reset mode */
    if (!(fdctrl->dor & FD_DOR_nRESET)) {
        FLOPPY_DPRINTF("Floppy controller in RESET state !\n");
        return;
    }
    if (!(fdctrl->msr & FD_MSR_RQM) || (fdctrl->msr & FD_MSR_DIO)) {
        FLOPPY_DPRINTF("error: controller not ready for writing\n");
        return;
    }
    fdctrl->dsr &= ~FD_DSR_PWRDOWN;

    FLOPPY_DPRINTF("%s: %02x\n", __func__, value);

    /* If data_len spans multiple sectors, the current position in the FIFO
     * wraps around while fdctrl->data_pos is the real position in the whole
     * request. */
    pos = fdctrl->data_pos++;
    pos %= FD_SECTOR_LEN;
    fdctrl->fifo[pos] = value;

    if (fdctrl->data_pos == fdctrl->data_len) {
        fdctrl->msr &= ~FD_MSR_RQM;
    }

    switch (fdctrl->phase) {
    case FD_PHASE_EXECUTION:
        /* For DMA requests, RQM should be cleared during execution phase, so
         * we would have errored out above. */
        assert(fdctrl->msr & FD_MSR_NONDMA);

        /* FIFO data write */
        if (pos == FD_SECTOR_LEN - 1 ||
            fdctrl->data_pos == fdctrl->data_len) {
            cur_drv = get_cur_drv(fdctrl);
            if (blk_pwrite(cur_drv->blk, fd_offset(cur_drv), fdctrl->fifo,
                           BDRV_SECTOR_SIZE, 0) < 0) {
                FLOPPY_DPRINTF("error writing sector %d\n",
                               fd_sector(cur_drv));
                break;
            }
            if (!fdctrl_seek_to_next_sect(fdctrl, cur_drv)) {
                FLOPPY_DPRINTF("error seeking to next sector %d\n",
                               fd_sector(cur_drv));
                break;
            }
        }

        /* Switch to result phase when done with the transfer */
        if (fdctrl->data_pos == fdctrl->data_len) {
            fdctrl_stop_transfer(fdctrl, 0x00, 0x00, 0x00);
        }
        break;

    case FD_PHASE_COMMAND:
        assert(!(fdctrl->msr & FD_MSR_NONDMA));
        assert(fdctrl->data_pos < FD_SECTOR_LEN);

        if (pos == 0) {
            /* The first byte specifies the command. Now we start reading
             * as many parameters as this command requires. */
            cmd = get_command(value);
            fdctrl->data_len = cmd->parameters + 1;
            if (cmd->parameters) {
                fdctrl->msr |= FD_MSR_RQM;
            }
            fdctrl->msr |= FD_MSR_CMDBUSY;
        }

        if (fdctrl->data_pos == fdctrl->data_len) {
            /* We have all parameters now, execute the command */
            fdctrl->phase = FD_PHASE_EXECUTION;

            if (fdctrl->data_state & FD_STATE_FORMAT) {
                fdctrl_format_sector(fdctrl);
                break;
            }

            cmd = get_command(fdctrl->fifo[0]);
            FLOPPY_DPRINTF("Calling handler for '%s'\n", cmd->name);
            cmd->handler(fdctrl, cmd->direction);
        }
        break;

    case FD_PHASE_RESULT:
    default:
        abort();
    }
}

static void fdctrl_result_timer(void *opaque)
{
    FDCtrl *fdctrl = opaque;
    FDrive *cur_drv = get_cur_drv(fdctrl);

    /* Pretend we are spinning.
     * This is needed for Coherent, which uses READ ID to check for
     * sector interleaving.
     */
    if (cur_drv->last_sect != 0) {
        cur_drv->sect = (cur_drv->sect % cur_drv->last_sect) + 1;
    }
    /* READ_ID can't automatically succeed! */
    if ((fdctrl->dsr & FD_DSR_DRATEMASK) != cur_drv->media_rate) {
        FLOPPY_DPRINTF("read id rate mismatch (fdc=%d, media=%d)\n",
                       fdctrl->dsr & FD_DSR_DRATEMASK, cur_drv->media_rate);
        fdctrl_stop_transfer(fdctrl, FD_SR0_ABNTERM, FD_SR1_MA, 0x00);
    } else {
        fdctrl_stop_transfer(fdctrl, 0x00, 0x00, 0x00);
    }
}

/* Init functions */

void fdctrl_init_drives(FloppyBus *bus, DriveInfo **fds)
{
    DeviceState *dev;
    int i;

    for (i = 0; i < MAX_FD; i++) {
        if (fds[i]) {
            dev = qdev_new("floppy");
            qdev_prop_set_uint32(dev, "unit", i);
            qdev_prop_set_enum(dev, "drive-type", FLOPPY_DRIVE_TYPE_AUTO);
            qdev_prop_set_drive_err(dev, "drive", blk_by_legacy_dinfo(fds[i]),
                                    &error_fatal);
            qdev_realize_and_unref(dev, &bus->bus, &error_fatal);
        }
    }
}

void fdctrl_realize_common(DeviceState *dev, FDCtrl *fdctrl, Error **errp)
{
    int i, j;
    FDrive *drive;
    static int command_tables_inited = 0;

    if (fdctrl->fallback == FLOPPY_DRIVE_TYPE_AUTO) {
        error_setg(errp, "Cannot choose a fallback FDrive type of 'auto'");
        return;
    }

    /* Fill 'command_to_handler' lookup table */
    if (!command_tables_inited) {
        command_tables_inited = 1;
        for (i = ARRAY_SIZE(handlers) - 1; i >= 0; i--) {
            for (j = 0; j < sizeof(command_to_handler); j++) {
                if ((j & handlers[i].mask) == handlers[i].value) {
                    command_to_handler[j] = i;
                }
            }
        }
    }

    FLOPPY_DPRINTF("init controller\n");
    fdctrl->fifo = qemu_memalign(512, FD_SECTOR_LEN);
    memset(fdctrl->fifo, 0, FD_SECTOR_LEN);
    fdctrl->fifo_size = 512;
    fdctrl->result_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                             fdctrl_result_timer, fdctrl);

    fdctrl->version = 0x90; /* Intel 82078 controller */
    fdctrl->config = FD_CONFIG_EIS | FD_CONFIG_EFIFO; /* Implicit seek, polling & FIFO enabled */
    fdctrl->num_floppies = MAX_FD;

    floppy_bus_create(fdctrl, &fdctrl->bus, dev);

    for (i = 0; i < MAX_FD; i++) {
        drive = &fdctrl->drives[i];
        drive->fdctrl = fdctrl;
        fd_init(drive);
        fd_revalidate(drive);
    }
}

static void fdc_register_types(void)
{
    type_register_static(&floppy_bus_info);
    type_register_static(&floppy_drive_info);
}

type_init(fdc_register_types)

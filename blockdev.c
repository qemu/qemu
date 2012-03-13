/*
 * QEMU host block devices
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 */

#include "block.h"
#include "blockdev.h"
#include "monitor.h"
#include "qerror.h"
#include "qemu-option.h"
#include "qemu-config.h"
#include "qemu-objects.h"
#include "sysemu.h"
#include "block_int.h"
#include "qmp-commands.h"
#include "trace.h"
#include "arch_init.h"

static QTAILQ_HEAD(drivelist, DriveInfo) drives = QTAILQ_HEAD_INITIALIZER(drives);

static const char *const if_name[IF_COUNT] = {
    [IF_NONE] = "none",
    [IF_IDE] = "ide",
    [IF_SCSI] = "scsi",
    [IF_FLOPPY] = "floppy",
    [IF_PFLASH] = "pflash",
    [IF_MTD] = "mtd",
    [IF_SD] = "sd",
    [IF_VIRTIO] = "virtio",
    [IF_XEN] = "xen",
};

static const int if_max_devs[IF_COUNT] = {
    /*
     * Do not change these numbers!  They govern how drive option
     * index maps to unit and bus.  That mapping is ABI.
     *
     * All controllers used to imlement if=T drives need to support
     * if_max_devs[T] units, for any T with if_max_devs[T] != 0.
     * Otherwise, some index values map to "impossible" bus, unit
     * values.
     *
     * For instance, if you change [IF_SCSI] to 255, -drive
     * if=scsi,index=12 no longer means bus=1,unit=5, but
     * bus=0,unit=12.  With an lsi53c895a controller (7 units max),
     * the drive can't be set up.  Regression.
     */
    [IF_IDE] = 2,
    [IF_SCSI] = 7,
};

/*
 * We automatically delete the drive when a device using it gets
 * unplugged.  Questionable feature, but we can't just drop it.
 * Device models call blockdev_mark_auto_del() to schedule the
 * automatic deletion, and generic qdev code calls blockdev_auto_del()
 * when deletion is actually safe.
 */
void blockdev_mark_auto_del(BlockDriverState *bs)
{
    DriveInfo *dinfo = drive_get_by_blockdev(bs);

    if (dinfo) {
        dinfo->auto_del = 1;
    }
}

void blockdev_auto_del(BlockDriverState *bs)
{
    DriveInfo *dinfo = drive_get_by_blockdev(bs);

    if (dinfo && dinfo->auto_del) {
        drive_put_ref(dinfo);
    }
}

static int drive_index_to_bus_id(BlockInterfaceType type, int index)
{
    int max_devs = if_max_devs[type];
    return max_devs ? index / max_devs : 0;
}

static int drive_index_to_unit_id(BlockInterfaceType type, int index)
{
    int max_devs = if_max_devs[type];
    return max_devs ? index % max_devs : index;
}

QemuOpts *drive_def(const char *optstr)
{
    return qemu_opts_parse(qemu_find_opts("drive"), optstr, 0);
}

QemuOpts *drive_add(BlockInterfaceType type, int index, const char *file,
                    const char *optstr)
{
    QemuOpts *opts;
    char buf[32];

    opts = drive_def(optstr);
    if (!opts) {
        return NULL;
    }
    if (type != IF_DEFAULT) {
        qemu_opt_set(opts, "if", if_name[type]);
    }
    if (index >= 0) {
        snprintf(buf, sizeof(buf), "%d", index);
        qemu_opt_set(opts, "index", buf);
    }
    if (file)
        qemu_opt_set(opts, "file", file);
    return opts;
}

DriveInfo *drive_get(BlockInterfaceType type, int bus, int unit)
{
    DriveInfo *dinfo;

    /* seek interface, bus and unit */

    QTAILQ_FOREACH(dinfo, &drives, next) {
        if (dinfo->type == type &&
	    dinfo->bus == bus &&
	    dinfo->unit == unit)
            return dinfo;
    }

    return NULL;
}

DriveInfo *drive_get_by_index(BlockInterfaceType type, int index)
{
    return drive_get(type,
                     drive_index_to_bus_id(type, index),
                     drive_index_to_unit_id(type, index));
}

int drive_get_max_bus(BlockInterfaceType type)
{
    int max_bus;
    DriveInfo *dinfo;

    max_bus = -1;
    QTAILQ_FOREACH(dinfo, &drives, next) {
        if(dinfo->type == type &&
           dinfo->bus > max_bus)
            max_bus = dinfo->bus;
    }
    return max_bus;
}

/* Get a block device.  This should only be used for single-drive devices
   (e.g. SD/Floppy/MTD).  Multi-disk devices (scsi/ide) should use the
   appropriate bus.  */
DriveInfo *drive_get_next(BlockInterfaceType type)
{
    static int next_block_unit[IF_COUNT];

    return drive_get(type, 0, next_block_unit[type]++);
}

DriveInfo *drive_get_by_blockdev(BlockDriverState *bs)
{
    DriveInfo *dinfo;

    QTAILQ_FOREACH(dinfo, &drives, next) {
        if (dinfo->bdrv == bs) {
            return dinfo;
        }
    }
    return NULL;
}

static void bdrv_format_print(void *opaque, const char *name)
{
    error_printf(" %s", name);
}

static void drive_uninit(DriveInfo *dinfo)
{
    qemu_opts_del(dinfo->opts);
    bdrv_delete(dinfo->bdrv);
    g_free(dinfo->id);
    QTAILQ_REMOVE(&drives, dinfo, next);
    g_free(dinfo);
}

void drive_put_ref(DriveInfo *dinfo)
{
    assert(dinfo->refcount);
    if (--dinfo->refcount == 0) {
        drive_uninit(dinfo);
    }
}

void drive_get_ref(DriveInfo *dinfo)
{
    dinfo->refcount++;
}

typedef struct {
    QEMUBH *bh;
    DriveInfo *dinfo;
} DrivePutRefBH;

static void drive_put_ref_bh(void *opaque)
{
    DrivePutRefBH *s = opaque;

    drive_put_ref(s->dinfo);
    qemu_bh_delete(s->bh);
    g_free(s);
}

/*
 * Release a drive reference in a BH
 *
 * It is not possible to use drive_put_ref() from a callback function when the
 * callers still need the drive.  In such cases we schedule a BH to release the
 * reference.
 */
static void drive_put_ref_bh_schedule(DriveInfo *dinfo)
{
    DrivePutRefBH *s;

    s = g_new(DrivePutRefBH, 1);
    s->bh = qemu_bh_new(drive_put_ref_bh, s);
    s->dinfo = dinfo;
    qemu_bh_schedule(s->bh);
}

static int parse_block_error_action(const char *buf, int is_read)
{
    if (!strcmp(buf, "ignore")) {
        return BLOCK_ERR_IGNORE;
    } else if (!is_read && !strcmp(buf, "enospc")) {
        return BLOCK_ERR_STOP_ENOSPC;
    } else if (!strcmp(buf, "stop")) {
        return BLOCK_ERR_STOP_ANY;
    } else if (!strcmp(buf, "report")) {
        return BLOCK_ERR_REPORT;
    } else {
        error_report("'%s' invalid %s error action",
                     buf, is_read ? "read" : "write");
        return -1;
    }
}

static bool do_check_io_limits(BlockIOLimit *io_limits)
{
    bool bps_flag;
    bool iops_flag;

    assert(io_limits);

    bps_flag  = (io_limits->bps[BLOCK_IO_LIMIT_TOTAL] != 0)
                 && ((io_limits->bps[BLOCK_IO_LIMIT_READ] != 0)
                 || (io_limits->bps[BLOCK_IO_LIMIT_WRITE] != 0));
    iops_flag = (io_limits->iops[BLOCK_IO_LIMIT_TOTAL] != 0)
                 && ((io_limits->iops[BLOCK_IO_LIMIT_READ] != 0)
                 || (io_limits->iops[BLOCK_IO_LIMIT_WRITE] != 0));
    if (bps_flag || iops_flag) {
        return false;
    }

    return true;
}

DriveInfo *drive_init(QemuOpts *opts, int default_to_scsi)
{
    const char *buf;
    const char *file = NULL;
    char devname[128];
    const char *serial;
    const char *mediastr = "";
    BlockInterfaceType type;
    enum { MEDIA_DISK, MEDIA_CDROM } media;
    int bus_id, unit_id;
    int cyls, heads, secs, translation;
    BlockDriver *drv = NULL;
    int max_devs;
    int index;
    int ro = 0;
    int bdrv_flags = 0;
    int on_read_error, on_write_error;
    const char *devaddr;
    DriveInfo *dinfo;
    BlockIOLimit io_limits;
    int snapshot = 0;
    bool copy_on_read;
    int ret;

    translation = BIOS_ATA_TRANSLATION_AUTO;
    media = MEDIA_DISK;

    /* extract parameters */
    bus_id  = qemu_opt_get_number(opts, "bus", 0);
    unit_id = qemu_opt_get_number(opts, "unit", -1);
    index   = qemu_opt_get_number(opts, "index", -1);

    cyls  = qemu_opt_get_number(opts, "cyls", 0);
    heads = qemu_opt_get_number(opts, "heads", 0);
    secs  = qemu_opt_get_number(opts, "secs", 0);

    snapshot = qemu_opt_get_bool(opts, "snapshot", 0);
    ro = qemu_opt_get_bool(opts, "readonly", 0);
    copy_on_read = qemu_opt_get_bool(opts, "copy-on-read", false);

    file = qemu_opt_get(opts, "file");
    serial = qemu_opt_get(opts, "serial");

    if ((buf = qemu_opt_get(opts, "if")) != NULL) {
        pstrcpy(devname, sizeof(devname), buf);
        for (type = 0; type < IF_COUNT && strcmp(buf, if_name[type]); type++)
            ;
        if (type == IF_COUNT) {
            error_report("unsupported bus type '%s'", buf);
            return NULL;
	}
    } else {
        type = default_to_scsi ? IF_SCSI : IF_IDE;
        pstrcpy(devname, sizeof(devname), if_name[type]);
    }

    max_devs = if_max_devs[type];

    if (cyls || heads || secs) {
        if (cyls < 1 || (type == IF_IDE && cyls > 16383)) {
            error_report("invalid physical cyls number");
	    return NULL;
	}
        if (heads < 1 || (type == IF_IDE && heads > 16)) {
            error_report("invalid physical heads number");
	    return NULL;
	}
        if (secs < 1 || (type == IF_IDE && secs > 63)) {
            error_report("invalid physical secs number");
	    return NULL;
	}
    }

    if ((buf = qemu_opt_get(opts, "trans")) != NULL) {
        if (!cyls) {
            error_report("'%s' trans must be used with cyls, heads and secs",
                         buf);
            return NULL;
        }
        if (!strcmp(buf, "none"))
            translation = BIOS_ATA_TRANSLATION_NONE;
        else if (!strcmp(buf, "lba"))
            translation = BIOS_ATA_TRANSLATION_LBA;
        else if (!strcmp(buf, "auto"))
            translation = BIOS_ATA_TRANSLATION_AUTO;
	else {
            error_report("'%s' invalid translation type", buf);
	    return NULL;
	}
    }

    if ((buf = qemu_opt_get(opts, "media")) != NULL) {
        if (!strcmp(buf, "disk")) {
	    media = MEDIA_DISK;
	} else if (!strcmp(buf, "cdrom")) {
            if (cyls || secs || heads) {
                error_report("CHS can't be set with media=%s", buf);
	        return NULL;
            }
	    media = MEDIA_CDROM;
	} else {
	    error_report("'%s' invalid media", buf);
	    return NULL;
	}
    }

    if ((buf = qemu_opt_get(opts, "cache")) != NULL) {
        if (bdrv_parse_cache_flags(buf, &bdrv_flags) != 0) {
            error_report("invalid cache option");
            return NULL;
        }
    }

#ifdef CONFIG_LINUX_AIO
    if ((buf = qemu_opt_get(opts, "aio")) != NULL) {
        if (!strcmp(buf, "native")) {
            bdrv_flags |= BDRV_O_NATIVE_AIO;
        } else if (!strcmp(buf, "threads")) {
            /* this is the default */
        } else {
           error_report("invalid aio option");
           return NULL;
        }
    }
#endif

    if ((buf = qemu_opt_get(opts, "format")) != NULL) {
       if (strcmp(buf, "?") == 0) {
           error_printf("Supported formats:");
           bdrv_iterate_format(bdrv_format_print, NULL);
           error_printf("\n");
           return NULL;
        }
        drv = bdrv_find_whitelisted_format(buf);
        if (!drv) {
            error_report("'%s' invalid format", buf);
            return NULL;
        }
    }

    /* disk I/O throttling */
    io_limits.bps[BLOCK_IO_LIMIT_TOTAL]  =
                           qemu_opt_get_number(opts, "bps", 0);
    io_limits.bps[BLOCK_IO_LIMIT_READ]   =
                           qemu_opt_get_number(opts, "bps_rd", 0);
    io_limits.bps[BLOCK_IO_LIMIT_WRITE]  =
                           qemu_opt_get_number(opts, "bps_wr", 0);
    io_limits.iops[BLOCK_IO_LIMIT_TOTAL] =
                           qemu_opt_get_number(opts, "iops", 0);
    io_limits.iops[BLOCK_IO_LIMIT_READ]  =
                           qemu_opt_get_number(opts, "iops_rd", 0);
    io_limits.iops[BLOCK_IO_LIMIT_WRITE] =
                           qemu_opt_get_number(opts, "iops_wr", 0);

    if (!do_check_io_limits(&io_limits)) {
        error_report("bps(iops) and bps_rd/bps_wr(iops_rd/iops_wr) "
                     "cannot be used at the same time");
        return NULL;
    }

    on_write_error = BLOCK_ERR_STOP_ENOSPC;
    if ((buf = qemu_opt_get(opts, "werror")) != NULL) {
        if (type != IF_IDE && type != IF_SCSI && type != IF_VIRTIO && type != IF_NONE) {
            error_report("werror is not supported by this bus type");
            return NULL;
        }

        on_write_error = parse_block_error_action(buf, 0);
        if (on_write_error < 0) {
            return NULL;
        }
    }

    on_read_error = BLOCK_ERR_REPORT;
    if ((buf = qemu_opt_get(opts, "rerror")) != NULL) {
        if (type != IF_IDE && type != IF_VIRTIO && type != IF_SCSI && type != IF_NONE) {
            error_report("rerror is not supported by this bus type");
            return NULL;
        }

        on_read_error = parse_block_error_action(buf, 1);
        if (on_read_error < 0) {
            return NULL;
        }
    }

    if ((devaddr = qemu_opt_get(opts, "addr")) != NULL) {
        if (type != IF_VIRTIO) {
            error_report("addr is not supported by this bus type");
            return NULL;
        }
    }

    /* compute bus and unit according index */

    if (index != -1) {
        if (bus_id != 0 || unit_id != -1) {
            error_report("index cannot be used with bus and unit");
            return NULL;
        }
        bus_id = drive_index_to_bus_id(type, index);
        unit_id = drive_index_to_unit_id(type, index);
    }

    /* if user doesn't specify a unit_id,
     * try to find the first free
     */

    if (unit_id == -1) {
       unit_id = 0;
       while (drive_get(type, bus_id, unit_id) != NULL) {
           unit_id++;
           if (max_devs && unit_id >= max_devs) {
               unit_id -= max_devs;
               bus_id++;
           }
       }
    }

    /* check unit id */

    if (max_devs && unit_id >= max_devs) {
        error_report("unit %d too big (max is %d)",
                     unit_id, max_devs - 1);
        return NULL;
    }

    /*
     * catch multiple definitions
     */

    if (drive_get(type, bus_id, unit_id) != NULL) {
        error_report("drive with bus=%d, unit=%d (index=%d) exists",
                     bus_id, unit_id, index);
        return NULL;
    }

    /* init */

    dinfo = g_malloc0(sizeof(*dinfo));
    if ((buf = qemu_opts_id(opts)) != NULL) {
        dinfo->id = g_strdup(buf);
    } else {
        /* no id supplied -> create one */
        dinfo->id = g_malloc0(32);
        if (type == IF_IDE || type == IF_SCSI)
            mediastr = (media == MEDIA_CDROM) ? "-cd" : "-hd";
        if (max_devs)
            snprintf(dinfo->id, 32, "%s%i%s%i",
                     devname, bus_id, mediastr, unit_id);
        else
            snprintf(dinfo->id, 32, "%s%s%i",
                     devname, mediastr, unit_id);
    }
    dinfo->bdrv = bdrv_new(dinfo->id);
    dinfo->devaddr = devaddr;
    dinfo->type = type;
    dinfo->bus = bus_id;
    dinfo->unit = unit_id;
    dinfo->opts = opts;
    dinfo->refcount = 1;
    if (serial)
        strncpy(dinfo->serial, serial, sizeof(dinfo->serial) - 1);
    QTAILQ_INSERT_TAIL(&drives, dinfo, next);

    bdrv_set_on_error(dinfo->bdrv, on_read_error, on_write_error);

    /* disk I/O throttling */
    bdrv_set_io_limits(dinfo->bdrv, &io_limits);

    switch(type) {
    case IF_IDE:
    case IF_SCSI:
    case IF_XEN:
    case IF_NONE:
        switch(media) {
	case MEDIA_DISK:
            if (cyls != 0) {
                bdrv_set_geometry_hint(dinfo->bdrv, cyls, heads, secs);
                bdrv_set_translation_hint(dinfo->bdrv, translation);
            }
	    break;
	case MEDIA_CDROM:
            dinfo->media_cd = 1;
	    break;
	}
        break;
    case IF_SD:
    case IF_FLOPPY:
    case IF_PFLASH:
    case IF_MTD:
        break;
    case IF_VIRTIO:
        /* add virtio block device */
        opts = qemu_opts_create(qemu_find_opts("device"), NULL, 0);
        if (arch_type == QEMU_ARCH_S390X) {
            qemu_opt_set(opts, "driver", "virtio-blk-s390");
        } else {
            qemu_opt_set(opts, "driver", "virtio-blk-pci");
        }
        qemu_opt_set(opts, "drive", dinfo->id);
        if (devaddr)
            qemu_opt_set(opts, "addr", devaddr);
        break;
    default:
        abort();
    }
    if (!file || !*file) {
        return dinfo;
    }
    if (snapshot) {
        /* always use cache=unsafe with snapshot */
        bdrv_flags &= ~BDRV_O_CACHE_MASK;
        bdrv_flags |= (BDRV_O_SNAPSHOT|BDRV_O_CACHE_WB|BDRV_O_NO_FLUSH);
    }

    if (copy_on_read) {
        bdrv_flags |= BDRV_O_COPY_ON_READ;
    }

    if (media == MEDIA_CDROM) {
        /* CDROM is fine for any interface, don't check.  */
        ro = 1;
    } else if (ro == 1) {
        if (type != IF_SCSI && type != IF_VIRTIO && type != IF_FLOPPY &&
            type != IF_NONE && type != IF_PFLASH) {
            error_report("readonly not supported by this bus type");
            goto err;
        }
    }

    bdrv_flags |= ro ? 0 : BDRV_O_RDWR;

    ret = bdrv_open(dinfo->bdrv, file, bdrv_flags, drv);
    if (ret < 0) {
        error_report("could not open disk image %s: %s",
                     file, strerror(-ret));
        goto err;
    }

    if (bdrv_key_required(dinfo->bdrv))
        autostart = 0;
    return dinfo;

err:
    bdrv_delete(dinfo->bdrv);
    g_free(dinfo->id);
    QTAILQ_REMOVE(&drives, dinfo, next);
    g_free(dinfo);
    return NULL;
}

void do_commit(Monitor *mon, const QDict *qdict)
{
    const char *device = qdict_get_str(qdict, "device");
    BlockDriverState *bs;
    int ret;

    if (!strcmp(device, "all")) {
        ret = bdrv_commit_all();
        if (ret == -EBUSY) {
            qerror_report(QERR_DEVICE_IN_USE, device);
            return;
        }
    } else {
        bs = bdrv_find(device);
        if (!bs) {
            qerror_report(QERR_DEVICE_NOT_FOUND, device);
            return;
        }
        ret = bdrv_commit(bs);
        if (ret == -EBUSY) {
            qerror_report(QERR_DEVICE_IN_USE, device);
            return;
        }
    }
}

static void blockdev_do_action(int kind, void *data, Error **errp)
{
    BlockdevAction action;
    BlockdevActionList list;

    action.kind = kind;
    action.data = data;
    list.value = &action;
    list.next = NULL;
    qmp_transaction(&list, errp);
}

void qmp_blockdev_snapshot_sync(const char *device, const char *snapshot_file,
                                bool has_format, const char *format,
                                bool has_mode, enum NewImageMode mode,
                                Error **errp)
{
    BlockdevSnapshot snapshot = {
        .device = (char *) device,
        .snapshot_file = (char *) snapshot_file,
        .has_format = has_format,
        .format = (char *) format,
        .has_mode = has_mode,
        .mode = mode,
    };
    blockdev_do_action(BLOCKDEV_ACTION_KIND_BLOCKDEV_SNAPSHOT_SYNC, &snapshot,
                       errp);
}


/* New and old BlockDriverState structs for group snapshots */
typedef struct BlkTransactionStates {
    BlockDriverState *old_bs;
    BlockDriverState *new_bs;
    QSIMPLEQ_ENTRY(BlkTransactionStates) entry;
} BlkTransactionStates;

/*
 * 'Atomic' group snapshots.  The snapshots are taken as a set, and if any fail
 *  then we do not pivot any of the devices in the group, and abandon the
 *  snapshots
 */
void qmp_transaction(BlockdevActionList *dev_list, Error **errp)
{
    int ret = 0;
    BlockdevActionList *dev_entry = dev_list;
    BlkTransactionStates *states, *next;

    QSIMPLEQ_HEAD(snap_bdrv_states, BlkTransactionStates) snap_bdrv_states;
    QSIMPLEQ_INIT(&snap_bdrv_states);

    /* drain all i/o before any snapshots */
    bdrv_drain_all();

    /* We don't do anything in this loop that commits us to the snapshot */
    while (NULL != dev_entry) {
        BlockdevAction *dev_info = NULL;
        BlockDriver *proto_drv;
        BlockDriver *drv;
        int flags;
        enum NewImageMode mode;
        const char *new_image_file;
        const char *device;
        const char *format = "qcow2";

        dev_info = dev_entry->value;
        dev_entry = dev_entry->next;

        states = g_malloc0(sizeof(BlkTransactionStates));
        QSIMPLEQ_INSERT_TAIL(&snap_bdrv_states, states, entry);

        switch (dev_info->kind) {
        case BLOCKDEV_ACTION_KIND_BLOCKDEV_SNAPSHOT_SYNC:
            device = dev_info->blockdev_snapshot_sync->device;
            if (!dev_info->blockdev_snapshot_sync->has_mode) {
                dev_info->blockdev_snapshot_sync->mode = NEW_IMAGE_MODE_ABSOLUTE_PATHS;
            }
            new_image_file = dev_info->blockdev_snapshot_sync->snapshot_file;
            if (dev_info->blockdev_snapshot_sync->has_format) {
                format = dev_info->blockdev_snapshot_sync->format;
            }
            mode = dev_info->blockdev_snapshot_sync->mode;
            break;
        default:
            abort();
        }

        drv = bdrv_find_format(format);
        if (!drv) {
            error_set(errp, QERR_INVALID_BLOCK_FORMAT, format);
            goto delete_and_fail;
        }

        states->old_bs = bdrv_find(device);
        if (!states->old_bs) {
            error_set(errp, QERR_DEVICE_NOT_FOUND, device);
            goto delete_and_fail;
        }

        if (bdrv_in_use(states->old_bs)) {
            error_set(errp, QERR_DEVICE_IN_USE, device);
            goto delete_and_fail;
        }

        if (!bdrv_is_read_only(states->old_bs) &&
             bdrv_is_inserted(states->old_bs)) {

            if (bdrv_flush(states->old_bs)) {
                error_set(errp, QERR_IO_ERROR);
                goto delete_and_fail;
            }
        }

        flags = states->old_bs->open_flags;

        proto_drv = bdrv_find_protocol(new_image_file);
        if (!proto_drv) {
            error_set(errp, QERR_INVALID_BLOCK_FORMAT, format);
            goto delete_and_fail;
        }

        /* create new image w/backing file */
        if (mode != NEW_IMAGE_MODE_EXISTING) {
            ret = bdrv_img_create(new_image_file, format,
                                  states->old_bs->filename,
                                  states->old_bs->drv->format_name,
                                  NULL, -1, flags);
            if (ret) {
                error_set(errp, QERR_OPEN_FILE_FAILED, new_image_file);
                goto delete_and_fail;
            }
        }

        /* We will manually add the backing_hd field to the bs later */
        states->new_bs = bdrv_new("");
        ret = bdrv_open(states->new_bs, new_image_file,
                        flags | BDRV_O_NO_BACKING, drv);
        if (ret != 0) {
            error_set(errp, QERR_OPEN_FILE_FAILED, new_image_file);
            goto delete_and_fail;
        }
    }


    /* Now we are going to do the actual pivot.  Everything up to this point
     * is reversible, but we are committed at this point */
    QSIMPLEQ_FOREACH(states, &snap_bdrv_states, entry) {
        /* This removes our old bs from the bdrv_states, and adds the new bs */
        bdrv_append(states->new_bs, states->old_bs);
    }

    /* success */
    goto exit;

delete_and_fail:
    /*
    * failure, and it is all-or-none; abandon each new bs, and keep using
    * the original bs for all images
    */
    QSIMPLEQ_FOREACH(states, &snap_bdrv_states, entry) {
        if (states->new_bs) {
             bdrv_delete(states->new_bs);
        }
    }
exit:
    QSIMPLEQ_FOREACH_SAFE(states, &snap_bdrv_states, entry, next) {
        g_free(states);
    }
    return;
}


static void eject_device(BlockDriverState *bs, int force, Error **errp)
{
    if (bdrv_in_use(bs)) {
        error_set(errp, QERR_DEVICE_IN_USE, bdrv_get_device_name(bs));
        return;
    }
    if (!bdrv_dev_has_removable_media(bs)) {
        error_set(errp, QERR_DEVICE_NOT_REMOVABLE, bdrv_get_device_name(bs));
        return;
    }

    if (bdrv_dev_is_medium_locked(bs) && !bdrv_dev_is_tray_open(bs)) {
        bdrv_dev_eject_request(bs, force);
        if (!force) {
            error_set(errp, QERR_DEVICE_LOCKED, bdrv_get_device_name(bs));
            return;
        }
    }

    bdrv_close(bs);
}

void qmp_eject(const char *device, bool has_force, bool force, Error **errp)
{
    BlockDriverState *bs;

    bs = bdrv_find(device);
    if (!bs) {
        error_set(errp, QERR_DEVICE_NOT_FOUND, device);
        return;
    }

    eject_device(bs, force, errp);
}

void qmp_block_passwd(const char *device, const char *password, Error **errp)
{
    BlockDriverState *bs;
    int err;

    bs = bdrv_find(device);
    if (!bs) {
        error_set(errp, QERR_DEVICE_NOT_FOUND, device);
        return;
    }

    err = bdrv_set_key(bs, password);
    if (err == -EINVAL) {
        error_set(errp, QERR_DEVICE_NOT_ENCRYPTED, bdrv_get_device_name(bs));
        return;
    } else if (err < 0) {
        error_set(errp, QERR_INVALID_PASSWORD);
        return;
    }
}

static void qmp_bdrv_open_encrypted(BlockDriverState *bs, const char *filename,
                                    int bdrv_flags, BlockDriver *drv,
                                    const char *password, Error **errp)
{
    if (bdrv_open(bs, filename, bdrv_flags, drv) < 0) {
        error_set(errp, QERR_OPEN_FILE_FAILED, filename);
        return;
    }

    if (bdrv_key_required(bs)) {
        if (password) {
            if (bdrv_set_key(bs, password) < 0) {
                error_set(errp, QERR_INVALID_PASSWORD);
            }
        } else {
            error_set(errp, QERR_DEVICE_ENCRYPTED, bdrv_get_device_name(bs),
                      bdrv_get_encrypted_filename(bs));
        }
    } else if (password) {
        error_set(errp, QERR_DEVICE_NOT_ENCRYPTED, bdrv_get_device_name(bs));
    }
}

void qmp_change_blockdev(const char *device, const char *filename,
                         bool has_format, const char *format, Error **errp)
{
    BlockDriverState *bs;
    BlockDriver *drv = NULL;
    int bdrv_flags;
    Error *err = NULL;

    bs = bdrv_find(device);
    if (!bs) {
        error_set(errp, QERR_DEVICE_NOT_FOUND, device);
        return;
    }

    if (format) {
        drv = bdrv_find_whitelisted_format(format);
        if (!drv) {
            error_set(errp, QERR_INVALID_BLOCK_FORMAT, format);
            return;
        }
    }

    eject_device(bs, 0, &err);
    if (error_is_set(&err)) {
        error_propagate(errp, err);
        return;
    }

    bdrv_flags = bdrv_is_read_only(bs) ? 0 : BDRV_O_RDWR;
    bdrv_flags |= bdrv_is_snapshot(bs) ? BDRV_O_SNAPSHOT : 0;

    qmp_bdrv_open_encrypted(bs, filename, bdrv_flags, drv, NULL, errp);
}

/* throttling disk I/O limits */
void qmp_block_set_io_throttle(const char *device, int64_t bps, int64_t bps_rd,
                               int64_t bps_wr, int64_t iops, int64_t iops_rd,
                               int64_t iops_wr, Error **errp)
{
    BlockIOLimit io_limits;
    BlockDriverState *bs;

    bs = bdrv_find(device);
    if (!bs) {
        error_set(errp, QERR_DEVICE_NOT_FOUND, device);
        return;
    }

    io_limits.bps[BLOCK_IO_LIMIT_TOTAL] = bps;
    io_limits.bps[BLOCK_IO_LIMIT_READ]  = bps_rd;
    io_limits.bps[BLOCK_IO_LIMIT_WRITE] = bps_wr;
    io_limits.iops[BLOCK_IO_LIMIT_TOTAL]= iops;
    io_limits.iops[BLOCK_IO_LIMIT_READ] = iops_rd;
    io_limits.iops[BLOCK_IO_LIMIT_WRITE]= iops_wr;

    if (!do_check_io_limits(&io_limits)) {
        error_set(errp, QERR_INVALID_PARAMETER_COMBINATION);
        return;
    }

    bs->io_limits = io_limits;
    bs->slice_time = BLOCK_IO_SLICE_TIME;

    if (!bs->io_limits_enabled && bdrv_io_limits_enabled(bs)) {
        bdrv_io_limits_enable(bs);
    } else if (bs->io_limits_enabled && !bdrv_io_limits_enabled(bs)) {
        bdrv_io_limits_disable(bs);
    } else {
        if (bs->block_timer) {
            qemu_mod_timer(bs->block_timer, qemu_get_clock_ns(vm_clock));
        }
    }
}

int do_drive_del(Monitor *mon, const QDict *qdict, QObject **ret_data)
{
    const char *id = qdict_get_str(qdict, "id");
    BlockDriverState *bs;

    bs = bdrv_find(id);
    if (!bs) {
        qerror_report(QERR_DEVICE_NOT_FOUND, id);
        return -1;
    }
    if (bdrv_in_use(bs)) {
        qerror_report(QERR_DEVICE_IN_USE, id);
        return -1;
    }

    /* quiesce block driver; prevent further io */
    bdrv_drain_all();
    bdrv_flush(bs);
    bdrv_close(bs);

    /* if we have a device attached to this BlockDriverState
     * then we need to make the drive anonymous until the device
     * can be removed.  If this is a drive with no device backing
     * then we can just get rid of the block driver state right here.
     */
    if (bdrv_get_attached_dev(bs)) {
        bdrv_make_anon(bs);
    } else {
        drive_uninit(drive_get_by_blockdev(bs));
    }

    return 0;
}

void qmp_block_resize(const char *device, int64_t size, Error **errp)
{
    BlockDriverState *bs;

    bs = bdrv_find(device);
    if (!bs) {
        error_set(errp, QERR_DEVICE_NOT_FOUND, device);
        return;
    }

    if (size < 0) {
        error_set(errp, QERR_INVALID_PARAMETER_VALUE, "size", "a >0 size");
        return;
    }

    switch (bdrv_truncate(bs, size)) {
    case 0:
        break;
    case -ENOMEDIUM:
        error_set(errp, QERR_DEVICE_HAS_NO_MEDIUM, device);
        break;
    case -ENOTSUP:
        error_set(errp, QERR_UNSUPPORTED);
        break;
    case -EACCES:
        error_set(errp, QERR_DEVICE_IS_READ_ONLY, device);
        break;
    case -EBUSY:
        error_set(errp, QERR_DEVICE_IN_USE, device);
        break;
    default:
        error_set(errp, QERR_UNDEFINED_ERROR);
        break;
    }
}

static QObject *qobject_from_block_job(BlockJob *job)
{
    return qobject_from_jsonf("{ 'type': %s,"
                              "'device': %s,"
                              "'len': %" PRId64 ","
                              "'offset': %" PRId64 ","
                              "'speed': %" PRId64 " }",
                              job->job_type->job_type,
                              bdrv_get_device_name(job->bs),
                              job->len,
                              job->offset,
                              job->speed);
}

static void block_stream_cb(void *opaque, int ret)
{
    BlockDriverState *bs = opaque;
    QObject *obj;

    trace_block_stream_cb(bs, bs->job, ret);

    assert(bs->job);
    obj = qobject_from_block_job(bs->job);
    if (ret < 0) {
        QDict *dict = qobject_to_qdict(obj);
        qdict_put(dict, "error", qstring_from_str(strerror(-ret)));
    }

    if (block_job_is_cancelled(bs->job)) {
        monitor_protocol_event(QEVENT_BLOCK_JOB_CANCELLED, obj);
    } else {
        monitor_protocol_event(QEVENT_BLOCK_JOB_COMPLETED, obj);
    }
    qobject_decref(obj);

    drive_put_ref_bh_schedule(drive_get_by_blockdev(bs));
}

void qmp_block_stream(const char *device, bool has_base,
                      const char *base, Error **errp)
{
    BlockDriverState *bs;
    BlockDriverState *base_bs = NULL;
    int ret;

    bs = bdrv_find(device);
    if (!bs) {
        error_set(errp, QERR_DEVICE_NOT_FOUND, device);
        return;
    }

    if (base) {
        base_bs = bdrv_find_backing_image(bs, base);
        if (base_bs == NULL) {
            error_set(errp, QERR_BASE_NOT_FOUND, base);
            return;
        }
    }

    ret = stream_start(bs, base_bs, base, block_stream_cb, bs);
    if (ret < 0) {
        switch (ret) {
        case -EBUSY:
            error_set(errp, QERR_DEVICE_IN_USE, device);
            return;
        default:
            error_set(errp, QERR_NOT_SUPPORTED);
            return;
        }
    }

    /* Grab a reference so hotplug does not delete the BlockDriverState from
     * underneath us.
     */
    drive_get_ref(drive_get_by_blockdev(bs));

    trace_qmp_block_stream(bs, bs->job);
}

static BlockJob *find_block_job(const char *device)
{
    BlockDriverState *bs;

    bs = bdrv_find(device);
    if (!bs || !bs->job) {
        return NULL;
    }
    return bs->job;
}

void qmp_block_job_set_speed(const char *device, int64_t value, Error **errp)
{
    BlockJob *job = find_block_job(device);

    if (!job) {
        error_set(errp, QERR_DEVICE_NOT_ACTIVE, device);
        return;
    }

    if (block_job_set_speed(job, value) < 0) {
        error_set(errp, QERR_NOT_SUPPORTED);
    }
}

void qmp_block_job_cancel(const char *device, Error **errp)
{
    BlockJob *job = find_block_job(device);

    if (!job) {
        error_set(errp, QERR_DEVICE_NOT_ACTIVE, device);
        return;
    }

    trace_qmp_block_job_cancel(job);
    block_job_cancel(job);
}

static void do_qmp_query_block_jobs_one(void *opaque, BlockDriverState *bs)
{
    BlockJobInfoList **prev = opaque;
    BlockJob *job = bs->job;

    if (job) {
        BlockJobInfoList *elem;
        BlockJobInfo *info = g_new(BlockJobInfo, 1);
        *info = (BlockJobInfo){
            .type   = g_strdup(job->job_type->job_type),
            .device = g_strdup(bdrv_get_device_name(bs)),
            .len    = job->len,
            .offset = job->offset,
            .speed  = job->speed,
        };

        elem = g_new0(BlockJobInfoList, 1);
        elem->value = info;

        (*prev)->next = elem;
        *prev = elem;
    }
}

BlockJobInfoList *qmp_query_block_jobs(Error **errp)
{
    /* Dummy is a fake list element for holding the head pointer */
    BlockJobInfoList dummy = {};
    BlockJobInfoList *prev = &dummy;
    bdrv_iterate(do_qmp_query_block_jobs_one, &prev);
    return dummy.next;
}

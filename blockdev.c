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
#include "sysemu.h"

static QTAILQ_HEAD(drivelist, DriveInfo) drives = QTAILQ_HEAD_INITIALIZER(drives);

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

    dinfo->auto_del = 1;
}

void blockdev_auto_del(BlockDriverState *bs)
{
    DriveInfo *dinfo = drive_get_by_blockdev(bs);

    if (dinfo->auto_del) {
        drive_uninit(dinfo);
    }
}

QemuOpts *drive_add(const char *file, const char *fmt, ...)
{
    va_list ap;
    char optstr[1024];
    QemuOpts *opts;

    va_start(ap, fmt);
    vsnprintf(optstr, sizeof(optstr), fmt, ap);
    va_end(ap);

    opts = qemu_opts_parse(qemu_find_opts("drive"), optstr, 0);
    if (!opts) {
        return NULL;
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
    fprintf(stderr, " %s", name);
}

void drive_uninit(DriveInfo *dinfo)
{
    qemu_opts_del(dinfo->opts);
    bdrv_delete(dinfo->bdrv);
    QTAILQ_REMOVE(&drives, dinfo, next);
    qemu_free(dinfo);
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
        fprintf(stderr, "qemu: '%s' invalid %s error action\n",
            buf, is_read ? "read" : "write");
        return -1;
    }
}

DriveInfo *drive_init(QemuOpts *opts, int default_to_scsi, int *fatal_error)
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
    int snapshot = 0;
    int ret;

    *fatal_error = 1;

    translation = BIOS_ATA_TRANSLATION_AUTO;

    if (default_to_scsi) {
        type = IF_SCSI;
        max_devs = MAX_SCSI_DEVS;
        pstrcpy(devname, sizeof(devname), "scsi");
    } else {
        type = IF_IDE;
        max_devs = MAX_IDE_DEVS;
        pstrcpy(devname, sizeof(devname), "ide");
    }
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

    file = qemu_opt_get(opts, "file");
    serial = qemu_opt_get(opts, "serial");

    if ((buf = qemu_opt_get(opts, "if")) != NULL) {
        pstrcpy(devname, sizeof(devname), buf);
        if (!strcmp(buf, "ide")) {
	    type = IF_IDE;
            max_devs = MAX_IDE_DEVS;
        } else if (!strcmp(buf, "scsi")) {
	    type = IF_SCSI;
            max_devs = MAX_SCSI_DEVS;
        } else if (!strcmp(buf, "floppy")) {
	    type = IF_FLOPPY;
            max_devs = 0;
        } else if (!strcmp(buf, "pflash")) {
	    type = IF_PFLASH;
            max_devs = 0;
	} else if (!strcmp(buf, "mtd")) {
	    type = IF_MTD;
            max_devs = 0;
	} else if (!strcmp(buf, "sd")) {
	    type = IF_SD;
            max_devs = 0;
        } else if (!strcmp(buf, "virtio")) {
            type = IF_VIRTIO;
            max_devs = 0;
	} else if (!strcmp(buf, "xen")) {
	    type = IF_XEN;
            max_devs = 0;
	} else if (!strcmp(buf, "none")) {
	    type = IF_NONE;
            max_devs = 0;
	} else {
            fprintf(stderr, "qemu: unsupported bus type '%s'\n", buf);
            return NULL;
	}
    }

    if (cyls || heads || secs) {
        if (cyls < 1 || (type == IF_IDE && cyls > 16383)) {
            fprintf(stderr, "qemu: '%s' invalid physical cyls number\n", buf);
	    return NULL;
	}
        if (heads < 1 || (type == IF_IDE && heads > 16)) {
            fprintf(stderr, "qemu: '%s' invalid physical heads number\n", buf);
	    return NULL;
	}
        if (secs < 1 || (type == IF_IDE && secs > 63)) {
            fprintf(stderr, "qemu: '%s' invalid physical secs number\n", buf);
	    return NULL;
	}
    }

    if ((buf = qemu_opt_get(opts, "trans")) != NULL) {
        if (!cyls) {
            fprintf(stderr,
                    "qemu: '%s' trans must be used with cyls,heads and secs\n",
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
            fprintf(stderr, "qemu: '%s' invalid translation type\n", buf);
	    return NULL;
	}
    }

    if ((buf = qemu_opt_get(opts, "media")) != NULL) {
        if (!strcmp(buf, "disk")) {
	    media = MEDIA_DISK;
	} else if (!strcmp(buf, "cdrom")) {
            if (cyls || secs || heads) {
                fprintf(stderr,
                        "qemu: '%s' invalid physical CHS format\n", buf);
	        return NULL;
            }
	    media = MEDIA_CDROM;
	} else {
	    fprintf(stderr, "qemu: '%s' invalid media\n", buf);
	    return NULL;
	}
    }

    if ((buf = qemu_opt_get(opts, "cache")) != NULL) {
        if (!strcmp(buf, "off") || !strcmp(buf, "none")) {
            bdrv_flags |= BDRV_O_NOCACHE;
        } else if (!strcmp(buf, "writeback")) {
            bdrv_flags |= BDRV_O_CACHE_WB;
        } else if (!strcmp(buf, "unsafe")) {
            bdrv_flags |= BDRV_O_CACHE_WB;
            bdrv_flags |= BDRV_O_NO_FLUSH;
        } else if (!strcmp(buf, "writethrough")) {
            /* this is the default */
        } else {
           fprintf(stderr, "qemu: invalid cache option\n");
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
           fprintf(stderr, "qemu: invalid aio option\n");
           return NULL;
        }
    }
#endif

    if ((buf = qemu_opt_get(opts, "format")) != NULL) {
       if (strcmp(buf, "?") == 0) {
            fprintf(stderr, "qemu: Supported formats:");
            bdrv_iterate_format(bdrv_format_print, NULL);
            fprintf(stderr, "\n");
	    return NULL;
        }
        drv = bdrv_find_whitelisted_format(buf);
        if (!drv) {
            fprintf(stderr, "qemu: '%s' invalid format\n", buf);
            return NULL;
        }
    }

    on_write_error = BLOCK_ERR_STOP_ENOSPC;
    if ((buf = qemu_opt_get(opts, "werror")) != NULL) {
        if (type != IF_IDE && type != IF_SCSI && type != IF_VIRTIO && type != IF_NONE) {
            fprintf(stderr, "werror is no supported by this format\n");
            return NULL;
        }

        on_write_error = parse_block_error_action(buf, 0);
        if (on_write_error < 0) {
            return NULL;
        }
    }

    on_read_error = BLOCK_ERR_REPORT;
    if ((buf = qemu_opt_get(opts, "rerror")) != NULL) {
        if (type != IF_IDE && type != IF_VIRTIO && type != IF_NONE) {
            fprintf(stderr, "rerror is no supported by this format\n");
            return NULL;
        }

        on_read_error = parse_block_error_action(buf, 1);
        if (on_read_error < 0) {
            return NULL;
        }
    }

    if ((devaddr = qemu_opt_get(opts, "addr")) != NULL) {
        if (type != IF_VIRTIO) {
            fprintf(stderr, "addr is not supported\n");
            return NULL;
        }
    }

    /* compute bus and unit according index */

    if (index != -1) {
        if (bus_id != 0 || unit_id != -1) {
            fprintf(stderr,
                    "qemu: index cannot be used with bus and unit\n");
            return NULL;
        }
        if (max_devs == 0)
        {
            unit_id = index;
            bus_id = 0;
        } else {
            unit_id = index % max_devs;
            bus_id = index / max_devs;
        }
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
        fprintf(stderr, "qemu: unit %d too big (max is %d)\n",
                unit_id, max_devs - 1);
        return NULL;
    }

    /*
     * ignore multiple definitions
     */

    if (drive_get(type, bus_id, unit_id) != NULL) {
        *fatal_error = 0;
        return NULL;
    }

    /* init */

    dinfo = qemu_mallocz(sizeof(*dinfo));
    if ((buf = qemu_opts_id(opts)) != NULL) {
        dinfo->id = qemu_strdup(buf);
    } else {
        /* no id supplied -> create one */
        dinfo->id = qemu_mallocz(32);
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
    if (serial)
        strncpy(dinfo->serial, serial, sizeof(dinfo->serial) - 1);
    QTAILQ_INSERT_TAIL(&drives, dinfo, next);

    bdrv_set_on_error(dinfo->bdrv, on_read_error, on_write_error);

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
            bdrv_set_type_hint(dinfo->bdrv, BDRV_TYPE_CDROM);
	    break;
	}
        break;
    case IF_SD:
        /* FIXME: This isn't really a floppy, but it's a reasonable
           approximation.  */
    case IF_FLOPPY:
        bdrv_set_type_hint(dinfo->bdrv, BDRV_TYPE_FLOPPY);
        break;
    case IF_PFLASH:
    case IF_MTD:
        break;
    case IF_VIRTIO:
        /* add virtio block device */
        opts = qemu_opts_create(qemu_find_opts("device"), NULL, 0);
        qemu_opt_set(opts, "driver", "virtio-blk-pci");
        qemu_opt_set(opts, "drive", dinfo->id);
        if (devaddr)
            qemu_opt_set(opts, "addr", devaddr);
        break;
    case IF_COUNT:
        abort();
    }
    if (!file || !*file) {
        *fatal_error = 0;
        return NULL;
    }
    if (snapshot) {
        /* always use cache=unsafe with snapshot */
        bdrv_flags &= ~BDRV_O_CACHE_MASK;
        bdrv_flags |= (BDRV_O_SNAPSHOT|BDRV_O_CACHE_WB|BDRV_O_NO_FLUSH);
    }

    if (media == MEDIA_CDROM) {
        /* CDROM is fine for any interface, don't check.  */
        ro = 1;
    } else if (ro == 1) {
        if (type != IF_SCSI && type != IF_VIRTIO && type != IF_FLOPPY && type != IF_NONE) {
            fprintf(stderr, "qemu: readonly flag not supported for drive with this interface\n");
            return NULL;
        }
    }

    bdrv_flags |= ro ? 0 : BDRV_O_RDWR;

    ret = bdrv_open(dinfo->bdrv, file, bdrv_flags, drv);
    if (ret < 0) {
        fprintf(stderr, "qemu: could not open disk image %s: %s\n",
                        file, strerror(-ret));
        return NULL;
    }

    if (bdrv_key_required(dinfo->bdrv))
        autostart = 0;
    *fatal_error = 0;
    return dinfo;
}

void do_commit(Monitor *mon, const QDict *qdict)
{
    const char *device = qdict_get_str(qdict, "device");
    BlockDriverState *bs;

    if (!strcmp(device, "all")) {
        bdrv_commit_all();
    } else {
        bs = bdrv_find(device);
        if (!bs) {
            qerror_report(QERR_DEVICE_NOT_FOUND, device);
            return;
        }
        bdrv_commit(bs);
    }
}

static int eject_device(Monitor *mon, BlockDriverState *bs, int force)
{
    if (!force) {
        if (!bdrv_is_removable(bs)) {
            qerror_report(QERR_DEVICE_NOT_REMOVABLE,
                           bdrv_get_device_name(bs));
            return -1;
        }
        if (bdrv_is_locked(bs)) {
            qerror_report(QERR_DEVICE_LOCKED, bdrv_get_device_name(bs));
            return -1;
        }
    }
    bdrv_close(bs);
    return 0;
}

int do_eject(Monitor *mon, const QDict *qdict, QObject **ret_data)
{
    BlockDriverState *bs;
    int force = qdict_get_try_bool(qdict, "force", 0);
    const char *filename = qdict_get_str(qdict, "device");

    bs = bdrv_find(filename);
    if (!bs) {
        qerror_report(QERR_DEVICE_NOT_FOUND, filename);
        return -1;
    }
    return eject_device(mon, bs, force);
}

int do_block_set_passwd(Monitor *mon, const QDict *qdict,
                        QObject **ret_data)
{
    BlockDriverState *bs;
    int err;

    bs = bdrv_find(qdict_get_str(qdict, "device"));
    if (!bs) {
        qerror_report(QERR_DEVICE_NOT_FOUND, qdict_get_str(qdict, "device"));
        return -1;
    }

    err = bdrv_set_key(bs, qdict_get_str(qdict, "password"));
    if (err == -EINVAL) {
        qerror_report(QERR_DEVICE_NOT_ENCRYPTED, bdrv_get_device_name(bs));
        return -1;
    } else if (err < 0) {
        qerror_report(QERR_INVALID_PASSWORD);
        return -1;
    }

    return 0;
}

int do_change_block(Monitor *mon, const char *device,
                    const char *filename, const char *fmt)
{
    BlockDriverState *bs;
    BlockDriver *drv = NULL;
    int bdrv_flags;

    bs = bdrv_find(device);
    if (!bs) {
        qerror_report(QERR_DEVICE_NOT_FOUND, device);
        return -1;
    }
    if (fmt) {
        drv = bdrv_find_whitelisted_format(fmt);
        if (!drv) {
            qerror_report(QERR_INVALID_BLOCK_FORMAT, fmt);
            return -1;
        }
    }
    if (eject_device(mon, bs, 0) < 0) {
        return -1;
    }
    bdrv_flags = bdrv_is_read_only(bs) ? 0 : BDRV_O_RDWR;
    bdrv_flags |= bdrv_is_snapshot(bs) ? BDRV_O_SNAPSHOT : 0;
    if (bdrv_open(bs, filename, bdrv_flags, drv) < 0) {
        qerror_report(QERR_OPEN_FILE_FAILED, filename);
        return -1;
    }
    return monitor_read_bdrv_key_start(mon, bs, NULL, NULL);
}

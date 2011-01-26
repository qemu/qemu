/*
 * QEMU host block devices
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 */

#ifndef BLOCKDEV_H
#define BLOCKDEV_H

#include "block.h"
#include "qemu-queue.h"

void blockdev_mark_auto_del(BlockDriverState *bs);
void blockdev_auto_del(BlockDriverState *bs);

#define BLOCK_SERIAL_STRLEN 20

typedef enum {
    IF_DEFAULT = -1,            /* for use with drive_add() only */
    IF_NONE,
    IF_IDE, IF_SCSI, IF_FLOPPY, IF_PFLASH, IF_MTD, IF_SD, IF_VIRTIO, IF_XEN,
    IF_COUNT
} BlockInterfaceType;

struct DriveInfo {
    BlockDriverState *bdrv;
    char *id;
    const char *devaddr;
    BlockInterfaceType type;
    int bus;
    int unit;
    int auto_del;               /* see blockdev_mark_auto_del() */
    QemuOpts *opts;
    char serial[BLOCK_SERIAL_STRLEN + 1];
    QTAILQ_ENTRY(DriveInfo) next;
    int refcount;
};

DriveInfo *drive_get(BlockInterfaceType type, int bus, int unit);
DriveInfo *drive_get_by_index(BlockInterfaceType type, int index);
int drive_get_max_bus(BlockInterfaceType type);
DriveInfo *drive_get_next(BlockInterfaceType type);
void drive_get_ref(DriveInfo *dinfo);
void drive_put_ref(DriveInfo *dinfo);
DriveInfo *drive_get_by_blockdev(BlockDriverState *bs);

QemuOpts *drive_def(const char *optstr);
QemuOpts *drive_add(BlockInterfaceType type, int index, const char *file,
                    const char *optstr);
DriveInfo *drive_init(QemuOpts *arg, int default_to_scsi);

/* device-hotplug */

DriveInfo *add_init_drive(const char *opts);

void do_commit(Monitor *mon, const QDict *qdict);
int do_eject(Monitor *mon, const QDict *qdict, QObject **ret_data);
int do_block_set_passwd(Monitor *mon, const QDict *qdict, QObject **ret_data);
int do_change_block(Monitor *mon, const char *device,
                    const char *filename, const char *fmt);
int do_drive_del(Monitor *mon, const QDict *qdict, QObject **ret_data);
int do_snapshot_blkdev(Monitor *mon, const QDict *qdict, QObject **ret_data);
int do_block_resize(Monitor *mon, const QDict *qdict, QObject **ret_data);

#endif

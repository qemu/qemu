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

#include "block/block.h"
#include "qemu/queue.h"

void blockdev_mark_auto_del(BlockBackend *blk);
void blockdev_auto_del(BlockBackend *blk);

typedef enum {
    IF_DEFAULT = -1,            /* for use with drive_add() only */
    /*
     * IF_NONE must be zero, because we want MachineClass member
     * block_default_type to default-initialize to IF_NONE
     */
    IF_NONE = 0,
    IF_IDE, IF_SCSI, IF_FLOPPY, IF_PFLASH, IF_MTD, IF_SD, IF_VIRTIO, IF_XEN,
    IF_COUNT
} BlockInterfaceType;

struct DriveInfo {
    BlockInterfaceType type;
    int bus;
    int unit;
    int auto_del;               /* see blockdev_mark_auto_del() */
    bool is_default;            /* Added by default_drive() ?  */
    int media_cd;
    QemuOpts *opts;
    QTAILQ_ENTRY(DriveInfo) next;
};

DriveInfo *blk_legacy_dinfo(BlockBackend *blk);
DriveInfo *blk_set_legacy_dinfo(BlockBackend *blk, DriveInfo *dinfo);
BlockBackend *blk_by_legacy_dinfo(DriveInfo *dinfo);

void override_max_devs(BlockInterfaceType type, int max_devs);

DriveInfo *drive_get(BlockInterfaceType type, int bus, int unit);
void drive_mark_claimed_by_board(void);
void drive_check_orphaned(void);
DriveInfo *drive_get_by_index(BlockInterfaceType type, int index);
int drive_get_max_bus(BlockInterfaceType type);
int drive_get_max_devs(BlockInterfaceType type);
DriveInfo *drive_get_next(BlockInterfaceType type);

QemuOpts *drive_def(const char *optstr);
QemuOpts *drive_add(BlockInterfaceType type, int index, const char *file,
                    const char *optstr);
DriveInfo *drive_new(QemuOpts *arg, BlockInterfaceType block_default_type,
                     Error **errp);

#endif

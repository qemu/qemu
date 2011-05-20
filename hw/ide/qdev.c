/*
 * ide bus support for qdev.
 *
 * Copyright (c) 2009 Gerd Hoffmann <kraxel@redhat.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */
#include <hw/hw.h>
#include "dma.h"
#include "qemu-error.h"
#include <hw/ide/internal.h>
#include "blockdev.h"
#include "sysemu.h"

/* --------------------------------- */

static char *idebus_get_fw_dev_path(DeviceState *dev);

static struct BusInfo ide_bus_info = {
    .name  = "IDE",
    .size  = sizeof(IDEBus),
    .get_fw_dev_path = idebus_get_fw_dev_path,
};

void ide_bus_new(IDEBus *idebus, DeviceState *dev, int bus_id)
{
    qbus_create_inplace(&idebus->qbus, &ide_bus_info, dev, NULL);
    idebus->bus_id = bus_id;
}

static char *idebus_get_fw_dev_path(DeviceState *dev)
{
    char path[30];

    snprintf(path, sizeof(path), "%s@%d", qdev_fw_name(dev),
             ((IDEBus*)dev->parent_bus)->bus_id);

    return strdup(path);
}

static int ide_qdev_init(DeviceState *qdev, DeviceInfo *base)
{
    IDEDevice *dev = DO_UPCAST(IDEDevice, qdev, qdev);
    IDEDeviceInfo *info = DO_UPCAST(IDEDeviceInfo, qdev, base);
    IDEBus *bus = DO_UPCAST(IDEBus, qbus, qdev->parent_bus);

    if (!dev->conf.bs) {
        error_report("No drive specified");
        goto err;
    }
    if (dev->unit == -1) {
        dev->unit = bus->master ? 1 : 0;
    }
    switch (dev->unit) {
    case 0:
        if (bus->master) {
            error_report("IDE unit %d is in use", dev->unit);
            goto err;
        }
        bus->master = dev;
        break;
    case 1:
        if (bus->slave) {
            error_report("IDE unit %d is in use", dev->unit);
            goto err;
        }
        bus->slave = dev;
        break;
    default:
        error_report("Invalid IDE unit %d", dev->unit);
        goto err;
    }
    return info->init(dev);

err:
    return -1;
}

static void ide_qdev_register(IDEDeviceInfo *info)
{
    info->qdev.init = ide_qdev_init;
    info->qdev.bus_info = &ide_bus_info;
    qdev_register(&info->qdev);
}

IDEDevice *ide_create_drive(IDEBus *bus, int unit, DriveInfo *drive)
{
    DeviceState *dev;

    dev = qdev_create(&bus->qbus, drive->media_cd ? "ide-cd" : "ide-hd");
    qdev_prop_set_uint32(dev, "unit", unit);
    qdev_prop_set_drive_nofail(dev, "drive", drive->bdrv);
    qdev_init_nofail(dev);
    return DO_UPCAST(IDEDevice, qdev, dev);
}

void ide_get_bs(BlockDriverState *bs[], BusState *qbus)
{
    IDEBus *bus = DO_UPCAST(IDEBus, qbus, qbus);
    bs[0] = bus->master ? bus->master->conf.bs : NULL;
    bs[1] = bus->slave  ? bus->slave->conf.bs  : NULL;
}

/* --------------------------------- */

typedef struct IDEDrive {
    IDEDevice dev;
} IDEDrive;

static int ide_dev_initfn(IDEDevice *dev, IDEDriveKind kind)
{
    IDEBus *bus = DO_UPCAST(IDEBus, qbus, dev->qdev.parent_bus);
    IDEState *s = bus->ifs + dev->unit;
    const char *serial;
    DriveInfo *dinfo;

    serial = dev->serial;
    if (!serial) {
        /* try to fall back to value set with legacy -drive serial=... */
        dinfo = drive_get_by_blockdev(dev->conf.bs);
        if (*dinfo->serial) {
            serial = dinfo->serial;
        }
    }

    if (ide_init_drive(s, dev->conf.bs, kind, dev->version, serial) < 0) {
        return -1;
    }

    if (!dev->version) {
        dev->version = qemu_strdup(s->version);
    }
    if (!dev->serial) {
        dev->serial = qemu_strdup(s->drive_serial_str);
    }

    add_boot_device_path(dev->conf.bootindex, &dev->qdev,
                         dev->unit ? "/disk@1" : "/disk@0");

    return 0;
}

static int ide_hd_initfn(IDEDevice *dev)
{
    return ide_dev_initfn(dev, IDE_HD);
}

static int ide_cd_initfn(IDEDevice *dev)
{
    return ide_dev_initfn(dev, IDE_CD);
}

static int ide_drive_initfn(IDEDevice *dev)
{
    DriveInfo *dinfo = drive_get_by_blockdev(dev->conf.bs);

    return ide_dev_initfn(dev, dinfo->media_cd ? IDE_CD : IDE_HD);
}

#define DEFINE_IDE_DEV_PROPERTIES()                     \
    DEFINE_PROP_UINT32("unit", IDEDrive, dev.unit, -1), \
    DEFINE_BLOCK_PROPERTIES(IDEDrive, dev.conf),        \
    DEFINE_PROP_STRING("ver",  IDEDrive, dev.version),  \
    DEFINE_PROP_STRING("serial",  IDEDrive, dev.serial)

static IDEDeviceInfo ide_dev_info[] = {
    {
        .qdev.name    = "ide-hd",
        .qdev.fw_name = "drive",
        .qdev.desc    = "virtual IDE disk",
        .qdev.size    = sizeof(IDEDrive),
        .init         = ide_hd_initfn,
        .qdev.props   = (Property[]) {
            DEFINE_IDE_DEV_PROPERTIES(),
            DEFINE_PROP_END_OF_LIST(),
        }
    },{
        .qdev.name    = "ide-cd",
        .qdev.fw_name = "drive",
        .qdev.desc    = "virtual IDE CD-ROM",
        .qdev.size    = sizeof(IDEDrive),
        .init         = ide_cd_initfn,
        .qdev.props   = (Property[]) {
            DEFINE_IDE_DEV_PROPERTIES(),
            DEFINE_PROP_END_OF_LIST(),
        }
    },{
        .qdev.name    = "ide-drive", /* legacy -device ide-drive */
        .qdev.fw_name = "drive",
        .qdev.desc    = "virtual IDE disk or CD-ROM (legacy)",
        .qdev.size    = sizeof(IDEDrive),
        .init         = ide_drive_initfn,
        .qdev.props   = (Property[]) {
            DEFINE_IDE_DEV_PROPERTIES(),
            DEFINE_PROP_END_OF_LIST(),
        }
    }
};

static void ide_dev_register(void)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(ide_dev_info); i++) {
        ide_qdev_register(&ide_dev_info[i]);
    }
}
device_init(ide_dev_register);

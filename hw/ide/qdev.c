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
#include "sysemu/dma.h"
#include "qemu/error-report.h"
#include <hw/ide/internal.h>
#include "sysemu/blockdev.h"
#include "hw/block/block.h"
#include "sysemu/sysemu.h"

/* --------------------------------- */

static char *idebus_get_fw_dev_path(DeviceState *dev);

static Property ide_props[] = {
    DEFINE_PROP_UINT32("unit", IDEDevice, unit, -1),
    DEFINE_PROP_END_OF_LIST(),
};

static void ide_bus_class_init(ObjectClass *klass, void *data)
{
    BusClass *k = BUS_CLASS(klass);

    k->get_fw_dev_path = idebus_get_fw_dev_path;
}

static const TypeInfo ide_bus_info = {
    .name = TYPE_IDE_BUS,
    .parent = TYPE_BUS,
    .instance_size = sizeof(IDEBus),
    .class_init = ide_bus_class_init,
};

void ide_bus_new(IDEBus *idebus, size_t idebus_size, DeviceState *dev,
                 int bus_id, int max_units)
{
    qbus_create_inplace(idebus, idebus_size, TYPE_IDE_BUS, dev, NULL);
    idebus->bus_id = bus_id;
    idebus->max_units = max_units;
}

static char *idebus_get_fw_dev_path(DeviceState *dev)
{
    char path[30];

    snprintf(path, sizeof(path), "%s@%x", qdev_fw_name(dev),
             ((IDEBus*)dev->parent_bus)->bus_id);

    return g_strdup(path);
}

static int ide_qdev_init(DeviceState *qdev)
{
    IDEDevice *dev = IDE_DEVICE(qdev);
    IDEDeviceClass *dc = IDE_DEVICE_GET_CLASS(dev);
    IDEBus *bus = DO_UPCAST(IDEBus, qbus, qdev->parent_bus);

    if (!dev->conf.bs) {
        error_report("No drive specified");
        goto err;
    }
    if (dev->unit == -1) {
        dev->unit = bus->master ? 1 : 0;
    }

    if (dev->unit >= bus->max_units) {
        error_report("Can't create IDE unit %d, bus supports only %d units",
                     dev->unit, bus->max_units);
        goto err;
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
    return dc->init(dev);

err:
    return -1;
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

int ide_get_geometry(BusState *bus, int unit,
                     int16_t *cyls, int8_t *heads, int8_t *secs)
{
    IDEState *s = &DO_UPCAST(IDEBus, qbus, bus)->ifs[unit];

    if (s->drive_kind != IDE_HD || !s->bs) {
        return -1;
    }

    *cyls = s->cylinders;
    *heads = s->heads;
    *secs = s->sectors;
    return 0;
}

int ide_get_bios_chs_trans(BusState *bus, int unit)
{
    return DO_UPCAST(IDEBus, qbus, bus)->ifs[unit].chs_trans;
}

/* --------------------------------- */

typedef struct IDEDrive {
    IDEDevice dev;
} IDEDrive;

static int ide_dev_initfn(IDEDevice *dev, IDEDriveKind kind)
{
    IDEBus *bus = DO_UPCAST(IDEBus, qbus, dev->qdev.parent_bus);
    IDEState *s = bus->ifs + dev->unit;
    Error *err = NULL;

    if (dev->conf.discard_granularity == -1) {
        dev->conf.discard_granularity = 512;
    } else if (dev->conf.discard_granularity &&
               dev->conf.discard_granularity != 512) {
        error_report("discard_granularity must be 512 for ide");
        return -1;
    }

    blkconf_serial(&dev->conf, &dev->serial);
    if (kind != IDE_CD) {
        blkconf_geometry(&dev->conf, &dev->chs_trans, 65536, 16, 255, &err);
        if (err) {
            error_report("%s", error_get_pretty(err));
            error_free(err);
            return -1;
        }
    }

    if (ide_init_drive(s, dev->conf.bs, kind,
                       dev->version, dev->serial, dev->model, dev->wwn,
                       dev->conf.cyls, dev->conf.heads, dev->conf.secs,
                       dev->chs_trans) < 0) {
        return -1;
    }

    if (!dev->version) {
        dev->version = g_strdup(s->version);
    }
    if (!dev->serial) {
        dev->serial = g_strdup(s->drive_serial_str);
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
    DEFINE_BLOCK_PROPERTIES(IDEDrive, dev.conf),        \
    DEFINE_PROP_STRING("ver",  IDEDrive, dev.version),  \
    DEFINE_PROP_UINT64("wwn",  IDEDrive, dev.wwn, 0),    \
    DEFINE_PROP_STRING("serial",  IDEDrive, dev.serial),\
    DEFINE_PROP_STRING("model", IDEDrive, dev.model)

static Property ide_hd_properties[] = {
    DEFINE_IDE_DEV_PROPERTIES(),
    DEFINE_BLOCK_CHS_PROPERTIES(IDEDrive, dev.conf),
    DEFINE_PROP_BIOS_CHS_TRANS("bios-chs-trans",
                IDEDrive, dev.chs_trans, BIOS_ATA_TRANSLATION_AUTO),
    DEFINE_PROP_END_OF_LIST(),
};

static void ide_hd_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    IDEDeviceClass *k = IDE_DEVICE_CLASS(klass);
    k->init = ide_hd_initfn;
    dc->fw_name = "drive";
    dc->desc = "virtual IDE disk";
    dc->props = ide_hd_properties;
}

static const TypeInfo ide_hd_info = {
    .name          = "ide-hd",
    .parent        = TYPE_IDE_DEVICE,
    .instance_size = sizeof(IDEDrive),
    .class_init    = ide_hd_class_init,
};

static Property ide_cd_properties[] = {
    DEFINE_IDE_DEV_PROPERTIES(),
    DEFINE_PROP_END_OF_LIST(),
};

static void ide_cd_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    IDEDeviceClass *k = IDE_DEVICE_CLASS(klass);
    k->init = ide_cd_initfn;
    dc->fw_name = "drive";
    dc->desc = "virtual IDE CD-ROM";
    dc->props = ide_cd_properties;
}

static const TypeInfo ide_cd_info = {
    .name          = "ide-cd",
    .parent        = TYPE_IDE_DEVICE,
    .instance_size = sizeof(IDEDrive),
    .class_init    = ide_cd_class_init,
};

static Property ide_drive_properties[] = {
    DEFINE_IDE_DEV_PROPERTIES(),
    DEFINE_PROP_END_OF_LIST(),
};

static void ide_drive_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    IDEDeviceClass *k = IDE_DEVICE_CLASS(klass);
    k->init = ide_drive_initfn;
    dc->fw_name = "drive";
    dc->desc = "virtual IDE disk or CD-ROM (legacy)";
    dc->props = ide_drive_properties;
}

static const TypeInfo ide_drive_info = {
    .name          = "ide-drive",
    .parent        = TYPE_IDE_DEVICE,
    .instance_size = sizeof(IDEDrive),
    .class_init    = ide_drive_class_init,
};

static void ide_device_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *k = DEVICE_CLASS(klass);
    k->init = ide_qdev_init;
    set_bit(DEVICE_CATEGORY_STORAGE, k->categories);
    k->bus_type = TYPE_IDE_BUS;
    k->props = ide_props;
}

static const TypeInfo ide_device_type_info = {
    .name = TYPE_IDE_DEVICE,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(IDEDevice),
    .abstract = true,
    .class_size = sizeof(IDEDeviceClass),
    .class_init = ide_device_class_init,
};

static void ide_register_types(void)
{
    type_register_static(&ide_bus_info);
    type_register_static(&ide_hd_info);
    type_register_static(&ide_cd_info);
    type_register_static(&ide_drive_info);
    type_register_static(&ide_device_type_info);
}

type_init(ide_register_types)

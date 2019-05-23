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

#include "qemu/osdep.h"
#include "hw/hw.h"
#include "sysemu/dma.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/module.h"
#include "hw/ide/internal.h"
#include "sysemu/block-backend.h"
#include "sysemu/blockdev.h"
#include "hw/block/block.h"
#include "sysemu/sysemu.h"
#include "qapi/visitor.h"

/* --------------------------------- */

static char *idebus_get_fw_dev_path(DeviceState *dev);
static void idebus_unrealize(BusState *qdev, Error **errp);

static Property ide_props[] = {
    DEFINE_PROP_UINT32("unit", IDEDevice, unit, -1),
    DEFINE_PROP_END_OF_LIST(),
};

static void ide_bus_class_init(ObjectClass *klass, void *data)
{
    BusClass *k = BUS_CLASS(klass);

    k->get_fw_dev_path = idebus_get_fw_dev_path;
    k->unrealize = idebus_unrealize;
}

static void idebus_unrealize(BusState *bus, Error **errp)
{
    IDEBus *ibus = IDE_BUS(bus);

    if (ibus->vmstate) {
        qemu_del_vm_change_state_handler(ibus->vmstate);
    }
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

static void ide_qdev_realize(DeviceState *qdev, Error **errp)
{
    IDEDevice *dev = IDE_DEVICE(qdev);
    IDEDeviceClass *dc = IDE_DEVICE_GET_CLASS(dev);
    IDEBus *bus = DO_UPCAST(IDEBus, qbus, qdev->parent_bus);

    if (dev->unit == -1) {
        dev->unit = bus->master ? 1 : 0;
    }

    if (dev->unit >= bus->max_units) {
        error_setg(errp, "Can't create IDE unit %d, bus supports only %d units",
                     dev->unit, bus->max_units);
        return;
    }

    switch (dev->unit) {
    case 0:
        if (bus->master) {
            error_setg(errp, "IDE unit %d is in use", dev->unit);
            return;
        }
        bus->master = dev;
        break;
    case 1:
        if (bus->slave) {
            error_setg(errp, "IDE unit %d is in use", dev->unit);
            return;
        }
        bus->slave = dev;
        break;
    default:
        error_setg(errp, "Invalid IDE unit %d", dev->unit);
        return;
    }
    dc->realize(dev, errp);
}

IDEDevice *ide_create_drive(IDEBus *bus, int unit, DriveInfo *drive)
{
    DeviceState *dev;

    dev = qdev_create(&bus->qbus, drive->media_cd ? "ide-cd" : "ide-hd");
    qdev_prop_set_uint32(dev, "unit", unit);
    qdev_prop_set_drive(dev, "drive", blk_by_legacy_dinfo(drive),
                        &error_fatal);
    qdev_init_nofail(dev);
    return DO_UPCAST(IDEDevice, qdev, dev);
}

int ide_get_geometry(BusState *bus, int unit,
                     int16_t *cyls, int8_t *heads, int8_t *secs)
{
    IDEState *s = &DO_UPCAST(IDEBus, qbus, bus)->ifs[unit];

    if (s->drive_kind != IDE_HD || !s->blk) {
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

static void ide_dev_initfn(IDEDevice *dev, IDEDriveKind kind, Error **errp)
{
    IDEBus *bus = DO_UPCAST(IDEBus, qbus, dev->qdev.parent_bus);
    IDEState *s = bus->ifs + dev->unit;
    int ret;

    if (!dev->conf.blk) {
        if (kind != IDE_CD) {
            error_setg(errp, "No drive specified");
            return;
        } else {
            /* Anonymous BlockBackend for an empty drive */
            dev->conf.blk = blk_new(qemu_get_aio_context(), 0, BLK_PERM_ALL);
            ret = blk_attach_dev(dev->conf.blk, &dev->qdev);
            assert(ret == 0);
        }
    }

    if (dev->conf.discard_granularity == -1) {
        dev->conf.discard_granularity = 512;
    } else if (dev->conf.discard_granularity &&
               dev->conf.discard_granularity != 512) {
        error_setg(errp, "discard_granularity must be 512 for ide");
        return;
    }

    blkconf_blocksizes(&dev->conf);
    if (dev->conf.logical_block_size != 512) {
        error_setg(errp, "logical_block_size must be 512 for IDE");
        return;
    }

    if (kind != IDE_CD) {
        if (!blkconf_geometry(&dev->conf, &dev->chs_trans, 65535, 16, 255,
                              errp)) {
            return;
        }
    }
    if (!blkconf_apply_backend_options(&dev->conf, kind == IDE_CD,
                                       kind != IDE_CD, errp)) {
        return;
    }

    if (ide_init_drive(s, dev->conf.blk, kind,
                       dev->version, dev->serial, dev->model, dev->wwn,
                       dev->conf.cyls, dev->conf.heads, dev->conf.secs,
                       dev->chs_trans, errp) < 0) {
        return;
    }

    if (!dev->version) {
        dev->version = g_strdup(s->version);
    }
    if (!dev->serial) {
        dev->serial = g_strdup(s->drive_serial_str);
    }

    add_boot_device_path(dev->conf.bootindex, &dev->qdev,
                         dev->unit ? "/disk@1" : "/disk@0");
}

static void ide_dev_get_bootindex(Object *obj, Visitor *v, const char *name,
                                  void *opaque, Error **errp)
{
    IDEDevice *d = IDE_DEVICE(obj);

    visit_type_int32(v, name, &d->conf.bootindex, errp);
}

static void ide_dev_set_bootindex(Object *obj, Visitor *v, const char *name,
                                  void *opaque, Error **errp)
{
    IDEDevice *d = IDE_DEVICE(obj);
    int32_t boot_index;
    Error *local_err = NULL;

    visit_type_int32(v, name, &boot_index, &local_err);
    if (local_err) {
        goto out;
    }
    /* check whether bootindex is present in fw_boot_order list  */
    check_boot_index(boot_index, &local_err);
    if (local_err) {
        goto out;
    }
    /* change bootindex to a new one */
    d->conf.bootindex = boot_index;

    if (d->unit != -1) {
        add_boot_device_path(d->conf.bootindex, &d->qdev,
                             d->unit ? "/disk@1" : "/disk@0");
    }
out:
    error_propagate(errp, local_err);
}

static void ide_dev_instance_init(Object *obj)
{
    object_property_add(obj, "bootindex", "int32",
                        ide_dev_get_bootindex,
                        ide_dev_set_bootindex, NULL, NULL, NULL);
    object_property_set_int(obj, -1, "bootindex", NULL);
}

static void ide_hd_realize(IDEDevice *dev, Error **errp)
{
    ide_dev_initfn(dev, IDE_HD, errp);
}

static void ide_cd_realize(IDEDevice *dev, Error **errp)
{
    ide_dev_initfn(dev, IDE_CD, errp);
}

static void ide_drive_realize(IDEDevice *dev, Error **errp)
{
    DriveInfo *dinfo = NULL;

    if (dev->conf.blk) {
        dinfo = blk_legacy_dinfo(dev->conf.blk);
    }

    ide_dev_initfn(dev, dinfo && dinfo->media_cd ? IDE_CD : IDE_HD, errp);
}

#define DEFINE_IDE_DEV_PROPERTIES()                     \
    DEFINE_BLOCK_PROPERTIES(IDEDrive, dev.conf),        \
    DEFINE_BLOCK_ERROR_PROPERTIES(IDEDrive, dev.conf),  \
    DEFINE_PROP_STRING("ver",  IDEDrive, dev.version),  \
    DEFINE_PROP_UINT64("wwn",  IDEDrive, dev.wwn, 0),    \
    DEFINE_PROP_STRING("serial",  IDEDrive, dev.serial),\
    DEFINE_PROP_STRING("model", IDEDrive, dev.model)

static Property ide_hd_properties[] = {
    DEFINE_IDE_DEV_PROPERTIES(),
    DEFINE_BLOCK_CHS_PROPERTIES(IDEDrive, dev.conf),
    DEFINE_PROP_BIOS_CHS_TRANS("bios-chs-trans",
                IDEDrive, dev.chs_trans, BIOS_ATA_TRANSLATION_AUTO),
    DEFINE_PROP_UINT16("rotation_rate", IDEDrive, dev.rotation_rate, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void ide_hd_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    IDEDeviceClass *k = IDE_DEVICE_CLASS(klass);

    k->realize  = ide_hd_realize;
    dc->fw_name = "drive";
    dc->desc    = "virtual IDE disk";
    dc->props   = ide_hd_properties;
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

    k->realize  = ide_cd_realize;
    dc->fw_name = "drive";
    dc->desc    = "virtual IDE CD-ROM";
    dc->props   = ide_cd_properties;
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

    k->realize  = ide_drive_realize;
    dc->fw_name = "drive";
    dc->desc    = "virtual IDE disk or CD-ROM (legacy)";
    dc->props   = ide_drive_properties;
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
    k->realize = ide_qdev_realize;
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
    .instance_init = ide_dev_instance_init,
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

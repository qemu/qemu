/*
 * IDE device functions
 *
 * Copyright (c) 2009 Gerd Hoffmann <kraxel@redhat.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
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
#include "qapi/error.h"
#include "qapi/qapi-types-block.h"
#include "qemu/error-report.h"
#include "qemu/module.h"
#include "hw/ide/ide-dev.h"
#include "system/block-backend.h"
#include "system/blockdev.h"
#include "system/system.h"
#include "qapi/visitor.h"
#include "ide-internal.h"

static const Property ide_props[] = {
    DEFINE_PROP_UINT32("unit", IDEDevice, unit, -1),
    DEFINE_PROP_BOOL("win2k-install-hack", IDEDevice, win2k_install_hack, false),
};

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

void ide_dev_initfn(IDEDevice *dev, IDEDriveKind kind, Error **errp)
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

    if (!blkconf_blocksizes(&dev->conf, errp)) {
        return;
    }

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

    if (ide_init_drive(s, dev, kind, errp) < 0) {
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

    add_boot_device_lchs(&dev->qdev, dev->unit ? "/disk@1" : "/disk@0",
                         dev->conf.lcyls,
                         dev->conf.lheads,
                         dev->conf.lsecs);
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

    if (!visit_type_int32(v, name, &boot_index, errp)) {
        return;
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
                        ide_dev_set_bootindex, NULL, NULL);
    object_property_set_int(obj, "bootindex", -1, NULL);
}

static void ide_hd_realize(IDEDevice *dev, Error **errp)
{
    ide_dev_initfn(dev, IDE_HD, errp);
}

static void ide_cd_realize(IDEDevice *dev, Error **errp)
{
    ide_dev_initfn(dev, IDE_CD, errp);
}

static const Property ide_hd_properties[] = {
    DEFINE_IDE_DEV_PROPERTIES(),
    DEFINE_BLOCK_CHS_PROPERTIES(IDEDrive, dev.conf),
    DEFINE_PROP_BIOS_CHS_TRANS("bios-chs-trans",
                IDEDrive, dev.chs_trans, BIOS_ATA_TRANSLATION_AUTO),
    DEFINE_PROP_UINT16("rotation_rate", IDEDrive, dev.rotation_rate, 0),
};

static void ide_hd_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    IDEDeviceClass *k = IDE_DEVICE_CLASS(klass);

    k->realize  = ide_hd_realize;
    dc->fw_name = "drive";
    dc->desc    = "virtual IDE disk";
    device_class_set_props(dc, ide_hd_properties);
}

static const TypeInfo ide_hd_info = {
    .name          = "ide-hd",
    .parent        = TYPE_IDE_DEVICE,
    .instance_size = sizeof(IDEDrive),
    .class_init    = ide_hd_class_init,
};

static const Property ide_cd_properties[] = {
    DEFINE_IDE_DEV_PROPERTIES(),
};

static void ide_cd_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    IDEDeviceClass *k = IDE_DEVICE_CLASS(klass);

    k->realize  = ide_cd_realize;
    dc->fw_name = "drive";
    dc->desc    = "virtual IDE CD-ROM";
    device_class_set_props(dc, ide_cd_properties);
}

static const TypeInfo ide_cd_info = {
    .name          = "ide-cd",
    .parent        = TYPE_IDE_DEVICE,
    .instance_size = sizeof(IDEDrive),
    .class_init    = ide_cd_class_init,
};

static void ide_device_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *k = DEVICE_CLASS(klass);
    k->realize = ide_qdev_realize;
    set_bit(DEVICE_CATEGORY_STORAGE, k->categories);
    k->bus_type = TYPE_IDE_BUS;
    device_class_set_props(k, ide_props);
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
    type_register_static(&ide_hd_info);
    type_register_static(&ide_cd_info);
    type_register_static(&ide_device_type_info);
}

type_init(ide_register_types)

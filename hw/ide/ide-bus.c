/*
 * ide bus support for qdev.
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
#include "qemu/error-report.h"
#include "qemu/module.h"
#include "system/block-backend.h"
#include "system/blockdev.h"
#include "system/runstate.h"
#include "ide-internal.h"

static char *idebus_get_fw_dev_path(DeviceState *dev);
static void idebus_unrealize(BusState *qdev);

static void ide_bus_class_init(ObjectClass *klass, void *data)
{
    BusClass *k = BUS_CLASS(klass);

    k->get_fw_dev_path = idebus_get_fw_dev_path;
    k->unrealize = idebus_unrealize;
}

static void idebus_unrealize(BusState *bus)
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

void ide_bus_init(IDEBus *idebus, size_t idebus_size, DeviceState *dev,
                 int bus_id, int max_units)
{
    qbus_init(idebus, idebus_size, TYPE_IDE_BUS, dev, NULL);
    idebus->bus_id = bus_id;
    idebus->max_units = max_units;
}

static char *idebus_get_fw_dev_path(DeviceState *dev)
{
    char path[30];

    snprintf(path, sizeof(path), "%s@%x", qdev_fw_name(dev),
             ((IDEBus *)dev->parent_bus)->bus_id);

    return g_strdup(path);
}

IDEDevice *ide_bus_create_drive(IDEBus *bus, int unit, DriveInfo *drive)
{
    DeviceState *dev;

    dev = qdev_new(drive->media_cd ? "ide-cd" : "ide-hd");
    qdev_prop_set_uint32(dev, "unit", unit);
    qdev_prop_set_drive_err(dev, "drive", blk_by_legacy_dinfo(drive),
                            &error_fatal);
    qdev_realize_and_unref(dev, &bus->qbus, &error_fatal);
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

static void ide_bus_register_type(void)
{
    type_register_static(&ide_bus_info);
}

type_init(ide_bus_register_type)

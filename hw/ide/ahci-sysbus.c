/*
 * QEMU AHCI Emulation (MMIO-mapped devices)
 *
 * Copyright (c) 2010 qiaochong@loongson.cn
 * Copyright (c) 2010 Roland Elek <elek.roland@gmail.com>
 * Copyright (c) 2010 Sebastian Herbszt <herbszt@gmx.de>
 * Copyright (c) 2010 Alexander Graf <agraf@suse.de>
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
 *
 */

#include "qemu/osdep.h"
#include "system/address-spaces.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"

#include "hw/ide/ahci-sysbus.h"
#include "ahci-internal.h"

static const VMStateDescription vmstate_sysbus_ahci = {
    .name = "sysbus-ahci",
    .fields = (const VMStateField[]) {
        VMSTATE_AHCI(ahci, SysbusAHCIState),
        VMSTATE_END_OF_LIST()
    },
};

static void sysbus_ahci_reset(DeviceState *dev)
{
    SysbusAHCIState *s = SYSBUS_AHCI(dev);

    ahci_reset(&s->ahci);
}

static void sysbus_ahci_init(Object *obj)
{
    SysbusAHCIState *s = SYSBUS_AHCI(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    ahci_init(&s->ahci, DEVICE(obj));

    sysbus_init_mmio(sbd, &s->ahci.mem);
    sysbus_init_irq(sbd, &s->ahci.irq);
}

static void sysbus_ahci_realize(DeviceState *dev, Error **errp)
{
    SysbusAHCIState *s = SYSBUS_AHCI(dev);

    ahci_realize(&s->ahci, dev, &address_space_memory);
}

static const Property sysbus_ahci_properties[] = {
    DEFINE_PROP_UINT32("num-ports", SysbusAHCIState, ahci.ports, 1),
};

static void sysbus_ahci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = sysbus_ahci_realize;
    dc->vmsd = &vmstate_sysbus_ahci;
    device_class_set_props(dc, sysbus_ahci_properties);
    device_class_set_legacy_reset(dc, sysbus_ahci_reset);
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
}

static const TypeInfo sysbus_ahci_types[] = {
    {
        .name          = TYPE_SYSBUS_AHCI,
        .parent        = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(SysbusAHCIState),
        .instance_init = sysbus_ahci_init,
        .class_init    = sysbus_ahci_class_init,
    },
};

DEFINE_TYPES(sysbus_ahci_types)

/* icc_bus.c
 * emulate x86 ICC (Interrupt Controller Communications) bus
 *
 * Copyright (c) 2013 Red Hat, Inc
 *
 * Authors:
 *     Igor Mammedov <imammedo@redhat.com>
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
 * License along with this library; if not, see <http://www.gnu.org/licenses/>
 */
#include "hw/cpu/icc_bus.h"
#include "hw/sysbus.h"

/* icc-bridge implementation */

static void icc_bus_init(Object *obj)
{
    BusState *b = BUS(obj);

    b->allow_hotplug = true;
}

static const TypeInfo icc_bus_info = {
    .name = TYPE_ICC_BUS,
    .parent = TYPE_BUS,
    .instance_size = sizeof(ICCBus),
    .instance_init = icc_bus_init,
};


/* icc-device implementation */

static void icc_device_realize(DeviceState *dev, Error **errp)
{
    ICCDeviceClass *idc = ICC_DEVICE_GET_CLASS(dev);

    /* convert to QOM */
    if (idc->realize) {
        idc->realize(dev, errp);
    }

}

static void icc_device_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = icc_device_realize;
    dc->bus_type = TYPE_ICC_BUS;
}

static const TypeInfo icc_device_info = {
    .name = TYPE_ICC_DEVICE,
    .parent = TYPE_DEVICE,
    .abstract = true,
    .instance_size = sizeof(ICCDevice),
    .class_size = sizeof(ICCDeviceClass),
    .class_init = icc_device_class_init,
};


/*  icc-bridge implementation */

typedef struct ICCBridgeState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    ICCBus icc_bus;
    MemoryRegion apic_container;
} ICCBridgeState;

#define ICC_BRIGDE(obj) OBJECT_CHECK(ICCBridgeState, (obj), TYPE_ICC_BRIDGE)

static void icc_bridge_init(Object *obj)
{
    ICCBridgeState *s = ICC_BRIGDE(obj);
    SysBusDevice *sb = SYS_BUS_DEVICE(obj);

    qbus_create_inplace(&s->icc_bus, sizeof(s->icc_bus), TYPE_ICC_BUS,
                        DEVICE(s), "icc");

    /* Do not change order of registering regions,
     * APIC must be first registered region, board maps it by 0 index
     */
    memory_region_init(&s->apic_container, obj, "icc-apic-container",
                       APIC_SPACE_SIZE);
    sysbus_init_mmio(sb, &s->apic_container);
    s->icc_bus.apic_address_space = &s->apic_container;
}

static void icc_bridge_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);
}

static const TypeInfo icc_bridge_info = {
    .name  = TYPE_ICC_BRIDGE,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_init  = icc_bridge_init,
    .instance_size  = sizeof(ICCBridgeState),
    .class_init = icc_bridge_class_init,
};


static void icc_bus_register_types(void)
{
    type_register_static(&icc_bus_info);
    type_register_static(&icc_device_info);
    type_register_static(&icc_bridge_info);
}

type_init(icc_bus_register_types)

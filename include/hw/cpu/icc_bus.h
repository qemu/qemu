/* icc_bus.h
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
#ifndef ICC_BUS_H
#define ICC_BUS_H

#include "exec/memory.h"
#include "hw/qdev-core.h"

#define TYPE_ICC_BUS "icc-bus"

#ifndef CONFIG_USER_ONLY

/**
 * ICCBus:
 *
 * ICC bus
 */
typedef struct ICCBus {
    /*< private >*/
    BusState parent_obj;
    /*< public >*/

    MemoryRegion *apic_address_space;
} ICCBus;

#define ICC_BUS(obj) OBJECT_CHECK(ICCBus, (obj), TYPE_ICC_BUS)

/**
 * ICCDevice:
 *
 * ICC device
 */
typedef struct ICCDevice {
    /*< private >*/
    DeviceState qdev;
    /*< public >*/
} ICCDevice;

/**
 * ICCDeviceClass:
 * @init: Initialization callback for derived classes.
 *
 * ICC device class
 */
typedef struct ICCDeviceClass {
    /*< private >*/
    DeviceClass parent_class;
    /*< public >*/

    int (*init)(ICCDevice *dev); /* TODO replace with QOM realize */
} ICCDeviceClass;

#define TYPE_ICC_DEVICE "icc-device"
#define ICC_DEVICE(obj) OBJECT_CHECK(ICCDevice, (obj), TYPE_ICC_DEVICE)
#define ICC_DEVICE_CLASS(klass) \
     OBJECT_CLASS_CHECK(ICCDeviceClass, (klass), TYPE_ICC_DEVICE)
#define ICC_DEVICE_GET_CLASS(obj) \
     OBJECT_GET_CLASS(ICCDeviceClass, (obj), TYPE_ICC_DEVICE)

#define TYPE_ICC_BRIDGE "icc-bridge"

#endif /* CONFIG_USER_ONLY */
#endif

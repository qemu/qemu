#ifndef HW_PLATFORM_BUS_H
#define HW_PLATFORM_BUS_H

/*
 *  Platform Bus device to support dynamic Sysbus devices
 *
 * Copyright (C) 2014 Freescale Semiconductor, Inc. All rights reserved.
 *
 * Author: Alexander Graf, <agraf@suse.de>
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

#include "hw/sysbus.h"

typedef struct PlatformBusDevice PlatformBusDevice;

#define TYPE_PLATFORM_BUS_DEVICE "platform-bus-device"
#define PLATFORM_BUS_DEVICE(obj) \
     OBJECT_CHECK(PlatformBusDevice, (obj), TYPE_PLATFORM_BUS_DEVICE)
#define PLATFORM_BUS_DEVICE_CLASS(klass) \
     OBJECT_CLASS_CHECK(PlatformBusDeviceClass, (klass), TYPE_PLATFORM_BUS_DEVICE)
#define PLATFORM_BUS_DEVICE_GET_CLASS(obj) \
     OBJECT_GET_CLASS(PlatformBusDeviceClass, (obj), TYPE_PLATFORM_BUS_DEVICE)

struct PlatformBusDevice {
    /*< private >*/
    SysBusDevice parent_obj;
    Notifier notifier;
    bool done_gathering;

    /*< public >*/
    uint32_t mmio_size;
    MemoryRegion mmio;

    uint32_t num_irqs;
    qemu_irq *irqs;
    unsigned long *used_irqs;
};

int platform_bus_get_irqn(PlatformBusDevice *platform_bus, SysBusDevice *sbdev,
                          int n);
hwaddr platform_bus_get_mmio_addr(PlatformBusDevice *pbus, SysBusDevice *sbdev,
                                  int n);

#endif /* HW_PLATFORM_BUS_H */

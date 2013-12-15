/*
 * QEMU VLYNQ Serial Interface support.
 *
 * Copyright (C) 2009-2011 Stefan Weil
 *
 * Portions of the code are copies from ssi.h.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) version 3 or any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef QEMU_VLYNQ_H
#define QEMU_VLYNQ_H

#include "hw/qdev.h"

typedef struct _VLYNQBus VLYNQBus;
typedef struct _VLYNQSlave VLYNQSlave;
typedef struct _VLYNQSlaveInfo VLYNQSlaveInfo;

#if 0
/* Slave devices.  */
typedef struct {
    int (*init)(VLYNQSlave *dev);
    uint32_t (*transfer)(VLYNQSlave *dev, uint32_t val);
} VLYNQSlaveInfo;

struct VLYNQSlave {
    DeviceState qdev;
    VLYNQSlaveInfo *info;
};

#define VLYNQ_SLAVE_FROM_QDEV(dev) DO_UPCAST(VLYNQSlave, qdev, dev)
#define FROM_VLYNQ_SLAVE(type, dev) DO_UPCAST(type, vlynqdev, dev)
#endif

typedef struct {
    DeviceState qdev;
    //~ uint32_t isairq[2];
    //~ int nirqs;
} VLYNQDevice;

typedef int (*vlynq_qdev_initfn)(VLYNQDevice *vlynq_dev);
typedef int (*VLYNQUnregisterFunc)(VLYNQDevice *vlynq_dev);

typedef struct {
    vlynq_qdev_initfn init;
    VLYNQUnregisterFunc exit;
} VLYNQDeviceInfo;

void vlynq_qdev_register(VLYNQDeviceInfo *info);

void vlynq_register_slave(VLYNQSlaveInfo *info);

DeviceState *vlynq_create_slave(VLYNQBus *bus, const char *name);

/* Master interface.  */
VLYNQBus *vlynq_create_bus(DeviceState *parent, const char *name);

uint32_t vlynq_transfer(VLYNQBus *bus, uint32_t val);

/* max111x.c */
void max111x_set_input(DeviceState *dev, int line, uint8_t value);

#endif

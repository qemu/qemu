/* QEMU Synchronous Serial Interface support.  */

/* In principle VLYNQ is a point-point interface.  As such the qemu
   implementation has a single slave device on a "bus".
   However it is fairly common for boards to have multiple slaves
   connected to a single master, and select devices with an external
   chip select.  This is implemented in qemu by having an explicit mux device.
   It is assumed that master and slave are both using the same transfer width.
   */

#ifndef QEMU_VLYNQ_H
#define QEMU_VLYNQ_H

#include "qdev.h"

typedef struct _VLYNQBus VLYNQBus;
typedef struct _VLYNQSlave VLYNQSlave;
typedef struct _VLYNQSlaveInfo VLYNQSlaveInfo;

#if 0
/* Slave devices.  */
typedef struct {
    DeviceInfo qdev;
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

void vlynq_register_slave(VLYNQSlaveInfo *info);

DeviceState *vlynq_create_slave(VLYNQBus *bus, const char *name);

/* Master interface.  */
VLYNQBus *vlynq_create_bus(DeviceState *parent, const char *name);

uint32_t vlynq_transfer(VLYNQBus *bus, uint32_t val);

/* max111x.c */
void max111x_set_input(DeviceState *dev, int line, uint8_t value);

#endif

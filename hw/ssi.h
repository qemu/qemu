/* QEMU Synchronous Serial Interface support.  */

/* In principle SSI is a point-point interface.  As such the qemu
   implementation has a single slave device on a "bus".
   However it is fairly common for boards to have multiple slaves
   connected to a single master, and select devices with an external
   chip select.  This is implemented in qemu by having an explicit mux device.
   It is assumed that master and slave are both using the same transfer width.
   */

#ifndef QEMU_SSI_H
#define QEMU_SSI_H

#include "qdev.h"

typedef struct SSISlave SSISlave;

/* Slave devices.  */
typedef struct {
    void (*init)(SSISlave *dev);
    uint32_t (*transfer)(SSISlave *dev, uint32_t val);
} SSISlaveInfo;

struct SSISlave {
    DeviceState qdev;
    SSISlaveInfo *info;
};

#define SSI_SLAVE_FROM_QDEV(dev) DO_UPCAST(SSISlave, qdev, dev)
#define FROM_SSI_SLAVE(type, dev) DO_UPCAST(type, ssidev, dev)

void ssi_register_slave(const char *name, int size, SSISlaveInfo *info);

DeviceState *ssi_create_slave(SSIBus *bus, const char *name);

/* Master interface.  */
SSIBus *ssi_create_bus(void);

uint32_t ssi_transfer(SSIBus *bus, uint32_t val);

#endif

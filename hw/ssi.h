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

#define TYPE_SSI_SLAVE "ssi-slave"
#define SSI_SLAVE(obj) \
     OBJECT_CHECK(SSISlave, (obj), TYPE_SSI_SLAVE)
#define SSI_SLAVE_CLASS(klass) \
     OBJECT_CLASS_CHECK(SSISlaveClass, (klass), TYPE_SSI_SLAVE)
#define SSI_SLAVE_GET_CLASS(obj) \
     OBJECT_GET_CLASS(SSISlaveClass, (obj), TYPE_SSI_SLAVE)

/* Slave devices.  */
typedef struct SSISlaveClass {
    DeviceClass parent_class;

    int (*init)(SSISlave *dev);
    uint32_t (*transfer)(SSISlave *dev, uint32_t val);
} SSISlaveClass;

struct SSISlave {
    DeviceState qdev;
};

#define SSI_SLAVE_FROM_QDEV(dev) DO_UPCAST(SSISlave, qdev, dev)
#define FROM_SSI_SLAVE(type, dev) DO_UPCAST(type, ssidev, dev)

DeviceState *ssi_create_slave(SSIBus *bus, const char *name);

/* Master interface.  */
SSIBus *ssi_create_bus(DeviceState *parent, const char *name);

uint32_t ssi_transfer(SSIBus *bus, uint32_t val);

/* max111x.c */
void max111x_set_input(DeviceState *dev, int line, uint8_t value);

#endif

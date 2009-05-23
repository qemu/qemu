/*
 * QEMU Synchronous Serial Interface support
 *
 * Copyright (c) 2009 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licenced under the GNU GPL v2.
 */

#include "ssi.h"

struct SSIBus {
    BusState qbus;
};

static void ssi_slave_init(DeviceState *dev, DeviceInfo *base_info)
{
    SSISlaveInfo *info = container_of(base_info, SSISlaveInfo, qdev);
    SSISlave *s = SSI_SLAVE_FROM_QDEV(dev);
    SSIBus *bus;

    bus = FROM_QBUS(SSIBus, qdev_get_parent_bus(dev));
    if (LIST_FIRST(&bus->qbus.children) != dev
        || LIST_NEXT(dev, sibling) != NULL) {
        hw_error("Too many devices on SSI bus");
    }

    s->info = info;
    info->init(s);
}

void ssi_register_slave(const char *name, int size, SSISlaveInfo *info)
{
    assert(size >= sizeof(SSISlave));
    info->qdev.init = ssi_slave_init;
    info->qdev.bus_type = BUS_TYPE_SSI;
    qdev_register(name, size, &info->qdev);
}

DeviceState *ssi_create_slave(SSIBus *bus, const char *name)
{
    DeviceState *dev;
    dev = qdev_create(&bus->qbus, name);
    qdev_init(dev);
    return dev;
}

SSIBus *ssi_create_bus(DeviceState *parent, const char *name)
{
    BusState *bus;
    bus = qbus_create(BUS_TYPE_SSI, sizeof(SSIBus), parent, name);
    return FROM_QBUS(SSIBus, bus);
}

uint32_t ssi_transfer(SSIBus *bus, uint32_t val)
{
    DeviceState *dev;
    SSISlave *slave;
    dev = LIST_FIRST(&bus->qbus.children);
    if (!dev) {
        return 0;
    }
    slave = SSI_SLAVE_FROM_QDEV(dev);
    return slave->info->transfer(slave, val);
}

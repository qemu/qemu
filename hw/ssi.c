/*
 * QEMU Synchronous Serial Interface support
 *
 * Copyright (c) 2009 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licensed under the GNU GPL v2.
 */

#include "ssi.h"

struct SSIBus {
    BusState qbus;
};

static struct BusInfo ssi_bus_info = {
    .name = "SSI",
    .size = sizeof(SSIBus),
};

static int ssi_slave_init(DeviceState *dev, DeviceInfo *base_info)
{
    SSISlaveInfo *info = container_of(base_info, SSISlaveInfo, qdev);
    SSISlave *s = SSI_SLAVE_FROM_QDEV(dev);
    SSIBus *bus;

    bus = FROM_QBUS(SSIBus, qdev_get_parent_bus(dev));
    if (QTAILQ_FIRST(&bus->qbus.children) != dev
        || QTAILQ_NEXT(dev, sibling) != NULL) {
        hw_error("Too many devices on SSI bus");
    }

    s->info = info;
    return info->init(s);
}

void ssi_register_slave(SSISlaveInfo *info)
{
    assert(info->qdev.size >= sizeof(SSISlave));
    info->qdev.init = ssi_slave_init;
    info->qdev.bus_info = &ssi_bus_info;
    qdev_register(&info->qdev);
}

DeviceState *ssi_create_slave(SSIBus *bus, const char *name)
{
    DeviceState *dev;
    dev = qdev_create(&bus->qbus, name);
    qdev_init_nofail(dev);
    return dev;
}

SSIBus *ssi_create_bus(DeviceState *parent, const char *name)
{
    BusState *bus;
    bus = qbus_create(&ssi_bus_info, parent, name);
    return FROM_QBUS(SSIBus, bus);
}

uint32_t ssi_transfer(SSIBus *bus, uint32_t val)
{
    DeviceState *dev;
    SSISlave *slave;
    dev = QTAILQ_FIRST(&bus->qbus.children);
    if (!dev) {
        return 0;
    }
    slave = SSI_SLAVE_FROM_QDEV(dev);
    return slave->info->transfer(slave, val);
}

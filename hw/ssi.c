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
    SSISlave *slave;
};

static void ssi_slave_init(DeviceState *dev, void *opaque)
{
    SSISlaveInfo *info = opaque;
    SSISlave *s = SSI_SLAVE_FROM_QDEV(dev);
    SSIBus *bus = qdev_get_bus(dev);

    bus->slave = s;
    s->info = info;
    info->init(s);
}

void ssi_register_slave(const char *name, int size, SSISlaveInfo *info)
{
    assert(size >= sizeof(SSISlave));
    qdev_register(name, size, ssi_slave_init, info);
}

DeviceState *ssi_create_slave(SSIBus *bus, const char *name)
{
    DeviceState *dev;
    dev = qdev_create(bus, name);
    qdev_init(dev);
    return dev;
}

SSIBus *ssi_create_bus(void)
{
    return qemu_mallocz(sizeof(SSIBus));
}

uint32_t ssi_transfer(SSIBus *bus, uint32_t val)
{
    if (!bus->slave) {
        return 0;
    }
    return bus->slave->info->transfer(bus->slave, val);
}

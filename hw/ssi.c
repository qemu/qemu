/*
 * QEMU Synchronous Serial Interface support
 *
 * Copyright (c) 2009 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licensed under the GNU GPL v2.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "ssi.h"

struct SSIBus {
    BusState qbus;
};

static struct BusInfo ssi_bus_info = {
    .name = "SSI",
    .size = sizeof(SSIBus),
};

static int ssi_slave_init(DeviceState *dev)
{
    SSISlave *s = SSI_SLAVE(dev);
    SSISlaveClass *ssc = SSI_SLAVE_GET_CLASS(s);
    SSIBus *bus;

    bus = FROM_QBUS(SSIBus, qdev_get_parent_bus(dev));
    if (QTAILQ_FIRST(&bus->qbus.children) != dev
        || QTAILQ_NEXT(dev, sibling) != NULL) {
        hw_error("Too many devices on SSI bus");
    }

    return ssc->init(s);
}

static void ssi_slave_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->init = ssi_slave_init;
    dc->bus_info = &ssi_bus_info;
}

static TypeInfo ssi_slave_info = {
    .name = TYPE_SSI_SLAVE,
    .parent = TYPE_DEVICE,
    .class_init = ssi_slave_class_init,
    .class_size = sizeof(SSISlaveClass),
    .abstract = true,
};

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
    SSISlaveClass *ssc;
    dev = QTAILQ_FIRST(&bus->qbus.children);
    if (!dev) {
        return 0;
    }
    slave = SSI_SLAVE(dev);
    ssc = SSI_SLAVE_GET_CLASS(slave);
    return ssc->transfer(slave, val);
}

static void ssi_slave_register_types(void)
{
    type_register_static(&ssi_slave_info);
}

type_init(ssi_slave_register_types)

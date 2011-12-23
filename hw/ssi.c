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

#define TYPE_SSI_BUS "SSI"
#define SSI_BUS(obj) OBJECT_CHECK(SSIBus, (obj), TYPE_SSI_BUS)

static const TypeInfo ssi_bus_info = {
    .name = TYPE_SSI_BUS,
    .parent = TYPE_BUS,
    .instance_size = sizeof(SSIBus),
};

static int ssi_slave_init(DeviceState *dev)
{
    SSISlave *s = SSI_SLAVE(dev);
    SSISlaveClass *ssc = SSI_SLAVE_GET_CLASS(s);
    SSIBus *bus;
    BusChild *kid;

    bus = FROM_QBUS(SSIBus, qdev_get_parent_bus(dev));
    kid = QTAILQ_FIRST(&bus->qbus.children);
    if (kid->child != dev || QTAILQ_NEXT(kid, sibling) != NULL) {
        hw_error("Too many devices on SSI bus");
    }

    return ssc->init(s);
}

static void ssi_slave_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->init = ssi_slave_init;
    dc->bus_type = TYPE_SSI_BUS;
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
    bus = qbus_create(TYPE_SSI_BUS, parent, name);
    return FROM_QBUS(SSIBus, bus);
}

uint32_t ssi_transfer(SSIBus *bus, uint32_t val)
{
    BusChild *kid;
    SSISlave *slave;
    SSISlaveClass *ssc;

    kid = QTAILQ_FIRST(&bus->qbus.children);
    if (!kid) {
        return 0;
    }
    slave = SSI_SLAVE(kid->child);
    ssc = SSI_SLAVE_GET_CLASS(slave);
    return ssc->transfer(slave, val);
}

static void ssi_slave_register_types(void)
{
    type_register_static(&ssi_bus_info);
    type_register_static(&ssi_slave_info);
}

type_init(ssi_slave_register_types)

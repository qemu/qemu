/*
 * QEMU I2C bus interface.
 *
 * Copyright (c) 2007 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licensed under the LGPL.
 */

#include "i2c.h"

struct i2c_bus
{
    BusState qbus;
    I2CSlave *current_dev;
    I2CSlave *dev;
    uint8_t saved_address;
};

static struct BusInfo i2c_bus_info = {
    .name = "I2C",
    .size = sizeof(i2c_bus),
    .props = (Property[]) {
        DEFINE_PROP_UINT8("address", struct I2CSlave, address, 0),
        DEFINE_PROP_END_OF_LIST(),
    }
};

static void i2c_bus_pre_save(void *opaque)
{
    i2c_bus *bus = opaque;

    bus->saved_address = bus->current_dev ? bus->current_dev->address : -1;
}

static int i2c_bus_post_load(void *opaque, int version_id)
{
    i2c_bus *bus = opaque;

    /* The bus is loaded before attached devices, so load and save the
       current device id.  Devices will check themselves as loaded.  */
    bus->current_dev = NULL;
    return 0;
}

static const VMStateDescription vmstate_i2c_bus = {
    .name = "i2c_bus",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .pre_save = i2c_bus_pre_save,
    .post_load = i2c_bus_post_load,
    .fields      = (VMStateField []) {
        VMSTATE_UINT8(saved_address, i2c_bus),
        VMSTATE_END_OF_LIST()
    }
};

/* Create a new I2C bus.  */
i2c_bus *i2c_init_bus(DeviceState *parent, const char *name)
{
    i2c_bus *bus;

    bus = FROM_QBUS(i2c_bus, qbus_create(&i2c_bus_info, parent, name));
    vmstate_register(NULL, -1, &vmstate_i2c_bus, bus);
    return bus;
}

void i2c_set_slave_address(I2CSlave *dev, uint8_t address)
{
    dev->address = address;
}

/* Return nonzero if bus is busy.  */
int i2c_bus_busy(i2c_bus *bus)
{
    return bus->current_dev != NULL;
}

/* Returns non-zero if the address is not valid.  */
/* TODO: Make this handle multiple masters.  */
int i2c_start_transfer(i2c_bus *bus, uint8_t address, int recv)
{
    DeviceState *qdev;
    I2CSlave *slave = NULL;

    QTAILQ_FOREACH(qdev, &bus->qbus.children, sibling) {
        I2CSlave *candidate = I2C_SLAVE_FROM_QDEV(qdev);
        if (candidate->address == address) {
            slave = candidate;
            break;
        }
    }

    if (!slave)
        return 1;

    /* If the bus is already busy, assume this is a repeated
       start condition.  */
    bus->current_dev = slave;
    slave->info->event(slave, recv ? I2C_START_RECV : I2C_START_SEND);
    return 0;
}

void i2c_end_transfer(i2c_bus *bus)
{
    I2CSlave *dev = bus->current_dev;

    if (!dev)
        return;

    dev->info->event(dev, I2C_FINISH);

    bus->current_dev = NULL;
}

int i2c_send(i2c_bus *bus, uint8_t data)
{
    I2CSlave *dev = bus->current_dev;

    if (!dev)
        return -1;

    return dev->info->send(dev, data);
}

int i2c_recv(i2c_bus *bus)
{
    I2CSlave *dev = bus->current_dev;

    if (!dev)
        return -1;

    return dev->info->recv(dev);
}

void i2c_nack(i2c_bus *bus)
{
    I2CSlave *dev = bus->current_dev;

    if (!dev)
        return;

    dev->info->event(dev, I2C_NACK);
}

static int i2c_slave_post_load(void *opaque, int version_id)
{
    I2CSlave *dev = opaque;
    i2c_bus *bus;
    bus = FROM_QBUS(i2c_bus, qdev_get_parent_bus(&dev->qdev));
    if (bus->saved_address == dev->address) {
        bus->current_dev = dev;
    }
    return 0;
}

const VMStateDescription vmstate_i2c_slave = {
    .name = "I2CSlave",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .post_load = i2c_slave_post_load,
    .fields      = (VMStateField []) {
        VMSTATE_UINT8(address, I2CSlave),
        VMSTATE_END_OF_LIST()
    }
};

static int i2c_slave_qdev_init(DeviceState *dev, DeviceInfo *base)
{
    I2CSlaveInfo *info = container_of(base, I2CSlaveInfo, qdev);
    I2CSlave *s = I2C_SLAVE_FROM_QDEV(dev);

    s->info = info;

    return info->init(s);
}

void i2c_register_slave(I2CSlaveInfo *info)
{
    assert(info->qdev.size >= sizeof(I2CSlave));
    info->qdev.init = i2c_slave_qdev_init;
    info->qdev.bus_info = &i2c_bus_info;
    qdev_register(&info->qdev);
}

DeviceState *i2c_create_slave(i2c_bus *bus, const char *name, uint8_t addr)
{
    DeviceState *dev;

    dev = qdev_create(&bus->qbus, name);
    qdev_prop_set_uint8(dev, "address", addr);
    qdev_init_nofail(dev);
    return dev;
}

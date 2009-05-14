/*
 * QEMU I2C bus interface.
 *
 * Copyright (c) 2007 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licenced under the LGPL.
 */

#include "i2c.h"

struct i2c_bus
{
    i2c_slave *current_dev;
    i2c_slave *dev;
    int saved_address;
};

static void i2c_bus_save(QEMUFile *f, void *opaque)
{
    i2c_bus *bus = (i2c_bus *)opaque;

    qemu_put_byte(f, bus->current_dev ? bus->current_dev->address : -1);
}

static int i2c_bus_load(QEMUFile *f, void *opaque, int version_id)
{
    i2c_bus *bus = (i2c_bus *)opaque;

    if (version_id != 1)
        return -EINVAL;

    /* The bus is loaded before attached devices, so load and save the
       current device id.  Devices will check themselves as loaded.  */
    bus->saved_address = (int8_t) qemu_get_byte(f);
    bus->current_dev = NULL;

    return 0;
}

/* Create a new I2C bus.  */
i2c_bus *i2c_init_bus(void)
{
    i2c_bus *bus;

    bus = (i2c_bus *)qemu_mallocz(sizeof(i2c_bus));
    register_savevm("i2c_bus", -1, 1, i2c_bus_save, i2c_bus_load, bus);
    return bus;
}

/* Create a new slave device.  */
i2c_slave *i2c_slave_init(i2c_bus *bus, int address, int size)
{
    i2c_slave *dev;

    if (size < sizeof(i2c_slave))
        hw_error("I2C struct too small");

    dev = (i2c_slave *)qemu_mallocz(size);
    dev->address = address;
    dev->next = bus->dev;
    bus->dev = dev;
    dev->qdev.bus = bus;

    return dev;
}

void i2c_set_slave_address(i2c_slave *dev, int address)
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
int i2c_start_transfer(i2c_bus *bus, int address, int recv)
{
    i2c_slave *dev;

    for (dev = bus->dev; dev; dev = dev->next) {
        if (dev->address == address)
            break;
    }

    if (!dev)
        return 1;

    /* If the bus is already busy, assume this is a repeated
       start condition.  */
    bus->current_dev = dev;
    dev->info->event(dev, recv ? I2C_START_RECV : I2C_START_SEND);
    return 0;
}

void i2c_end_transfer(i2c_bus *bus)
{
    i2c_slave *dev = bus->current_dev;

    if (!dev)
        return;

    dev->info->event(dev, I2C_FINISH);

    bus->current_dev = NULL;
}

int i2c_send(i2c_bus *bus, uint8_t data)
{
    i2c_slave *dev = bus->current_dev;

    if (!dev)
        return -1;

    return dev->info->send(dev, data);
}

int i2c_recv(i2c_bus *bus)
{
    i2c_slave *dev = bus->current_dev;

    if (!dev)
        return -1;

    return dev->info->recv(dev);
}

void i2c_nack(i2c_bus *bus)
{
    i2c_slave *dev = bus->current_dev;

    if (!dev)
        return;

    dev->info->event(dev, I2C_NACK);
}

void i2c_slave_save(QEMUFile *f, i2c_slave *dev)
{
    qemu_put_byte(f, dev->address);
}

void i2c_slave_load(QEMUFile *f, i2c_slave *dev)
{
    i2c_bus *bus;
    bus = qdev_get_bus(&dev->qdev);
    dev->address = qemu_get_byte(f);
    if (bus->saved_address == dev->address) {
        bus->current_dev = dev;
    }
}

static void i2c_slave_qdev_init(DeviceState *dev, void *opaque)
{
    I2CSlaveInfo *info = opaque;
    i2c_slave *s = I2C_SLAVE_FROM_QDEV(dev);

    s->info = info;
    s->bus = qdev_get_bus(dev);
    s->address = qdev_get_prop_int(dev, "address", 0);
    s->next = s->bus->dev;
    s->bus->dev = s;

    s->event = info->event;
    s->recv = info->recv;
    s->send = info->send;

    info->init(s);
}

void i2c_register_slave(const char *name, int size, I2CSlaveInfo *info)
{
    assert(size >= sizeof(i2c_slave));
    qdev_register(name, size, i2c_slave_qdev_init, info);
}

DeviceState *i2c_create_slave(i2c_bus *bus, const char *name, int addr)
{
    DeviceState *dev;

    dev = qdev_create(bus, name);
    qdev_set_prop_int(dev, "address", addr);
    qdev_init(dev);
    return dev;
}

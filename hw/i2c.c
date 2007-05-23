/* 
 * QEMU I2C bus interface.
 *
 * Copyright (c) 2007 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licenced under the LGPL.
 */

#include "vl.h"

struct i2c_bus
{
    i2c_slave *current_dev;
    i2c_slave *dev;
};

/* Create a new I2C bus.  */
i2c_bus *i2c_init_bus(void)
{
    i2c_bus *bus;

    bus = (i2c_bus *)qemu_mallocz(sizeof(i2c_bus));
    return bus;
}

/* Create a new slave device.  */
i2c_slave *i2c_slave_init(i2c_bus *bus, int address, int size)
{
    i2c_slave *dev;

    if (size < sizeof(i2c_slave))
        cpu_abort(cpu_single_env, "I2C struct too small");

    dev = (i2c_slave *)qemu_mallocz(size);
    dev->address = address;
    dev->next = bus->dev;
    bus->dev = dev;

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

/* Returns nonzero if the bus is already busy, or is the address is not
   valid.  */
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
    dev->event(dev, recv ? I2C_START_RECV : I2C_START_SEND);
    return 0;
}

void i2c_end_transfer(i2c_bus *bus)
{
    i2c_slave *dev = bus->current_dev;

    if (!dev)
        return;

    dev->event(dev, I2C_FINISH);

    bus->current_dev = NULL;
}

int i2c_send(i2c_bus *bus, uint8_t data)
{
    i2c_slave *dev = bus->current_dev;

    if (!dev)
        return -1;

    return dev->send(dev, data);
}

int i2c_recv(i2c_bus *bus)
{
    i2c_slave *dev = bus->current_dev;

    if (!dev)
        return -1;

    return dev->recv(dev);
}

void i2c_nack(i2c_bus *bus)
{
    i2c_slave *dev = bus->current_dev;

    if (!dev)
        return;

    dev->event(dev, I2C_NACK);
}


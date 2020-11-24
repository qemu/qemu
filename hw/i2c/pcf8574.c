/*
 * PCF8574 IO Expander device
 *
 * Implements pcf8574 i2c device
 * Currently, it does not implement all the functionalities of this chip.
 * Written by Jay Mehta
 *
 * Copyright (c) 2020 Nanosonics Ltd.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "hw/i2c/i2c.h"
#include "qemu/module.h"
#include "qemu/error-report.h"
#include "util/nano_utils.h"

#ifndef DEBUG_PCF8574
#define DEBUG_PCF8574 0
#endif

typedef enum {
    WRITE_MODE,
    READ_MODE
} PortRegisterMode;

/*
 * Register masks
 */
#define TYPE_PCF8574 "pcf8574"
#define PCF8574(obj) OBJECT_CHECK(PCF8574State, (obj), TYPE_PCF8574)

typedef struct PCF8574State {
    I2CSlave parent_obj;
    uint8_t port_register;
    PortRegisterMode port_register_mode;
} PCF8574State;


static int pcf8574_event(I2CSlave *i2c, enum i2c_event event)
{
    PCF8574State *s = PCF8574(i2c);

    DPRINTF(TYPE_PCF8574, DEBUG_PCF8574, "Function called. Event = %d.\n", (uint8_t) event);
    switch (event) {
    case I2C_START_RECV:
        // Value 1 in R/W bit (BIT 0) in address byte indicates that master is trying to read the ports.
        s->port_register_mode = READ_MODE;
        break;
    case I2C_START_SEND:
        // Value 0 in R/W bit (BIT 0) in address byte indicates that master is trying to write the ports.
        s->port_register_mode = WRITE_MODE;
        break;
    default:
        break;
    }

    return 0;
}

static uint8_t pcf8574_recv(I2CSlave *i2c)
{
    PCF8574State *s = PCF8574(i2c);

    DPRINTF(TYPE_PCF8574, DEBUG_PCF8574, "Function called. Returning data = %d. Address = 0x%x\n", s->port_register, s->parent_obj.address);

    if(s->port_register_mode == READ_MODE)
    {
        return s->port_register;
    }
    else
    {
        DPRINTF(TYPE_PCF8574, DEBUG_PCF8574, "Invalid mode, expecting 'Read mode'. Current mode = %d.\n", s->port_register_mode);
        return 0;
    }
}

static int pcf8574_send(I2CSlave *i2c, uint8_t data)
{
    PCF8574State *s = PCF8574(i2c);

    DPRINTF(TYPE_PCF8574, DEBUG_PCF8574, "Function called. Data = %d. Address = 0x%x\n", data, s->parent_obj.address);
    if(s->port_register_mode == WRITE_MODE)
    {
        s->port_register = data;
    }
    else
    {
        DPRINTF(TYPE_PCF8574, DEBUG_PCF8574, "Invalid mode, expecting 'Write mode'. Current mode = %d.\n", s->port_register_mode);
        return 1;
    }

    return 0;
}

static void pcf8574_reset(DeviceState *dev)
{
    PCF8574State *s = PCF8574(dev);
    DPRINTF(TYPE_PCF8574, DEBUG_PCF8574, "Function called. Address = 0x%x\n", s->parent_obj.address);
    s->port_register_mode = READ_MODE;
}

static void pcf8574_device_realize(DeviceState *dev, Error **errp)
{
    PCF8574State *s = PCF8574(dev);
    s->port_register_mode = READ_MODE;
    s->port_register = 0x00;
}

static void pcf8574_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    I2CSlaveClass *k = I2C_SLAVE_CLASS(klass);

    k->event = pcf8574_event;
    k->recv = pcf8574_recv;
    k->send = pcf8574_send;
    dc->reset = pcf8574_reset;
    dc->realize = pcf8574_device_realize;
}

static const TypeInfo pcf8574_info = {
    .name          = TYPE_PCF8574,
    .parent        = TYPE_I2C_SLAVE,
    .instance_size = sizeof(PCF8574State),
    .class_init    = pcf8574_class_init,
};

static void pcf8574_register_types(void)
{
    type_register_static(&pcf8574_info);
}

type_init(pcf8574_register_types)

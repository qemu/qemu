/*
 * PCF8575 IO Expander device
 *
 * Implements pcf8575 i2c device
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

#ifndef DEBUG_PCF8575
#define DEBUG_PCF8575 0
#endif

typedef enum {
    WRITE_MODE,
    READ_MODE
} PortRegisterMode;

/*
 * Register masks
 */
#define TYPE_PCF8575 "pcf8575"
#define PCF8575(obj) OBJECT_CHECK(PCF8575State, (obj), TYPE_PCF8575)

typedef struct PCF8575State {
    I2CSlave parent_obj;
    uint16_t port_register;
    uint8_t port_register_byte;
    PortRegisterMode port_register_mode;
} PCF8575State;


static int pcf8575_event(I2CSlave *i2c, enum i2c_event event)
{
    PCF8575State *s = PCF8575(i2c);

    DPRINTF(TYPE_PCF8575, DEBUG_PCF8575, "Function called. Event = %d.\n", (uint8_t) event);
    switch (event) {
    case I2C_START_RECV:
        // Value 1 in R/W bit (BIT 0) in address byte indicates that master is trying to read the ports.
        s->port_register_mode = READ_MODE;
        break;
    case I2C_START_SEND:
        // Value 0 in R/W bit (BIT 0) in address byte indicates that master is trying to write the ports.
        s->port_register_mode = WRITE_MODE;
        break;
    case I2C_FINISH:
        s->port_register_byte = 0;
        break;
    default:
        break;
    }

    return 0;
}

static uint8_t pcf8575_recv(I2CSlave *i2c)
{
    PCF8575State *s = PCF8575(i2c);
    uint8_t return_value = 0;

    DPRINTF(TYPE_PCF8575, DEBUG_PCF8575, "Function called. Returning data = %d. Address = 0x%x\n", s->port_register, s->parent_obj.address);

    if(s->port_register_mode == READ_MODE)
    {
        return_value = (uint8_t) (s->port_register >> (8 * s->port_register_byte));
        s->port_register_byte++;
        return return_value;
    }
    else
    {
        DPRINTF(TYPE_PCF8575, DEBUG_PCF8575, "Invalid mode, expecting 'Read mode'. Current mode = %d.\n", s->port_register_mode);
        return return_value;
    }
}

static int pcf8575_send(I2CSlave *i2c, uint8_t data)
{
    PCF8575State *s = PCF8575(i2c);

    DPRINTF(TYPE_PCF8575, DEBUG_PCF8575, "Function called. Data = %d. Address = 0x%x\n", data, s->parent_obj.address);
    if(s->port_register_mode == WRITE_MODE)
    {
        if(s->port_register_byte == 0)
        {
            s->port_register = (s->port_register & 0xFF00) | (uint16_t) data;
            s->port_register_byte++;
        }
        else
        {
            s->port_register = (s->port_register & 0x00FF) | ((uint16_t) (data << 8));
            s->port_register_byte = 0;
        }
    }
    else
    {
        DPRINTF(TYPE_PCF8575, DEBUG_PCF8575, "Invalid mode, expecting 'Write mode'. Current mode = %d.\n", s->port_register_mode);
        return 1;
    }

    return 0;
}

static void pcf8575_reset(DeviceState *dev)
{
    PCF8575State *s = PCF8575(dev);
    DPRINTF(TYPE_PCF8575, DEBUG_PCF8575, "Function called. Address = 0x%x\n", s->parent_obj.address);
    s->port_register_mode = READ_MODE;
    s->port_register_byte = 0;
}

static void pcf8575_device_realize(DeviceState *dev, Error **errp)
{
    PCF8575State *s = PCF8575(dev);
    s->port_register_mode = READ_MODE;
    s->port_register = 0x0000;
}

static void pcf8575_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    I2CSlaveClass *k = I2C_SLAVE_CLASS(klass);

    k->event = pcf8575_event;
    k->recv = pcf8575_recv;
    k->send = pcf8575_send;
    dc->reset = pcf8575_reset;
    dc->realize = pcf8575_device_realize;
}

static const TypeInfo pcf8575_info = {
    .name          = TYPE_PCF8575,
    .parent        = TYPE_I2C_SLAVE,
    .instance_size = sizeof(PCF8575State),
    .class_init    = pcf8575_class_init,
};

static void pcf8575_register_types(void)
{
    type_register_static(&pcf8575_info);
}

type_init(pcf8575_register_types)

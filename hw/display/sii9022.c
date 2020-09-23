/*
 * Silicon Image SiI9022
 *
 * This is a pretty hollow emulation: all we do is acknowledge that we
 * exist (chip ID) and confirm that we get switched over into DDC mode
 * so the emulated host can proceed to read out EDID data. All subsequent
 * set-up of connectors etc will be acknowledged and ignored.
 *
 * Copyright (C) 2018 Linus Walleij
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "hw/i2c/i2c.h"
#include "migration/vmstate.h"
#include "hw/display/i2c-ddc.h"
#include "trace.h"
#include "qom/object.h"

#define SII9022_SYS_CTRL_DATA 0x1a
#define SII9022_SYS_CTRL_PWR_DWN 0x10
#define SII9022_SYS_CTRL_AV_MUTE 0x08
#define SII9022_SYS_CTRL_DDC_BUS_REQ 0x04
#define SII9022_SYS_CTRL_DDC_BUS_GRTD 0x02
#define SII9022_SYS_CTRL_OUTPUT_MODE 0x01
#define SII9022_SYS_CTRL_OUTPUT_HDMI 1
#define SII9022_SYS_CTRL_OUTPUT_DVI 0
#define SII9022_REG_CHIPID 0x1b
#define SII9022_INT_ENABLE 0x3c
#define SII9022_INT_STATUS 0x3d
#define SII9022_INT_STATUS_HOTPLUG 0x01;
#define SII9022_INT_STATUS_PLUGGED 0x04;

#define TYPE_SII9022 "sii9022"
OBJECT_DECLARE_SIMPLE_TYPE(sii9022_state, SII9022)

struct sii9022_state {
    I2CSlave parent_obj;
    uint8_t ptr;
    bool addr_byte;
    bool ddc_req;
    bool ddc_skip_finish;
    bool ddc;
};

static const VMStateDescription vmstate_sii9022 = {
    .name = "sii9022",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_I2C_SLAVE(parent_obj, sii9022_state),
        VMSTATE_UINT8(ptr, sii9022_state),
        VMSTATE_BOOL(addr_byte, sii9022_state),
        VMSTATE_BOOL(ddc_req, sii9022_state),
        VMSTATE_BOOL(ddc_skip_finish, sii9022_state),
        VMSTATE_BOOL(ddc, sii9022_state),
        VMSTATE_END_OF_LIST()
    }
};

static int sii9022_event(I2CSlave *i2c, enum i2c_event event)
{
    sii9022_state *s = SII9022(i2c);

    switch (event) {
    case I2C_START_SEND:
        s->addr_byte = true;
        break;
    case I2C_START_RECV:
        break;
    case I2C_FINISH:
        break;
    case I2C_NACK:
        break;
    }

    return 0;
}

static uint8_t sii9022_rx(I2CSlave *i2c)
{
    sii9022_state *s = SII9022(i2c);
    uint8_t res = 0x00;

    switch (s->ptr) {
    case SII9022_SYS_CTRL_DATA:
        if (s->ddc_req) {
            /* Acknowledge DDC bus request */
            res = SII9022_SYS_CTRL_DDC_BUS_GRTD | SII9022_SYS_CTRL_DDC_BUS_REQ;
        }
        break;
    case SII9022_REG_CHIPID:
        res = 0xb0;
        break;
    case SII9022_INT_STATUS:
        /* Something is cold-plugged in, no interrupts */
        res = SII9022_INT_STATUS_PLUGGED;
        break;
    default:
        break;
    }

    trace_sii9022_read_reg(s->ptr, res);
    s->ptr++;

    return res;
}

static int sii9022_tx(I2CSlave *i2c, uint8_t data)
{
    sii9022_state *s = SII9022(i2c);

    if (s->addr_byte) {
        s->ptr = data;
        s->addr_byte = false;
        return 0;
    }

    switch (s->ptr) {
    case SII9022_SYS_CTRL_DATA:
        if (data & SII9022_SYS_CTRL_DDC_BUS_REQ) {
            s->ddc_req = true;
            if (data & SII9022_SYS_CTRL_DDC_BUS_GRTD) {
                s->ddc = true;
                /* Skip this finish since we just switched to DDC */
                s->ddc_skip_finish = true;
                trace_sii9022_switch_mode("DDC");
            }
        } else {
            s->ddc_req = false;
            s->ddc = false;
            trace_sii9022_switch_mode("normal");
        }
        break;
    default:
        break;
    }

    trace_sii9022_write_reg(s->ptr, data);
    s->ptr++;

    return 0;
}

static void sii9022_reset(DeviceState *dev)
{
    sii9022_state *s = SII9022(dev);

    s->ptr = 0;
    s->addr_byte = false;
    s->ddc_req = false;
    s->ddc_skip_finish = false;
    s->ddc = false;
}

static void sii9022_realize(DeviceState *dev, Error **errp)
{
    I2CBus *bus;

    bus = I2C_BUS(qdev_get_parent_bus(dev));
    i2c_slave_create_simple(bus, TYPE_I2CDDC, 0x50);
}

static void sii9022_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    I2CSlaveClass *k = I2C_SLAVE_CLASS(klass);

    k->event = sii9022_event;
    k->recv = sii9022_rx;
    k->send = sii9022_tx;
    dc->reset = sii9022_reset;
    dc->realize = sii9022_realize;
    dc->vmsd = &vmstate_sii9022;
}

static const TypeInfo sii9022_info = {
    .name          = TYPE_SII9022,
    .parent        = TYPE_I2C_SLAVE,
    .instance_size = sizeof(sii9022_state),
    .class_init    = sii9022_class_init,
};

static void sii9022_register_types(void)
{
    type_register_static(&sii9022_info);
}

type_init(sii9022_register_types)

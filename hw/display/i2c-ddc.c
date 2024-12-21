/* A simple I2C slave for returning monitor EDID data via DDC.
 *
 * Copyright (c) 2011 Linaro Limited
 * Written by Peter Maydell
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/i2c/i2c.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "hw/display/i2c-ddc.h"

#ifndef DEBUG_I2CDDC
#define DEBUG_I2CDDC 0
#endif

#define DPRINTF(fmt, ...) do {                                                 \
    if (DEBUG_I2CDDC) {                                                        \
        qemu_log("i2c-ddc: " fmt , ## __VA_ARGS__);                            \
    }                                                                          \
} while (0)

static void i2c_ddc_reset(DeviceState *ds)
{
    I2CDDCState *s = I2CDDC(ds);

    s->firstbyte = false;
    s->reg = 0;
}

static int i2c_ddc_event(I2CSlave *i2c, enum i2c_event event)
{
    I2CDDCState *s = I2CDDC(i2c);

    if (event == I2C_START_SEND) {
        s->firstbyte = true;
    }

    return 0;
}

static uint8_t i2c_ddc_rx(I2CSlave *i2c)
{
    I2CDDCState *s = I2CDDC(i2c);

    int value;
    value = s->edid_blob[s->reg % sizeof(s->edid_blob)];
    s->reg++;
    return value;
}

static int i2c_ddc_tx(I2CSlave *i2c, uint8_t data)
{
    I2CDDCState *s = I2CDDC(i2c);
    if (s->firstbyte) {
        s->reg = data;
        s->firstbyte = false;
        DPRINTF("[EDID] Written new pointer: %u\n", data);
        return 0;
    }

    /* Ignore all writes */
    s->reg++;
    return 0;
}

static void i2c_ddc_init(Object *obj)
{
    I2CDDCState *s = I2CDDC(obj);

    qemu_edid_generate(s->edid_blob, sizeof(s->edid_blob), &s->edid_info);
}

static const VMStateDescription vmstate_i2c_ddc = {
    .name = TYPE_I2CDDC,
    .version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_BOOL(firstbyte, I2CDDCState),
        VMSTATE_UINT8(reg, I2CDDCState),
        VMSTATE_END_OF_LIST()
    }
};

static const Property i2c_ddc_properties[] = {
    DEFINE_EDID_PROPERTIES(I2CDDCState, edid_info),
};

static void i2c_ddc_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    I2CSlaveClass *isc = I2C_SLAVE_CLASS(oc);

    device_class_set_legacy_reset(dc, i2c_ddc_reset);
    dc->vmsd = &vmstate_i2c_ddc;
    device_class_set_props(dc, i2c_ddc_properties);
    isc->event = i2c_ddc_event;
    isc->recv = i2c_ddc_rx;
    isc->send = i2c_ddc_tx;
}

static const TypeInfo i2c_ddc_info = {
    .name = TYPE_I2CDDC,
    .parent = TYPE_I2C_SLAVE,
    .instance_size = sizeof(I2CDDCState),
    .instance_init = i2c_ddc_init,
    .class_init = i2c_ddc_class_init
};

static void ddc_register_devices(void)
{
    type_register_static(&i2c_ddc_info);
}

type_init(ddc_register_devices);

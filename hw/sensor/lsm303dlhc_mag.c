/*
 * LSM303DLHC I2C magnetometer.
 *
 * Copyright (C) 2021 Linaro Ltd.
 * Written by Kevin Townsend <kevin.townsend@linaro.org>
 *
 * Based on: https://www.st.com/resource/en/datasheet/lsm303dlhc.pdf
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

/*
 * The I2C address associated with this device is set on the command-line when
 * initialising the machine, but the following address is standard: 0x1E.
 *
 * Get and set functions for 'mag-x', 'mag-y' and 'mag-z' assume that
 * 1 = 0.001 uT. (NOTE the 1 gauss = 100 uT, so setting a value of 100,000
 * would be equal to 1 gauss or 100 uT.)
 *
 * Get and set functions for 'temperature' assume that 1 = 0.001 C, so 23.6 C
 * would be equal to 23600.
 */

#include "qemu/osdep.h"
#include "hw/i2c/i2c.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "qemu/module.h"
#include "qemu/log.h"

enum LSM303DLHCMagReg {
    LSM303DLHC_MAG_REG_CRA          = 0x00,
    LSM303DLHC_MAG_REG_CRB          = 0x01,
    LSM303DLHC_MAG_REG_MR           = 0x02,
    LSM303DLHC_MAG_REG_OUT_X_H      = 0x03,
    LSM303DLHC_MAG_REG_OUT_X_L      = 0x04,
    LSM303DLHC_MAG_REG_OUT_Z_H      = 0x05,
    LSM303DLHC_MAG_REG_OUT_Z_L      = 0x06,
    LSM303DLHC_MAG_REG_OUT_Y_H      = 0x07,
    LSM303DLHC_MAG_REG_OUT_Y_L      = 0x08,
    LSM303DLHC_MAG_REG_SR           = 0x09,
    LSM303DLHC_MAG_REG_IRA          = 0x0A,
    LSM303DLHC_MAG_REG_IRB          = 0x0B,
    LSM303DLHC_MAG_REG_IRC          = 0x0C,
    LSM303DLHC_MAG_REG_TEMP_OUT_H   = 0x31,
    LSM303DLHC_MAG_REG_TEMP_OUT_L   = 0x32
};

typedef struct LSM303DLHCMagState {
    I2CSlave parent_obj;
    uint8_t cra;
    uint8_t crb;
    uint8_t mr;
    int16_t x;
    int16_t z;
    int16_t y;
    int16_t x_lock;
    int16_t z_lock;
    int16_t y_lock;
    uint8_t sr;
    uint8_t ira;
    uint8_t irb;
    uint8_t irc;
    int16_t temperature;
    int16_t temperature_lock;
    uint8_t len;
    uint8_t buf;
    uint8_t pointer;
} LSM303DLHCMagState;

#define TYPE_LSM303DLHC_MAG "lsm303dlhc_mag"
OBJECT_DECLARE_SIMPLE_TYPE(LSM303DLHCMagState, LSM303DLHC_MAG)

/*
 * Conversion factor from Gauss to sensor values for each GN gain setting,
 * in units "lsb per Gauss" (see data sheet table 3). There is no documented
 * behaviour if the GN setting in CRB is incorrectly set to 0b000;
 * we arbitrarily make it the same as 0b001.
 */
uint32_t xy_gain[] = { 1100, 1100, 855, 670, 450, 400, 330, 230 };
uint32_t z_gain[] = { 980, 980, 760, 600, 400, 355, 295, 205 };

static void lsm303dlhc_mag_get_x(Object *obj, Visitor *v, const char *name,
                                 void *opaque, Error **errp)
{
    LSM303DLHCMagState *s = LSM303DLHC_MAG(obj);
    int gm = extract32(s->crb, 5, 3);

    /* Convert to uT where 1000 = 1 uT. Conversion factor depends on gain. */
    int64_t value = muldiv64(s->x, 100000, xy_gain[gm]);
    visit_type_int(v, name, &value, errp);
}

static void lsm303dlhc_mag_get_y(Object *obj, Visitor *v, const char *name,
                                 void *opaque, Error **errp)
{
    LSM303DLHCMagState *s = LSM303DLHC_MAG(obj);
    int gm = extract32(s->crb, 5, 3);

    /* Convert to uT where 1000 = 1 uT. Conversion factor depends on gain. */
    int64_t value = muldiv64(s->y, 100000, xy_gain[gm]);
    visit_type_int(v, name, &value, errp);
}

static void lsm303dlhc_mag_get_z(Object *obj, Visitor *v, const char *name,
                                 void *opaque, Error **errp)
{
    LSM303DLHCMagState *s = LSM303DLHC_MAG(obj);
    int gm = extract32(s->crb, 5, 3);

    /* Convert to uT where 1000 = 1 uT. Conversion factor depends on gain. */
    int64_t value = muldiv64(s->z, 100000, z_gain[gm]);
    visit_type_int(v, name, &value, errp);
}

static void lsm303dlhc_mag_set_x(Object *obj, Visitor *v, const char *name,
                                 void *opaque, Error **errp)
{
    LSM303DLHCMagState *s = LSM303DLHC_MAG(obj);
    int64_t value;
    int64_t reg;
    int gm = extract32(s->crb, 5, 3);

    if (!visit_type_int(v, name, &value, errp)) {
        return;
    }

    reg = muldiv64(value, xy_gain[gm], 100000);

    /* Make sure we are within a 12-bit limit. */
    if (reg > 2047 || reg < -2048) {
        error_setg(errp, "value %" PRId64 " out of register's range", value);
        return;
    }

    s->x = (int16_t)reg;
}

static void lsm303dlhc_mag_set_y(Object *obj, Visitor *v, const char *name,
                                 void *opaque, Error **errp)
{
    LSM303DLHCMagState *s = LSM303DLHC_MAG(obj);
    int64_t value;
    int64_t reg;
    int gm = extract32(s->crb, 5, 3);

    if (!visit_type_int(v, name, &value, errp)) {
        return;
    }

    reg = muldiv64(value, xy_gain[gm], 100000);

    /* Make sure we are within a 12-bit limit. */
    if (reg > 2047 || reg < -2048) {
        error_setg(errp, "value %" PRId64 " out of register's range", value);
        return;
    }

    s->y = (int16_t)reg;
}

static void lsm303dlhc_mag_set_z(Object *obj, Visitor *v, const char *name,
                                 void *opaque, Error **errp)
{
    LSM303DLHCMagState *s = LSM303DLHC_MAG(obj);
    int64_t value;
    int64_t reg;
    int gm = extract32(s->crb, 5, 3);

    if (!visit_type_int(v, name, &value, errp)) {
        return;
    }

    reg = muldiv64(value, z_gain[gm], 100000);

    /* Make sure we are within a 12-bit limit. */
    if (reg > 2047 || reg < -2048) {
        error_setg(errp, "value %" PRId64 " out of register's range", value);
        return;
    }

    s->z = (int16_t)reg;
}

/*
 * Get handler for the temperature property.
 */
static void lsm303dlhc_mag_get_temperature(Object *obj, Visitor *v,
                                           const char *name, void *opaque,
                                           Error **errp)
{
    LSM303DLHCMagState *s = LSM303DLHC_MAG(obj);
    int64_t value;

    /* Convert to 1 lsb = 0.125 C to 1 = 0.001 C for 'temperature' property. */
    value = s->temperature * 125;

    visit_type_int(v, name, &value, errp);
}

/*
 * Set handler for the temperature property.
 */
static void lsm303dlhc_mag_set_temperature(Object *obj, Visitor *v,
                                           const char *name, void *opaque,
                                           Error **errp)
{
    LSM303DLHCMagState *s = LSM303DLHC_MAG(obj);
    int64_t value;

    if (!visit_type_int(v, name, &value, errp)) {
        return;
    }

    /* Input temperature is in 0.001 C units. Convert to 1 lsb = 0.125 C. */
    value /= 125;

    if (value > 2047 || value < -2048) {
        error_setg(errp, "value %" PRId64 " lsb is out of range", value);
        return;
    }

    s->temperature = (int16_t)value;
}

/*
 * Callback handler whenever a 'I2C_START_RECV' (read) event is received.
 */
static void lsm303dlhc_mag_read(LSM303DLHCMagState *s)
{
    /*
     * Set the LOCK bit whenever a new read attempt is made. This will be
     * cleared in I2C_FINISH. Note that DRDY is always set to 1 in this driver.
     */
    s->sr = 0x3;

    /*
     * Copy the current X/Y/Z and temp. values into the locked registers so
     * that 'mag-x', 'mag-y', 'mag-z' and 'temperature' can continue to be
     * updated via QOM, etc., without corrupting the current read event.
     */
    s->x_lock = s->x;
    s->z_lock = s->z;
    s->y_lock = s->y;
    s->temperature_lock = s->temperature;
}

/*
 * Callback handler whenever a 'I2C_FINISH' event is received.
 */
static void lsm303dlhc_mag_finish(LSM303DLHCMagState *s)
{
    /*
     * Clear the LOCK bit when the read attempt terminates.
     * This bit is initially set in the I2C_START_RECV handler.
     */
    s->sr = 0x1;
}

/*
 * Callback handler when a device attempts to write to a register.
 */
static void lsm303dlhc_mag_write(LSM303DLHCMagState *s)
{
    switch (s->pointer) {
    case LSM303DLHC_MAG_REG_CRA:
        s->cra = s->buf;
        break;
    case LSM303DLHC_MAG_REG_CRB:
        /* Make sure gain is at least 1, falling back to 1 on an error. */
        if (s->buf >> 5 == 0) {
            s->buf = 1 << 5;
        }
        s->crb = s->buf;
        break;
    case LSM303DLHC_MAG_REG_MR:
        s->mr = s->buf;
        break;
    case LSM303DLHC_MAG_REG_SR:
        s->sr = s->buf;
        break;
    case LSM303DLHC_MAG_REG_IRA:
        s->ira = s->buf;
        break;
    case LSM303DLHC_MAG_REG_IRB:
        s->irb = s->buf;
        break;
    case LSM303DLHC_MAG_REG_IRC:
        s->irc = s->buf;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "reg is read-only: 0x%02X", s->buf);
        break;
    }
}

/*
 * Low-level master-to-slave transaction handler.
 */
static int lsm303dlhc_mag_send(I2CSlave *i2c, uint8_t data)
{
    LSM303DLHCMagState *s = LSM303DLHC_MAG(i2c);

    if (s->len == 0) {
        /* First byte is the reg pointer */
        s->pointer = data;
        s->len++;
    } else if (s->len == 1) {
        /* Second byte is the new register value. */
        s->buf = data;
        lsm303dlhc_mag_write(s);
    } else {
        g_assert_not_reached();
    }

    return 0;
}

/*
 * Low-level slave-to-master transaction handler (read attempts).
 */
static uint8_t lsm303dlhc_mag_recv(I2CSlave *i2c)
{
    LSM303DLHCMagState *s = LSM303DLHC_MAG(i2c);
    uint8_t resp;

    switch (s->pointer) {
    case LSM303DLHC_MAG_REG_CRA:
        resp = s->cra;
        break;
    case LSM303DLHC_MAG_REG_CRB:
        resp = s->crb;
        break;
    case LSM303DLHC_MAG_REG_MR:
        resp = s->mr;
        break;
    case LSM303DLHC_MAG_REG_OUT_X_H:
        resp = (uint8_t)(s->x_lock >> 8);
        break;
    case LSM303DLHC_MAG_REG_OUT_X_L:
        resp = (uint8_t)(s->x_lock);
        break;
    case LSM303DLHC_MAG_REG_OUT_Z_H:
        resp = (uint8_t)(s->z_lock >> 8);
        break;
    case LSM303DLHC_MAG_REG_OUT_Z_L:
        resp = (uint8_t)(s->z_lock);
        break;
    case LSM303DLHC_MAG_REG_OUT_Y_H:
        resp = (uint8_t)(s->y_lock >> 8);
        break;
    case LSM303DLHC_MAG_REG_OUT_Y_L:
        resp = (uint8_t)(s->y_lock);
        break;
    case LSM303DLHC_MAG_REG_SR:
        resp = s->sr;
        break;
    case LSM303DLHC_MAG_REG_IRA:
        resp = s->ira;
        break;
    case LSM303DLHC_MAG_REG_IRB:
        resp = s->irb;
        break;
    case LSM303DLHC_MAG_REG_IRC:
        resp = s->irc;
        break;
    case LSM303DLHC_MAG_REG_TEMP_OUT_H:
        /* Check if the temperature sensor is enabled or not (CRA & 0x80). */
        if (s->cra & 0x80) {
            resp = (uint8_t)(s->temperature_lock >> 8);
        } else {
            resp = 0;
        }
        break;
    case LSM303DLHC_MAG_REG_TEMP_OUT_L:
        if (s->cra & 0x80) {
            resp = (uint8_t)(s->temperature_lock & 0xff);
        } else {
            resp = 0;
        }
        break;
    default:
        resp = 0;
        break;
    }

    /*
     * The address pointer on the LSM303DLHC auto-increments whenever a byte
     * is read, without the master device having to request the next address.
     *
     * The auto-increment process has the following logic:
     *
     *   - if (s->pointer == 8) then s->pointer = 3
     *   - else: if (s->pointer == 12) then s->pointer = 0
     *   - else: s->pointer += 1
     *
     * Reading an invalid address return 0.
     */
    if (s->pointer == LSM303DLHC_MAG_REG_OUT_Y_L) {
        s->pointer = LSM303DLHC_MAG_REG_OUT_X_H;
    } else if (s->pointer == LSM303DLHC_MAG_REG_IRC) {
        s->pointer = LSM303DLHC_MAG_REG_CRA;
    } else {
        s->pointer++;
    }

    return resp;
}

/*
 * Bus state change handler.
 */
static int lsm303dlhc_mag_event(I2CSlave *i2c, enum i2c_event event)
{
    LSM303DLHCMagState *s = LSM303DLHC_MAG(i2c);

    switch (event) {
    case I2C_START_SEND:
        break;
    case I2C_START_RECV:
        lsm303dlhc_mag_read(s);
        break;
    case I2C_FINISH:
        lsm303dlhc_mag_finish(s);
        break;
    case I2C_NACK:
        break;
    default:
        return -1;
    }

    s->len = 0;
    return 0;
}

/*
 * Device data description using VMSTATE macros.
 */
static const VMStateDescription vmstate_lsm303dlhc_mag = {
    .name = "LSM303DLHC_MAG",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (const VMStateField[]) {

        VMSTATE_I2C_SLAVE(parent_obj, LSM303DLHCMagState),
        VMSTATE_UINT8(len, LSM303DLHCMagState),
        VMSTATE_UINT8(buf, LSM303DLHCMagState),
        VMSTATE_UINT8(pointer, LSM303DLHCMagState),
        VMSTATE_UINT8(cra, LSM303DLHCMagState),
        VMSTATE_UINT8(crb, LSM303DLHCMagState),
        VMSTATE_UINT8(mr, LSM303DLHCMagState),
        VMSTATE_INT16(x, LSM303DLHCMagState),
        VMSTATE_INT16(z, LSM303DLHCMagState),
        VMSTATE_INT16(y, LSM303DLHCMagState),
        VMSTATE_INT16(x_lock, LSM303DLHCMagState),
        VMSTATE_INT16(z_lock, LSM303DLHCMagState),
        VMSTATE_INT16(y_lock, LSM303DLHCMagState),
        VMSTATE_UINT8(sr, LSM303DLHCMagState),
        VMSTATE_UINT8(ira, LSM303DLHCMagState),
        VMSTATE_UINT8(irb, LSM303DLHCMagState),
        VMSTATE_UINT8(irc, LSM303DLHCMagState),
        VMSTATE_INT16(temperature, LSM303DLHCMagState),
        VMSTATE_INT16(temperature_lock, LSM303DLHCMagState),
        VMSTATE_END_OF_LIST()
    }
};

/*
 * Put the device into post-reset default state.
 */
static void lsm303dlhc_mag_default_cfg(LSM303DLHCMagState *s)
{
    /* Set the device into is default reset state. */
    s->len = 0;
    s->pointer = 0;         /* Current register. */
    s->buf = 0;             /* Shared buffer. */
    s->cra = 0x10;          /* Temp Enabled = 0, Data Rate = 15.0 Hz. */
    s->crb = 0x20;          /* Gain = +/- 1.3 Gauss. */
    s->mr = 0x3;            /* Operating Mode = Sleep. */
    s->x = 0;
    s->z = 0;
    s->y = 0;
    s->x_lock = 0;
    s->z_lock = 0;
    s->y_lock = 0;
    s->sr = 0x1;            /* DRDY = 1. */
    s->ira = 0x48;
    s->irb = 0x34;
    s->irc = 0x33;
    s->temperature = 0;     /* Default to 0 degrees C (0/8 lsb = 0 C). */
    s->temperature_lock = 0;
}

/*
 * Callback handler when DeviceState 'reset' is set to true.
 */
static void lsm303dlhc_mag_reset(DeviceState *dev)
{
    I2CSlave *i2c = I2C_SLAVE(dev);
    LSM303DLHCMagState *s = LSM303DLHC_MAG(i2c);

    /* Set the device into its default reset state. */
    lsm303dlhc_mag_default_cfg(s);
}

/*
 * Initialisation of any public properties.
 */
static void lsm303dlhc_mag_initfn(Object *obj)
{
    object_property_add(obj, "mag-x", "int",
                lsm303dlhc_mag_get_x,
                lsm303dlhc_mag_set_x, NULL, NULL);

    object_property_add(obj, "mag-y", "int",
                lsm303dlhc_mag_get_y,
                lsm303dlhc_mag_set_y, NULL, NULL);

    object_property_add(obj, "mag-z", "int",
                lsm303dlhc_mag_get_z,
                lsm303dlhc_mag_set_z, NULL, NULL);

    object_property_add(obj, "temperature", "int",
                lsm303dlhc_mag_get_temperature,
                lsm303dlhc_mag_set_temperature, NULL, NULL);
}

/*
 * Set the virtual method pointers (bus state change, tx/rx, etc.).
 */
static void lsm303dlhc_mag_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    I2CSlaveClass *k = I2C_SLAVE_CLASS(klass);

    device_class_set_legacy_reset(dc, lsm303dlhc_mag_reset);
    dc->vmsd = &vmstate_lsm303dlhc_mag;
    k->event = lsm303dlhc_mag_event;
    k->recv = lsm303dlhc_mag_recv;
    k->send = lsm303dlhc_mag_send;
}

static const TypeInfo lsm303dlhc_mag_info = {
    .name = TYPE_LSM303DLHC_MAG,
    .parent = TYPE_I2C_SLAVE,
    .instance_size = sizeof(LSM303DLHCMagState),
    .instance_init = lsm303dlhc_mag_initfn,
    .class_init = lsm303dlhc_mag_class_init,
};

static void lsm303dlhc_mag_register_types(void)
{
    type_register_static(&lsm303dlhc_mag_info);
}

type_init(lsm303dlhc_mag_register_types)

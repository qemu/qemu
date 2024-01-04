// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2017-2021 Joel Stanley <joel@jms.id.au>, IBM Corporation
 *
 * Infineon DPS310 temperature and humidity sensor
 *
 * https://www.infineon.com/cms/en/product/sensor/pressure-sensors/pressure-sensors-for-iot/dps310/
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/i2c/i2c.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "migration/vmstate.h"

#define NUM_REGISTERS   0x33

typedef struct DPS310State {
    /*< private >*/
    I2CSlave i2c;

    /*< public >*/
    uint8_t regs[NUM_REGISTERS];

    uint8_t len;
    uint8_t pointer;

} DPS310State;

#define TYPE_DPS310 "dps310"
#define DPS310(obj) OBJECT_CHECK(DPS310State, (obj), TYPE_DPS310)

#define DPS310_PRS_B2           0x00
#define DPS310_PRS_B1           0x01
#define DPS310_PRS_B0           0x02
#define DPS310_TMP_B2           0x03
#define DPS310_TMP_B1           0x04
#define DPS310_TMP_B0           0x05
#define DPS310_PRS_CFG          0x06
#define DPS310_TMP_CFG          0x07
#define  DPS310_TMP_RATE_BITS   (0x70)
#define DPS310_MEAS_CFG         0x08
#define  DPS310_MEAS_CTRL_BITS  (0x07)
#define   DPS310_PRESSURE_EN    BIT(0)
#define   DPS310_TEMP_EN        BIT(1)
#define   DPS310_BACKGROUND     BIT(2)
#define  DPS310_PRS_RDY         BIT(4)
#define  DPS310_TMP_RDY         BIT(5)
#define  DPS310_SENSOR_RDY      BIT(6)
#define  DPS310_COEF_RDY        BIT(7)
#define DPS310_CFG_REG          0x09
#define DPS310_RESET            0x0c
#define  DPS310_RESET_MAGIC     (BIT(0) | BIT(3))
#define DPS310_COEF_BASE        0x10
#define DPS310_COEF_LAST        0x21
#define DPS310_COEF_SRC         0x28

static void dps310_reset(DeviceState *dev)
{
    DPS310State *s = DPS310(dev);

    static const uint8_t regs_reset_state[sizeof(s->regs)] = {
        0xfe, 0x2f, 0xee, 0x02, 0x69, 0xa6, 0x00, 0x80, 0xc7, 0x00, 0x00, 0x00,
        0x00, 0x10, 0x00, 0x00, 0x0e, 0x1e, 0xdd, 0x13, 0xca, 0x5f, 0x21, 0x52,
        0xf9, 0xc6, 0x04, 0xd1, 0xdb, 0x47, 0x00, 0x5b, 0xfb, 0x3a, 0x00, 0x00,
        0x20, 0x49, 0x4e, 0xa5, 0x90, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x60, 0x15, 0x02
    };

    memcpy(s->regs, regs_reset_state, sizeof(s->regs));
    s->pointer = 0;

    /* TODO: assert these after some timeout ? */
    s->regs[DPS310_MEAS_CFG] = DPS310_COEF_RDY | DPS310_SENSOR_RDY
        | DPS310_TMP_RDY | DPS310_PRS_RDY;
}

static uint8_t dps310_read(DPS310State *s, uint8_t reg)
{
    if (reg >= sizeof(s->regs)) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: register 0x%02x out of bounds\n",
                      __func__, s->pointer);
        return 0xFF;
    }

    switch (reg) {
    case DPS310_PRS_B2:
    case DPS310_PRS_B1:
    case DPS310_PRS_B0:
    case DPS310_TMP_B2:
    case DPS310_TMP_B1:
    case DPS310_TMP_B0:
    case DPS310_PRS_CFG:
    case DPS310_TMP_CFG:
    case DPS310_MEAS_CFG:
    case DPS310_CFG_REG:
    case DPS310_COEF_BASE...DPS310_COEF_LAST:
    case DPS310_COEF_SRC:
    case 0x32: /* Undocumented register to indicate workaround not required */
        return s->regs[reg];
    default:
        qemu_log_mask(LOG_UNIMP, "%s: register 0x%02x unimplemented\n",
                      __func__, reg);
        return 0xFF;
    }
}

static void dps310_write(DPS310State *s, uint8_t reg, uint8_t data)
{
    if (reg >= sizeof(s->regs)) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: register %d out of bounds\n",
                      __func__, s->pointer);
        return;
    }

    switch (reg) {
    case DPS310_RESET:
        if (data == DPS310_RESET_MAGIC) {
            device_cold_reset(DEVICE(s));
        }
        break;
    case DPS310_PRS_CFG:
    case DPS310_TMP_CFG:
    case DPS310_MEAS_CFG:
    case DPS310_CFG_REG:
        s->regs[reg] = data;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: register 0x%02x unimplemented\n",
                      __func__, reg);
        return;
    }
}

static uint8_t dps310_rx(I2CSlave *i2c)
{
    DPS310State *s = DPS310(i2c);

    if (s->len == 1) {
        return dps310_read(s, s->pointer++);
    } else {
        return 0xFF;
    }
}

static int dps310_tx(I2CSlave *i2c, uint8_t data)
{
    DPS310State *s = DPS310(i2c);

    if (s->len == 0) {
        /*
         * first byte is the register pointer for a read or write
         * operation
         */
        s->pointer = data;
        s->len++;
    } else if (s->len == 1) {
        dps310_write(s, s->pointer++, data);
    }

    return 0;
}

static int dps310_event(I2CSlave *i2c, enum i2c_event event)
{
    DPS310State *s = DPS310(i2c);

    switch (event) {
    case I2C_START_SEND:
        s->pointer = 0xFF;
        s->len = 0;
        break;
    case I2C_START_RECV:
        if (s->len != 1) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: invalid recv sequence\n",
                          __func__);
        }
        break;
    default:
        break;
    }

    return 0;
}

static const VMStateDescription vmstate_dps310 = {
    .name = "DPS310",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8(len, DPS310State),
        VMSTATE_UINT8_ARRAY(regs, DPS310State, NUM_REGISTERS),
        VMSTATE_UINT8(pointer, DPS310State),
        VMSTATE_I2C_SLAVE(i2c, DPS310State),
        VMSTATE_END_OF_LIST()
    }
};

static void dps310_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    I2CSlaveClass *k = I2C_SLAVE_CLASS(klass);

    k->event = dps310_event;
    k->recv = dps310_rx;
    k->send = dps310_tx;
    dc->reset = dps310_reset;
    dc->vmsd = &vmstate_dps310;
}

static const TypeInfo dps310_info = {
    .name          = TYPE_DPS310,
    .parent        = TYPE_I2C_SLAVE,
    .instance_size = sizeof(DPS310State),
    .class_init    = dps310_class_init,
};

static void dps310_register_types(void)
{
    type_register_static(&dps310_info);
}

type_init(dps310_register_types)

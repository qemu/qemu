/*
 * AXP-2XX PMU Emulation, supported lists:
 *   AXP209
 *   AXP221
 *
 * Copyright (C) 2022 Strahinja Jankovic <strahinja.p.jankovic@gmail.com>
 * Copyright (C) 2023 qianfan Zhao <qianfanguijin@163.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * SPDX-License-Identifier: MIT
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qom/object.h"
#include "trace.h"
#include "hw/i2c/i2c.h"
#include "migration/vmstate.h"

#define TYPE_AXP2XX     "axp2xx_pmu"
#define TYPE_AXP209_PMU "axp209_pmu"
#define TYPE_AXP221_PMU "axp221_pmu"

OBJECT_DECLARE_TYPE(AXP2xxI2CState, AXP2xxClass, AXP2XX)

#define NR_REGS                            (0xff)

/* A simple I2C slave which returns values of ID or CNT register. */
typedef struct AXP2xxI2CState {
    /*< private >*/
    I2CSlave i2c;
    /*< public >*/
    uint8_t regs[NR_REGS];  /* peripheral registers */
    uint8_t ptr;            /* current register index */
    uint8_t count;          /* counter used for tx/rx */
} AXP2xxI2CState;

typedef struct AXP2xxClass {
    /*< private >*/
    I2CSlaveClass parent_class;
    /*< public >*/
    void (*reset_enter)(AXP2xxI2CState *s, ResetType type);
} AXP2xxClass;

#define AXP209_CHIP_VERSION_ID             (0x01)
#define AXP209_DC_DC2_OUT_V_CTRL_RESET     (0x16)

/* Reset all counters and load ID register */
static void axp209_reset_enter(AXP2xxI2CState *s, ResetType type)
{
    memset(s->regs, 0, NR_REGS);
    s->ptr = 0;
    s->count = 0;

    s->regs[0x03] = AXP209_CHIP_VERSION_ID;
    s->regs[0x23] = AXP209_DC_DC2_OUT_V_CTRL_RESET;

    s->regs[0x30] = 0x60;
    s->regs[0x32] = 0x46;
    s->regs[0x34] = 0x41;
    s->regs[0x35] = 0x22;
    s->regs[0x36] = 0x5d;
    s->regs[0x37] = 0x08;
    s->regs[0x38] = 0xa5;
    s->regs[0x39] = 0x1f;
    s->regs[0x3a] = 0x68;
    s->regs[0x3b] = 0x5f;
    s->regs[0x3c] = 0xfc;
    s->regs[0x3d] = 0x16;
    s->regs[0x40] = 0xd8;
    s->regs[0x42] = 0xff;
    s->regs[0x43] = 0x3b;
    s->regs[0x80] = 0xe0;
    s->regs[0x82] = 0x83;
    s->regs[0x83] = 0x80;
    s->regs[0x84] = 0x32;
    s->regs[0x86] = 0xff;
    s->regs[0x90] = 0x07;
    s->regs[0x91] = 0xa0;
    s->regs[0x92] = 0x07;
    s->regs[0x93] = 0x07;
}

#define AXP221_PWR_STATUS_ACIN_PRESENT          BIT(7)
#define AXP221_PWR_STATUS_ACIN_AVAIL            BIT(6)
#define AXP221_PWR_STATUS_VBUS_PRESENT          BIT(5)
#define AXP221_PWR_STATUS_VBUS_USED             BIT(4)
#define AXP221_PWR_STATUS_BAT_CHARGING          BIT(2)
#define AXP221_PWR_STATUS_ACIN_VBUS_POWERED     BIT(1)

/* Reset all counters and load ID register */
static void axp221_reset_enter(AXP2xxI2CState *s, ResetType type)
{
    memset(s->regs, 0, NR_REGS);
    s->ptr = 0;
    s->count = 0;

    /* input power status register */
    s->regs[0x00] = AXP221_PWR_STATUS_ACIN_PRESENT
                    | AXP221_PWR_STATUS_ACIN_AVAIL
                    | AXP221_PWR_STATUS_ACIN_VBUS_POWERED;

    s->regs[0x01] = 0x00; /* no battery is connected */

    /*
     * CHIPID register, no documented on datasheet, but it is checked in
     * u-boot spl. I had read it from AXP221s and got 0x06 value.
     * So leave 06h here.
     */
    s->regs[0x03] = 0x06;

    s->regs[0x10] = 0xbf;
    s->regs[0x13] = 0x01;
    s->regs[0x30] = 0x60;
    s->regs[0x31] = 0x03;
    s->regs[0x32] = 0x43;
    s->regs[0x33] = 0xc6;
    s->regs[0x34] = 0x45;
    s->regs[0x35] = 0x0e;
    s->regs[0x36] = 0x5d;
    s->regs[0x37] = 0x08;
    s->regs[0x38] = 0xa5;
    s->regs[0x39] = 0x1f;
    s->regs[0x3c] = 0xfc;
    s->regs[0x3d] = 0x16;
    s->regs[0x80] = 0x80;
    s->regs[0x82] = 0xe0;
    s->regs[0x84] = 0x32;
    s->regs[0x8f] = 0x01;

    s->regs[0x90] = 0x07;
    s->regs[0x91] = 0x1f;
    s->regs[0x92] = 0x07;
    s->regs[0x93] = 0x1f;

    s->regs[0x40] = 0xd8;
    s->regs[0x41] = 0xff;
    s->regs[0x42] = 0x03;
    s->regs[0x43] = 0x03;

    s->regs[0xb8] = 0xc0;
    s->regs[0xb9] = 0x64;
    s->regs[0xe6] = 0xa0;
}

static void axp2xx_reset_enter(Object *obj, ResetType type)
{
    AXP2xxI2CState *s = AXP2XX(obj);
    AXP2xxClass *sc = AXP2XX_GET_CLASS(s);

    sc->reset_enter(s, type);
}

/* Handle events from master. */
static int axp2xx_event(I2CSlave *i2c, enum i2c_event event)
{
    AXP2xxI2CState *s = AXP2XX(i2c);

    s->count = 0;

    return 0;
}

/* Called when master requests read */
static uint8_t axp2xx_rx(I2CSlave *i2c)
{
    AXP2xxI2CState *s = AXP2XX(i2c);
    uint8_t ret = 0xff;

    if (s->ptr < NR_REGS) {
        ret = s->regs[s->ptr++];
    }

    trace_axp2xx_rx(s->ptr - 1, ret);

    return ret;
}

/*
 * Called when master sends write.
 * Update ptr with byte 0, then perform write with second byte.
 */
static int axp2xx_tx(I2CSlave *i2c, uint8_t data)
{
    AXP2xxI2CState *s = AXP2XX(i2c);

    if (s->count == 0) {
        /* Store register address */
        s->ptr = data;
        s->count++;
        trace_axp2xx_select(data);
    } else {
        trace_axp2xx_tx(s->ptr, data);
        s->regs[s->ptr++] = data;
    }

    return 0;
}

static const VMStateDescription vmstate_axp2xx = {
    .name = TYPE_AXP2XX,
    .version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8_ARRAY(regs, AXP2xxI2CState, NR_REGS),
        VMSTATE_UINT8(ptr, AXP2xxI2CState),
        VMSTATE_UINT8(count, AXP2xxI2CState),
        VMSTATE_END_OF_LIST()
    }
};

static void axp2xx_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    I2CSlaveClass *isc = I2C_SLAVE_CLASS(oc);
    ResettableClass *rc = RESETTABLE_CLASS(oc);

    rc->phases.enter = axp2xx_reset_enter;
    dc->vmsd = &vmstate_axp2xx;
    isc->event = axp2xx_event;
    isc->recv = axp2xx_rx;
    isc->send = axp2xx_tx;
}

static const TypeInfo axp2xx_info = {
    .name = TYPE_AXP2XX,
    .parent = TYPE_I2C_SLAVE,
    .instance_size = sizeof(AXP2xxI2CState),
    .class_size = sizeof(AXP2xxClass),
    .class_init = axp2xx_class_init,
    .abstract = true,
};

static void axp209_class_init(ObjectClass *oc, void *data)
{
    AXP2xxClass *sc = AXP2XX_CLASS(oc);

    sc->reset_enter = axp209_reset_enter;
}

static const TypeInfo axp209_info = {
    .name = TYPE_AXP209_PMU,
    .parent = TYPE_AXP2XX,
    .class_init = axp209_class_init
};

static void axp221_class_init(ObjectClass *oc, void *data)
{
    AXP2xxClass *sc = AXP2XX_CLASS(oc);

    sc->reset_enter = axp221_reset_enter;
}

static const TypeInfo axp221_info = {
    .name = TYPE_AXP221_PMU,
    .parent = TYPE_AXP2XX,
    .class_init = axp221_class_init,
};

static void axp2xx_register_devices(void)
{
    type_register_static(&axp2xx_info);
    type_register_static(&axp209_info);
    type_register_static(&axp221_info);
}

type_init(axp2xx_register_devices);

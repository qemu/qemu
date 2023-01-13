/*
 * AXP-209 PMU Emulation
 *
 * Copyright (C) 2022 Strahinja Jankovic <strahinja.p.jankovic@gmail.com>
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
#include "trace.h"
#include "hw/i2c/i2c.h"
#include "migration/vmstate.h"

#define TYPE_AXP209_PMU "axp209_pmu"

#define AXP209(obj) \
    OBJECT_CHECK(AXP209I2CState, (obj), TYPE_AXP209_PMU)

/* registers */
enum {
    REG_POWER_STATUS = 0x0u,
    REG_OPERATING_MODE,
    REG_OTG_VBUS_STATUS,
    REG_CHIP_VERSION,
    REG_DATA_CACHE_0,
    REG_DATA_CACHE_1,
    REG_DATA_CACHE_2,
    REG_DATA_CACHE_3,
    REG_DATA_CACHE_4,
    REG_DATA_CACHE_5,
    REG_DATA_CACHE_6,
    REG_DATA_CACHE_7,
    REG_DATA_CACHE_8,
    REG_DATA_CACHE_9,
    REG_DATA_CACHE_A,
    REG_DATA_CACHE_B,
    REG_POWER_OUTPUT_CTRL = 0x12u,
    REG_DC_DC2_OUT_V_CTRL = 0x23u,
    REG_DC_DC2_DVS_CTRL = 0x25u,
    REG_DC_DC3_OUT_V_CTRL = 0x27u,
    REG_LDO2_4_OUT_V_CTRL,
    REG_LDO3_OUT_V_CTRL,
    REG_VBUS_CH_MGMT = 0x30u,
    REG_SHUTDOWN_V_CTRL,
    REG_SHUTDOWN_CTRL,
    REG_CHARGE_CTRL_1,
    REG_CHARGE_CTRL_2,
    REG_SPARE_CHARGE_CTRL,
    REG_PEK_KEY_CTRL,
    REG_DC_DC_FREQ_SET,
    REG_CHR_TEMP_TH_SET,
    REG_CHR_HIGH_TEMP_TH_CTRL,
    REG_IPSOUT_WARN_L1,
    REG_IPSOUT_WARN_L2,
    REG_DISCHR_TEMP_TH_SET,
    REG_DISCHR_HIGH_TEMP_TH_CTRL,
    REG_IRQ_BANK_1_CTRL = 0x40u,
    REG_IRQ_BANK_2_CTRL,
    REG_IRQ_BANK_3_CTRL,
    REG_IRQ_BANK_4_CTRL,
    REG_IRQ_BANK_5_CTRL,
    REG_IRQ_BANK_1_STAT = 0x48u,
    REG_IRQ_BANK_2_STAT,
    REG_IRQ_BANK_3_STAT,
    REG_IRQ_BANK_4_STAT,
    REG_IRQ_BANK_5_STAT,
    REG_ADC_ACIN_V_H = 0x56u,
    REG_ADC_ACIN_V_L,
    REG_ADC_ACIN_CURR_H,
    REG_ADC_ACIN_CURR_L,
    REG_ADC_VBUS_V_H,
    REG_ADC_VBUS_V_L,
    REG_ADC_VBUS_CURR_H,
    REG_ADC_VBUS_CURR_L,
    REG_ADC_INT_TEMP_H,
    REG_ADC_INT_TEMP_L,
    REG_ADC_TEMP_SENS_V_H = 0x62u,
    REG_ADC_TEMP_SENS_V_L,
    REG_ADC_BAT_V_H = 0x78u,
    REG_ADC_BAT_V_L,
    REG_ADC_BAT_DISCHR_CURR_H,
    REG_ADC_BAT_DISCHR_CURR_L,
    REG_ADC_BAT_CHR_CURR_H,
    REG_ADC_BAT_CHR_CURR_L,
    REG_ADC_IPSOUT_V_H,
    REG_ADC_IPSOUT_V_L,
    REG_DC_DC_MOD_SEL = 0x80u,
    REG_ADC_EN_1,
    REG_ADC_EN_2,
    REG_ADC_SR_CTRL,
    REG_ADC_IN_RANGE,
    REG_GPIO1_ADC_IRQ_RISING_TH,
    REG_GPIO1_ADC_IRQ_FALLING_TH,
    REG_TIMER_CTRL = 0x8au,
    REG_VBUS_CTRL_MON_SRP,
    REG_OVER_TEMP_SHUTDOWN = 0x8fu,
    REG_GPIO0_FEAT_SET,
    REG_GPIO_OUT_HIGH_SET,
    REG_GPIO1_FEAT_SET,
    REG_GPIO2_FEAT_SET,
    REG_GPIO_SIG_STATE_SET_MON,
    REG_GPIO3_SET,
    REG_COULOMB_CNTR_CTRL = 0xb8u,
    REG_POWER_MEAS_RES,
    NR_REGS
};

#define AXP209_CHIP_VERSION_ID             (0x01)
#define AXP209_DC_DC2_OUT_V_CTRL_RESET     (0x16)
#define AXP209_IRQ_BANK_1_CTRL_RESET       (0xd8)

/* A simple I2C slave which returns values of ID or CNT register. */
typedef struct AXP209I2CState {
    /*< private >*/
    I2CSlave i2c;
    /*< public >*/
    uint8_t regs[NR_REGS];  /* peripheral registers */
    uint8_t ptr;            /* current register index */
    uint8_t count;          /* counter used for tx/rx */
} AXP209I2CState;

/* Reset all counters and load ID register */
static void axp209_reset_enter(Object *obj, ResetType type)
{
    AXP209I2CState *s = AXP209(obj);

    memset(s->regs, 0, NR_REGS);
    s->ptr = 0;
    s->count = 0;
    s->regs[REG_CHIP_VERSION] = AXP209_CHIP_VERSION_ID;
    s->regs[REG_DC_DC2_OUT_V_CTRL] = AXP209_DC_DC2_OUT_V_CTRL_RESET;
    s->regs[REG_IRQ_BANK_1_CTRL] = AXP209_IRQ_BANK_1_CTRL_RESET;
}

/* Handle events from master. */
static int axp209_event(I2CSlave *i2c, enum i2c_event event)
{
    AXP209I2CState *s = AXP209(i2c);

    s->count = 0;

    return 0;
}

/* Called when master requests read */
static uint8_t axp209_rx(I2CSlave *i2c)
{
    AXP209I2CState *s = AXP209(i2c);
    uint8_t ret = 0xff;

    if (s->ptr < NR_REGS) {
        ret = s->regs[s->ptr++];
    }

    trace_axp209_rx(s->ptr - 1, ret);

    return ret;
}

/*
 * Called when master sends write.
 * Update ptr with byte 0, then perform write with second byte.
 */
static int axp209_tx(I2CSlave *i2c, uint8_t data)
{
    AXP209I2CState *s = AXP209(i2c);

    if (s->count == 0) {
        /* Store register address */
        s->ptr = data;
        s->count++;
        trace_axp209_select(data);
    } else {
        trace_axp209_tx(s->ptr, data);
        if (s->ptr == REG_DC_DC2_OUT_V_CTRL) {
            s->regs[s->ptr++] = data;
        }
    }

    return 0;
}

static const VMStateDescription vmstate_axp209 = {
    .name = TYPE_AXP209_PMU,
    .version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8_ARRAY(regs, AXP209I2CState, NR_REGS),
        VMSTATE_UINT8(count, AXP209I2CState),
        VMSTATE_UINT8(ptr, AXP209I2CState),
        VMSTATE_END_OF_LIST()
    }
};

static void axp209_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    I2CSlaveClass *isc = I2C_SLAVE_CLASS(oc);
    ResettableClass *rc = RESETTABLE_CLASS(oc);

    rc->phases.enter = axp209_reset_enter;
    dc->vmsd = &vmstate_axp209;
    isc->event = axp209_event;
    isc->recv = axp209_rx;
    isc->send = axp209_tx;
}

static const TypeInfo axp209_info = {
    .name = TYPE_AXP209_PMU,
    .parent = TYPE_I2C_SLAVE,
    .instance_size = sizeof(AXP209I2CState),
    .class_init = axp209_class_init
};

static void axp209_register_devices(void)
{
    type_register_static(&axp209_info);
}

type_init(axp209_register_devices);

/*
 * Nuvoton NPCM7xx PWM Module
 *
 * Copyright 2020 Google LLC
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

#include "qemu/osdep.h"
#include "hw/irq.h"
#include "hw/qdev-clock.h"
#include "hw/qdev-properties.h"
#include "hw/misc/npcm7xx_pwm.h"
#include "hw/registerfields.h"
#include "migration/vmstate.h"
#include "qemu/bitops.h"
#include "qemu/error-report.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/units.h"
#include "trace.h"

REG32(NPCM7XX_PWM_PPR, 0x00);
REG32(NPCM7XX_PWM_CSR, 0x04);
REG32(NPCM7XX_PWM_PCR, 0x08);
REG32(NPCM7XX_PWM_CNR0, 0x0c);
REG32(NPCM7XX_PWM_CMR0, 0x10);
REG32(NPCM7XX_PWM_PDR0, 0x14);
REG32(NPCM7XX_PWM_CNR1, 0x18);
REG32(NPCM7XX_PWM_CMR1, 0x1c);
REG32(NPCM7XX_PWM_PDR1, 0x20);
REG32(NPCM7XX_PWM_CNR2, 0x24);
REG32(NPCM7XX_PWM_CMR2, 0x28);
REG32(NPCM7XX_PWM_PDR2, 0x2c);
REG32(NPCM7XX_PWM_CNR3, 0x30);
REG32(NPCM7XX_PWM_CMR3, 0x34);
REG32(NPCM7XX_PWM_PDR3, 0x38);
REG32(NPCM7XX_PWM_PIER, 0x3c);
REG32(NPCM7XX_PWM_PIIR, 0x40);
REG32(NPCM7XX_PWM_PWDR0, 0x44);
REG32(NPCM7XX_PWM_PWDR1, 0x48);
REG32(NPCM7XX_PWM_PWDR2, 0x4c);
REG32(NPCM7XX_PWM_PWDR3, 0x50);

/* Register field definitions. */
#define NPCM7XX_PPR(rv, index)      extract32((rv), npcm7xx_ppr_base[index], 8)
#define NPCM7XX_CSR(rv, index)      extract32((rv), npcm7xx_csr_base[index], 3)
#define NPCM7XX_CH(rv, index)       extract32((rv), npcm7xx_ch_base[index], 4)
#define NPCM7XX_CH_EN               BIT(0)
#define NPCM7XX_CH_INV              BIT(2)
#define NPCM7XX_CH_MOD              BIT(3)

#define NPCM7XX_MAX_CMR             65535
#define NPCM7XX_MAX_CNR             65535

/* Offset of each PWM channel's prescaler in the PPR register. */
static const int npcm7xx_ppr_base[] = { 0, 0, 8, 8 };
/* Offset of each PWM channel's clock selector in the CSR register. */
static const int npcm7xx_csr_base[] = { 0, 4, 8, 12 };
/* Offset of each PWM channel's control variable in the PCR register. */
static const int npcm7xx_ch_base[] = { 0, 8, 12, 16 };

static uint32_t npcm7xx_pwm_calculate_freq(NPCM7xxPWM *p)
{
    uint32_t ppr;
    uint32_t csr;
    uint32_t freq;

    if (!p->running) {
        return 0;
    }

    csr = NPCM7XX_CSR(p->module->csr, p->index);
    ppr = NPCM7XX_PPR(p->module->ppr, p->index);
    freq = clock_get_hz(p->module->clock);
    freq /= ppr + 1;
    /* csr can only be 0~4 */
    if (csr > 4) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid csr value %u\n",
                      __func__, csr);
        csr = 4;
    }
    /* freq won't be changed if csr == 4. */
    if (csr < 4) {
        freq >>= csr + 1;
    }

    return freq / (p->cnr + 1);
}

static uint32_t npcm7xx_pwm_calculate_duty(NPCM7xxPWM *p)
{
    uint32_t duty;

    if (p->running) {
        if (p->cnr == 0) {
            duty = 0;
        } else if (p->cmr >= p->cnr) {
            duty = NPCM7XX_PWM_MAX_DUTY;
        } else {
            duty = (uint64_t)NPCM7XX_PWM_MAX_DUTY * (p->cmr + 1) / (p->cnr + 1);
        }
    } else {
        duty = 0;
    }

    if (p->inverted) {
        duty = NPCM7XX_PWM_MAX_DUTY - duty;
    }

    return duty;
}

static void npcm7xx_pwm_update_freq(NPCM7xxPWM *p)
{
    uint32_t freq = npcm7xx_pwm_calculate_freq(p);

    if (freq != p->freq) {
        trace_npcm7xx_pwm_update_freq(DEVICE(p->module)->canonical_path,
                                      p->index, p->freq, freq);
        p->freq = freq;
    }
}

static void npcm7xx_pwm_update_duty(NPCM7xxPWM *p)
{
    uint32_t duty = npcm7xx_pwm_calculate_duty(p);

    if (duty != p->duty) {
        trace_npcm7xx_pwm_update_duty(DEVICE(p->module)->canonical_path,
                                      p->index, p->duty, duty);
        p->duty = duty;
        qemu_set_irq(p->module->duty_gpio_out[p->index], p->duty);
    }
}

static void npcm7xx_pwm_update_output(NPCM7xxPWM *p)
{
    npcm7xx_pwm_update_freq(p);
    npcm7xx_pwm_update_duty(p);
}

static void npcm7xx_pwm_write_ppr(NPCM7xxPWMState *s, uint32_t new_ppr)
{
    int i;
    uint32_t old_ppr = s->ppr;

    QEMU_BUILD_BUG_ON(ARRAY_SIZE(npcm7xx_ppr_base) != NPCM7XX_PWM_PER_MODULE);
    s->ppr = new_ppr;
    for (i = 0; i < NPCM7XX_PWM_PER_MODULE; ++i) {
        if (NPCM7XX_PPR(old_ppr, i) != NPCM7XX_PPR(new_ppr, i)) {
            npcm7xx_pwm_update_freq(&s->pwm[i]);
        }
    }
}

static void npcm7xx_pwm_write_csr(NPCM7xxPWMState *s, uint32_t new_csr)
{
    int i;
    uint32_t old_csr = s->csr;

    QEMU_BUILD_BUG_ON(ARRAY_SIZE(npcm7xx_csr_base) != NPCM7XX_PWM_PER_MODULE);
    s->csr = new_csr;
    for (i = 0; i < NPCM7XX_PWM_PER_MODULE; ++i) {
        if (NPCM7XX_CSR(old_csr, i) != NPCM7XX_CSR(new_csr, i)) {
            npcm7xx_pwm_update_freq(&s->pwm[i]);
        }
    }
}

static void npcm7xx_pwm_write_pcr(NPCM7xxPWMState *s, uint32_t new_pcr)
{
    int i;
    bool inverted;
    uint32_t pcr;
    NPCM7xxPWM *p;

    s->pcr = new_pcr;
    QEMU_BUILD_BUG_ON(ARRAY_SIZE(npcm7xx_ch_base) != NPCM7XX_PWM_PER_MODULE);
    for (i = 0; i < NPCM7XX_PWM_PER_MODULE; ++i) {
        p = &s->pwm[i];
        pcr = NPCM7XX_CH(new_pcr, i);
        inverted = pcr & NPCM7XX_CH_INV;

        /*
         * We only run a PWM channel with toggle mode. Single-shot mode does not
         * generate frequency and duty-cycle values.
         */
        if ((pcr & NPCM7XX_CH_EN) && (pcr & NPCM7XX_CH_MOD)) {
            if (p->running) {
                /* Re-run this PWM channel if inverted changed. */
                if (p->inverted ^ inverted) {
                    p->inverted = inverted;
                    npcm7xx_pwm_update_duty(p);
                }
            } else {
                /* Run this PWM channel. */
                p->running = true;
                p->inverted = inverted;
                npcm7xx_pwm_update_output(p);
            }
        } else {
            /* Clear this PWM channel. */
            p->running = false;
            p->inverted = inverted;
            npcm7xx_pwm_update_output(p);
        }
    }

}

static hwaddr npcm7xx_cnr_index(hwaddr offset)
{
    switch (offset) {
    case A_NPCM7XX_PWM_CNR0:
        return 0;
    case A_NPCM7XX_PWM_CNR1:
        return 1;
    case A_NPCM7XX_PWM_CNR2:
        return 2;
    case A_NPCM7XX_PWM_CNR3:
        return 3;
    default:
        g_assert_not_reached();
    }
}

static hwaddr npcm7xx_cmr_index(hwaddr offset)
{
    switch (offset) {
    case A_NPCM7XX_PWM_CMR0:
        return 0;
    case A_NPCM7XX_PWM_CMR1:
        return 1;
    case A_NPCM7XX_PWM_CMR2:
        return 2;
    case A_NPCM7XX_PWM_CMR3:
        return 3;
    default:
        g_assert_not_reached();
    }
}

static hwaddr npcm7xx_pdr_index(hwaddr offset)
{
    switch (offset) {
    case A_NPCM7XX_PWM_PDR0:
        return 0;
    case A_NPCM7XX_PWM_PDR1:
        return 1;
    case A_NPCM7XX_PWM_PDR2:
        return 2;
    case A_NPCM7XX_PWM_PDR3:
        return 3;
    default:
        g_assert_not_reached();
    }
}

static hwaddr npcm7xx_pwdr_index(hwaddr offset)
{
    switch (offset) {
    case A_NPCM7XX_PWM_PWDR0:
        return 0;
    case A_NPCM7XX_PWM_PWDR1:
        return 1;
    case A_NPCM7XX_PWM_PWDR2:
        return 2;
    case A_NPCM7XX_PWM_PWDR3:
        return 3;
    default:
        g_assert_not_reached();
    }
}

static uint64_t npcm7xx_pwm_read(void *opaque, hwaddr offset, unsigned size)
{
    NPCM7xxPWMState *s = opaque;
    uint64_t value = 0;

    switch (offset) {
    case A_NPCM7XX_PWM_CNR0:
    case A_NPCM7XX_PWM_CNR1:
    case A_NPCM7XX_PWM_CNR2:
    case A_NPCM7XX_PWM_CNR3:
        value = s->pwm[npcm7xx_cnr_index(offset)].cnr;
        break;

    case A_NPCM7XX_PWM_CMR0:
    case A_NPCM7XX_PWM_CMR1:
    case A_NPCM7XX_PWM_CMR2:
    case A_NPCM7XX_PWM_CMR3:
        value = s->pwm[npcm7xx_cmr_index(offset)].cmr;
        break;

    case A_NPCM7XX_PWM_PDR0:
    case A_NPCM7XX_PWM_PDR1:
    case A_NPCM7XX_PWM_PDR2:
    case A_NPCM7XX_PWM_PDR3:
        value = s->pwm[npcm7xx_pdr_index(offset)].pdr;
        break;

    case A_NPCM7XX_PWM_PWDR0:
    case A_NPCM7XX_PWM_PWDR1:
    case A_NPCM7XX_PWM_PWDR2:
    case A_NPCM7XX_PWM_PWDR3:
        value = s->pwm[npcm7xx_pwdr_index(offset)].pwdr;
        break;

    case A_NPCM7XX_PWM_PPR:
        value = s->ppr;
        break;

    case A_NPCM7XX_PWM_CSR:
        value = s->csr;
        break;

    case A_NPCM7XX_PWM_PCR:
        value = s->pcr;
        break;

    case A_NPCM7XX_PWM_PIER:
        value = s->pier;
        break;

    case A_NPCM7XX_PWM_PIIR:
        value = s->piir;
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid offset 0x%04" HWADDR_PRIx "\n",
                      __func__, offset);
        break;
    }

    trace_npcm7xx_pwm_read(DEVICE(s)->canonical_path, offset, value);
    return value;
}

static void npcm7xx_pwm_write(void *opaque, hwaddr offset,
                                uint64_t v, unsigned size)
{
    NPCM7xxPWMState *s = opaque;
    NPCM7xxPWM *p;
    uint32_t value = v;

    trace_npcm7xx_pwm_write(DEVICE(s)->canonical_path, offset, value);
    switch (offset) {
    case A_NPCM7XX_PWM_CNR0:
    case A_NPCM7XX_PWM_CNR1:
    case A_NPCM7XX_PWM_CNR2:
    case A_NPCM7XX_PWM_CNR3:
        p = &s->pwm[npcm7xx_cnr_index(offset)];
        if (value > NPCM7XX_MAX_CNR) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: invalid cnr value: %u", __func__, value);
            p->cnr = NPCM7XX_MAX_CNR;
        } else {
            p->cnr = value;
        }
        npcm7xx_pwm_update_output(p);
        break;

    case A_NPCM7XX_PWM_CMR0:
    case A_NPCM7XX_PWM_CMR1:
    case A_NPCM7XX_PWM_CMR2:
    case A_NPCM7XX_PWM_CMR3:
        p = &s->pwm[npcm7xx_cmr_index(offset)];
        if (value > NPCM7XX_MAX_CMR) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: invalid cmr value: %u", __func__, value);
            p->cmr = NPCM7XX_MAX_CMR;
        } else {
            p->cmr = value;
        }
        npcm7xx_pwm_update_output(p);
        break;

    case A_NPCM7XX_PWM_PDR0:
    case A_NPCM7XX_PWM_PDR1:
    case A_NPCM7XX_PWM_PDR2:
    case A_NPCM7XX_PWM_PDR3:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: register @ 0x%04" HWADDR_PRIx " is read-only\n",
                      __func__, offset);
        break;

    case A_NPCM7XX_PWM_PWDR0:
    case A_NPCM7XX_PWM_PWDR1:
    case A_NPCM7XX_PWM_PWDR2:
    case A_NPCM7XX_PWM_PWDR3:
        qemu_log_mask(LOG_UNIMP,
                     "%s: register @ 0x%04" HWADDR_PRIx " is not implemented\n",
                     __func__, offset);
        break;

    case A_NPCM7XX_PWM_PPR:
        npcm7xx_pwm_write_ppr(s, value);
        break;

    case A_NPCM7XX_PWM_CSR:
        npcm7xx_pwm_write_csr(s, value);
        break;

    case A_NPCM7XX_PWM_PCR:
        npcm7xx_pwm_write_pcr(s, value);
        break;

    case A_NPCM7XX_PWM_PIER:
        qemu_log_mask(LOG_UNIMP,
                     "%s: register @ 0x%04" HWADDR_PRIx " is not implemented\n",
                     __func__, offset);
        break;

    case A_NPCM7XX_PWM_PIIR:
        qemu_log_mask(LOG_UNIMP,
                     "%s: register @ 0x%04" HWADDR_PRIx " is not implemented\n",
                     __func__, offset);
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid offset 0x%04" HWADDR_PRIx "\n",
                      __func__, offset);
        break;
    }
}

static const struct MemoryRegionOps npcm7xx_pwm_ops = {
    .read       = npcm7xx_pwm_read,
    .write      = npcm7xx_pwm_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid      = {
        .min_access_size        = 4,
        .max_access_size        = 4,
        .unaligned              = false,
    },
};

static void npcm7xx_pwm_enter_reset(Object *obj, ResetType type)
{
    NPCM7xxPWMState *s = NPCM7XX_PWM(obj);
    int i;

    for (i = 0; i < NPCM7XX_PWM_PER_MODULE; i++) {
        NPCM7xxPWM *p = &s->pwm[i];

        p->cnr = 0x00000000;
        p->cmr = 0x00000000;
        p->pdr = 0x00000000;
        p->pwdr = 0x00000000;
    }

    s->ppr = 0x00000000;
    s->csr = 0x00000000;
    s->pcr = 0x00000000;
    s->pier = 0x00000000;
    s->piir = 0x00000000;
}

static void npcm7xx_pwm_hold_reset(Object *obj)
{
    NPCM7xxPWMState *s = NPCM7XX_PWM(obj);
    int i;

    for (i = 0; i < NPCM7XX_PWM_PER_MODULE; i++) {
        qemu_irq_lower(s->pwm[i].irq);
    }
}

static void npcm7xx_pwm_init(Object *obj)
{
    NPCM7xxPWMState *s = NPCM7XX_PWM(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    int i;

    QEMU_BUILD_BUG_ON(ARRAY_SIZE(s->pwm) != NPCM7XX_PWM_PER_MODULE);
    for (i = 0; i < NPCM7XX_PWM_PER_MODULE; i++) {
        NPCM7xxPWM *p = &s->pwm[i];
        p->module = s;
        p->index = i;
        sysbus_init_irq(sbd, &p->irq);
    }

    memory_region_init_io(&s->iomem, obj, &npcm7xx_pwm_ops, s,
                          TYPE_NPCM7XX_PWM, 4 * KiB);
    sysbus_init_mmio(sbd, &s->iomem);
    s->clock = qdev_init_clock_in(DEVICE(s), "clock", NULL, NULL, 0);

    for (i = 0; i < NPCM7XX_PWM_PER_MODULE; ++i) {
        object_property_add_uint32_ptr(obj, "freq[*]",
                &s->pwm[i].freq, OBJ_PROP_FLAG_READ);
        object_property_add_uint32_ptr(obj, "duty[*]",
                &s->pwm[i].duty, OBJ_PROP_FLAG_READ);
    }
    qdev_init_gpio_out_named(DEVICE(s), s->duty_gpio_out,
                             "duty-gpio-out", NPCM7XX_PWM_PER_MODULE);
}

static const VMStateDescription vmstate_npcm7xx_pwm = {
    .name = "npcm7xx-pwm",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (const VMStateField[]) {
        VMSTATE_BOOL(running, NPCM7xxPWM),
        VMSTATE_BOOL(inverted, NPCM7xxPWM),
        VMSTATE_UINT8(index, NPCM7xxPWM),
        VMSTATE_UINT32(cnr, NPCM7xxPWM),
        VMSTATE_UINT32(cmr, NPCM7xxPWM),
        VMSTATE_UINT32(pdr, NPCM7xxPWM),
        VMSTATE_UINT32(pwdr, NPCM7xxPWM),
        VMSTATE_UINT32(freq, NPCM7xxPWM),
        VMSTATE_UINT32(duty, NPCM7xxPWM),
        VMSTATE_END_OF_LIST(),
    },
};

static const VMStateDescription vmstate_npcm7xx_pwm_module = {
    .name = "npcm7xx-pwm-module",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (const VMStateField[]) {
        VMSTATE_CLOCK(clock, NPCM7xxPWMState),
        VMSTATE_STRUCT_ARRAY(pwm, NPCM7xxPWMState,
                             NPCM7XX_PWM_PER_MODULE, 0, vmstate_npcm7xx_pwm,
                             NPCM7xxPWM),
        VMSTATE_UINT32(ppr, NPCM7xxPWMState),
        VMSTATE_UINT32(csr, NPCM7xxPWMState),
        VMSTATE_UINT32(pcr, NPCM7xxPWMState),
        VMSTATE_UINT32(pier, NPCM7xxPWMState),
        VMSTATE_UINT32(piir, NPCM7xxPWMState),
        VMSTATE_END_OF_LIST(),
    },
};

static void npcm7xx_pwm_class_init(ObjectClass *klass, void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "NPCM7xx PWM Controller";
    dc->vmsd = &vmstate_npcm7xx_pwm_module;
    rc->phases.enter = npcm7xx_pwm_enter_reset;
    rc->phases.hold = npcm7xx_pwm_hold_reset;
}

static const TypeInfo npcm7xx_pwm_info = {
    .name               = TYPE_NPCM7XX_PWM,
    .parent             = TYPE_SYS_BUS_DEVICE,
    .instance_size      = sizeof(NPCM7xxPWMState),
    .class_init         = npcm7xx_pwm_class_init,
    .instance_init      = npcm7xx_pwm_init,
};

static void npcm7xx_pwm_register_type(void)
{
    type_register_static(&npcm7xx_pwm_info);
}
type_init(npcm7xx_pwm_register_type);

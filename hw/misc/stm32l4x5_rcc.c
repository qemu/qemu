/*
 * STM32L4X5 RCC (Reset and clock control)
 *
 * Copyright (c) 2023 Arnaud Minier <arnaud.minier@telecom-paris.fr>
 * Copyright (c) 2023 Inès Varhol <ines.varhol@telecom-paris.fr>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 * The reference used is the STMicroElectronics RM0351 Reference manual
 * for STM32L4x5 and STM32L4x6 advanced Arm ® -based 32-bit MCUs.
 *
 * Inspired by the BCM2835 CPRMAN clock manager implementation by Luc Michel.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qapi/error.h"
#include "migration/vmstate.h"
#include "hw/misc/stm32l4x5_rcc.h"
#include "hw/misc/stm32l4x5_rcc_internals.h"
#include "hw/clock.h"
#include "hw/irq.h"
#include "hw/qdev-clock.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "hw/registerfields.h"
#include "trace.h"

#define HSE_DEFAULT_FRQ 48000000ULL
#define HSI_FRQ 16000000ULL
#define MSI_DEFAULT_FRQ 4000000ULL
#define LSE_FRQ 32768ULL
#define LSI_FRQ 32000ULL

/*
 * Function to simply acknowledge and propagate changes in a clock mux
 * frequency.
 * `bypass_source` allows to bypass the period of the current source and just
 * consider it equal to 0. This is useful during the hold phase of reset.
 */
static void clock_mux_update(RccClockMuxState *mux, bool bypass_source)
{
    uint64_t src_freq;
    Clock *current_source = mux->srcs[mux->src];
    uint32_t freq_multiplier = 0;
    bool clk_changed = false;

    /*
     * To avoid rounding errors, we use the clock period instead of the
     * frequency.
     * This means that the multiplier of the mux becomes the divider of
     * the clock and the divider of the mux becomes the multiplier of the
     * clock.
     */
    if (!bypass_source && mux->enabled && mux->divider) {
        freq_multiplier = mux->divider;
    }

    clk_changed |= clock_set_mul_div(mux->out, freq_multiplier, mux->multiplier);
    clk_changed |= clock_set(mux->out, clock_get(current_source));
    if (clk_changed) {
        clock_propagate(mux->out);
    }

    src_freq = clock_get_hz(current_source);
    /* TODO: can we simply detect if the config changed so that we reduce log spam ? */
    trace_stm32l4x5_rcc_mux_update(mux->id, mux->src, src_freq,
                                   mux->multiplier, mux->divider);
}

static void clock_mux_src_update(void *opaque, ClockEvent event)
{
    RccClockMuxState **backref = opaque;
    RccClockMuxState *s = *backref;
    /*
     * The backref value is equal to:
     * s->backref + (sizeof(RccClockMuxState *) * update_src).
     * By subtracting we can get back the index of the updated clock.
     */
    const uint32_t update_src = backref - s->backref;
    /* Only update if the clock that was updated is the current source */
    if (update_src == s->src) {
        clock_mux_update(s, false);
    }
}

static void clock_mux_init(Object *obj)
{
    RccClockMuxState *s = RCC_CLOCK_MUX(obj);
    size_t i;

    for (i = 0; i < RCC_NUM_CLOCK_MUX_SRC; i++) {
        char *name = g_strdup_printf("srcs[%zu]", i);
        s->backref[i] = s;
        s->srcs[i] = qdev_init_clock_in(DEVICE(s), name,
                                        clock_mux_src_update,
                                        &s->backref[i],
                                        ClockUpdate);
        g_free(name);
    }

    s->out = qdev_init_clock_out(DEVICE(s), "out");
}

static void clock_mux_reset_enter(Object *obj, ResetType type)
{
    RccClockMuxState *s = RCC_CLOCK_MUX(obj);
    set_clock_mux_init_info(s, s->id);
}

static void clock_mux_reset_hold(Object *obj, ResetType type)
{
    RccClockMuxState *s = RCC_CLOCK_MUX(obj);
    clock_mux_update(s, true);
}

static void clock_mux_reset_exit(Object *obj, ResetType type)
{
    RccClockMuxState *s = RCC_CLOCK_MUX(obj);
    clock_mux_update(s, false);
}

static const VMStateDescription clock_mux_vmstate = {
    .name = TYPE_RCC_CLOCK_MUX,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(id, RccClockMuxState),
        VMSTATE_ARRAY_CLOCK(srcs, RccClockMuxState,
                            RCC_NUM_CLOCK_MUX_SRC),
        VMSTATE_BOOL(enabled, RccClockMuxState),
        VMSTATE_UINT32(src, RccClockMuxState),
        VMSTATE_UINT32(multiplier, RccClockMuxState),
        VMSTATE_UINT32(divider, RccClockMuxState),
        VMSTATE_END_OF_LIST()
    }
};

static void clock_mux_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->phases.enter = clock_mux_reset_enter;
    rc->phases.hold = clock_mux_reset_hold;
    rc->phases.exit = clock_mux_reset_exit;
    dc->vmsd = &clock_mux_vmstate;
    /* Reason: Part of Stm32l4x5RccState component */
    dc->user_creatable = false;
}

static void clock_mux_set_enable(RccClockMuxState *mux, bool enabled)
{
    if (mux->enabled == enabled) {
        return;
    }

    if (enabled) {
        trace_stm32l4x5_rcc_mux_enable(mux->id);
    } else {
        trace_stm32l4x5_rcc_mux_disable(mux->id);
    }

    mux->enabled = enabled;
    clock_mux_update(mux, false);
}

static void clock_mux_set_factor(RccClockMuxState *mux,
                                 uint32_t multiplier, uint32_t divider)
{
    if (mux->multiplier == multiplier && mux->divider == divider) {
        return;
    }
    trace_stm32l4x5_rcc_mux_set_factor(mux->id,
        mux->multiplier, multiplier, mux->divider, divider);

    mux->multiplier = multiplier;
    mux->divider = divider;
    clock_mux_update(mux, false);
}

static void clock_mux_set_source(RccClockMuxState *mux, RccClockMuxSource src)
{
    if (mux->src == src) {
        return;
    }

    trace_stm32l4x5_rcc_mux_set_src(mux->id, mux->src, src);
    mux->src = src;
    clock_mux_update(mux, false);
}

/*
 * Acknowledge and propagate changes in a PLL frequency.
 * `bypass_source` allows to bypass the period of the current source and just
 * consider it equal to 0. This is useful during the hold phase of reset.
 */
static void pll_update(RccPllState *pll, bool bypass_source)
{
    uint64_t vco_freq, old_channel_freq, channel_freq;
    int i;

    /* The common PLLM factor is handled by the PLL mux */
    vco_freq = muldiv64(clock_get_hz(pll->in), pll->vco_multiplier, 1);

    for (i = 0; i < RCC_NUM_CHANNEL_PLL_OUT; i++) {
        if (!pll->channel_exists[i]) {
            continue;
        }

        old_channel_freq = clock_get_hz(pll->channels[i]);
        if (bypass_source ||
            !pll->enabled ||
            !pll->channel_enabled[i] ||
            !pll->channel_divider[i]) {
            channel_freq = 0;
        } else {
            channel_freq = muldiv64(vco_freq,
                                    1,
                                    pll->channel_divider[i]);
        }

        /* No change, early continue to avoid log spam and useless propagation */
        if (old_channel_freq == channel_freq) {
            continue;
        }

        clock_update_hz(pll->channels[i], channel_freq);
        trace_stm32l4x5_rcc_pll_update(pll->id, i, vco_freq,
            old_channel_freq, channel_freq);
    }
}

static void pll_src_update(void *opaque, ClockEvent event)
{
    RccPllState *s = opaque;
    pll_update(s, false);
}

static void pll_init(Object *obj)
{
    RccPllState *s = RCC_PLL(obj);
    size_t i;

    s->in = qdev_init_clock_in(DEVICE(s), "in",
                               pll_src_update, s, ClockUpdate);

    const char *names[] = {
        "out-p", "out-q", "out-r",
    };

    for (i = 0; i < RCC_NUM_CHANNEL_PLL_OUT; i++) {
        s->channels[i] = qdev_init_clock_out(DEVICE(s), names[i]);
    }
}

static void pll_reset_enter(Object *obj, ResetType type)
{
    RccPllState *s = RCC_PLL(obj);
    set_pll_init_info(s, s->id);
}

static void pll_reset_hold(Object *obj, ResetType type)
{
    RccPllState *s = RCC_PLL(obj);
    pll_update(s, true);
}

static void pll_reset_exit(Object *obj, ResetType type)
{
    RccPllState *s = RCC_PLL(obj);
    pll_update(s, false);
}

static const VMStateDescription pll_vmstate = {
    .name = TYPE_RCC_PLL,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(id, RccPllState),
        VMSTATE_CLOCK(in, RccPllState),
        VMSTATE_ARRAY_CLOCK(channels, RccPllState,
                            RCC_NUM_CHANNEL_PLL_OUT),
        VMSTATE_BOOL(enabled, RccPllState),
        VMSTATE_UINT32(vco_multiplier, RccPllState),
        VMSTATE_BOOL_ARRAY(channel_enabled, RccPllState, RCC_NUM_CHANNEL_PLL_OUT),
        VMSTATE_BOOL_ARRAY(channel_exists, RccPllState, RCC_NUM_CHANNEL_PLL_OUT),
        VMSTATE_UINT32_ARRAY(channel_divider, RccPllState, RCC_NUM_CHANNEL_PLL_OUT),
        VMSTATE_END_OF_LIST()
    }
};

static void pll_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->phases.enter = pll_reset_enter;
    rc->phases.hold = pll_reset_hold;
    rc->phases.exit = pll_reset_exit;
    dc->vmsd = &pll_vmstate;
    /* Reason: Part of Stm32l4x5RccState component */
    dc->user_creatable = false;
}

static void pll_set_vco_multiplier(RccPllState *pll, uint32_t vco_multiplier)
{
    if (pll->vco_multiplier == vco_multiplier) {
        return;
    }

    if (vco_multiplier < 8 || vco_multiplier > 86) {
        qemu_log_mask(LOG_GUEST_ERROR,
            "%s: VCO multiplier is out of bound (%u) for PLL %u\n",
            __func__, vco_multiplier, pll->id);
        return;
    }

    trace_stm32l4x5_rcc_pll_set_vco_multiplier(pll->id,
        pll->vco_multiplier, vco_multiplier);

    pll->vco_multiplier = vco_multiplier;
    pll_update(pll, false);
}

static void pll_set_enable(RccPllState *pll, bool enabled)
{
    if (pll->enabled == enabled) {
        return;
    }

    pll->enabled = enabled;
    pll_update(pll, false);
}

static void pll_set_channel_enable(RccPllState *pll,
                                   PllCommonChannels channel,
                                   bool enabled)
{
    if (pll->channel_enabled[channel] == enabled) {
        return;
    }

    if (enabled) {
        trace_stm32l4x5_rcc_pll_channel_enable(pll->id, channel);
    } else {
        trace_stm32l4x5_rcc_pll_channel_disable(pll->id, channel);
    }

    pll->channel_enabled[channel] = enabled;
    pll_update(pll, false);
}

static void pll_set_channel_divider(RccPllState *pll,
                                    PllCommonChannels channel,
                                    uint32_t divider)
{
    if (pll->channel_divider[channel] == divider) {
        return;
    }

    trace_stm32l4x5_rcc_pll_set_channel_divider(pll->id,
        channel, pll->channel_divider[channel], divider);

    pll->channel_divider[channel] = divider;
    pll_update(pll, false);
}

static void rcc_update_irq(Stm32l4x5RccState *s)
{
    /*
     * TODO: Handle LSECSSF and CSSF flags when the CSS is implemented.
     */
    if (s->cifr & CIFR_IRQ_MASK) {
        qemu_irq_raise(s->irq);
    } else {
        qemu_irq_lower(s->irq);
    }
}

static void rcc_update_msi(Stm32l4x5RccState *s, uint32_t previous_value)
{
    uint32_t val;

    static const uint32_t msirange[] = {
        100000, 200000, 400000, 800000, 1000000, 2000000,
        4000000, 8000000, 16000000, 24000000, 32000000, 48000000
    };
    /* MSIRANGE and MSIRGSEL */
    val = extract32(s->cr, R_CR_MSIRGSEL_SHIFT, R_CR_MSIRGSEL_LENGTH);
    if (val) {
        /* MSIRGSEL is set, use the MSIRANGE field */
        val = extract32(s->cr, R_CR_MSIRANGE_SHIFT, R_CR_MSIRANGE_LENGTH);
    } else {
        /* MSIRGSEL is not set, use the MSISRANGE field */
        val = extract32(s->csr, R_CSR_MSISRANGE_SHIFT, R_CSR_MSISRANGE_LENGTH);
    }

    if (val < ARRAY_SIZE(msirange)) {
        clock_update_hz(s->msi_rc, msirange[val]);
    } else {
        /*
         * There is a hardware write protection if the value is out of bound.
         * Restore the previous value.
         */
        s->cr = (s->cr & ~R_CSR_MSISRANGE_MASK) |
                (previous_value & R_CSR_MSISRANGE_MASK);
    }
}

/*
 * TODO: Add write-protection for all registers:
 * DONE: CR
 */

static void rcc_update_cr_register(Stm32l4x5RccState *s, uint32_t previous_value)
{
    int val;
    const RccClockMuxSource current_pll_src =
        CLOCK_MUX_INIT_INFO[RCC_CLOCK_MUX_PLL_INPUT].src_mapping[
            s->clock_muxes[RCC_CLOCK_MUX_PLL_INPUT].src];

    /* PLLSAI2ON and update PLLSAI2RDY */
    val = FIELD_EX32(s->cr, CR, PLLSAI2ON);
    pll_set_enable(&s->plls[RCC_PLL_PLLSAI2], val);
    s->cr = (s->cr & ~R_CR_PLLSAI2RDY_MASK) |
            (val << R_CR_PLLSAI2RDY_SHIFT);
    if (s->cier & R_CIER_PLLSAI2RDYIE_MASK) {
        s->cifr |= R_CIFR_PLLSAI2RDYF_MASK;
    }

    /* PLLSAI1ON and update PLLSAI1RDY */
    val = FIELD_EX32(s->cr, CR, PLLSAI1ON);
    pll_set_enable(&s->plls[RCC_PLL_PLLSAI1], val);
    s->cr = (s->cr & ~R_CR_PLLSAI1RDY_MASK) |
            (val << R_CR_PLLSAI1RDY_SHIFT);
    if (s->cier & R_CIER_PLLSAI1RDYIE_MASK) {
        s->cifr |= R_CIFR_PLLSAI1RDYF_MASK;
    }

    /*
     * PLLON and update PLLRDY
     * PLLON cannot be reset if the PLL clock is used as the system clock.
     */
    val = FIELD_EX32(s->cr, CR, PLLON);
    if (FIELD_EX32(s->cfgr, CFGR, SWS) != 0b11) {
        pll_set_enable(&s->plls[RCC_PLL_PLL], val);
        s->cr = (s->cr & ~R_CR_PLLRDY_MASK) |
                (val << R_CR_PLLRDY_SHIFT);
        if (s->cier & R_CIER_PLLRDYIE_MASK) {
            s->cifr |= R_CIFR_PLLRDYF_MASK;
        }
    } else {
        s->cr |= R_CR_PLLON_MASK;
    }

    /* CSSON: TODO */
    /* HSEBYP: TODO */

    /*
     * HSEON and update HSERDY.
     * HSEON cannot be reset if the HSE oscillator is used directly or
     * indirectly as the system clock.
     */
    val = FIELD_EX32(s->cr, CR, HSEON);
    if (FIELD_EX32(s->cfgr, CFGR, SWS) != 0b10 &&
        current_pll_src != RCC_CLOCK_MUX_SRC_HSE) {
        s->cr = (s->cr & ~R_CR_HSERDY_MASK) |
                (val << R_CR_HSERDY_SHIFT);
        if (val) {
            clock_update_hz(s->hse, s->hse_frequency);
            if (s->cier & R_CIER_HSERDYIE_MASK) {
                s->cifr |= R_CIFR_HSERDYF_MASK;
            }
        } else {
            clock_update(s->hse, 0);
        }
    } else {
        s->cr |= R_CR_HSEON_MASK;
    }

    /* HSIAFS: TODO*/
    /* HSIKERON: TODO*/

    /*
     * HSION and update HSIRDY
     * HSION is set by hardware if the HSI16 is used directly
     * or indirectly as system clock.
     */
    if (FIELD_EX32(s->cfgr, CFGR, SWS) == 0b01 ||
        current_pll_src == RCC_CLOCK_MUX_SRC_HSI) {
        s->cr |= (R_CR_HSION_MASK | R_CR_HSIRDY_MASK);
        clock_update_hz(s->hsi16_rc, HSI_FRQ);
        if (s->cier & R_CIER_HSIRDYIE_MASK) {
            s->cifr |= R_CIFR_HSIRDYF_MASK;
        }
    } else {
        val = FIELD_EX32(s->cr, CR, HSION);
        if (val) {
            clock_update_hz(s->hsi16_rc, HSI_FRQ);
            s->cr |= R_CR_HSIRDY_MASK;
            if (s->cier & R_CIER_HSIRDYIE_MASK) {
                s->cifr |= R_CIFR_HSIRDYF_MASK;
            }
        } else {
            clock_update(s->hsi16_rc, 0);
            s->cr &= ~R_CR_HSIRDY_MASK;
        }
    }

    /* MSIPLLEN: TODO */

    /*
     * MSION and update MSIRDY
     * Set by hardware when used directly or indirectly as system clock.
     */
    if (FIELD_EX32(s->cfgr, CFGR, SWS) == 0b00 ||
        current_pll_src == RCC_CLOCK_MUX_SRC_MSI) {
            s->cr |= (R_CR_MSION_MASK | R_CR_MSIRDY_MASK);
            if (!(previous_value & R_CR_MSION_MASK) && (s->cier & R_CIER_MSIRDYIE_MASK)) {
                s->cifr |= R_CIFR_MSIRDYF_MASK;
            }
            rcc_update_msi(s, previous_value);
    } else {
        val = FIELD_EX32(s->cr, CR, MSION);
        if (val) {
            s->cr |= R_CR_MSIRDY_MASK;
            rcc_update_msi(s, previous_value);
            if (s->cier & R_CIER_MSIRDYIE_MASK) {
                s->cifr |= R_CIFR_MSIRDYF_MASK;
            }
        } else {
            s->cr &= ~R_CR_MSIRDY_MASK;
            clock_update(s->msi_rc, 0);
        }
    }
    rcc_update_irq(s);
}

static void rcc_update_cfgr_register(Stm32l4x5RccState *s)
{
    uint32_t val;
    /* MCOPRE */
    val = FIELD_EX32(s->cfgr, CFGR, MCOPRE);
    if (val > 0b100) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Invalid MCOPRE value: 0x%"PRIx32"\n",
                      __func__, val);
        clock_mux_set_enable(&s->clock_muxes[RCC_CLOCK_MUX_MCO], false);
    } else {
        clock_mux_set_factor(&s->clock_muxes[RCC_CLOCK_MUX_MCO],
                             1, 1 << val);
    }

    /* MCOSEL */
    val = FIELD_EX32(s->cfgr, CFGR, MCOSEL);
    if (val > 0b111) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Invalid MCOSEL value: 0x%"PRIx32"\n",
                      __func__, val);
        clock_mux_set_enable(&s->clock_muxes[RCC_CLOCK_MUX_MCO], false);
    } else {
        if (val == 0) {
            clock_mux_set_enable(&s->clock_muxes[RCC_CLOCK_MUX_MCO], false);
        } else {
            clock_mux_set_enable(&s->clock_muxes[RCC_CLOCK_MUX_MCO], true);
            clock_mux_set_source(&s->clock_muxes[RCC_CLOCK_MUX_MCO],
                                 val - 1);
        }
    }

    /* STOPWUCK */
    /* TODO */

    /* PPRE2 */
    val = FIELD_EX32(s->cfgr, CFGR, PPRE2);
    if (val < 0b100) {
        clock_mux_set_factor(&s->clock_muxes[RCC_CLOCK_MUX_PCLK2],
                             1, 1);
    } else {
        clock_mux_set_factor(&s->clock_muxes[RCC_CLOCK_MUX_PCLK2],
                             1, 1 << (val - 0b11));
    }

    /* PPRE1 */
    val = FIELD_EX32(s->cfgr, CFGR, PPRE1);
    if (val < 0b100) {
        clock_mux_set_factor(&s->clock_muxes[RCC_CLOCK_MUX_PCLK1],
                             1, 1);
    } else {
        clock_mux_set_factor(&s->clock_muxes[RCC_CLOCK_MUX_PCLK1],
                             1, 1 << (val - 0b11));
    }

    /* HPRE */
    val = FIELD_EX32(s->cfgr, CFGR, HPRE);
    if (val < 0b1000) {
        clock_mux_set_factor(&s->clock_muxes[RCC_CLOCK_MUX_HCLK],
                             1, 1);
    } else {
        clock_mux_set_factor(&s->clock_muxes[RCC_CLOCK_MUX_HCLK],
                             1, 1 << (val - 0b111));
    }

    /* Update SWS */
    val = FIELD_EX32(s->cfgr, CFGR, SW);
    clock_mux_set_source(&s->clock_muxes[RCC_CLOCK_MUX_SYSCLK],
                         val);
    s->cfgr &= ~R_CFGR_SWS_MASK;
    s->cfgr |= val << R_CFGR_SWS_SHIFT;
}

static void rcc_update_ahb1enr(Stm32l4x5RccState *s)
{
    #define AHB1ENR_SET_ENABLE(_peripheral_name) \
        clock_mux_set_enable(&s->clock_muxes[RCC_CLOCK_MUX_##_peripheral_name], \
            FIELD_EX32(s->ahb1enr, AHB1ENR, _peripheral_name##EN))

    /* DMA2DEN: reserved for STM32L475xx */
    AHB1ENR_SET_ENABLE(TSC);
    AHB1ENR_SET_ENABLE(CRC);
    AHB1ENR_SET_ENABLE(FLASH);
    AHB1ENR_SET_ENABLE(DMA2);
    AHB1ENR_SET_ENABLE(DMA1);

    #undef AHB1ENR_SET_ENABLE
}

static void rcc_update_ahb2enr(Stm32l4x5RccState *s)
{
    #define AHB2ENR_SET_ENABLE(_peripheral_name) \
        clock_mux_set_enable(&s->clock_muxes[RCC_CLOCK_MUX_##_peripheral_name], \
            FIELD_EX32(s->ahb2enr, AHB2ENR, _peripheral_name##EN))

    AHB2ENR_SET_ENABLE(RNG);
    /* HASHEN: reserved for STM32L475xx */
    AHB2ENR_SET_ENABLE(AES);
    /* DCMIEN: reserved for STM32L475xx */
    AHB2ENR_SET_ENABLE(ADC);
    AHB2ENR_SET_ENABLE(OTGFS);
    /* GPIOIEN: reserved for STM32L475xx */
    AHB2ENR_SET_ENABLE(GPIOA);
    AHB2ENR_SET_ENABLE(GPIOB);
    AHB2ENR_SET_ENABLE(GPIOC);
    AHB2ENR_SET_ENABLE(GPIOD);
    AHB2ENR_SET_ENABLE(GPIOE);
    AHB2ENR_SET_ENABLE(GPIOF);
    AHB2ENR_SET_ENABLE(GPIOG);
    AHB2ENR_SET_ENABLE(GPIOH);

    #undef AHB2ENR_SET_ENABLE
}

static void rcc_update_ahb3enr(Stm32l4x5RccState *s)
{
    #define AHB3ENR_SET_ENABLE(_peripheral_name) \
        clock_mux_set_enable(&s->clock_muxes[RCC_CLOCK_MUX_##_peripheral_name], \
            FIELD_EX32(s->ahb3enr, AHB3ENR, _peripheral_name##EN))

    AHB3ENR_SET_ENABLE(QSPI);
    AHB3ENR_SET_ENABLE(FMC);

    #undef AHB3ENR_SET_ENABLE
}

static void rcc_update_apb1enr(Stm32l4x5RccState *s)
{
    #define APB1ENR1_SET_ENABLE(_peripheral_name) \
        clock_mux_set_enable(&s->clock_muxes[RCC_CLOCK_MUX_##_peripheral_name], \
            FIELD_EX32(s->apb1enr1, APB1ENR1, _peripheral_name##EN))
    #define APB1ENR2_SET_ENABLE(_peripheral_name) \
        clock_mux_set_enable(&s->clock_muxes[RCC_CLOCK_MUX_##_peripheral_name], \
            FIELD_EX32(s->apb1enr2, APB1ENR2, _peripheral_name##EN))

    /* APB1ENR1 */
    APB1ENR1_SET_ENABLE(LPTIM1);
    APB1ENR1_SET_ENABLE(OPAMP);
    APB1ENR1_SET_ENABLE(DAC1);
    APB1ENR1_SET_ENABLE(PWR);
    /* CAN2: reserved for STM32L4x5 */
    APB1ENR1_SET_ENABLE(CAN1);
    /* CRSEN: reserved for STM32L4x5 */
    APB1ENR1_SET_ENABLE(I2C3);
    APB1ENR1_SET_ENABLE(I2C2);
    APB1ENR1_SET_ENABLE(I2C1);
    APB1ENR1_SET_ENABLE(UART5);
    APB1ENR1_SET_ENABLE(UART4);
    APB1ENR1_SET_ENABLE(USART3);
    APB1ENR1_SET_ENABLE(USART2);
    APB1ENR1_SET_ENABLE(SPI3);
    APB1ENR1_SET_ENABLE(SPI2);
    APB1ENR1_SET_ENABLE(WWDG);
    /* RTCAPB: reserved for STM32L4x5 */
    APB1ENR1_SET_ENABLE(LCD);
    APB1ENR1_SET_ENABLE(TIM7);
    APB1ENR1_SET_ENABLE(TIM6);
    APB1ENR1_SET_ENABLE(TIM5);
    APB1ENR1_SET_ENABLE(TIM4);
    APB1ENR1_SET_ENABLE(TIM3);
    APB1ENR1_SET_ENABLE(TIM2);

    /* APB1ENR2 */
    APB1ENR2_SET_ENABLE(LPTIM2);
    APB1ENR2_SET_ENABLE(SWPMI1);
    /* I2C4EN: reserved for STM32L4x5 */
    APB1ENR2_SET_ENABLE(LPUART1);

    #undef APB1ENR1_SET_ENABLE
    #undef APB1ENR2_SET_ENABLE
}

static void rcc_update_apb2enr(Stm32l4x5RccState *s)
{
    #define APB2ENR_SET_ENABLE(_peripheral_name) \
        clock_mux_set_enable(&s->clock_muxes[RCC_CLOCK_MUX_##_peripheral_name], \
            FIELD_EX32(s->apb2enr, APB2ENR, _peripheral_name##EN))

    APB2ENR_SET_ENABLE(DFSDM1);
    APB2ENR_SET_ENABLE(SAI2);
    APB2ENR_SET_ENABLE(SAI1);
    APB2ENR_SET_ENABLE(TIM17);
    APB2ENR_SET_ENABLE(TIM16);
    APB2ENR_SET_ENABLE(TIM15);
    APB2ENR_SET_ENABLE(USART1);
    APB2ENR_SET_ENABLE(TIM8);
    APB2ENR_SET_ENABLE(SPI1);
    APB2ENR_SET_ENABLE(TIM1);
    APB2ENR_SET_ENABLE(SDMMC1);
    APB2ENR_SET_ENABLE(FW);
    APB2ENR_SET_ENABLE(SYSCFG);

    #undef APB2ENR_SET_ENABLE
}

/*
 * The 3 PLLs share the same register layout
 * so we can use the same function for all of them
 * Note: no frequency bounds checking is done here.
 */
static void rcc_update_pllsaixcfgr(Stm32l4x5RccState *s, RccPll pll_id)
{
    uint32_t reg, val;
    switch (pll_id) {
    case RCC_PLL_PLL:
        reg = s->pllcfgr;
        break;
    case RCC_PLL_PLLSAI1:
        reg = s->pllsai1cfgr;
        break;
    case RCC_PLL_PLLSAI2:
        reg = s->pllsai2cfgr;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Invalid PLL ID: %u\n", __func__, pll_id);
        return;
    }

    /* PLLPDIV */
    val = FIELD_EX32(reg, PLLCFGR, PLLPDIV);
    /* 1 is a reserved value */
    if (val == 0) {
        /* Get PLLP value */
        val = FIELD_EX32(reg, PLLCFGR, PLLP);
        pll_set_channel_divider(&s->plls[pll_id], RCC_PLL_COMMON_CHANNEL_P,
            (val ? 17 : 7));
    } else if (val > 1) {
        pll_set_channel_divider(&s->plls[pll_id], RCC_PLL_COMMON_CHANNEL_P,
            val);
    }


    /* PLLR */
    val = FIELD_EX32(reg, PLLCFGR, PLLR);
    pll_set_channel_divider(&s->plls[pll_id], RCC_PLL_COMMON_CHANNEL_R,
        2 * (val + 1));

    /* PLLREN */
    val = FIELD_EX32(reg, PLLCFGR, PLLREN);
    pll_set_channel_enable(&s->plls[pll_id], RCC_PLL_COMMON_CHANNEL_R, val);

    /* PLLQ */
    val = FIELD_EX32(reg, PLLCFGR, PLLQ);
    pll_set_channel_divider(&s->plls[pll_id], RCC_PLL_COMMON_CHANNEL_Q,
        2 * (val + 1));

    /* PLLQEN */
    val = FIELD_EX32(reg, PLLCFGR, PLLQEN);
    pll_set_channel_enable(&s->plls[pll_id], RCC_PLL_COMMON_CHANNEL_Q, val);

    /* PLLPEN */
    val = FIELD_EX32(reg, PLLCFGR, PLLPEN);
    pll_set_channel_enable(&s->plls[pll_id], RCC_PLL_COMMON_CHANNEL_P, val);

    /* PLLN */
    val = FIELD_EX32(reg, PLLCFGR, PLLN);
    pll_set_vco_multiplier(&s->plls[pll_id], val);
}

static void rcc_update_pllcfgr(Stm32l4x5RccState *s)
{
    int val;

    /* Use common layout */
    rcc_update_pllsaixcfgr(s, RCC_PLL_PLL);

    /* Fetch specific fields for pllcfgr */

    /* PLLM */
    val = FIELD_EX32(s->pllcfgr, PLLCFGR, PLLM);
    clock_mux_set_factor(&s->clock_muxes[RCC_CLOCK_MUX_PLL_INPUT], 1, (val + 1));

    /* PLLSRC */
    val = FIELD_EX32(s->pllcfgr, PLLCFGR, PLLSRC);
    if (val == 0) {
        clock_mux_set_enable(&s->clock_muxes[RCC_CLOCK_MUX_PLL_INPUT], false);
    } else {
        clock_mux_set_source(&s->clock_muxes[RCC_CLOCK_MUX_PLL_INPUT], val - 1);
        clock_mux_set_enable(&s->clock_muxes[RCC_CLOCK_MUX_PLL_INPUT], true);
    }
}

static void rcc_update_ccipr(Stm32l4x5RccState *s)
{
    #define CCIPR_SET_SOURCE(_peripheral_name) \
        clock_mux_set_source(&s->clock_muxes[RCC_CLOCK_MUX_##_peripheral_name], \
            FIELD_EX32(s->ccipr, CCIPR, _peripheral_name##SEL))

    CCIPR_SET_SOURCE(DFSDM1);
    CCIPR_SET_SOURCE(SWPMI1);
    CCIPR_SET_SOURCE(ADC);
    CCIPR_SET_SOURCE(CLK48);
    CCIPR_SET_SOURCE(SAI2);
    CCIPR_SET_SOURCE(SAI1);
    CCIPR_SET_SOURCE(LPTIM2);
    CCIPR_SET_SOURCE(LPTIM1);
    CCIPR_SET_SOURCE(I2C3);
    CCIPR_SET_SOURCE(I2C2);
    CCIPR_SET_SOURCE(I2C1);
    CCIPR_SET_SOURCE(LPUART1);
    CCIPR_SET_SOURCE(UART5);
    CCIPR_SET_SOURCE(UART4);
    CCIPR_SET_SOURCE(USART3);
    CCIPR_SET_SOURCE(USART2);
    CCIPR_SET_SOURCE(USART1);

    #undef CCIPR_SET_SOURCE
}

static void rcc_update_bdcr(Stm32l4x5RccState *s)
{
    int val;

    /* LSCOSEL */
    val = FIELD_EX32(s->bdcr, BDCR, LSCOSEL);
    clock_mux_set_source(&s->clock_muxes[RCC_CLOCK_MUX_LSCO], val);

    val = FIELD_EX32(s->bdcr, BDCR, LSCOEN);
    clock_mux_set_enable(&s->clock_muxes[RCC_CLOCK_MUX_LSCO], val);

    /* BDRST */
    /*
     * The documentation is not clear if the RTCEN flag disables the RTC and
     * the LCD common mux or if it only affects the RTC.
     * As the LCDEN flag exists, we assume here that it only affects the RTC.
     */
    val = FIELD_EX32(s->bdcr, BDCR, RTCEN);
    clock_mux_set_enable(&s->clock_muxes[RCC_CLOCK_MUX_RTC], val);
    /* LCD and RTC share the same clock */
    val = FIELD_EX32(s->bdcr, BDCR, RTCSEL);
    clock_mux_set_source(&s->clock_muxes[RCC_CLOCK_MUX_LCD_AND_RTC_COMMON], val);

    /* LSECSSON */
    /* LSEDRV[1:0] */
    /* LSEBYP */

    /* LSEON: Update LSERDY at the same time */
    val = FIELD_EX32(s->bdcr, BDCR, LSEON);
    if (val) {
        clock_update_hz(s->lse_crystal, LSE_FRQ);
        s->bdcr |= R_BDCR_LSERDY_MASK;
        if (s->cier & R_CIER_LSERDYIE_MASK) {
            s->cifr |= R_CIFR_LSERDYF_MASK;
        }
    } else {
        clock_update(s->lse_crystal, 0);
        s->bdcr &= ~R_BDCR_LSERDY_MASK;
    }

    rcc_update_irq(s);
}

static void rcc_update_csr(Stm32l4x5RccState *s)
{
    int val;

    /* Reset flags: Not implemented */
    /* MSISRANGE: Not implemented after reset */

    /* LSION: Update LSIRDY at the same time */
    val = FIELD_EX32(s->csr, CSR, LSION);
    if (val) {
        clock_update_hz(s->lsi_rc, LSI_FRQ);
        s->csr |= R_CSR_LSIRDY_MASK;
        if (s->cier & R_CIER_LSIRDYIE_MASK) {
            s->cifr |= R_CIFR_LSIRDYF_MASK;
        }
    } else {
        /*
         * TODO: Handle when the LSI is set independently of LSION.
         * E.g. when the LSI is set by the RTC.
         * See the reference manual for more details.
         */
        clock_update(s->lsi_rc, 0);
        s->csr &= ~R_CSR_LSIRDY_MASK;
    }

    rcc_update_irq(s);
}

static void stm32l4x5_rcc_reset_hold(Object *obj, ResetType type)
{
    Stm32l4x5RccState *s = STM32L4X5_RCC(obj);
    s->cr = 0x00000063;
    /*
     * Factory-programmed calibration data
     * From the reference manual: 0x10XX 00XX
     * Value taken from a real card.
     */
    s->icscr = 0x106E0082;
    s->cfgr = 0x0;
    s->pllcfgr = 0x00001000;
    s->pllsai1cfgr = 0x00001000;
    s->pllsai2cfgr = 0x00001000;
    s->cier = 0x0;
    s->cifr = 0x0;
    s->ahb1rstr = 0x0;
    s->ahb2rstr = 0x0;
    s->ahb3rstr = 0x0;
    s->apb1rstr1 = 0x0;
    s->apb1rstr2 = 0x0;
    s->apb2rstr = 0x0;
    s->ahb1enr = 0x00000100;
    s->ahb2enr = 0x0;
    s->ahb3enr = 0x0;
    s->apb1enr1 = 0x0;
    s->apb1enr2 = 0x0;
    s->apb2enr = 0x0;
    s->ahb1smenr = 0x00011303;
    s->ahb2smenr = 0x000532FF;
    s->ahb3smenr =  0x00000101;
    s->apb1smenr1 = 0xF2FECA3F;
    s->apb1smenr2 = 0x00000025;
    s->apb2smenr = 0x01677C01;
    s->ccipr = 0x0;
    s->bdcr = 0x0;
    s->csr = 0x0C000600;
}

static uint64_t stm32l4x5_rcc_read(void *opaque, hwaddr addr,
                                     unsigned int size)
{
    Stm32l4x5RccState *s = opaque;
    uint64_t retvalue = 0;

    switch (addr) {
    case A_CR:
        retvalue = s->cr;
        break;
    case A_ICSCR:
        retvalue = s->icscr;
        break;
    case A_CFGR:
        retvalue = s->cfgr;
        break;
    case A_PLLCFGR:
        retvalue = s->pllcfgr;
        break;
    case A_PLLSAI1CFGR:
        retvalue = s->pllsai1cfgr;
        break;
    case A_PLLSAI2CFGR:
        retvalue = s->pllsai2cfgr;
        break;
    case A_CIER:
        retvalue = s->cier;
        break;
    case A_CIFR:
        retvalue = s->cifr;
        break;
    case A_CICR:
        /* CICR is write only, return the reset value = 0 */
        break;
    case A_AHB1RSTR:
        retvalue = s->ahb1rstr;
        break;
    case A_AHB2RSTR:
        retvalue = s->ahb2rstr;
        break;
    case A_AHB3RSTR:
        retvalue = s->ahb3rstr;
        break;
    case A_APB1RSTR1:
        retvalue = s->apb1rstr1;
        break;
    case A_APB1RSTR2:
        retvalue = s->apb1rstr2;
        break;
    case A_APB2RSTR:
        retvalue = s->apb2rstr;
        break;
    case A_AHB1ENR:
        retvalue = s->ahb1enr;
        break;
    case A_AHB2ENR:
        retvalue = s->ahb2enr;
        break;
    case A_AHB3ENR:
        retvalue = s->ahb3enr;
        break;
    case A_APB1ENR1:
        retvalue = s->apb1enr1;
        break;
    case A_APB1ENR2:
        retvalue = s->apb1enr2;
        break;
    case A_APB2ENR:
        retvalue = s->apb2enr;
        break;
    case A_AHB1SMENR:
        retvalue = s->ahb1smenr;
        break;
    case A_AHB2SMENR:
        retvalue = s->ahb2smenr;
        break;
    case A_AHB3SMENR:
        retvalue = s->ahb3smenr;
        break;
    case A_APB1SMENR1:
        retvalue = s->apb1smenr1;
        break;
    case A_APB1SMENR2:
        retvalue = s->apb1smenr2;
        break;
    case A_APB2SMENR:
        retvalue = s->apb2smenr;
        break;
    case A_CCIPR:
        retvalue = s->ccipr;
        break;
    case A_BDCR:
        retvalue = s->bdcr;
        break;
    case A_CSR:
        retvalue = s->csr;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad offset 0x%"HWADDR_PRIx"\n", __func__, addr);
        break;
    }

    trace_stm32l4x5_rcc_read(addr, retvalue);

    return retvalue;
}

static void stm32l4x5_rcc_write(void *opaque, hwaddr addr,
                                  uint64_t val64, unsigned int size)
{
    Stm32l4x5RccState *s = opaque;
    uint32_t previous_value = 0;
    const uint32_t value = val64;

    trace_stm32l4x5_rcc_write(addr, value);

    switch (addr) {
    case A_CR:
        previous_value = s->cr;
        s->cr = (s->cr & CR_READ_SET_MASK) |
                (value & (CR_READ_SET_MASK | ~CR_READ_ONLY_MASK));
        rcc_update_cr_register(s, previous_value);
        break;
    case A_ICSCR:
        s->icscr = value & ~ICSCR_READ_ONLY_MASK;
        qemu_log_mask(LOG_UNIMP,
                "%s: Side-effects not implemented for ICSCR\n", __func__);
        break;
    case A_CFGR:
        s->cfgr = value & ~CFGR_READ_ONLY_MASK;
        rcc_update_cfgr_register(s);
        break;
    case A_PLLCFGR:
        s->pllcfgr = value;
        rcc_update_pllcfgr(s);
        break;
    case A_PLLSAI1CFGR:
        s->pllsai1cfgr = value;
        rcc_update_pllsaixcfgr(s, RCC_PLL_PLLSAI1);
        break;
    case A_PLLSAI2CFGR:
        s->pllsai2cfgr = value;
        rcc_update_pllsaixcfgr(s, RCC_PLL_PLLSAI2);
        break;
    case A_CIER:
        s->cier = value;
        qemu_log_mask(LOG_UNIMP,
                "%s: Side-effects not implemented for CIER\n", __func__);
        break;
    case A_CIFR:
        qemu_log_mask(LOG_GUEST_ERROR,
            "%s: Write attempt into read-only register (CIFR) 0x%"PRIx32"\n",
            __func__, value);
        break;
    case A_CICR:
        /* Clear interrupt flags by writing a 1 to the CICR register */
        s->cifr &= ~value;
        rcc_update_irq(s);
        break;
    /* Reset behaviors are not implemented */
    case A_AHB1RSTR:
        s->ahb1rstr = value;
        qemu_log_mask(LOG_UNIMP,
                "%s: Side-effects not implemented for AHB1RSTR\n", __func__);
        break;
    case A_AHB2RSTR:
        s->ahb2rstr = value;
        qemu_log_mask(LOG_UNIMP,
                "%s: Side-effects not implemented for AHB2RSTR\n", __func__);
        break;
    case A_AHB3RSTR:
        s->ahb3rstr = value;
        qemu_log_mask(LOG_UNIMP,
                "%s: Side-effects not implemented for AHB3RSTR\n", __func__);
        break;
    case A_APB1RSTR1:
        s->apb1rstr1 = value;
        qemu_log_mask(LOG_UNIMP,
                "%s: Side-effects not implemented for APB1RSTR1\n", __func__);
        break;
    case A_APB1RSTR2:
        s->apb1rstr2 = value;
        qemu_log_mask(LOG_UNIMP,
                "%s: Side-effects not implemented for APB1RSTR2\n", __func__);
        break;
    case A_APB2RSTR:
        s->apb2rstr = value;
        qemu_log_mask(LOG_UNIMP,
                "%s: Side-effects not implemented for APB2RSTR\n", __func__);
        break;
    case A_AHB1ENR:
        s->ahb1enr = value;
        rcc_update_ahb1enr(s);
        break;
    case A_AHB2ENR:
        s->ahb2enr = value;
        rcc_update_ahb2enr(s);
        break;
    case A_AHB3ENR:
        s->ahb3enr = value;
        rcc_update_ahb3enr(s);
        break;
    case A_APB1ENR1:
        s->apb1enr1 = value;
        rcc_update_apb1enr(s);
        break;
    case A_APB1ENR2:
        s->apb1enr2 = value;
        rcc_update_apb1enr(s);
        break;
    case A_APB2ENR:
        s->apb2enr = (s->apb2enr & APB2ENR_READ_SET_MASK) | value;
        rcc_update_apb2enr(s);
        break;
    /* Behaviors for Sleep and Stop modes are not implemented */
    case A_AHB1SMENR:
        s->ahb1smenr = value;
        qemu_log_mask(LOG_UNIMP,
                "%s: Side-effects not implemented for AHB1SMENR\n", __func__);
        break;
    case A_AHB2SMENR:
        s->ahb2smenr = value;
        qemu_log_mask(LOG_UNIMP,
                "%s: Side-effects not implemented for AHB2SMENR\n", __func__);
        break;
    case A_AHB3SMENR:
        s->ahb3smenr = value;
        qemu_log_mask(LOG_UNIMP,
                "%s: Side-effects not implemented for AHB3SMENR\n", __func__);
        break;
    case A_APB1SMENR1:
        s->apb1smenr1 = value;
        qemu_log_mask(LOG_UNIMP,
                "%s: Side-effects not implemented for APB1SMENR1\n", __func__);
        break;
    case A_APB1SMENR2:
        s->apb1smenr2 = value;
        qemu_log_mask(LOG_UNIMP,
                "%s: Side-effects not implemented for APB1SMENR2\n", __func__);
        break;
    case A_APB2SMENR:
        s->apb2smenr = value;
        qemu_log_mask(LOG_UNIMP,
                "%s: Side-effects not implemented for APB2SMENR\n", __func__);
        break;
    case A_CCIPR:
        s->ccipr = value;
        rcc_update_ccipr(s);
        break;
    case A_BDCR:
        s->bdcr = value & ~BDCR_READ_ONLY_MASK;
        rcc_update_bdcr(s);
        break;
    case A_CSR:
        s->csr = value & ~CSR_READ_ONLY_MASK;
        rcc_update_csr(s);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad offset 0x%"HWADDR_PRIx"\n", __func__, addr);
    }
}

static const MemoryRegionOps stm32l4x5_rcc_ops = {
    .read = stm32l4x5_rcc_read,
    .write = stm32l4x5_rcc_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .max_access_size = 4,
        .min_access_size = 4,
        .unaligned = false
    },
    .impl = {
        .max_access_size = 4,
        .min_access_size = 4,
        .unaligned = false
    },
};

static const ClockPortInitArray stm32l4x5_rcc_clocks = {
    QDEV_CLOCK_IN(Stm32l4x5RccState, hsi16_rc, NULL, 0),
    QDEV_CLOCK_IN(Stm32l4x5RccState, msi_rc, NULL, 0),
    QDEV_CLOCK_IN(Stm32l4x5RccState, hse, NULL, 0),
    QDEV_CLOCK_IN(Stm32l4x5RccState, lsi_rc, NULL, 0),
    QDEV_CLOCK_IN(Stm32l4x5RccState, lse_crystal, NULL, 0),
    QDEV_CLOCK_IN(Stm32l4x5RccState, sai1_extclk, NULL, 0),
    QDEV_CLOCK_IN(Stm32l4x5RccState, sai2_extclk, NULL, 0),
    QDEV_CLOCK_END
};


static void stm32l4x5_rcc_init(Object *obj)
{
    Stm32l4x5RccState *s = STM32L4X5_RCC(obj);
    size_t i;

    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);

    memory_region_init_io(&s->mmio, obj, &stm32l4x5_rcc_ops, s,
                          TYPE_STM32L4X5_RCC, 0x400);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);

    qdev_init_clocks(DEVICE(s), stm32l4x5_rcc_clocks);

    for (i = 0; i < RCC_NUM_PLL; i++) {
        object_initialize_child(obj, PLL_INIT_INFO[i].name,
                                &s->plls[i], TYPE_RCC_PLL);
        set_pll_init_info(&s->plls[i], i);
    }

    for (i = 0; i < RCC_NUM_CLOCK_MUX; i++) {
        char *alias;

        object_initialize_child(obj, CLOCK_MUX_INIT_INFO[i].name,
                                &s->clock_muxes[i],
                                TYPE_RCC_CLOCK_MUX);
        set_clock_mux_init_info(&s->clock_muxes[i], i);

        if (!CLOCK_MUX_INIT_INFO[i].hidden) {
            /* Expose muxes output as RCC outputs */
            alias = g_strdup_printf("%s-out", CLOCK_MUX_INIT_INFO[i].name);
            qdev_alias_clock(DEVICE(&s->clock_muxes[i]), "out", DEVICE(obj), alias);
            g_free(alias);
        }
    }

    s->gnd = clock_new(obj, "gnd");
}

static void connect_mux_sources(Stm32l4x5RccState *s,
                                RccClockMuxState *mux,
                                const RccClockMuxSource *clk_mapping)
{
    size_t i;

    Clock * const CLK_SRC_MAPPING[] = {
        [RCC_CLOCK_MUX_SRC_GND] = s->gnd,
        [RCC_CLOCK_MUX_SRC_HSI] = s->hsi16_rc,
        [RCC_CLOCK_MUX_SRC_HSE] = s->hse,
        [RCC_CLOCK_MUX_SRC_MSI] = s->msi_rc,
        [RCC_CLOCK_MUX_SRC_LSI] = s->lsi_rc,
        [RCC_CLOCK_MUX_SRC_LSE] = s->lse_crystal,
        [RCC_CLOCK_MUX_SRC_SAI1_EXTCLK] = s->sai1_extclk,
        [RCC_CLOCK_MUX_SRC_SAI2_EXTCLK] = s->sai2_extclk,
        [RCC_CLOCK_MUX_SRC_PLL] =
            s->plls[RCC_PLL_PLL].channels[RCC_PLL_CHANNEL_PLLCLK],
        [RCC_CLOCK_MUX_SRC_PLLSAI1] =
            s->plls[RCC_PLL_PLLSAI1].channels[RCC_PLLSAI1_CHANNEL_PLLSAI1CLK],
        [RCC_CLOCK_MUX_SRC_PLLSAI2] =
            s->plls[RCC_PLL_PLLSAI2].channels[RCC_PLLSAI2_CHANNEL_PLLSAI2CLK],
        [RCC_CLOCK_MUX_SRC_PLLSAI3] =
            s->plls[RCC_PLL_PLL].channels[RCC_PLL_CHANNEL_PLLSAI3CLK],
        [RCC_CLOCK_MUX_SRC_PLL48M1] =
            s->plls[RCC_PLL_PLL].channels[RCC_PLL_CHANNEL_PLL48M1CLK],
        [RCC_CLOCK_MUX_SRC_PLL48M2] =
            s->plls[RCC_PLL_PLLSAI1].channels[RCC_PLLSAI1_CHANNEL_PLL48M2CLK],
        [RCC_CLOCK_MUX_SRC_PLLADC1] =
            s->plls[RCC_PLL_PLLSAI1].channels[RCC_PLLSAI1_CHANNEL_PLLADC1CLK],
        [RCC_CLOCK_MUX_SRC_PLLADC2] =
            s->plls[RCC_PLL_PLLSAI2] .channels[RCC_PLLSAI2_CHANNEL_PLLADC2CLK],
        [RCC_CLOCK_MUX_SRC_SYSCLK] = s->clock_muxes[RCC_CLOCK_MUX_SYSCLK].out,
        [RCC_CLOCK_MUX_SRC_HCLK] = s->clock_muxes[RCC_CLOCK_MUX_HCLK].out,
        [RCC_CLOCK_MUX_SRC_PCLK1] = s->clock_muxes[RCC_CLOCK_MUX_PCLK1].out,
        [RCC_CLOCK_MUX_SRC_PCLK2] = s->clock_muxes[RCC_CLOCK_MUX_PCLK2].out,
        [RCC_CLOCK_MUX_SRC_HSE_OVER_32] = s->clock_muxes[RCC_CLOCK_MUX_HSE_OVER_32].out,
        [RCC_CLOCK_MUX_SRC_LCD_AND_RTC_COMMON] =
            s->clock_muxes[RCC_CLOCK_MUX_LCD_AND_RTC_COMMON].out,
    };

    assert(ARRAY_SIZE(CLK_SRC_MAPPING) == RCC_CLOCK_MUX_SRC_NUMBER);

    for (i = 0; i < RCC_NUM_CLOCK_MUX_SRC; i++) {
        RccClockMuxSource mapping = clk_mapping[i];
        clock_set_source(mux->srcs[i], CLK_SRC_MAPPING[mapping]);
    }
}


static const VMStateDescription vmstate_stm32l4x5_rcc = {
    .name = TYPE_STM32L4X5_RCC,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(cr, Stm32l4x5RccState),
        VMSTATE_UINT32(icscr, Stm32l4x5RccState),
        VMSTATE_UINT32(cfgr, Stm32l4x5RccState),
        VMSTATE_UINT32(pllcfgr, Stm32l4x5RccState),
        VMSTATE_UINT32(pllsai1cfgr, Stm32l4x5RccState),
        VMSTATE_UINT32(pllsai2cfgr, Stm32l4x5RccState),
        VMSTATE_UINT32(cier, Stm32l4x5RccState),
        VMSTATE_UINT32(cifr, Stm32l4x5RccState),
        VMSTATE_UINT32(ahb1rstr, Stm32l4x5RccState),
        VMSTATE_UINT32(ahb2rstr, Stm32l4x5RccState),
        VMSTATE_UINT32(ahb3rstr, Stm32l4x5RccState),
        VMSTATE_UINT32(apb1rstr1, Stm32l4x5RccState),
        VMSTATE_UINT32(apb1rstr2, Stm32l4x5RccState),
        VMSTATE_UINT32(apb2rstr, Stm32l4x5RccState),
        VMSTATE_UINT32(ahb1enr, Stm32l4x5RccState),
        VMSTATE_UINT32(ahb2enr, Stm32l4x5RccState),
        VMSTATE_UINT32(ahb3enr, Stm32l4x5RccState),
        VMSTATE_UINT32(apb1enr1, Stm32l4x5RccState),
        VMSTATE_UINT32(apb1enr2, Stm32l4x5RccState),
        VMSTATE_UINT32(apb2enr, Stm32l4x5RccState),
        VMSTATE_UINT32(ahb1smenr, Stm32l4x5RccState),
        VMSTATE_UINT32(ahb2smenr, Stm32l4x5RccState),
        VMSTATE_UINT32(ahb3smenr, Stm32l4x5RccState),
        VMSTATE_UINT32(apb1smenr1, Stm32l4x5RccState),
        VMSTATE_UINT32(apb1smenr2, Stm32l4x5RccState),
        VMSTATE_UINT32(apb2smenr, Stm32l4x5RccState),
        VMSTATE_UINT32(ccipr, Stm32l4x5RccState),
        VMSTATE_UINT32(bdcr, Stm32l4x5RccState),
        VMSTATE_UINT32(csr, Stm32l4x5RccState),
        VMSTATE_CLOCK(hsi16_rc, Stm32l4x5RccState),
        VMSTATE_CLOCK(msi_rc, Stm32l4x5RccState),
        VMSTATE_CLOCK(hse, Stm32l4x5RccState),
        VMSTATE_CLOCK(lsi_rc, Stm32l4x5RccState),
        VMSTATE_CLOCK(lse_crystal, Stm32l4x5RccState),
        VMSTATE_CLOCK(sai1_extclk, Stm32l4x5RccState),
        VMSTATE_CLOCK(sai2_extclk, Stm32l4x5RccState),
        VMSTATE_END_OF_LIST()
    }
};


static void stm32l4x5_rcc_realize(DeviceState *dev, Error **errp)
{
    Stm32l4x5RccState *s = STM32L4X5_RCC(dev);
    size_t i;

    if (s->hse_frequency <  4000000ULL ||
        s->hse_frequency > 48000000ULL) {
            error_setg(errp,
                "HSE frequency is outside of the allowed [4-48]Mhz range: %" PRIx64 "",
                s->hse_frequency);
            return;
        }

    for (i = 0; i < RCC_NUM_PLL; i++) {
        RccPllState *pll = &s->plls[i];

        clock_set_source(pll->in, s->clock_muxes[RCC_CLOCK_MUX_PLL_INPUT].out);

        if (!qdev_realize(DEVICE(pll), NULL, errp)) {
            return;
        }
    }

    for (i = 0; i < RCC_NUM_CLOCK_MUX; i++) {
        RccClockMuxState *clock_mux = &s->clock_muxes[i];

        connect_mux_sources(s, clock_mux, CLOCK_MUX_INIT_INFO[i].src_mapping);

        if (!qdev_realize(DEVICE(clock_mux), NULL, errp)) {
            return;
        }
    }

    /*
     * Start clocks after everything is connected
     * to propagate the frequencies along the tree.
     */
    clock_update_hz(s->msi_rc, MSI_DEFAULT_FRQ);
    clock_update_hz(s->sai1_extclk, s->sai1_extclk_frequency);
    clock_update_hz(s->sai2_extclk, s->sai2_extclk_frequency);
    clock_update(s->gnd, 0);
}

static const Property stm32l4x5_rcc_properties[] = {
    DEFINE_PROP_UINT64("hse_frequency", Stm32l4x5RccState,
        hse_frequency, HSE_DEFAULT_FRQ),
    DEFINE_PROP_UINT64("sai1_extclk_frequency", Stm32l4x5RccState,
        sai1_extclk_frequency, 0),
    DEFINE_PROP_UINT64("sai2_extclk_frequency", Stm32l4x5RccState,
        sai2_extclk_frequency, 0),
};

static void stm32l4x5_rcc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    assert(ARRAY_SIZE(CLOCK_MUX_INIT_INFO) == RCC_NUM_CLOCK_MUX);

    rc->phases.hold = stm32l4x5_rcc_reset_hold;
    device_class_set_props(dc, stm32l4x5_rcc_properties);
    dc->realize = stm32l4x5_rcc_realize;
    dc->vmsd = &vmstate_stm32l4x5_rcc;
}

static const TypeInfo stm32l4x5_rcc_types[] = {
    {
        .name           = TYPE_STM32L4X5_RCC,
        .parent         = TYPE_SYS_BUS_DEVICE,
        .instance_size  = sizeof(Stm32l4x5RccState),
        .instance_init  = stm32l4x5_rcc_init,
        .class_init     = stm32l4x5_rcc_class_init,
    }, {
        .name = TYPE_RCC_CLOCK_MUX,
        .parent = TYPE_DEVICE,
        .instance_size = sizeof(RccClockMuxState),
        .instance_init = clock_mux_init,
        .class_init = clock_mux_class_init,
    }, {
        .name = TYPE_RCC_PLL,
        .parent = TYPE_DEVICE,
        .instance_size = sizeof(RccPllState),
        .instance_init = pll_init,
        .class_init = pll_class_init,
    }
};

DEFINE_TYPES(stm32l4x5_rcc_types)

/*
 * BCM2835 CPRMAN clock manager
 *
 * Copyright (c) 2020 Luc Michel <luc@lmichel.fr>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

/*
 * This peripheral is roughly divided into 3 main parts:
 *   - the PLLs
 *   - the PLL channels
 *   - the clock muxes
 *
 * A main oscillator (xosc) feeds all the PLLs. Each PLLs has one or more
 * channels. Those channel are then connected to the clock muxes. Each mux has
 * multiples sources (usually the xosc, some of the PLL channels and some "test
 * debug" clocks). A mux is configured to select a given source through its
 * control register. Each mux has one output clock that also goes out of the
 * CPRMAN. This output clock usually connects to another peripheral in the SoC
 * (so a given mux is dedicated to a peripheral).
 *
 * At each level (PLL, channel and mux), the clock can be altered through
 * dividers (and multipliers in case of the PLLs), and can be disabled (in this
 * case, the next levels see no clock).
 *
 * This can be sum-up as follows (this is an example and not the actual BCM2835
 * clock tree):
 *
 *          /-->[PLL]-|->[PLL channel]--...            [mux]--> to peripherals
 *          |         |->[PLL channel]  muxes takes    [mux]
 *          |         \->[PLL channel]  inputs from    [mux]
 *          |                           some channels  [mux]
 * [xosc]---|-->[PLL]-|->[PLL channel]  and other srcs [mux]
 *          |         \->[PLL channel]           ...-->[mux]
 *          |                                          [mux]
 *          \-->[PLL]--->[PLL channel]                 [mux]
 *
 * The page at https://elinux.org/The_Undocumented_Pi gives the actual clock
 * tree configuration.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "migration/vmstate.h"
#include "hw/qdev-properties.h"
#include "hw/misc/bcm2835_cprman.h"
#include "hw/misc/bcm2835_cprman_internals.h"
#include "trace.h"

/* PLL */

static bool pll_is_locked(const CprmanPllState *pll)
{
    return !FIELD_EX32(*pll->reg_a2w_ctrl, A2W_PLLx_CTRL, PWRDN)
        && !FIELD_EX32(*pll->reg_cm, CM_PLLx, ANARST);
}

static void pll_update(CprmanPllState *pll)
{
    uint64_t freq, ndiv, fdiv, pdiv;

    if (!pll_is_locked(pll)) {
        clock_update(pll->out, 0);
        return;
    }

    pdiv = FIELD_EX32(*pll->reg_a2w_ctrl, A2W_PLLx_CTRL, PDIV);

    if (!pdiv) {
        clock_update(pll->out, 0);
        return;
    }

    ndiv = FIELD_EX32(*pll->reg_a2w_ctrl, A2W_PLLx_CTRL, NDIV);
    fdiv = FIELD_EX32(*pll->reg_a2w_frac, A2W_PLLx_FRAC, FRAC);

    if (pll->reg_a2w_ana[1] & pll->prediv_mask) {
        /* The prescaler doubles the parent frequency */
        ndiv *= 2;
        fdiv *= 2;
    }

    /*
     * We have a multiplier with an integer part (ndiv) and a fractional part
     * (fdiv), and a divider (pdiv).
     */
    freq = clock_get_hz(pll->xosc_in) *
        ((ndiv << R_A2W_PLLx_FRAC_FRAC_LENGTH) + fdiv);
    freq /= pdiv;
    freq >>= R_A2W_PLLx_FRAC_FRAC_LENGTH;

    clock_update_hz(pll->out, freq);
}

static void pll_xosc_update(void *opaque)
{
    pll_update(CPRMAN_PLL(opaque));
}

static void pll_init(Object *obj)
{
    CprmanPllState *s = CPRMAN_PLL(obj);

    s->xosc_in = qdev_init_clock_in(DEVICE(s), "xosc-in", pll_xosc_update, s);
    s->out = qdev_init_clock_out(DEVICE(s), "out");
}

static const VMStateDescription pll_vmstate = {
    .name = TYPE_CPRMAN_PLL,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_CLOCK(xosc_in, CprmanPllState),
        VMSTATE_END_OF_LIST()
    }
};

static void pll_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &pll_vmstate;
}

static const TypeInfo cprman_pll_info = {
    .name = TYPE_CPRMAN_PLL,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(CprmanPllState),
    .class_init = pll_class_init,
    .instance_init = pll_init,
};


/* PLL channel */

static void pll_channel_update(CprmanPllChannelState *channel)
{
    clock_update(channel->out, 0);
}

/* Update a PLL and all its channels */
static void pll_update_all_channels(BCM2835CprmanState *s,
                                    CprmanPllState *pll)
{
    size_t i;

    pll_update(pll);

    for (i = 0; i < CPRMAN_NUM_PLL_CHANNEL; i++) {
        CprmanPllChannelState *channel = &s->channels[i];
        if (channel->parent == pll->id) {
            pll_channel_update(channel);
        }
    }
}

static void pll_channel_pll_in_update(void *opaque)
{
    pll_channel_update(CPRMAN_PLL_CHANNEL(opaque));
}

static void pll_channel_init(Object *obj)
{
    CprmanPllChannelState *s = CPRMAN_PLL_CHANNEL(obj);

    s->pll_in = qdev_init_clock_in(DEVICE(s), "pll-in",
                                   pll_channel_pll_in_update, s);
    s->out = qdev_init_clock_out(DEVICE(s), "out");
}

static const VMStateDescription pll_channel_vmstate = {
    .name = TYPE_CPRMAN_PLL_CHANNEL,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_CLOCK(pll_in, CprmanPllChannelState),
        VMSTATE_END_OF_LIST()
    }
};

static void pll_channel_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &pll_channel_vmstate;
}

static const TypeInfo cprman_pll_channel_info = {
    .name = TYPE_CPRMAN_PLL_CHANNEL,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(CprmanPllChannelState),
    .class_init = pll_channel_class_init,
    .instance_init = pll_channel_init,
};


/* CPRMAN "top level" model */

static uint32_t get_cm_lock(const BCM2835CprmanState *s)
{
    static const int CM_LOCK_MAPPING[CPRMAN_NUM_PLL] = {
        [CPRMAN_PLLA] = R_CM_LOCK_FLOCKA_SHIFT,
        [CPRMAN_PLLC] = R_CM_LOCK_FLOCKC_SHIFT,
        [CPRMAN_PLLD] = R_CM_LOCK_FLOCKD_SHIFT,
        [CPRMAN_PLLH] = R_CM_LOCK_FLOCKH_SHIFT,
        [CPRMAN_PLLB] = R_CM_LOCK_FLOCKB_SHIFT,
    };

    uint32_t r = 0;
    size_t i;

    for (i = 0; i < CPRMAN_NUM_PLL; i++) {
        r |= pll_is_locked(&s->plls[i]) << CM_LOCK_MAPPING[i];
    }

    return r;
}

static uint64_t cprman_read(void *opaque, hwaddr offset,
                            unsigned size)
{
    BCM2835CprmanState *s = CPRMAN(opaque);
    uint64_t r = 0;
    size_t idx = offset / sizeof(uint32_t);

    switch (idx) {
    case R_CM_LOCK:
        r = get_cm_lock(s);
        break;

    default:
        r = s->regs[idx];
    }

    trace_bcm2835_cprman_read(offset, r);
    return r;
}

static inline void update_pll_and_channels_from_cm(BCM2835CprmanState *s,
                                                   size_t idx)
{
    size_t i;

    for (i = 0; i < CPRMAN_NUM_PLL; i++) {
        if (PLL_INIT_INFO[i].cm_offset == idx) {
            pll_update_all_channels(s, &s->plls[i]);
            return;
        }
    }
}

static inline void update_channel_from_a2w(BCM2835CprmanState *s, size_t idx)
{
    size_t i;

    for (i = 0; i < CPRMAN_NUM_PLL_CHANNEL; i++) {
        if (PLL_CHANNEL_INIT_INFO[i].a2w_ctrl_offset == idx) {
            pll_channel_update(&s->channels[i]);
            return;
        }
    }
}

#define CASE_PLL_A2W_REGS(pll_) \
    case R_A2W_ ## pll_ ## _CTRL: \
    case R_A2W_ ## pll_ ## _ANA0: \
    case R_A2W_ ## pll_ ## _ANA1: \
    case R_A2W_ ## pll_ ## _ANA2: \
    case R_A2W_ ## pll_ ## _ANA3: \
    case R_A2W_ ## pll_ ## _FRAC

static void cprman_write(void *opaque, hwaddr offset,
                         uint64_t value, unsigned size)
{
    BCM2835CprmanState *s = CPRMAN(opaque);
    size_t idx = offset / sizeof(uint32_t);

    if (FIELD_EX32(value, CPRMAN, PASSWORD) != CPRMAN_PASSWORD) {
        trace_bcm2835_cprman_write_invalid_magic(offset, value);
        return;
    }

    value &= ~R_CPRMAN_PASSWORD_MASK;

    trace_bcm2835_cprman_write(offset, value);
    s->regs[idx] = value;

    switch (idx) {
    case R_CM_PLLA ... R_CM_PLLH:
    case R_CM_PLLB:
        /*
         * A given CM_PLLx register is shared by both the PLL and the channels
         * of this PLL.
         */
        update_pll_and_channels_from_cm(s, idx);
        break;

    CASE_PLL_A2W_REGS(PLLA) :
        pll_update(&s->plls[CPRMAN_PLLA]);
        break;

    CASE_PLL_A2W_REGS(PLLC) :
        pll_update(&s->plls[CPRMAN_PLLC]);
        break;

    CASE_PLL_A2W_REGS(PLLD) :
        pll_update(&s->plls[CPRMAN_PLLD]);
        break;

    CASE_PLL_A2W_REGS(PLLH) :
        pll_update(&s->plls[CPRMAN_PLLH]);
        break;

    CASE_PLL_A2W_REGS(PLLB) :
        pll_update(&s->plls[CPRMAN_PLLB]);
        break;

    case R_A2W_PLLA_DSI0:
    case R_A2W_PLLA_CORE:
    case R_A2W_PLLA_PER:
    case R_A2W_PLLA_CCP2:
    case R_A2W_PLLC_CORE2:
    case R_A2W_PLLC_CORE1:
    case R_A2W_PLLC_PER:
    case R_A2W_PLLC_CORE0:
    case R_A2W_PLLD_DSI0:
    case R_A2W_PLLD_CORE:
    case R_A2W_PLLD_PER:
    case R_A2W_PLLD_DSI1:
    case R_A2W_PLLH_AUX:
    case R_A2W_PLLH_RCAL:
    case R_A2W_PLLH_PIX:
    case R_A2W_PLLB_ARM:
        update_channel_from_a2w(s, idx);
        break;
    }
}

#undef CASE_PLL_A2W_REGS

static const MemoryRegionOps cprman_ops = {
    .read = cprman_read,
    .write = cprman_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        /*
         * Although this hasn't been checked against real hardware, nor the
         * information can be found in a datasheet, it seems reasonable because
         * of the "PASSWORD" magic value found in every registers.
         */
        .min_access_size        = 4,
        .max_access_size        = 4,
        .unaligned              = false,
    },
    .impl = {
        .max_access_size = 4,
    },
};

static void cprman_reset(DeviceState *dev)
{
    BCM2835CprmanState *s = CPRMAN(dev);
    size_t i;

    memset(s->regs, 0, sizeof(s->regs));

    for (i = 0; i < CPRMAN_NUM_PLL; i++) {
        device_cold_reset(DEVICE(&s->plls[i]));
    }

    for (i = 0; i < CPRMAN_NUM_PLL_CHANNEL; i++) {
        device_cold_reset(DEVICE(&s->channels[i]));
    }

    clock_update_hz(s->xosc, s->xosc_freq);
}

static void cprman_init(Object *obj)
{
    BCM2835CprmanState *s = CPRMAN(obj);
    size_t i;

    for (i = 0; i < CPRMAN_NUM_PLL; i++) {
        object_initialize_child(obj, PLL_INIT_INFO[i].name,
                                &s->plls[i], TYPE_CPRMAN_PLL);
        set_pll_init_info(s, &s->plls[i], i);
    }

    for (i = 0; i < CPRMAN_NUM_PLL_CHANNEL; i++) {
        object_initialize_child(obj, PLL_CHANNEL_INIT_INFO[i].name,
                                &s->channels[i],
                                TYPE_CPRMAN_PLL_CHANNEL);
        set_pll_channel_init_info(s, &s->channels[i], i);
    }

    s->xosc = clock_new(obj, "xosc");

    memory_region_init_io(&s->iomem, obj, &cprman_ops,
                          s, "bcm2835-cprman", 0x2000);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static void cprman_realize(DeviceState *dev, Error **errp)
{
    BCM2835CprmanState *s = CPRMAN(dev);
    size_t i;

    for (i = 0; i < CPRMAN_NUM_PLL; i++) {
        CprmanPllState *pll = &s->plls[i];

        clock_set_source(pll->xosc_in, s->xosc);

        if (!qdev_realize(DEVICE(pll), NULL, errp)) {
            return;
        }
    }

    for (i = 0; i < CPRMAN_NUM_PLL_CHANNEL; i++) {
        CprmanPllChannelState *channel = &s->channels[i];
        CprmanPll parent = PLL_CHANNEL_INIT_INFO[i].parent;
        Clock *parent_clk = s->plls[parent].out;

        clock_set_source(channel->pll_in, parent_clk);

        if (!qdev_realize(DEVICE(channel), NULL, errp)) {
            return;
        }
    }
}

static const VMStateDescription cprman_vmstate = {
    .name = TYPE_BCM2835_CPRMAN,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, BCM2835CprmanState, CPRMAN_NUM_REGS),
        VMSTATE_END_OF_LIST()
    }
};

static Property cprman_properties[] = {
    DEFINE_PROP_UINT32("xosc-freq-hz", BCM2835CprmanState, xosc_freq, 19200000),
    DEFINE_PROP_END_OF_LIST()
};

static void cprman_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = cprman_realize;
    dc->reset = cprman_reset;
    dc->vmsd = &cprman_vmstate;
    device_class_set_props(dc, cprman_properties);
}

static const TypeInfo cprman_info = {
    .name = TYPE_BCM2835_CPRMAN,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(BCM2835CprmanState),
    .class_init = cprman_class_init,
    .instance_init = cprman_init,
};

static void cprman_register_types(void)
{
    type_register_static(&cprman_info);
    type_register_static(&cprman_pll_info);
    type_register_static(&cprman_pll_channel_info);
}

type_init(cprman_register_types);

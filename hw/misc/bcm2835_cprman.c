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
 *
 * The CPRMAN exposes clock outputs with the name of the clock mux suffixed
 * with "-out" (e.g. "uart-out", "h264-out", ...).
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "migration/vmstate.h"
#include "hw/qdev-properties.h"
#include "hw/misc/bcm2835_cprman.h"
#include "hw/misc/bcm2835_cprman_internals.h"
#include "trace.h"

/* PLL */

static void pll_reset(DeviceState *dev)
{
    CprmanPllState *s = CPRMAN_PLL(dev);
    const PLLResetInfo *info = &PLL_RESET_INFO[s->id];

    *s->reg_cm = info->cm;
    *s->reg_a2w_ctrl = info->a2w_ctrl;
    memcpy(s->reg_a2w_ana, info->a2w_ana, sizeof(info->a2w_ana));
    *s->reg_a2w_frac = info->a2w_frac;
}

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

static void pll_xosc_update(void *opaque, ClockEvent event)
{
    pll_update(CPRMAN_PLL(opaque));
}

static void pll_init(Object *obj)
{
    CprmanPllState *s = CPRMAN_PLL(obj);

    s->xosc_in = qdev_init_clock_in(DEVICE(s), "xosc-in", pll_xosc_update,
                                    s, ClockUpdate);
    s->out = qdev_init_clock_out(DEVICE(s), "out");
}

static const VMStateDescription pll_vmstate = {
    .name = TYPE_CPRMAN_PLL,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_CLOCK(xosc_in, CprmanPllState),
        VMSTATE_END_OF_LIST()
    }
};

static void pll_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, pll_reset);
    dc->vmsd = &pll_vmstate;
    /* Reason: Part of BCM2835CprmanState component */
    dc->user_creatable = false;
}

static const TypeInfo cprman_pll_info = {
    .name = TYPE_CPRMAN_PLL,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(CprmanPllState),
    .class_init = pll_class_init,
    .instance_init = pll_init,
};


/* PLL channel */

static void pll_channel_reset(DeviceState *dev)
{
    CprmanPllChannelState *s = CPRMAN_PLL_CHANNEL(dev);
    const PLLChannelResetInfo *info = &PLL_CHANNEL_RESET_INFO[s->id];

    *s->reg_a2w_ctrl = info->a2w_ctrl;
}

static bool pll_channel_is_enabled(CprmanPllChannelState *channel)
{
    /*
     * XXX I'm not sure of the purpose of the LOAD field. The Linux driver does
     * not set it when enabling the channel, but does clear it when disabling
     * it.
     */
    return !FIELD_EX32(*channel->reg_a2w_ctrl, A2W_PLLx_CHANNELy, DISABLE)
        && !(*channel->reg_cm & channel->hold_mask);
}

static void pll_channel_update(CprmanPllChannelState *channel)
{
    uint64_t freq, div;

    if (!pll_channel_is_enabled(channel)) {
        clock_update(channel->out, 0);
        return;
    }

    div = FIELD_EX32(*channel->reg_a2w_ctrl, A2W_PLLx_CHANNELy, DIV);

    if (!div) {
        /*
         * It seems that when the divider value is 0, it is considered as
         * being maximum by the hardware (see the Linux driver).
         */
        div = R_A2W_PLLx_CHANNELy_DIV_MASK;
    }

    /* Some channels have an additional fixed divider */
    freq = clock_get_hz(channel->pll_in) / (div * channel->fixed_divider);

    clock_update_hz(channel->out, freq);
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

static void pll_channel_pll_in_update(void *opaque, ClockEvent event)
{
    pll_channel_update(CPRMAN_PLL_CHANNEL(opaque));
}

static void pll_channel_init(Object *obj)
{
    CprmanPllChannelState *s = CPRMAN_PLL_CHANNEL(obj);

    s->pll_in = qdev_init_clock_in(DEVICE(s), "pll-in",
                                   pll_channel_pll_in_update, s,
                                   ClockUpdate);
    s->out = qdev_init_clock_out(DEVICE(s), "out");
}

static const VMStateDescription pll_channel_vmstate = {
    .name = TYPE_CPRMAN_PLL_CHANNEL,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_CLOCK(pll_in, CprmanPllChannelState),
        VMSTATE_END_OF_LIST()
    }
};

static void pll_channel_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, pll_channel_reset);
    dc->vmsd = &pll_channel_vmstate;
    /* Reason: Part of BCM2835CprmanState component */
    dc->user_creatable = false;
}

static const TypeInfo cprman_pll_channel_info = {
    .name = TYPE_CPRMAN_PLL_CHANNEL,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(CprmanPllChannelState),
    .class_init = pll_channel_class_init,
    .instance_init = pll_channel_init,
};


/* clock mux */

static bool clock_mux_is_enabled(CprmanClockMuxState *mux)
{
    return FIELD_EX32(*mux->reg_ctl, CM_CLOCKx_CTL, ENABLE);
}

static void clock_mux_update(CprmanClockMuxState *mux)
{
    uint64_t freq;
    uint32_t div, src = FIELD_EX32(*mux->reg_ctl, CM_CLOCKx_CTL, SRC);
    bool enabled = clock_mux_is_enabled(mux);

    *mux->reg_ctl = FIELD_DP32(*mux->reg_ctl, CM_CLOCKx_CTL, BUSY, enabled);

    if (!enabled) {
        clock_update(mux->out, 0);
        return;
    }

    freq = clock_get_hz(mux->srcs[src]);

    if (mux->int_bits == 0 && mux->frac_bits == 0) {
        clock_update_hz(mux->out, freq);
        return;
    }

    /*
     * The divider has an integer and a fractional part. The size of each part
     * varies with the muxes (int_bits and frac_bits). Both parts are
     * concatenated, with the integer part always starting at bit 12.
     *
     *         31          12 11          0
     *        ------------------------------
     * CM_DIV |      |  int  |  frac  |    |
     *        ------------------------------
     *                <-----> <------>
     *                int_bits frac_bits
     */
    div = extract32(*mux->reg_div,
                    R_CM_CLOCKx_DIV_FRAC_LENGTH - mux->frac_bits,
                    mux->int_bits + mux->frac_bits);

    if (!div) {
        clock_update(mux->out, 0);
        return;
    }

    freq = muldiv64(freq, 1 << mux->frac_bits, div);

    clock_update_hz(mux->out, freq);
}

static void clock_mux_src_update(void *opaque, ClockEvent event)
{
    CprmanClockMuxState **backref = opaque;
    CprmanClockMuxState *s = *backref;
    CprmanClockMuxSource src = backref - s->backref;

    if (FIELD_EX32(*s->reg_ctl, CM_CLOCKx_CTL, SRC) != src) {
        return;
    }

    clock_mux_update(s);
}

static void clock_mux_reset(DeviceState *dev)
{
    CprmanClockMuxState *clock = CPRMAN_CLOCK_MUX(dev);
    const ClockMuxResetInfo *info = &CLOCK_MUX_RESET_INFO[clock->id];

    *clock->reg_ctl = info->cm_ctl;
    *clock->reg_div = info->cm_div;
}

static void clock_mux_init(Object *obj)
{
    CprmanClockMuxState *s = CPRMAN_CLOCK_MUX(obj);
    size_t i;

    for (i = 0; i < CPRMAN_NUM_CLOCK_MUX_SRC; i++) {
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

static const VMStateDescription clock_mux_vmstate = {
    .name = TYPE_CPRMAN_CLOCK_MUX,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_ARRAY_CLOCK(srcs, CprmanClockMuxState,
                            CPRMAN_NUM_CLOCK_MUX_SRC),
        VMSTATE_END_OF_LIST()
    }
};

static void clock_mux_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, clock_mux_reset);
    dc->vmsd = &clock_mux_vmstate;
    /* Reason: Part of BCM2835CprmanState component */
    dc->user_creatable = false;
}

static const TypeInfo cprman_clock_mux_info = {
    .name = TYPE_CPRMAN_CLOCK_MUX,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(CprmanClockMuxState),
    .class_init = clock_mux_class_init,
    .instance_init = clock_mux_init,
};


/* DSI0HSCK mux */

static void dsi0hsck_mux_update(CprmanDsi0HsckMuxState *s)
{
    bool src_is_plld = FIELD_EX32(*s->reg_cm, CM_DSI0HSCK, SELPLLD);
    Clock *src = src_is_plld ? s->plld_in : s->plla_in;

    clock_update(s->out, clock_get(src));
}

static void dsi0hsck_mux_in_update(void *opaque, ClockEvent event)
{
    dsi0hsck_mux_update(CPRMAN_DSI0HSCK_MUX(opaque));
}

static void dsi0hsck_mux_init(Object *obj)
{
    CprmanDsi0HsckMuxState *s = CPRMAN_DSI0HSCK_MUX(obj);
    DeviceState *dev = DEVICE(obj);

    s->plla_in = qdev_init_clock_in(dev, "plla-in", dsi0hsck_mux_in_update,
                                    s, ClockUpdate);
    s->plld_in = qdev_init_clock_in(dev, "plld-in", dsi0hsck_mux_in_update,
                                    s, ClockUpdate);
    s->out = qdev_init_clock_out(DEVICE(s), "out");
}

static const VMStateDescription dsi0hsck_mux_vmstate = {
    .name = TYPE_CPRMAN_DSI0HSCK_MUX,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_CLOCK(plla_in, CprmanDsi0HsckMuxState),
        VMSTATE_CLOCK(plld_in, CprmanDsi0HsckMuxState),
        VMSTATE_END_OF_LIST()
    }
};

static void dsi0hsck_mux_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &dsi0hsck_mux_vmstate;
    /* Reason: Part of BCM2835CprmanState component */
    dc->user_creatable = false;
}

static const TypeInfo cprman_dsi0hsck_mux_info = {
    .name = TYPE_CPRMAN_DSI0HSCK_MUX,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(CprmanDsi0HsckMuxState),
    .class_init = dsi0hsck_mux_class_init,
    .instance_init = dsi0hsck_mux_init,
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

static inline void update_mux_from_cm(BCM2835CprmanState *s, size_t idx)
{
    size_t i;

    for (i = 0; i < CPRMAN_NUM_CLOCK_MUX; i++) {
        if ((CLOCK_MUX_INIT_INFO[i].cm_offset == idx) ||
            (CLOCK_MUX_INIT_INFO[i].cm_offset + 4 == idx)) {
            /* matches CM_CTL or CM_DIV mux register */
            clock_mux_update(&s->clock_muxes[i]);
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

    case R_CM_GNRICCTL ... R_CM_SMIDIV:
    case R_CM_TCNTCNT ... R_CM_VECDIV:
    case R_CM_PULSECTL ... R_CM_PULSEDIV:
    case R_CM_SDCCTL ... R_CM_ARMCTL:
    case R_CM_AVEOCTL ... R_CM_EMMCDIV:
    case R_CM_EMMC2CTL ... R_CM_EMMC2DIV:
        update_mux_from_cm(s, idx);
        break;

    case R_CM_DSI0HSCK:
        dsi0hsck_mux_update(&s->dsi0hsck_mux);
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

    device_cold_reset(DEVICE(&s->dsi0hsck_mux));

    for (i = 0; i < CPRMAN_NUM_CLOCK_MUX; i++) {
        device_cold_reset(DEVICE(&s->clock_muxes[i]));
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

    object_initialize_child(obj, "dsi0hsck-mux",
                            &s->dsi0hsck_mux, TYPE_CPRMAN_DSI0HSCK_MUX);
    s->dsi0hsck_mux.reg_cm = &s->regs[R_CM_DSI0HSCK];

    for (i = 0; i < CPRMAN_NUM_CLOCK_MUX; i++) {
        char *alias;

        object_initialize_child(obj, CLOCK_MUX_INIT_INFO[i].name,
                                &s->clock_muxes[i],
                                TYPE_CPRMAN_CLOCK_MUX);
        set_clock_mux_init_info(s, &s->clock_muxes[i], i);

        /* Expose muxes output as CPRMAN outputs */
        alias = g_strdup_printf("%s-out", CLOCK_MUX_INIT_INFO[i].name);
        qdev_alias_clock(DEVICE(&s->clock_muxes[i]), "out", DEVICE(obj), alias);
        g_free(alias);
    }

    s->xosc = clock_new(obj, "xosc");
    s->gnd = clock_new(obj, "gnd");

    clock_set(s->gnd, 0);

    memory_region_init_io(&s->iomem, obj, &cprman_ops,
                          s, "bcm2835-cprman", 0x2000);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static void connect_mux_sources(BCM2835CprmanState *s,
                                CprmanClockMuxState *mux,
                                const CprmanPllChannel *clk_mapping)
{
    size_t i;
    Clock *td0 = s->clock_muxes[CPRMAN_CLOCK_TD0].out;
    Clock *td1 = s->clock_muxes[CPRMAN_CLOCK_TD1].out;

    /* For sources from 0 to 3. Source 4 to 9 are mux specific */
    Clock * const CLK_SRC_MAPPING[] = {
        [CPRMAN_CLOCK_SRC_GND] = s->gnd,
        [CPRMAN_CLOCK_SRC_XOSC] = s->xosc,
        [CPRMAN_CLOCK_SRC_TD0] = td0,
        [CPRMAN_CLOCK_SRC_TD1] = td1,
    };

    for (i = 0; i < CPRMAN_NUM_CLOCK_MUX_SRC; i++) {
        CprmanPllChannel mapping = clk_mapping[i];
        Clock *src;

        if (mapping == CPRMAN_CLOCK_SRC_FORCE_GROUND) {
            src = s->gnd;
        } else if (mapping == CPRMAN_CLOCK_SRC_DSI0HSCK) {
            src = s->dsi0hsck_mux.out;
        } else if (i < CPRMAN_CLOCK_SRC_PLLA) {
            src = CLK_SRC_MAPPING[i];
        } else {
            src = s->channels[mapping].out;
        }

        clock_set_source(mux->srcs[i], src);
    }
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

    clock_set_source(s->dsi0hsck_mux.plla_in,
                     s->channels[CPRMAN_PLLA_CHANNEL_DSI0].out);
    clock_set_source(s->dsi0hsck_mux.plld_in,
                     s->channels[CPRMAN_PLLD_CHANNEL_DSI0].out);

    if (!qdev_realize(DEVICE(&s->dsi0hsck_mux), NULL, errp)) {
        return;
    }

    for (i = 0; i < CPRMAN_NUM_CLOCK_MUX; i++) {
        CprmanClockMuxState *clock_mux = &s->clock_muxes[i];

        connect_mux_sources(s, clock_mux, CLOCK_MUX_INIT_INFO[i].src_mapping);

        if (!qdev_realize(DEVICE(clock_mux), NULL, errp)) {
            return;
        }
    }
}

static const VMStateDescription cprman_vmstate = {
    .name = TYPE_BCM2835_CPRMAN,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, BCM2835CprmanState, CPRMAN_NUM_REGS),
        VMSTATE_END_OF_LIST()
    }
};

static const Property cprman_properties[] = {
    DEFINE_PROP_UINT32("xosc-freq-hz", BCM2835CprmanState, xosc_freq, 19200000),
};

static void cprman_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = cprman_realize;
    device_class_set_legacy_reset(dc, cprman_reset);
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
    type_register_static(&cprman_clock_mux_info);
    type_register_static(&cprman_dsi0hsck_mux_info);
}

type_init(cprman_register_types);

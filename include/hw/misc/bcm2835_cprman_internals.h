/*
 * BCM2835 CPRMAN clock manager
 *
 * Copyright (c) 2020 Luc Michel <luc@lmichel.fr>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_CPRMAN_INTERNALS_H
#define HW_MISC_CPRMAN_INTERNALS_H

#include "hw/registerfields.h"
#include "hw/misc/bcm2835_cprman.h"

#define TYPE_CPRMAN_PLL "bcm2835-cprman-pll"
#define TYPE_CPRMAN_PLL_CHANNEL "bcm2835-cprman-pll-channel"

DECLARE_INSTANCE_CHECKER(CprmanPllState, CPRMAN_PLL,
                         TYPE_CPRMAN_PLL)
DECLARE_INSTANCE_CHECKER(CprmanPllChannelState, CPRMAN_PLL_CHANNEL,
                         TYPE_CPRMAN_PLL_CHANNEL)

/* Register map */

/* PLLs */
REG32(CM_PLLA, 0x104)
    FIELD(CM_PLLA, LOADDSI0, 0, 1)
    FIELD(CM_PLLA, HOLDDSI0, 1, 1)
    FIELD(CM_PLLA, LOADCCP2, 2, 1)
    FIELD(CM_PLLA, HOLDCCP2, 3, 1)
    FIELD(CM_PLLA, LOADCORE, 4, 1)
    FIELD(CM_PLLA, HOLDCORE, 5, 1)
    FIELD(CM_PLLA, LOADPER, 6, 1)
    FIELD(CM_PLLA, HOLDPER, 7, 1)
    FIELD(CM_PLLx, ANARST, 8, 1)
REG32(CM_PLLC, 0x108)
    FIELD(CM_PLLC, LOADCORE0, 0, 1)
    FIELD(CM_PLLC, HOLDCORE0, 1, 1)
    FIELD(CM_PLLC, LOADCORE1, 2, 1)
    FIELD(CM_PLLC, HOLDCORE1, 3, 1)
    FIELD(CM_PLLC, LOADCORE2, 4, 1)
    FIELD(CM_PLLC, HOLDCORE2, 5, 1)
    FIELD(CM_PLLC, LOADPER, 6, 1)
    FIELD(CM_PLLC, HOLDPER, 7, 1)
REG32(CM_PLLD, 0x10c)
    FIELD(CM_PLLD, LOADDSI0, 0, 1)
    FIELD(CM_PLLD, HOLDDSI0, 1, 1)
    FIELD(CM_PLLD, LOADDSI1, 2, 1)
    FIELD(CM_PLLD, HOLDDSI1, 3, 1)
    FIELD(CM_PLLD, LOADCORE, 4, 1)
    FIELD(CM_PLLD, HOLDCORE, 5, 1)
    FIELD(CM_PLLD, LOADPER, 6, 1)
    FIELD(CM_PLLD, HOLDPER, 7, 1)
REG32(CM_PLLH, 0x110)
    FIELD(CM_PLLH, LOADPIX, 0, 1)
    FIELD(CM_PLLH, LOADAUX, 1, 1)
    FIELD(CM_PLLH, LOADRCAL, 2, 1)
REG32(CM_PLLB, 0x170)
    FIELD(CM_PLLB, LOADARM, 0, 1)
    FIELD(CM_PLLB, HOLDARM, 1, 1)

REG32(A2W_PLLA_CTRL, 0x1100)
    FIELD(A2W_PLLx_CTRL, NDIV, 0, 10)
    FIELD(A2W_PLLx_CTRL, PDIV, 12, 3)
    FIELD(A2W_PLLx_CTRL, PWRDN, 16, 1)
    FIELD(A2W_PLLx_CTRL, PRST_DISABLE, 17, 1)
REG32(A2W_PLLC_CTRL, 0x1120)
REG32(A2W_PLLD_CTRL, 0x1140)
REG32(A2W_PLLH_CTRL, 0x1160)
REG32(A2W_PLLB_CTRL, 0x11e0)

REG32(A2W_PLLA_ANA0, 0x1010)
REG32(A2W_PLLA_ANA1, 0x1014)
    FIELD(A2W_PLLx_ANA1, FB_PREDIV, 14, 1)
REG32(A2W_PLLA_ANA2, 0x1018)
REG32(A2W_PLLA_ANA3, 0x101c)

REG32(A2W_PLLC_ANA0, 0x1030)
REG32(A2W_PLLC_ANA1, 0x1034)
REG32(A2W_PLLC_ANA2, 0x1038)
REG32(A2W_PLLC_ANA3, 0x103c)

REG32(A2W_PLLD_ANA0, 0x1050)
REG32(A2W_PLLD_ANA1, 0x1054)
REG32(A2W_PLLD_ANA2, 0x1058)
REG32(A2W_PLLD_ANA3, 0x105c)

REG32(A2W_PLLH_ANA0, 0x1070)
REG32(A2W_PLLH_ANA1, 0x1074)
    FIELD(A2W_PLLH_ANA1, FB_PREDIV, 11, 1)
REG32(A2W_PLLH_ANA2, 0x1078)
REG32(A2W_PLLH_ANA3, 0x107c)

REG32(A2W_PLLB_ANA0, 0x10f0)
REG32(A2W_PLLB_ANA1, 0x10f4)
REG32(A2W_PLLB_ANA2, 0x10f8)
REG32(A2W_PLLB_ANA3, 0x10fc)

REG32(A2W_PLLA_FRAC, 0x1200)
    FIELD(A2W_PLLx_FRAC, FRAC, 0, 20)
REG32(A2W_PLLC_FRAC, 0x1220)
REG32(A2W_PLLD_FRAC, 0x1240)
REG32(A2W_PLLH_FRAC, 0x1260)
REG32(A2W_PLLB_FRAC, 0x12e0)

/* PLL channels */
REG32(A2W_PLLA_DSI0, 0x1300)
    FIELD(A2W_PLLx_CHANNELy, DIV, 0, 8)
    FIELD(A2W_PLLx_CHANNELy, DISABLE, 8, 1)
REG32(A2W_PLLA_CORE, 0x1400)
REG32(A2W_PLLA_PER, 0x1500)
REG32(A2W_PLLA_CCP2, 0x1600)

REG32(A2W_PLLC_CORE2, 0x1320)
REG32(A2W_PLLC_CORE1, 0x1420)
REG32(A2W_PLLC_PER, 0x1520)
REG32(A2W_PLLC_CORE0, 0x1620)

REG32(A2W_PLLD_DSI0, 0x1340)
REG32(A2W_PLLD_CORE, 0x1440)
REG32(A2W_PLLD_PER, 0x1540)
REG32(A2W_PLLD_DSI1, 0x1640)

REG32(A2W_PLLH_AUX, 0x1360)
REG32(A2W_PLLH_RCAL, 0x1460)
REG32(A2W_PLLH_PIX, 0x1560)
REG32(A2W_PLLH_STS, 0x1660)

REG32(A2W_PLLB_ARM, 0x13e0)

/* misc registers */
REG32(CM_LOCK, 0x114)
    FIELD(CM_LOCK, FLOCKH, 12, 1)
    FIELD(CM_LOCK, FLOCKD, 11, 1)
    FIELD(CM_LOCK, FLOCKC, 10, 1)
    FIELD(CM_LOCK, FLOCKB, 9, 1)
    FIELD(CM_LOCK, FLOCKA, 8, 1)

/*
 * This field is common to all registers. Each register write value must match
 * the CPRMAN_PASSWORD magic value in its 8 MSB.
 */
FIELD(CPRMAN, PASSWORD, 24, 8)
#define CPRMAN_PASSWORD 0x5a

/* PLL init info */
typedef struct PLLInitInfo {
    const char *name;
    size_t cm_offset;
    size_t a2w_ctrl_offset;
    size_t a2w_ana_offset;
    uint32_t prediv_mask; /* Prediv bit in ana[1] */
    size_t a2w_frac_offset;
} PLLInitInfo;

#define FILL_PLL_INIT_INFO(pll_)                \
    .cm_offset = R_CM_ ## pll_,                 \
    .a2w_ctrl_offset = R_A2W_ ## pll_ ## _CTRL, \
    .a2w_ana_offset = R_A2W_ ## pll_ ## _ANA0,  \
    .a2w_frac_offset = R_A2W_ ## pll_ ## _FRAC

static const PLLInitInfo PLL_INIT_INFO[] = {
    [CPRMAN_PLLA] = {
        .name = "plla",
        .prediv_mask = R_A2W_PLLx_ANA1_FB_PREDIV_MASK,
        FILL_PLL_INIT_INFO(PLLA),
    },
    [CPRMAN_PLLC] = {
        .name = "pllc",
        .prediv_mask = R_A2W_PLLx_ANA1_FB_PREDIV_MASK,
        FILL_PLL_INIT_INFO(PLLC),
    },
    [CPRMAN_PLLD] = {
        .name = "plld",
        .prediv_mask = R_A2W_PLLx_ANA1_FB_PREDIV_MASK,
        FILL_PLL_INIT_INFO(PLLD),
    },
    [CPRMAN_PLLH] = {
        .name = "pllh",
        .prediv_mask = R_A2W_PLLH_ANA1_FB_PREDIV_MASK,
        FILL_PLL_INIT_INFO(PLLH),
    },
    [CPRMAN_PLLB] = {
        .name = "pllb",
        .prediv_mask = R_A2W_PLLx_ANA1_FB_PREDIV_MASK,
        FILL_PLL_INIT_INFO(PLLB),
    },
};

#undef FILL_PLL_CHANNEL_INIT_INFO

static inline void set_pll_init_info(BCM2835CprmanState *s,
                                     CprmanPllState *pll,
                                     CprmanPll id)
{
    pll->id = id;
    pll->reg_cm = &s->regs[PLL_INIT_INFO[id].cm_offset];
    pll->reg_a2w_ctrl = &s->regs[PLL_INIT_INFO[id].a2w_ctrl_offset];
    pll->reg_a2w_ana = &s->regs[PLL_INIT_INFO[id].a2w_ana_offset];
    pll->prediv_mask = PLL_INIT_INFO[id].prediv_mask;
    pll->reg_a2w_frac = &s->regs[PLL_INIT_INFO[id].a2w_frac_offset];
}


/* PLL channel init info */
typedef struct PLLChannelInitInfo {
    const char *name;
    CprmanPll parent;
    size_t cm_offset;
    uint32_t cm_hold_mask;
    uint32_t cm_load_mask;
    size_t a2w_ctrl_offset;
    unsigned int fixed_divider;
} PLLChannelInitInfo;

#define FILL_PLL_CHANNEL_INIT_INFO_common(pll_, channel_)            \
    .parent = CPRMAN_ ## pll_,                                       \
    .cm_offset = R_CM_ ## pll_,                                      \
    .cm_load_mask = R_CM_ ## pll_ ## _ ## LOAD ## channel_ ## _MASK, \
    .a2w_ctrl_offset = R_A2W_ ## pll_ ## _ ## channel_

#define FILL_PLL_CHANNEL_INIT_INFO(pll_, channel_)                   \
    FILL_PLL_CHANNEL_INIT_INFO_common(pll_, channel_),               \
    .cm_hold_mask = R_CM_ ## pll_ ## _ ## HOLD ## channel_ ## _MASK, \
    .fixed_divider = 1

#define FILL_PLL_CHANNEL_INIT_INFO_nohold(pll_, channel_) \
    FILL_PLL_CHANNEL_INIT_INFO_common(pll_, channel_),    \
    .cm_hold_mask = 0

static PLLChannelInitInfo PLL_CHANNEL_INIT_INFO[] = {
    [CPRMAN_PLLA_CHANNEL_DSI0] = {
        .name = "plla-dsi0",
        FILL_PLL_CHANNEL_INIT_INFO(PLLA, DSI0),
    },
    [CPRMAN_PLLA_CHANNEL_CORE] = {
        .name = "plla-core",
        FILL_PLL_CHANNEL_INIT_INFO(PLLA, CORE),
    },
    [CPRMAN_PLLA_CHANNEL_PER] = {
        .name = "plla-per",
        FILL_PLL_CHANNEL_INIT_INFO(PLLA, PER),
    },
    [CPRMAN_PLLA_CHANNEL_CCP2] = {
        .name = "plla-ccp2",
        FILL_PLL_CHANNEL_INIT_INFO(PLLA, CCP2),
    },

    [CPRMAN_PLLC_CHANNEL_CORE2] = {
        .name = "pllc-core2",
        FILL_PLL_CHANNEL_INIT_INFO(PLLC, CORE2),
    },
    [CPRMAN_PLLC_CHANNEL_CORE1] = {
        .name = "pllc-core1",
        FILL_PLL_CHANNEL_INIT_INFO(PLLC, CORE1),
    },
    [CPRMAN_PLLC_CHANNEL_PER] = {
        .name = "pllc-per",
        FILL_PLL_CHANNEL_INIT_INFO(PLLC, PER),
    },
    [CPRMAN_PLLC_CHANNEL_CORE0] = {
        .name = "pllc-core0",
        FILL_PLL_CHANNEL_INIT_INFO(PLLC, CORE0),
    },

    [CPRMAN_PLLD_CHANNEL_DSI0] = {
        .name = "plld-dsi0",
        FILL_PLL_CHANNEL_INIT_INFO(PLLD, DSI0),
    },
    [CPRMAN_PLLD_CHANNEL_CORE] = {
        .name = "plld-core",
        FILL_PLL_CHANNEL_INIT_INFO(PLLD, CORE),
    },
    [CPRMAN_PLLD_CHANNEL_PER] = {
        .name = "plld-per",
        FILL_PLL_CHANNEL_INIT_INFO(PLLD, PER),
    },
    [CPRMAN_PLLD_CHANNEL_DSI1] = {
        .name = "plld-dsi1",
        FILL_PLL_CHANNEL_INIT_INFO(PLLD, DSI1),
    },

    [CPRMAN_PLLH_CHANNEL_AUX] = {
        .name = "pllh-aux",
        .fixed_divider = 1,
        FILL_PLL_CHANNEL_INIT_INFO_nohold(PLLH, AUX),
    },
    [CPRMAN_PLLH_CHANNEL_RCAL] = {
        .name = "pllh-rcal",
        .fixed_divider = 10,
        FILL_PLL_CHANNEL_INIT_INFO_nohold(PLLH, RCAL),
    },
    [CPRMAN_PLLH_CHANNEL_PIX] = {
        .name = "pllh-pix",
        .fixed_divider = 10,
        FILL_PLL_CHANNEL_INIT_INFO_nohold(PLLH, PIX),
    },

    [CPRMAN_PLLB_CHANNEL_ARM] = {
        .name = "pllb-arm",
        FILL_PLL_CHANNEL_INIT_INFO(PLLB, ARM),
    },
};

#undef FILL_PLL_CHANNEL_INIT_INFO_nohold
#undef FILL_PLL_CHANNEL_INIT_INFO
#undef FILL_PLL_CHANNEL_INIT_INFO_common

static inline void set_pll_channel_init_info(BCM2835CprmanState *s,
                                             CprmanPllChannelState *channel,
                                             CprmanPllChannel id)
{
    channel->id = id;
    channel->parent = PLL_CHANNEL_INIT_INFO[id].parent;
    channel->reg_cm = &s->regs[PLL_CHANNEL_INIT_INFO[id].cm_offset];
    channel->hold_mask = PLL_CHANNEL_INIT_INFO[id].cm_hold_mask;
    channel->load_mask = PLL_CHANNEL_INIT_INFO[id].cm_load_mask;
    channel->reg_a2w_ctrl = &s->regs[PLL_CHANNEL_INIT_INFO[id].a2w_ctrl_offset];
    channel->fixed_divider = PLL_CHANNEL_INIT_INFO[id].fixed_divider;
}

#endif

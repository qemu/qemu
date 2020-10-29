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
#define TYPE_CPRMAN_CLOCK_MUX "bcm2835-cprman-clock-mux"
#define TYPE_CPRMAN_DSI0HSCK_MUX "bcm2835-cprman-dsi0hsck-mux"

DECLARE_INSTANCE_CHECKER(CprmanPllState, CPRMAN_PLL,
                         TYPE_CPRMAN_PLL)
DECLARE_INSTANCE_CHECKER(CprmanPllChannelState, CPRMAN_PLL_CHANNEL,
                         TYPE_CPRMAN_PLL_CHANNEL)
DECLARE_INSTANCE_CHECKER(CprmanClockMuxState, CPRMAN_CLOCK_MUX,
                         TYPE_CPRMAN_CLOCK_MUX)
DECLARE_INSTANCE_CHECKER(CprmanDsi0HsckMuxState, CPRMAN_DSI0HSCK_MUX,
                         TYPE_CPRMAN_DSI0HSCK_MUX)

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

/* Clock muxes */
REG32(CM_GNRICCTL, 0x000)
    FIELD(CM_CLOCKx_CTL, SRC, 0, 4)
    FIELD(CM_CLOCKx_CTL, ENABLE, 4, 1)
    FIELD(CM_CLOCKx_CTL, KILL, 5, 1)
    FIELD(CM_CLOCKx_CTL, GATE, 6, 1)
    FIELD(CM_CLOCKx_CTL, BUSY, 7, 1)
    FIELD(CM_CLOCKx_CTL, BUSYD, 8, 1)
    FIELD(CM_CLOCKx_CTL, MASH, 9, 2)
    FIELD(CM_CLOCKx_CTL, FLIP, 11, 1)
REG32(CM_GNRICDIV, 0x004)
    FIELD(CM_CLOCKx_DIV, FRAC, 0, 12)
REG32(CM_VPUCTL, 0x008)
REG32(CM_VPUDIV, 0x00c)
REG32(CM_SYSCTL, 0x010)
REG32(CM_SYSDIV, 0x014)
REG32(CM_PERIACTL, 0x018)
REG32(CM_PERIADIV, 0x01c)
REG32(CM_PERIICTL, 0x020)
REG32(CM_PERIIDIV, 0x024)
REG32(CM_H264CTL, 0x028)
REG32(CM_H264DIV, 0x02c)
REG32(CM_ISPCTL, 0x030)
REG32(CM_ISPDIV, 0x034)
REG32(CM_V3DCTL, 0x038)
REG32(CM_V3DDIV, 0x03c)
REG32(CM_CAM0CTL, 0x040)
REG32(CM_CAM0DIV, 0x044)
REG32(CM_CAM1CTL, 0x048)
REG32(CM_CAM1DIV, 0x04c)
REG32(CM_CCP2CTL, 0x050)
REG32(CM_CCP2DIV, 0x054)
REG32(CM_DSI0ECTL, 0x058)
REG32(CM_DSI0EDIV, 0x05c)
REG32(CM_DSI0PCTL, 0x060)
REG32(CM_DSI0PDIV, 0x064)
REG32(CM_DPICTL, 0x068)
REG32(CM_DPIDIV, 0x06c)
REG32(CM_GP0CTL, 0x070)
REG32(CM_GP0DIV, 0x074)
REG32(CM_GP1CTL, 0x078)
REG32(CM_GP1DIV, 0x07c)
REG32(CM_GP2CTL, 0x080)
REG32(CM_GP2DIV, 0x084)
REG32(CM_HSMCTL, 0x088)
REG32(CM_HSMDIV, 0x08c)
REG32(CM_OTPCTL, 0x090)
REG32(CM_OTPDIV, 0x094)
REG32(CM_PCMCTL, 0x098)
REG32(CM_PCMDIV, 0x09c)
REG32(CM_PWMCTL, 0x0a0)
REG32(CM_PWMDIV, 0x0a4)
REG32(CM_SLIMCTL, 0x0a8)
REG32(CM_SLIMDIV, 0x0ac)
REG32(CM_SMICTL, 0x0b0)
REG32(CM_SMIDIV, 0x0b4)
REG32(CM_TCNTCTL, 0x0c0)
REG32(CM_TCNTCNT, 0x0c4)
REG32(CM_TECCTL, 0x0c8)
REG32(CM_TECDIV, 0x0cc)
REG32(CM_TD0CTL, 0x0d0)
REG32(CM_TD0DIV, 0x0d4)
REG32(CM_TD1CTL, 0x0d8)
REG32(CM_TD1DIV, 0x0dc)
REG32(CM_TSENSCTL, 0x0e0)
REG32(CM_TSENSDIV, 0x0e4)
REG32(CM_TIMERCTL, 0x0e8)
REG32(CM_TIMERDIV, 0x0ec)
REG32(CM_UARTCTL, 0x0f0)
REG32(CM_UARTDIV, 0x0f4)
REG32(CM_VECCTL, 0x0f8)
REG32(CM_VECDIV, 0x0fc)
REG32(CM_PULSECTL, 0x190)
REG32(CM_PULSEDIV, 0x194)
REG32(CM_SDCCTL, 0x1a8)
REG32(CM_SDCDIV, 0x1ac)
REG32(CM_ARMCTL, 0x1b0)
REG32(CM_AVEOCTL, 0x1b8)
REG32(CM_AVEODIV, 0x1bc)
REG32(CM_EMMCCTL, 0x1c0)
REG32(CM_EMMCDIV, 0x1c4)
REG32(CM_EMMC2CTL, 0x1d0)
REG32(CM_EMMC2DIV, 0x1d4)

/* misc registers */
REG32(CM_LOCK, 0x114)
    FIELD(CM_LOCK, FLOCKH, 12, 1)
    FIELD(CM_LOCK, FLOCKD, 11, 1)
    FIELD(CM_LOCK, FLOCKC, 10, 1)
    FIELD(CM_LOCK, FLOCKB, 9, 1)
    FIELD(CM_LOCK, FLOCKA, 8, 1)

REG32(CM_DSI0HSCK, 0x120)
    FIELD(CM_DSI0HSCK, SELPLLD, 0, 1)

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

/* Clock mux init info */
typedef struct ClockMuxInitInfo {
    const char *name;
    size_t cm_offset; /* cm_offset[0]->CM_CTL, cm_offset[1]->CM_DIV */
    int int_bits;
    int frac_bits;

    CprmanPllChannel src_mapping[CPRMAN_NUM_CLOCK_MUX_SRC];
} ClockMuxInitInfo;

/*
 * Each clock mux can have up to 10 sources. Sources 0 to 3 are always the
 * same (ground, xosc, td0, td1). Sources 4 to 9 are mux specific, and are not
 * always populated. The following macros catch all those cases.
 */

/* Unknown mapping. Connect everything to ground */
#define SRC_MAPPING_INFO_unknown                          \
    .src_mapping = {                                      \
        CPRMAN_CLOCK_SRC_FORCE_GROUND, /* gnd */          \
        CPRMAN_CLOCK_SRC_FORCE_GROUND, /* xosc */         \
        CPRMAN_CLOCK_SRC_FORCE_GROUND, /* test debug 0 */ \
        CPRMAN_CLOCK_SRC_FORCE_GROUND, /* test debug 1 */ \
        CPRMAN_CLOCK_SRC_FORCE_GROUND, /* pll a */        \
        CPRMAN_CLOCK_SRC_FORCE_GROUND, /* pll c */        \
        CPRMAN_CLOCK_SRC_FORCE_GROUND, /* pll d */        \
        CPRMAN_CLOCK_SRC_FORCE_GROUND, /* pll h */        \
        CPRMAN_CLOCK_SRC_FORCE_GROUND, /* pll c, core1 */ \
        CPRMAN_CLOCK_SRC_FORCE_GROUND, /* pll c, core2 */ \
    }

/* Only the oscillator and the two test debug clocks */
#define SRC_MAPPING_INFO_xosc          \
    .src_mapping = {                   \
        CPRMAN_CLOCK_SRC_NORMAL,       \
        CPRMAN_CLOCK_SRC_NORMAL,       \
        CPRMAN_CLOCK_SRC_NORMAL,       \
        CPRMAN_CLOCK_SRC_NORMAL,       \
        CPRMAN_CLOCK_SRC_FORCE_GROUND, \
        CPRMAN_CLOCK_SRC_FORCE_GROUND, \
        CPRMAN_CLOCK_SRC_FORCE_GROUND, \
        CPRMAN_CLOCK_SRC_FORCE_GROUND, \
        CPRMAN_CLOCK_SRC_FORCE_GROUND, \
        CPRMAN_CLOCK_SRC_FORCE_GROUND, \
    }

/* All the PLL "core" channels */
#define SRC_MAPPING_INFO_core      \
    .src_mapping = {               \
        CPRMAN_CLOCK_SRC_NORMAL,   \
        CPRMAN_CLOCK_SRC_NORMAL,   \
        CPRMAN_CLOCK_SRC_NORMAL,   \
        CPRMAN_CLOCK_SRC_NORMAL,   \
        CPRMAN_PLLA_CHANNEL_CORE,  \
        CPRMAN_PLLC_CHANNEL_CORE0, \
        CPRMAN_PLLD_CHANNEL_CORE,  \
        CPRMAN_PLLH_CHANNEL_AUX,   \
        CPRMAN_PLLC_CHANNEL_CORE1, \
        CPRMAN_PLLC_CHANNEL_CORE2, \
    }

/* All the PLL "per" channels */
#define SRC_MAPPING_INFO_periph        \
    .src_mapping = {                   \
        CPRMAN_CLOCK_SRC_NORMAL,       \
        CPRMAN_CLOCK_SRC_NORMAL,       \
        CPRMAN_CLOCK_SRC_NORMAL,       \
        CPRMAN_CLOCK_SRC_NORMAL,       \
        CPRMAN_PLLA_CHANNEL_PER,       \
        CPRMAN_PLLC_CHANNEL_PER,       \
        CPRMAN_PLLD_CHANNEL_PER,       \
        CPRMAN_CLOCK_SRC_FORCE_GROUND, \
        CPRMAN_CLOCK_SRC_FORCE_GROUND, \
        CPRMAN_CLOCK_SRC_FORCE_GROUND, \
    }

/*
 * The DSI0 channels. This one got an intermediate mux between the PLL channels
 * and the clock input.
 */
#define SRC_MAPPING_INFO_dsi0          \
    .src_mapping = {                   \
        CPRMAN_CLOCK_SRC_NORMAL,       \
        CPRMAN_CLOCK_SRC_NORMAL,       \
        CPRMAN_CLOCK_SRC_NORMAL,       \
        CPRMAN_CLOCK_SRC_NORMAL,       \
        CPRMAN_CLOCK_SRC_DSI0HSCK,     \
        CPRMAN_CLOCK_SRC_FORCE_GROUND, \
        CPRMAN_CLOCK_SRC_FORCE_GROUND, \
        CPRMAN_CLOCK_SRC_FORCE_GROUND, \
        CPRMAN_CLOCK_SRC_FORCE_GROUND, \
        CPRMAN_CLOCK_SRC_FORCE_GROUND, \
    }

/* The DSI1 channel */
#define SRC_MAPPING_INFO_dsi1          \
    .src_mapping = {                   \
        CPRMAN_CLOCK_SRC_NORMAL,       \
        CPRMAN_CLOCK_SRC_NORMAL,       \
        CPRMAN_CLOCK_SRC_NORMAL,       \
        CPRMAN_CLOCK_SRC_NORMAL,       \
        CPRMAN_PLLD_CHANNEL_DSI1,      \
        CPRMAN_CLOCK_SRC_FORCE_GROUND, \
        CPRMAN_CLOCK_SRC_FORCE_GROUND, \
        CPRMAN_CLOCK_SRC_FORCE_GROUND, \
        CPRMAN_CLOCK_SRC_FORCE_GROUND, \
        CPRMAN_CLOCK_SRC_FORCE_GROUND, \
    }

#define FILL_CLOCK_MUX_SRC_MAPPING_INIT_INFO(kind_) \
    SRC_MAPPING_INFO_ ## kind_

#define FILL_CLOCK_MUX_INIT_INFO(clock_, kind_) \
    .cm_offset = R_CM_ ## clock_ ## CTL,        \
    FILL_CLOCK_MUX_SRC_MAPPING_INIT_INFO(kind_)

static ClockMuxInitInfo CLOCK_MUX_INIT_INFO[] = {
    [CPRMAN_CLOCK_GNRIC] = {
        .name = "gnric",
        FILL_CLOCK_MUX_INIT_INFO(GNRIC, unknown),
    },
    [CPRMAN_CLOCK_VPU] = {
        .name = "vpu",
        .int_bits = 12,
        .frac_bits = 8,
        FILL_CLOCK_MUX_INIT_INFO(VPU, core),
    },
    [CPRMAN_CLOCK_SYS] = {
        .name = "sys",
        FILL_CLOCK_MUX_INIT_INFO(SYS, unknown),
    },
    [CPRMAN_CLOCK_PERIA] = {
        .name = "peria",
        FILL_CLOCK_MUX_INIT_INFO(PERIA, unknown),
    },
    [CPRMAN_CLOCK_PERII] = {
        .name = "perii",
        FILL_CLOCK_MUX_INIT_INFO(PERII, unknown),
    },
    [CPRMAN_CLOCK_H264] = {
        .name = "h264",
        .int_bits = 4,
        .frac_bits = 8,
        FILL_CLOCK_MUX_INIT_INFO(H264, core),
    },
    [CPRMAN_CLOCK_ISP] = {
        .name = "isp",
        .int_bits = 4,
        .frac_bits = 8,
        FILL_CLOCK_MUX_INIT_INFO(ISP, core),
    },
    [CPRMAN_CLOCK_V3D] = {
        .name = "v3d",
        FILL_CLOCK_MUX_INIT_INFO(V3D, core),
    },
    [CPRMAN_CLOCK_CAM0] = {
        .name = "cam0",
        .int_bits = 4,
        .frac_bits = 8,
        FILL_CLOCK_MUX_INIT_INFO(CAM0, periph),
    },
    [CPRMAN_CLOCK_CAM1] = {
        .name = "cam1",
        .int_bits = 4,
        .frac_bits = 8,
        FILL_CLOCK_MUX_INIT_INFO(CAM1, periph),
    },
    [CPRMAN_CLOCK_CCP2] = {
        .name = "ccp2",
        FILL_CLOCK_MUX_INIT_INFO(CCP2, unknown),
    },
    [CPRMAN_CLOCK_DSI0E] = {
        .name = "dsi0e",
        .int_bits = 4,
        .frac_bits = 8,
        FILL_CLOCK_MUX_INIT_INFO(DSI0E, dsi0),
    },
    [CPRMAN_CLOCK_DSI0P] = {
        .name = "dsi0p",
        .int_bits = 0,
        .frac_bits = 0,
        FILL_CLOCK_MUX_INIT_INFO(DSI0P, dsi0),
    },
    [CPRMAN_CLOCK_DPI] = {
        .name = "dpi",
        .int_bits = 4,
        .frac_bits = 8,
        FILL_CLOCK_MUX_INIT_INFO(DPI, periph),
    },
    [CPRMAN_CLOCK_GP0] = {
        .name = "gp0",
        .int_bits = 12,
        .frac_bits = 12,
        FILL_CLOCK_MUX_INIT_INFO(GP0, periph),
    },
    [CPRMAN_CLOCK_GP1] = {
        .name = "gp1",
        .int_bits = 12,
        .frac_bits = 12,
        FILL_CLOCK_MUX_INIT_INFO(GP1, periph),
    },
    [CPRMAN_CLOCK_GP2] = {
        .name = "gp2",
        .int_bits = 12,
        .frac_bits = 12,
        FILL_CLOCK_MUX_INIT_INFO(GP2, periph),
    },
    [CPRMAN_CLOCK_HSM] = {
        .name = "hsm",
        .int_bits = 4,
        .frac_bits = 8,
        FILL_CLOCK_MUX_INIT_INFO(HSM, periph),
    },
    [CPRMAN_CLOCK_OTP] = {
        .name = "otp",
        .int_bits = 4,
        .frac_bits = 0,
        FILL_CLOCK_MUX_INIT_INFO(OTP, xosc),
    },
    [CPRMAN_CLOCK_PCM] = {
        .name = "pcm",
        .int_bits = 12,
        .frac_bits = 12,
        FILL_CLOCK_MUX_INIT_INFO(PCM, periph),
    },
    [CPRMAN_CLOCK_PWM] = {
        .name = "pwm",
        .int_bits = 12,
        .frac_bits = 12,
        FILL_CLOCK_MUX_INIT_INFO(PWM, periph),
    },
    [CPRMAN_CLOCK_SLIM] = {
        .name = "slim",
        .int_bits = 12,
        .frac_bits = 12,
        FILL_CLOCK_MUX_INIT_INFO(SLIM, periph),
    },
    [CPRMAN_CLOCK_SMI] = {
        .name = "smi",
        .int_bits = 4,
        .frac_bits = 8,
        FILL_CLOCK_MUX_INIT_INFO(SMI, periph),
    },
    [CPRMAN_CLOCK_TEC] = {
        .name = "tec",
        .int_bits = 6,
        .frac_bits = 0,
        FILL_CLOCK_MUX_INIT_INFO(TEC, xosc),
    },
    [CPRMAN_CLOCK_TD0] = {
        .name = "td0",
        FILL_CLOCK_MUX_INIT_INFO(TD0, unknown),
    },
    [CPRMAN_CLOCK_TD1] = {
        .name = "td1",
        FILL_CLOCK_MUX_INIT_INFO(TD1, unknown),
    },
    [CPRMAN_CLOCK_TSENS] = {
        .name = "tsens",
        .int_bits = 5,
        .frac_bits = 0,
        FILL_CLOCK_MUX_INIT_INFO(TSENS, xosc),
    },
    [CPRMAN_CLOCK_TIMER] = {
        .name = "timer",
        .int_bits = 6,
        .frac_bits = 12,
        FILL_CLOCK_MUX_INIT_INFO(TIMER, xosc),
    },
    [CPRMAN_CLOCK_UART] = {
        .name = "uart",
        .int_bits = 10,
        .frac_bits = 12,
        FILL_CLOCK_MUX_INIT_INFO(UART, periph),
    },
    [CPRMAN_CLOCK_VEC] = {
        .name = "vec",
        .int_bits = 4,
        .frac_bits = 0,
        FILL_CLOCK_MUX_INIT_INFO(VEC, periph),
    },
    [CPRMAN_CLOCK_PULSE] = {
        .name = "pulse",
        FILL_CLOCK_MUX_INIT_INFO(PULSE, xosc),
    },
    [CPRMAN_CLOCK_SDC] = {
        .name = "sdram",
        .int_bits = 6,
        .frac_bits = 0,
        FILL_CLOCK_MUX_INIT_INFO(SDC, core),
    },
    [CPRMAN_CLOCK_ARM] = {
        .name = "arm",
        FILL_CLOCK_MUX_INIT_INFO(ARM, unknown),
    },
    [CPRMAN_CLOCK_AVEO] = {
        .name = "aveo",
        .int_bits = 4,
        .frac_bits = 0,
        FILL_CLOCK_MUX_INIT_INFO(AVEO, periph),
    },
    [CPRMAN_CLOCK_EMMC] = {
        .name = "emmc",
        .int_bits = 4,
        .frac_bits = 8,
        FILL_CLOCK_MUX_INIT_INFO(EMMC, periph),
    },
    [CPRMAN_CLOCK_EMMC2] = {
        .name = "emmc2",
        .int_bits = 4,
        .frac_bits = 8,
        FILL_CLOCK_MUX_INIT_INFO(EMMC2, unknown),
    },
};

#undef FILL_CLOCK_MUX_INIT_INFO
#undef FILL_CLOCK_MUX_SRC_MAPPING_INIT_INFO
#undef SRC_MAPPING_INFO_dsi1
#undef SRC_MAPPING_INFO_dsi0
#undef SRC_MAPPING_INFO_periph
#undef SRC_MAPPING_INFO_core
#undef SRC_MAPPING_INFO_xosc
#undef SRC_MAPPING_INFO_unknown

static inline void set_clock_mux_init_info(BCM2835CprmanState *s,
                                           CprmanClockMuxState *mux,
                                           CprmanClockMux id)
{
    mux->id = id;
    mux->reg_ctl = &s->regs[CLOCK_MUX_INIT_INFO[id].cm_offset];
    mux->reg_div = &s->regs[CLOCK_MUX_INIT_INFO[id].cm_offset + 1];
    mux->int_bits = CLOCK_MUX_INIT_INFO[id].int_bits;
    mux->frac_bits = CLOCK_MUX_INIT_INFO[id].frac_bits;
}


/*
 * Object reset info
 * Those values have been dumped from a Raspberry Pi 3 Model B v1.2 using the
 * clk debugfs interface in Linux.
 */
typedef struct PLLResetInfo {
    uint32_t cm;
    uint32_t a2w_ctrl;
    uint32_t a2w_ana[4];
    uint32_t a2w_frac;
} PLLResetInfo;

static const PLLResetInfo PLL_RESET_INFO[] = {
    [CPRMAN_PLLA] = {
        .cm = 0x0000008a,
        .a2w_ctrl = 0x0002103a,
        .a2w_frac = 0x00098000,
        .a2w_ana = { 0x00000000, 0x00144000, 0x00000000, 0x00000100 }
    },

    [CPRMAN_PLLC] = {
        .cm = 0x00000228,
        .a2w_ctrl = 0x0002103e,
        .a2w_frac = 0x00080000,
        .a2w_ana = { 0x00000000, 0x00144000, 0x00000000, 0x00000100 }
    },

    [CPRMAN_PLLD] = {
        .cm = 0x0000020a,
        .a2w_ctrl = 0x00021034,
        .a2w_frac = 0x00015556,
        .a2w_ana = { 0x00000000, 0x00144000, 0x00000000, 0x00000100 }
    },

    [CPRMAN_PLLH] = {
        .cm = 0x00000000,
        .a2w_ctrl = 0x0002102d,
        .a2w_frac = 0x00000000,
        .a2w_ana = { 0x00900000, 0x0000000c, 0x00000000, 0x00000000 }
    },

    [CPRMAN_PLLB] = {
        /* unknown */
        .cm = 0x00000000,
        .a2w_ctrl = 0x00000000,
        .a2w_frac = 0x00000000,
        .a2w_ana = { 0x00000000, 0x00000000, 0x00000000, 0x00000000 }
    }
};

typedef struct PLLChannelResetInfo {
    /*
     * Even though a PLL channel has a CM register, it shares it with its
     * parent PLL. The parent already takes care of the reset value.
     */
    uint32_t a2w_ctrl;
} PLLChannelResetInfo;

static const PLLChannelResetInfo PLL_CHANNEL_RESET_INFO[] = {
    [CPRMAN_PLLA_CHANNEL_DSI0] = { .a2w_ctrl = 0x00000100 },
    [CPRMAN_PLLA_CHANNEL_CORE] = { .a2w_ctrl = 0x00000003 },
    [CPRMAN_PLLA_CHANNEL_PER] = { .a2w_ctrl = 0x00000000 }, /* unknown */
    [CPRMAN_PLLA_CHANNEL_CCP2] = { .a2w_ctrl = 0x00000100 },

    [CPRMAN_PLLC_CHANNEL_CORE2] = { .a2w_ctrl = 0x00000100 },
    [CPRMAN_PLLC_CHANNEL_CORE1] = { .a2w_ctrl = 0x00000100 },
    [CPRMAN_PLLC_CHANNEL_PER] = { .a2w_ctrl = 0x00000002 },
    [CPRMAN_PLLC_CHANNEL_CORE0] = { .a2w_ctrl = 0x00000002 },

    [CPRMAN_PLLD_CHANNEL_DSI0] = { .a2w_ctrl = 0x00000100 },
    [CPRMAN_PLLD_CHANNEL_CORE] = { .a2w_ctrl = 0x00000004 },
    [CPRMAN_PLLD_CHANNEL_PER] = { .a2w_ctrl = 0x00000004 },
    [CPRMAN_PLLD_CHANNEL_DSI1] = { .a2w_ctrl = 0x00000100 },

    [CPRMAN_PLLH_CHANNEL_AUX] = { .a2w_ctrl = 0x00000004 },
    [CPRMAN_PLLH_CHANNEL_RCAL] = { .a2w_ctrl = 0x00000000 },
    [CPRMAN_PLLH_CHANNEL_PIX] = { .a2w_ctrl = 0x00000000 },

    [CPRMAN_PLLB_CHANNEL_ARM] = { .a2w_ctrl = 0x00000000 }, /* unknown */
};

typedef struct ClockMuxResetInfo {
    uint32_t cm_ctl;
    uint32_t cm_div;
} ClockMuxResetInfo;

static const ClockMuxResetInfo CLOCK_MUX_RESET_INFO[] = {
    [CPRMAN_CLOCK_GNRIC] = {
        .cm_ctl = 0, /* unknown */
        .cm_div = 0
    },

    [CPRMAN_CLOCK_VPU] = {
        .cm_ctl = 0x00000245,
        .cm_div = 0x00003000,
    },

    [CPRMAN_CLOCK_SYS] = {
        .cm_ctl = 0, /* unknown */
        .cm_div = 0
    },

    [CPRMAN_CLOCK_PERIA] = {
        .cm_ctl = 0, /* unknown */
        .cm_div = 0
    },

    [CPRMAN_CLOCK_PERII] = {
        .cm_ctl = 0, /* unknown */
        .cm_div = 0
    },

    [CPRMAN_CLOCK_H264] = {
        .cm_ctl = 0x00000244,
        .cm_div = 0x00003000,
    },

    [CPRMAN_CLOCK_ISP] = {
        .cm_ctl = 0x00000244,
        .cm_div = 0x00003000,
    },

    [CPRMAN_CLOCK_V3D] = {
        .cm_ctl = 0, /* unknown */
        .cm_div = 0
    },

    [CPRMAN_CLOCK_CAM0] = {
        .cm_ctl = 0x00000000,
        .cm_div = 0x00000000,
    },

    [CPRMAN_CLOCK_CAM1] = {
        .cm_ctl = 0x00000000,
        .cm_div = 0x00000000,
    },

    [CPRMAN_CLOCK_CCP2] = {
        .cm_ctl = 0, /* unknown */
        .cm_div = 0
    },

    [CPRMAN_CLOCK_DSI0E] = {
        .cm_ctl = 0x00000000,
        .cm_div = 0x00000000,
    },

    [CPRMAN_CLOCK_DSI0P] = {
        .cm_ctl = 0x00000000,
        .cm_div = 0x00000000,
    },

    [CPRMAN_CLOCK_DPI] = {
        .cm_ctl = 0x00000000,
        .cm_div = 0x00000000,
    },

    [CPRMAN_CLOCK_GP0] = {
        .cm_ctl = 0x00000200,
        .cm_div = 0x00000000,
    },

    [CPRMAN_CLOCK_GP1] = {
        .cm_ctl = 0x00000096,
        .cm_div = 0x00014000,
    },

    [CPRMAN_CLOCK_GP2] = {
        .cm_ctl = 0x00000291,
        .cm_div = 0x00249f00,
    },

    [CPRMAN_CLOCK_HSM] = {
        .cm_ctl = 0x00000000,
        .cm_div = 0x00000000,
    },

    [CPRMAN_CLOCK_OTP] = {
        .cm_ctl = 0x00000091,
        .cm_div = 0x00004000,
    },

    [CPRMAN_CLOCK_PCM] = {
        .cm_ctl = 0x00000200,
        .cm_div = 0x00000000,
    },

    [CPRMAN_CLOCK_PWM] = {
        .cm_ctl = 0x00000200,
        .cm_div = 0x00000000,
    },

    [CPRMAN_CLOCK_SLIM] = {
        .cm_ctl = 0x00000200,
        .cm_div = 0x00000000,
    },

    [CPRMAN_CLOCK_SMI] = {
        .cm_ctl = 0x00000000,
        .cm_div = 0x00000000,
    },

    [CPRMAN_CLOCK_TEC] = {
        .cm_ctl = 0x00000000,
        .cm_div = 0x00000000,
    },

    [CPRMAN_CLOCK_TD0] = {
        .cm_ctl = 0, /* unknown */
        .cm_div = 0
    },

    [CPRMAN_CLOCK_TD1] = {
        .cm_ctl = 0, /* unknown */
        .cm_div = 0
    },

    [CPRMAN_CLOCK_TSENS] = {
        .cm_ctl = 0x00000091,
        .cm_div = 0x0000a000,
    },

    [CPRMAN_CLOCK_TIMER] = {
        .cm_ctl = 0x00000291,
        .cm_div = 0x00013333,
    },

    [CPRMAN_CLOCK_UART] = {
        .cm_ctl = 0x00000296,
        .cm_div = 0x0000a6ab,
    },

    [CPRMAN_CLOCK_VEC] = {
        .cm_ctl = 0x00000097,
        .cm_div = 0x00002000,
    },

    [CPRMAN_CLOCK_PULSE] = {
        .cm_ctl = 0, /* unknown */
        .cm_div = 0
    },

    [CPRMAN_CLOCK_SDC] = {
        .cm_ctl = 0x00004006,
        .cm_div = 0x00003000,
    },

    [CPRMAN_CLOCK_ARM] = {
        .cm_ctl = 0, /* unknown */
        .cm_div = 0
    },

    [CPRMAN_CLOCK_AVEO] = {
        .cm_ctl = 0x00000000,
        .cm_div = 0x00000000,
    },

    [CPRMAN_CLOCK_EMMC] = {
        .cm_ctl = 0x00000295,
        .cm_div = 0x00006000,
    },

    [CPRMAN_CLOCK_EMMC2] = {
        .cm_ctl = 0, /* unknown */
        .cm_div = 0
    },
};

#endif

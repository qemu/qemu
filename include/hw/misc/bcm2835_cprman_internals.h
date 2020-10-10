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

DECLARE_INSTANCE_CHECKER(CprmanPllState, CPRMAN_PLL,
                         TYPE_CPRMAN_PLL)

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

#endif

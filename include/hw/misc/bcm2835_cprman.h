/*
 * BCM2835 CPRMAN clock manager
 *
 * Copyright (c) 2020 Luc Michel <luc@lmichel.fr>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_CPRMAN_H
#define HW_MISC_CPRMAN_H

#include "hw/sysbus.h"
#include "hw/qdev-clock.h"

#define TYPE_BCM2835_CPRMAN "bcm2835-cprman"

typedef struct BCM2835CprmanState BCM2835CprmanState;

DECLARE_INSTANCE_CHECKER(BCM2835CprmanState, CPRMAN,
                         TYPE_BCM2835_CPRMAN)

#define CPRMAN_NUM_REGS (0x2000 / sizeof(uint32_t))

typedef enum CprmanPll {
    CPRMAN_PLLA = 0,
    CPRMAN_PLLC,
    CPRMAN_PLLD,
    CPRMAN_PLLH,
    CPRMAN_PLLB,

    CPRMAN_NUM_PLL
} CprmanPll;

typedef struct CprmanPllState {
    /*< private >*/
    DeviceState parent_obj;

    /*< public >*/
    CprmanPll id;

    uint32_t *reg_cm;
    uint32_t *reg_a2w_ctrl;
    uint32_t *reg_a2w_ana; /* ANA[0] .. ANA[3] */
    uint32_t prediv_mask; /* prediv bit in ana[1] */
    uint32_t *reg_a2w_frac;

    Clock *xosc_in;
    Clock *out;
} CprmanPllState;

struct BCM2835CprmanState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;

    CprmanPllState plls[CPRMAN_NUM_PLL];

    uint32_t regs[CPRMAN_NUM_REGS];
    uint32_t xosc_freq;

    Clock *xosc;
};

#endif

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

struct BCM2835CprmanState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;

    uint32_t regs[CPRMAN_NUM_REGS];
    uint32_t xosc_freq;

    Clock *xosc;
};

#endif

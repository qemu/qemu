/*
 * BCM2838 Random Number Generator emulation
 *
 * Copyright (C) 2022 Sergey Pushkarev <sergey.pushkarev@auriga.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef BCM2838_RNG200_H
#define BCM2838_RNG200_H

#include "qom/object.h"
#include "qemu/fifo8.h"
#include "system/rng.h"
#include "hw/core/sysbus.h"
#include "hw/core/ptimer.h"
#include "hw/core/qdev-clock.h"
#include "hw/core/irq.h"

#define TYPE_BCM2838_RNG200 "bcm2838-rng200"
OBJECT_DECLARE_SIMPLE_TYPE(BCM2838Rng200State, BCM2838_RNG200)

#define N_BCM2838_RNG200_REGS 10

struct BCM2838Rng200State {
    SysBusDevice busdev;
    MemoryRegion iomem;

    ptimer_state *ptimer;
    RngBackend *rng;
    Clock *clock;

    uint32_t rng_fifo_cap;
    Fifo8    fifo;

    qemu_irq irq;

    uint32_t regs[N_BCM2838_RNG200_REGS];
};

#endif /* BCM2838_RNG200_H */

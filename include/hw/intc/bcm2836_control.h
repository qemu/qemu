/*
 * Raspberry Pi emulation (c) 2012 Gregory Estrade
 * Upstreaming code cleanup [including bcm2835_*] (c) 2013 Jan Petrous
 *
 * Rasperry Pi 2 emulation and refactoring Copyright (c) 2015, Microsoft
 * Written by Andrew Baumann
 *
 * This code is licensed under the GNU GPLv2 and later.
 */

#ifndef BCM2836_CONTROL_H
#define BCM2836_CONTROL_H

#include "hw/sysbus.h"

/* 4 mailboxes per core, for 16 total */
#define BCM2836_NCORES 4
#define BCM2836_MBPERCORE 4

#define TYPE_BCM2836_CONTROL "bcm2836-control"
#define BCM2836_CONTROL(obj) \
    OBJECT_CHECK(BCM2836ControlState, (obj), TYPE_BCM2836_CONTROL)

typedef struct BCM2836ControlState {
    /*< private >*/
    SysBusDevice busdev;
    /*< public >*/
    MemoryRegion iomem;

    /* mailbox state */
    uint32_t mailboxes[BCM2836_NCORES * BCM2836_MBPERCORE];

    /* interrupt routing/control registers */
    uint8_t route_gpu_irq, route_gpu_fiq;
    uint32_t timercontrol[BCM2836_NCORES];
    uint32_t mailboxcontrol[BCM2836_NCORES];

    /* interrupt status regs (derived from input pins; not visible to user) */
    bool gpu_irq, gpu_fiq;
    uint8_t timerirqs[BCM2836_NCORES];

    /* interrupt source registers, post-routing (also input-derived; visible) */
    uint32_t irqsrc[BCM2836_NCORES];
    uint32_t fiqsrc[BCM2836_NCORES];

    /* outputs to CPU cores */
    qemu_irq irq[BCM2836_NCORES];
    qemu_irq fiq[BCM2836_NCORES];
} BCM2836ControlState;

#endif

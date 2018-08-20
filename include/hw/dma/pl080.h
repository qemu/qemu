/*
 * ARM PrimeCell PL080/PL081 DMA controller
 *
 * Copyright (c) 2006 CodeSourcery.
 * Copyright (c) 2018 Linaro Limited
 * Written by Paul Brook, Peter Maydell
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

/* This is a model of the Arm PrimeCell PL080/PL081 DMA controller:
 * The PL080 TRM is:
 * http://infocenter.arm.com/help/topic/com.arm.doc.ddi0196g/DDI0196.pdf
 * and the PL081 TRM is:
 * http://infocenter.arm.com/help/topic/com.arm.doc.ddi0218e/DDI0218.pdf
 *
 * QEMU interface:
 * + sysbus IRQ 0: DMACINTR combined interrupt line
 * + sysbus IRQ 1: DMACINTERR error interrupt request
 * + sysbus IRQ 2: DMACINTTC count interrupt request
 * + sysbus MMIO region 0: MemoryRegion for the device's registers
 * + QOM property "downstream": MemoryRegion defining where DMA
 *   bus master transactions are made
 */

#ifndef HW_DMA_PL080_H
#define HW_DMA_PL080_H

#include "hw/sysbus.h"

#define PL080_MAX_CHANNELS 8

typedef struct {
    uint32_t src;
    uint32_t dest;
    uint32_t lli;
    uint32_t ctrl;
    uint32_t conf;
} pl080_channel;

#define TYPE_PL080 "pl080"
#define TYPE_PL081 "pl081"
#define PL080(obj) OBJECT_CHECK(PL080State, (obj), TYPE_PL080)

typedef struct PL080State {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    uint8_t tc_int;
    uint8_t tc_mask;
    uint8_t err_int;
    uint8_t err_mask;
    uint32_t conf;
    uint32_t sync;
    uint32_t req_single;
    uint32_t req_burst;
    pl080_channel chan[PL080_MAX_CHANNELS];
    int nchannels;
    /* Flag to avoid recursive DMA invocations.  */
    int running;
    qemu_irq irq;
    qemu_irq interr;
    qemu_irq inttc;

    MemoryRegion *downstream;
    AddressSpace downstream_as;
} PL080State;

#endif

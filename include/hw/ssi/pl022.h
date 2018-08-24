/*
 * ARM PrimeCell PL022 Synchronous Serial Port
 *
 * Copyright (c) 2007 CodeSourcery.
 * Written by Paul Brook
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

/* This is a model of the Arm PrimeCell PL022 synchronous serial port.
 * The PL022 TRM is:
 * http://infocenter.arm.com/help/topic/com.arm.doc.ddi0194h/DDI0194H_ssp_pl022_trm.pdf
 *
 * QEMU interface:
 * + sysbus IRQ: SSPINTR combined interrupt line
 * + sysbus MMIO region 0: MemoryRegion for the device's registers
 */

#ifndef HW_SSI_PL022_H
#define HW_SSI_PL022_H

#include "hw/sysbus.h"

#define TYPE_PL022 "pl022"
#define PL022(obj) OBJECT_CHECK(PL022State, (obj), TYPE_PL022)

typedef struct PL022State {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    uint32_t cr0;
    uint32_t cr1;
    uint32_t bitmask;
    uint32_t sr;
    uint32_t cpsr;
    uint32_t is;
    uint32_t im;
    /* The FIFO head points to the next empty entry.  */
    int tx_fifo_head;
    int rx_fifo_head;
    int tx_fifo_len;
    int rx_fifo_len;
    uint16_t tx_fifo[8];
    uint16_t rx_fifo[8];
    qemu_irq irq;
    SSIBus *ssi;
} PL022State;

#endif

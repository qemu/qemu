/*
 * calypso_inth.h — Calypso INTH (Interrupt Handler) QOM device
 *
 * Two-level interrupt controller with 32 IRQ lines,
 * priority-based arbitration, and IRQ/FIQ routing.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_INTC_CALYPSO_INTH_H
#define HW_INTC_CALYPSO_INTH_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_CALYPSO_INTH "calypso-inth"
OBJECT_DECLARE_SIMPLE_TYPE(CalypsoINTHState, CALYPSO_INTH)

#define CALYPSO_INTH_NUM_IRQS  32

struct CalypsoINTHState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;

    /* Output lines to CPU */
    qemu_irq parent_irq;   /* CPU IRQ line */
    qemu_irq parent_fiq;   /* CPU FIQ line */

    /* Interrupt Level Registers: bits[4:0]=priority, bit[8]=FIQ */
    uint16_t ilr[CALYPSO_INTH_NUM_IRQS];

    uint16_t ith_v;        /* Current highest-priority pending IRQ number */
    uint32_t pending;      /* Bitmask of pending IRQs */
    uint32_t mask;         /* Bitmask: 1 = masked (disabled) */
};

#endif /* HW_INTC_CALYPSO_INTH_H */

/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Goldfish PIC
 *
 * (c) 2020 Laurent Vivier <laurent@vivier.eu>
 *
 */

#ifndef HW_INTC_GOLDFISH_PIC_H
#define HW_INTC_GOLDFISH_PIC_H

#define TYPE_GOLDFISH_PIC "goldfish_pic"
OBJECT_DECLARE_SIMPLE_TYPE(GoldfishPICState, GOLDFISH_PIC)

#define GOLDFISH_PIC_IRQ_NB 32

struct GoldfishPICState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    qemu_irq irq;

    uint32_t pending;
    uint32_t enabled;

    /* statistics */
    uint64_t stats_irq_count[32];
    /* for tracing */
    uint8_t idx;
};

#endif

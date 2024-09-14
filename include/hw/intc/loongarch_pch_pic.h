/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * LoongArch 7A1000 I/O interrupt controller definitions
 *
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 */

#ifndef HW_LOONGARCH_PCH_PIC_H
#define HW_LOONGARCH_PCH_PIC_H

#include "hw/intc/loongarch_pic_common.h"

#define TYPE_LOONGARCH_PCH_PIC "loongarch_pch_pic"
#define PCH_PIC_NAME(name) TYPE_LOONGARCH_PCH_PIC#name
OBJECT_DECLARE_SIMPLE_TYPE(LoongArchPCHPIC, LOONGARCH_PCH_PIC)

struct LoongArchPCHPIC {
    SysBusDevice parent_obj;
    qemu_irq parent_irq[64];
    uint64_t int_mask; /*0x020 interrupt mask register*/
    uint64_t htmsi_en; /*0x040 1=msi*/
    uint64_t intedge; /*0x060 edge=1 level  =0*/
    uint64_t intclr; /*0x080 for clean edge int,set 1 clean,set 0 is noused*/
    uint64_t auto_crtl0; /*0x0c0*/
    uint64_t auto_crtl1; /*0x0e0*/
    uint64_t last_intirr;    /* edge detection */
    uint64_t intirr; /* 0x380 interrupt request register */
    uint64_t intisr; /* 0x3a0 interrupt service register */
    /*
     * 0x3e0 interrupt level polarity selection
     * register 0 for high level trigger
     */
    uint64_t int_polarity;

    uint8_t route_entry[64]; /*0x100 - 0x138*/
    uint8_t htmsi_vector[64]; /*0x200 - 0x238*/

    MemoryRegion iomem32_low;
    MemoryRegion iomem32_high;
    MemoryRegion iomem8;
    unsigned int irq_num;
};
#endif /* HW_LOONGARCH_PCH_PIC_H */

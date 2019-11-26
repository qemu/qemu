/*
 * Model of the Xilinx Versal
 *
 * Copyright (c) 2018 Xilinx Inc.
 * Written by Edgar E. Iglesias
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#ifndef XLNX_VERSAL_H
#define XLNX_VERSAL_H

#include "hw/sysbus.h"
#include "hw/arm/boot.h"
#include "hw/intc/arm_gicv3.h"

#define TYPE_XLNX_VERSAL "xlnx-versal"
#define XLNX_VERSAL(obj) OBJECT_CHECK(Versal, (obj), TYPE_XLNX_VERSAL)

#define XLNX_VERSAL_NR_ACPUS   2
#define XLNX_VERSAL_NR_UARTS   2
#define XLNX_VERSAL_NR_GEMS    2
#define XLNX_VERSAL_NR_IRQS    192

typedef struct Versal {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    struct {
        struct {
            MemoryRegion mr;
            ARMCPU *cpu[XLNX_VERSAL_NR_ACPUS];
            GICv3State gic;
        } apu;
    } fpd;

    MemoryRegion mr_ps;

    struct {
        /* 4 ranges to access DDR.  */
        MemoryRegion mr_ddr_ranges[4];
    } noc;

    struct {
        MemoryRegion mr_ocm;

        struct {
            SysBusDevice *uart[XLNX_VERSAL_NR_UARTS];
            SysBusDevice *gem[XLNX_VERSAL_NR_GEMS];
        } iou;
    } lpd;

    struct {
        MemoryRegion *mr_ddr;
        uint32_t psci_conduit;
    } cfg;
} Versal;

/* Memory-map and IRQ definitions. Copied a subset from
 * auto-generated files.  */

#define VERSAL_GIC_MAINT_IRQ        9
#define VERSAL_TIMER_VIRT_IRQ       11
#define VERSAL_TIMER_S_EL1_IRQ      13
#define VERSAL_TIMER_NS_EL1_IRQ     14
#define VERSAL_TIMER_NS_EL2_IRQ     10

#define VERSAL_UART0_IRQ_0         18
#define VERSAL_UART1_IRQ_0         19
#define VERSAL_GEM0_IRQ_0          56
#define VERSAL_GEM0_WAKE_IRQ_0     57
#define VERSAL_GEM1_IRQ_0          58
#define VERSAL_GEM1_WAKE_IRQ_0     59

/* Architecturally reserved IRQs suitable for virtualization.  */
#define VERSAL_RSVD_IRQ_FIRST 111
#define VERSAL_RSVD_IRQ_LAST  118

#define MM_TOP_RSVD                 0xa0000000U
#define MM_TOP_RSVD_SIZE            0x4000000
#define MM_GIC_APU_DIST_MAIN        0xf9000000U
#define MM_GIC_APU_DIST_MAIN_SIZE   0x10000
#define MM_GIC_APU_REDIST_0         0xf9080000U
#define MM_GIC_APU_REDIST_0_SIZE    0x80000

#define MM_UART0                    0xff000000U
#define MM_UART0_SIZE               0x10000
#define MM_UART1                    0xff010000U
#define MM_UART1_SIZE               0x10000

#define MM_GEM0                     0xff0c0000U
#define MM_GEM0_SIZE                0x10000
#define MM_GEM1                     0xff0d0000U
#define MM_GEM1_SIZE                0x10000

#define MM_OCM                      0xfffc0000U
#define MM_OCM_SIZE                 0x40000

#define MM_TOP_DDR                  0x0
#define MM_TOP_DDR_SIZE             0x80000000U
#define MM_TOP_DDR_2                0x800000000ULL
#define MM_TOP_DDR_2_SIZE           0x800000000ULL
#define MM_TOP_DDR_3                0xc000000000ULL
#define MM_TOP_DDR_3_SIZE           0x4000000000ULL
#define MM_TOP_DDR_4                0x10000000000ULL
#define MM_TOP_DDR_4_SIZE           0xb780000000ULL

#define MM_PSM_START                0xffc80000U
#define MM_PSM_END                  0xffcf0000U

#define MM_CRL                      0xff5e0000U
#define MM_CRL_SIZE                 0x300000
#define MM_IOU_SCNTR                0xff130000U
#define MM_IOU_SCNTR_SIZE           0x10000
#define MM_IOU_SCNTRS               0xff140000U
#define MM_IOU_SCNTRS_SIZE          0x10000
#define MM_FPD_CRF                  0xfd1a0000U
#define MM_FPD_CRF_SIZE             0x140000

#define MM_PMC_CRP                  0xf1260000U
#define MM_PMC_CRP_SIZE             0x10000
#endif

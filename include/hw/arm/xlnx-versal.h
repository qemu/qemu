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
#include "hw/cpu/cluster.h"
#include "hw/or-irq.h"
#include "hw/sd/sdhci.h"
#include "hw/intc/arm_gicv3.h"
#include "hw/char/pl011.h"
#include "hw/dma/xlnx-zdma.h"
#include "hw/net/cadence_gem.h"
#include "hw/rtc/xlnx-zynqmp-rtc.h"
#include "qom/object.h"
#include "hw/usb/xlnx-usb-subsystem.h"
#include "hw/misc/xlnx-versal-xramc.h"
#include "hw/nvram/xlnx-bbram.h"
#include "hw/nvram/xlnx-versal-efuse.h"
#include "hw/ssi/xlnx-versal-ospi.h"
#include "hw/dma/xlnx_csu_dma.h"
#include "hw/misc/xlnx-versal-crl.h"
#include "hw/misc/xlnx-versal-pmc-iou-slcr.h"
#include "hw/misc/xlnx-versal-trng.h"
#include "hw/net/xlnx-versal-canfd.h"
#include "hw/misc/xlnx-versal-cfu.h"
#include "hw/misc/xlnx-versal-cframe-reg.h"

#define TYPE_XLNX_VERSAL "xlnx-versal"
OBJECT_DECLARE_SIMPLE_TYPE(Versal, XLNX_VERSAL)

#define XLNX_VERSAL_NR_ACPUS   2
#define XLNX_VERSAL_NR_RCPUS   2
#define XLNX_VERSAL_NR_UARTS   2
#define XLNX_VERSAL_NR_GEMS    2
#define XLNX_VERSAL_NR_ADMAS   8
#define XLNX_VERSAL_NR_SDS     2
#define XLNX_VERSAL_NR_XRAM    4
#define XLNX_VERSAL_NR_IRQS    192
#define XLNX_VERSAL_NR_CANFD   2
#define XLNX_VERSAL_CANFD_REF_CLK (24 * 1000 * 1000)
#define XLNX_VERSAL_NR_CFRAME  15

struct Versal {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    struct {
        struct {
            MemoryRegion mr;
            CPUClusterState cluster;
            ARMCPU cpu[XLNX_VERSAL_NR_ACPUS];
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
            PL011State uart[XLNX_VERSAL_NR_UARTS];
            CadenceGEMState gem[XLNX_VERSAL_NR_GEMS];
            XlnxZDMA adma[XLNX_VERSAL_NR_ADMAS];
            VersalUsb2 usb;
            CanBusState *canbus[XLNX_VERSAL_NR_CANFD];
            XlnxVersalCANFDState canfd[XLNX_VERSAL_NR_CANFD];
        } iou;

        /* Real-time Processing Unit.  */
        struct {
            MemoryRegion mr;
            MemoryRegion mr_ps_alias;

            CPUClusterState cluster;
            ARMCPU cpu[XLNX_VERSAL_NR_RCPUS];
        } rpu;

        struct {
            OrIRQState irq_orgate;
            XlnxXramCtrl ctrl[XLNX_VERSAL_NR_XRAM];
        } xram;

        XlnxVersalCRL crl;
    } lpd;

    /* The Platform Management Controller subsystem.  */
    struct {
        struct {
            SDHCIState sd[XLNX_VERSAL_NR_SDS];
            XlnxVersalPmcIouSlcr slcr;

            struct {
                XlnxVersalOspi ospi;
                XlnxCSUDMA dma_src;
                XlnxCSUDMA dma_dst;
                MemoryRegion linear_mr;
                OrIRQState irq_orgate;
            } ospi;
        } iou;

        XlnxZynqMPRTC rtc;
        XlnxVersalTRng trng;
        XlnxBBRam bbram;
        XlnxEFuse efuse;
        XlnxVersalEFuseCtrl efuse_ctrl;
        XlnxVersalEFuseCache efuse_cache;
        XlnxVersalCFUAPB cfu_apb;
        XlnxVersalCFUFDRO cfu_fdro;
        XlnxVersalCFUSFR cfu_sfr;
        XlnxVersalCFrameReg cframe[XLNX_VERSAL_NR_CFRAME];
        XlnxVersalCFrameBcastReg cframe_bcast;

        OrIRQState apb_irq_orgate;
    } pmc;

    struct {
        MemoryRegion *mr_ddr;
    } cfg;
};

/* Memory-map and IRQ definitions. Copied a subset from
 * auto-generated files.  */

#define VERSAL_GIC_MAINT_IRQ        9
#define VERSAL_TIMER_VIRT_IRQ       11
#define VERSAL_TIMER_S_EL1_IRQ      13
#define VERSAL_TIMER_NS_EL1_IRQ     14
#define VERSAL_TIMER_NS_EL2_IRQ     10

#define VERSAL_CRL_IRQ             10
#define VERSAL_UART0_IRQ_0         18
#define VERSAL_UART1_IRQ_0         19
#define VERSAL_CANFD0_IRQ_0        20
#define VERSAL_CANFD1_IRQ_0        21
#define VERSAL_USB0_IRQ_0          22
#define VERSAL_GEM0_IRQ_0          56
#define VERSAL_GEM0_WAKE_IRQ_0     57
#define VERSAL_GEM1_IRQ_0          58
#define VERSAL_GEM1_WAKE_IRQ_0     59
#define VERSAL_ADMA_IRQ_0          60
#define VERSAL_XRAM_IRQ_0          79
#define VERSAL_CFU_IRQ_0           120
#define VERSAL_PMC_APB_IRQ         121
#define VERSAL_OSPI_IRQ            124
#define VERSAL_SD0_IRQ_0           126
#define VERSAL_EFUSE_IRQ           139
#define VERSAL_TRNG_IRQ            141
#define VERSAL_RTC_ALARM_IRQ       142
#define VERSAL_RTC_SECONDS_IRQ     143

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

#define MM_CANFD0                   0xff060000U
#define MM_CANFD0_SIZE              0x10000
#define MM_CANFD1                   0xff070000U
#define MM_CANFD1_SIZE              0x10000

#define MM_GEM0                     0xff0c0000U
#define MM_GEM0_SIZE                0x10000
#define MM_GEM1                     0xff0d0000U
#define MM_GEM1_SIZE                0x10000

#define MM_ADMA_CH0                 0xffa80000U
#define MM_ADMA_CH0_SIZE            0x10000

#define MM_OCM                      0xfffc0000U
#define MM_OCM_SIZE                 0x40000

#define MM_XRAM                     0xfe800000
#define MM_XRAMC                    0xff8e0000
#define MM_XRAMC_SIZE               0x10000

#define MM_USB2_CTRL_REGS           0xFF9D0000
#define MM_USB2_CTRL_REGS_SIZE      0x10000

#define MM_USB_0                    0xFE200000
#define MM_USB_0_SIZE               0x10000

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
#define MM_FPD_FPD_APU              0xfd5c0000
#define MM_FPD_FPD_APU_SIZE         0x100

#define MM_PMC_PMC_IOU_SLCR         0xf1060000
#define MM_PMC_PMC_IOU_SLCR_SIZE    0x10000

#define MM_PMC_OSPI                 0xf1010000
#define MM_PMC_OSPI_SIZE            0x10000

#define MM_PMC_OSPI_DAC             0xc0000000
#define MM_PMC_OSPI_DAC_SIZE        0x20000000

#define MM_PMC_OSPI_DMA_DST         0xf1011800
#define MM_PMC_OSPI_DMA_SRC         0xf1011000

#define MM_PMC_SD0                  0xf1040000U
#define MM_PMC_SD0_SIZE             0x10000
#define MM_PMC_BBRAM_CTRL           0xf11f0000
#define MM_PMC_BBRAM_CTRL_SIZE      0x00050
#define MM_PMC_EFUSE_CTRL           0xf1240000
#define MM_PMC_EFUSE_CTRL_SIZE      0x00104
#define MM_PMC_EFUSE_CACHE          0xf1250000
#define MM_PMC_EFUSE_CACHE_SIZE     0x00C00

#define MM_PMC_CFU_APB              0xf12b0000
#define MM_PMC_CFU_APB_SIZE         0x10000
#define MM_PMC_CFU_STREAM           0xf12c0000
#define MM_PMC_CFU_STREAM_SIZE      0x1000
#define MM_PMC_CFU_SFR              0xf12c1000
#define MM_PMC_CFU_SFR_SIZE         0x1000
#define MM_PMC_CFU_FDRO             0xf12c2000
#define MM_PMC_CFU_FDRO_SIZE        0x1000
#define MM_PMC_CFU_STREAM_2         0xf1f80000
#define MM_PMC_CFU_STREAM_2_SIZE    0x40000

#define MM_PMC_CFRAME0_REG          0xf12d0000
#define MM_PMC_CFRAME0_REG_SIZE     0x1000
#define MM_PMC_CFRAME0_FDRI         0xf12d1000
#define MM_PMC_CFRAME0_FDRI_SIZE    0x1000
#define MM_PMC_CFRAME1_REG          0xf12d2000
#define MM_PMC_CFRAME1_REG_SIZE     0x1000
#define MM_PMC_CFRAME1_FDRI         0xf12d3000
#define MM_PMC_CFRAME1_FDRI_SIZE    0x1000
#define MM_PMC_CFRAME2_REG          0xf12d4000
#define MM_PMC_CFRAME2_REG_SIZE     0x1000
#define MM_PMC_CFRAME2_FDRI         0xf12d5000
#define MM_PMC_CFRAME2_FDRI_SIZE    0x1000
#define MM_PMC_CFRAME3_REG          0xf12d6000
#define MM_PMC_CFRAME3_REG_SIZE     0x1000
#define MM_PMC_CFRAME3_FDRI         0xf12d7000
#define MM_PMC_CFRAME3_FDRI_SIZE    0x1000
#define MM_PMC_CFRAME4_REG          0xf12d8000
#define MM_PMC_CFRAME4_REG_SIZE     0x1000
#define MM_PMC_CFRAME4_FDRI         0xf12d9000
#define MM_PMC_CFRAME4_FDRI_SIZE    0x1000
#define MM_PMC_CFRAME5_REG          0xf12da000
#define MM_PMC_CFRAME5_REG_SIZE     0x1000
#define MM_PMC_CFRAME5_FDRI         0xf12db000
#define MM_PMC_CFRAME5_FDRI_SIZE    0x1000
#define MM_PMC_CFRAME6_REG          0xf12dc000
#define MM_PMC_CFRAME6_REG_SIZE     0x1000
#define MM_PMC_CFRAME6_FDRI         0xf12dd000
#define MM_PMC_CFRAME6_FDRI_SIZE    0x1000
#define MM_PMC_CFRAME7_REG          0xf12de000
#define MM_PMC_CFRAME7_REG_SIZE     0x1000
#define MM_PMC_CFRAME7_FDRI         0xf12df000
#define MM_PMC_CFRAME7_FDRI_SIZE    0x1000
#define MM_PMC_CFRAME8_REG          0xf12e0000
#define MM_PMC_CFRAME8_REG_SIZE     0x1000
#define MM_PMC_CFRAME8_FDRI         0xf12e1000
#define MM_PMC_CFRAME8_FDRI_SIZE    0x1000
#define MM_PMC_CFRAME9_REG          0xf12e2000
#define MM_PMC_CFRAME9_REG_SIZE     0x1000
#define MM_PMC_CFRAME9_FDRI         0xf12e3000
#define MM_PMC_CFRAME9_FDRI_SIZE    0x1000
#define MM_PMC_CFRAME10_REG         0xf12e4000
#define MM_PMC_CFRAME10_REG_SIZE    0x1000
#define MM_PMC_CFRAME10_FDRI        0xf12e5000
#define MM_PMC_CFRAME10_FDRI_SIZE   0x1000
#define MM_PMC_CFRAME11_REG         0xf12e6000
#define MM_PMC_CFRAME11_REG_SIZE    0x1000
#define MM_PMC_CFRAME11_FDRI        0xf12e7000
#define MM_PMC_CFRAME11_FDRI_SIZE   0x1000
#define MM_PMC_CFRAME12_REG         0xf12e8000
#define MM_PMC_CFRAME12_REG_SIZE    0x1000
#define MM_PMC_CFRAME12_FDRI        0xf12e9000
#define MM_PMC_CFRAME12_FDRI_SIZE   0x1000
#define MM_PMC_CFRAME13_REG         0xf12ea000
#define MM_PMC_CFRAME13_REG_SIZE    0x1000
#define MM_PMC_CFRAME13_FDRI        0xf12eb000
#define MM_PMC_CFRAME13_FDRI_SIZE   0x1000
#define MM_PMC_CFRAME14_REG         0xf12ec000
#define MM_PMC_CFRAME14_REG_SIZE    0x1000
#define MM_PMC_CFRAME14_FDRI        0xf12ed000
#define MM_PMC_CFRAME14_FDRI_SIZE   0x1000
#define MM_PMC_CFRAME_BCAST_REG       0xf12ee000
#define MM_PMC_CFRAME_BCAST_REG_SIZE  0x1000
#define MM_PMC_CFRAME_BCAST_FDRI      0xf12ef000
#define MM_PMC_CFRAME_BCAST_FDRI_SIZE 0x1000

#define MM_PMC_CRP                  0xf1260000U
#define MM_PMC_CRP_SIZE             0x10000
#define MM_PMC_RTC                  0xf12a0000
#define MM_PMC_RTC_SIZE             0x10000
#define MM_PMC_TRNG                 0xf1230000
#define MM_PMC_TRNG_SIZE            0x10000
#endif

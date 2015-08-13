/*
 * IMX31 Clock Control Module
 *
 * Copyright (C) 2012 NICTA
 * Updated by Jean-Christophe Dubois <jcd@tribudubois.net>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef IMX_CCM_H
#define IMX_CCM_H

#include "hw/sysbus.h"

/* CCMR */
#define CCMR_FPME (1<<0)
#define CCMR_MPE  (1<<3)
#define CCMR_MDS  (1<<7)
#define CCMR_FPMF (1<<26)
#define CCMR_PRCS (3<<1)

/* PDR0 */
#define PDR0_MCU_PODF_SHIFT (0)
#define PDR0_MCU_PODF_MASK (0x7)
#define PDR0_MAX_PODF_SHIFT (3)
#define PDR0_MAX_PODF_MASK (0x7)
#define PDR0_IPG_PODF_SHIFT (6)
#define PDR0_IPG_PODF_MASK (0x3)
#define PDR0_NFC_PODF_SHIFT (8)
#define PDR0_NFC_PODF_MASK (0x7)
#define PDR0_HSP_PODF_SHIFT (11)
#define PDR0_HSP_PODF_MASK (0x7)
#define PDR0_PER_PODF_SHIFT (16)
#define PDR0_PER_PODF_MASK (0x1f)
#define PDR0_CSI_PODF_SHIFT (23)
#define PDR0_CSI_PODF_MASK (0x1ff)

#define EXTRACT(value, name) (((value) >> PDR0_##name##_PODF_SHIFT) \
                              & PDR0_##name##_PODF_MASK)
#define INSERT(value, name) (((value) & PDR0_##name##_PODF_MASK) << \
                             PDR0_##name##_PODF_SHIFT)

/* PLL control registers */
#define PD(v) (((v) >> 26) & 0xf)
#define MFD(v) (((v) >> 16) & 0x3ff)
#define MFI(v) (((v) >> 10) & 0xf);
#define MFN(v) ((v) & 0x3ff)

#define PLL_PD(x)               (((x) & 0xf) << 26)
#define PLL_MFD(x)              (((x) & 0x3ff) << 16)
#define PLL_MFI(x)              (((x) & 0xf) << 10)
#define PLL_MFN(x)              (((x) & 0x3ff) << 0)

#define TYPE_IMX_CCM "imx.ccm"
#define IMX_CCM(obj) OBJECT_CHECK(IMXCCMState, (obj), TYPE_IMX_CCM)

typedef struct IMXCCMState {
    /* <private> */
    SysBusDevice parent_obj;

    /* <public> */
    MemoryRegion iomem;

    uint32_t ccmr;
    uint32_t pdr0;
    uint32_t pdr1;
    uint32_t mpctl;
    uint32_t spctl;
    uint32_t cgr[3];
    uint32_t pmcr0;
    uint32_t pmcr1;

    /* Frequencies precalculated on register changes */
    uint32_t pll_refclk_freq;
    uint32_t mcu_clk_freq;
    uint32_t hsp_clk_freq;
    uint32_t ipg_clk_freq;
} IMXCCMState;

typedef enum  {
    NOCLK,
    MCU,
    HSP,
    IPG,
    CLK_32k
} IMXClk;

uint32_t imx_clock_frequency(DeviceState *s, IMXClk clock);

#endif /* IMX_CCM_H */

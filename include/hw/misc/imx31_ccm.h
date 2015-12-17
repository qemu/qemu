/*
 * IMX31 Clock Control Module
 *
 * Copyright (C) 2012 NICTA
 * Updated by Jean-Christophe Dubois <jcd@tribudubois.net>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef IMX31_CCM_H
#define IMX31_CCM_H

#include "hw/misc/imx_ccm.h"

/* CCMR */
#define CCMR_FPME    (1<<0)
#define CCMR_MPE     (1<<3)
#define CCMR_MDS     (1<<7)
#define CCMR_FPMF    (1<<26)
#define CCMR_PRCS    (3<<1)

#define PMCR0_DFSUP1 (1<<31)

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

#define TYPE_IMX31_CCM "imx31.ccm"
#define IMX31_CCM(obj) OBJECT_CHECK(IMX31CCMState, (obj), TYPE_IMX31_CCM)

typedef struct IMX31CCMState {
    /* <private> */
    IMXCCMState parent_obj;

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
} IMX31CCMState;

#endif /* IMX31_CCM_H */

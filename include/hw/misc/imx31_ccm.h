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

#define IMX31_CCM_CCMR_REG  0
#define IMX31_CCM_PDR0_REG  1
#define IMX31_CCM_PDR1_REG  2
#define IMX31_CCM_RCSR_REG  3
#define IMX31_CCM_MPCTL_REG 4
#define IMX31_CCM_UPCTL_REG 5
#define IMX31_CCM_SPCTL_REG 6
#define IMX31_CCM_COSR_REG  7
#define IMX31_CCM_CGR0_REG  8
#define IMX31_CCM_CGR1_REG  9
#define IMX31_CCM_CGR2_REG  10
#define IMX31_CCM_WIMR_REG  11
#define IMX31_CCM_LDC_REG   12
#define IMX31_CCM_DCVR0_REG 13
#define IMX31_CCM_DCVR1_REG 14
#define IMX31_CCM_DCVR2_REG 15
#define IMX31_CCM_DCVR3_REG 16
#define IMX31_CCM_LTR0_REG  17
#define IMX31_CCM_LTR1_REG  18
#define IMX31_CCM_LTR2_REG  19
#define IMX31_CCM_LTR3_REG  20
#define IMX31_CCM_LTBR0_REG 21
#define IMX31_CCM_LTBR1_REG 22
#define IMX31_CCM_PMCR0_REG 23
#define IMX31_CCM_PMCR1_REG 24
#define IMX31_CCM_PDR2_REG  25
#define IMX31_CCM_MAX_REG   26

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

    uint32_t reg[IMX31_CCM_MAX_REG];

} IMX31CCMState;

#endif /* IMX31_CCM_H */

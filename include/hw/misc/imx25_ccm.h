/*
 * IMX25 Clock Control Module
 *
 * Copyright (C) 2012 NICTA
 * Updated by Jean-Christophe Dubois <jcd@tribudubois.net>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef IMX25_CCM_H
#define IMX25_CCM_H

#include "hw/misc/imx_ccm.h"
#include "qom/object.h"

#define IMX25_CCM_MPCTL_REG  0
#define IMX25_CCM_UPCTL_REG  1
#define IMX25_CCM_CCTL_REG   2
#define IMX25_CCM_CGCR0_REG  3
#define IMX25_CCM_CGCR1_REG  4
#define IMX25_CCM_CGCR2_REG  5
#define IMX25_CCM_PCDR0_REG  6
#define IMX25_CCM_PCDR1_REG  7
#define IMX25_CCM_PCDR2_REG  8
#define IMX25_CCM_PCDR3_REG  9
#define IMX25_CCM_RCSR_REG   10
#define IMX25_CCM_CRDR_REG   11
#define IMX25_CCM_DCVR0_REG  12
#define IMX25_CCM_DCVR1_REG  13
#define IMX25_CCM_DCVR2_REG  14
#define IMX25_CCM_DCVR3_REG  15
#define IMX25_CCM_LTR0_REG   16
#define IMX25_CCM_LTR1_REG   17
#define IMX25_CCM_LTR2_REG   18
#define IMX25_CCM_LTR3_REG   19
#define IMX25_CCM_LTBR0_REG  20
#define IMX25_CCM_LTBR1_REG  21
#define IMX25_CCM_PMCR0_REG  22
#define IMX25_CCM_PMCR1_REG  23
#define IMX25_CCM_PMCR2_REG  24
#define IMX25_CCM_MCR_REG    25
#define IMX25_CCM_LPIMR0_REG 26
#define IMX25_CCM_LPIMR1_REG 27
#define IMX25_CCM_MAX_REG    28

/* CCTL */
#define CCTL_ARM_CLK_DIV_SHIFT (30)
#define CCTL_ARM_CLK_DIV_MASK  (0x3)
#define CCTL_AHB_CLK_DIV_SHIFT (28)
#define CCTL_AHB_CLK_DIV_MASK  (0x3)
#define CCTL_MPLL_BYPASS_SHIFT (22)
#define CCTL_MPLL_BYPASS_MASK  (0x1)
#define CCTL_USB_DIV_SHIFT     (16)
#define CCTL_USB_DIV_MASK      (0x3F)
#define CCTL_ARM_SRC_SHIFT     (13)
#define CCTL_ARM_SRC_MASK      (0x1)
#define CCTL_UPLL_DIS_SHIFT    (23)
#define CCTL_UPLL_DIS_MASK     (0x1)

#define EXTRACT(value, name) (((value) >> CCTL_##name##_SHIFT) \
                              & CCTL_##name##_MASK)
#define INSERT(value, name) (((value) & CCTL_##name##_MASK) << \
                             CCTL_##name##_SHIFT)

#define TYPE_IMX25_CCM "imx25.ccm"
OBJECT_DECLARE_SIMPLE_TYPE(IMX25CCMState, IMX25_CCM)

struct IMX25CCMState {
    /* <private> */
    IMXCCMState parent_obj;

    /* <public> */
    MemoryRegion iomem;

    uint32_t reg[IMX25_CCM_MAX_REG];

};

#endif /* IMX25_CCM_H */

/*
 * IMX7 System Reset Controller
 *
 * Copyright (C) 2023 Jean-Christophe Dubois <jcd@tribudubois.net>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef IMX7_SRC_H
#define IMX7_SRC_H

#include "hw/sysbus.h"
#include "qemu/bitops.h"
#include "qom/object.h"

#define SRC_SCR 0
#define SRC_A7RCR0 1
#define SRC_A7RCR1 2
#define SRC_M4RCR 3
#define SRC_ERCR 5
#define SRC_HSICPHY_RCR 7
#define SRC_USBOPHY1_RCR 8
#define SRC_USBOPHY2_RCR 9
#define SRC_MPIPHY_RCR 10
#define SRC_PCIEPHY_RCR 11
#define SRC_SBMR1 22
#define SRC_SRSR 23
#define SRC_SISR 26
#define SRC_SIMR 27
#define SRC_SBMR2 28
#define SRC_GPR1 29
#define SRC_GPR2 30
#define SRC_GPR3 31
#define SRC_GPR4 32
#define SRC_GPR5 33
#define SRC_GPR6 34
#define SRC_GPR7 35
#define SRC_GPR8 36
#define SRC_GPR9 37
#define SRC_GPR10 38
#define SRC_MAX 39

/* SRC_A7SCR1 */
#define R_CORE1_ENABLE_SHIFT     1
#define R_CORE1_ENABLE_LENGTH    1
/* SRC_A7SCR0 */
#define R_CORE1_RST_SHIFT        5
#define R_CORE1_RST_LENGTH       1
#define R_CORE0_RST_SHIFT        4
#define R_CORE0_RST_LENGTH       1

#define TYPE_IMX7_SRC "imx7.src"
OBJECT_DECLARE_SIMPLE_TYPE(IMX7SRCState, IMX7_SRC)

struct IMX7SRCState {
    /* <private> */
    SysBusDevice parent_obj;

    /* <public> */
    MemoryRegion iomem;

    uint32_t regs[SRC_MAX];
};

#endif /* IMX7_SRC_H */

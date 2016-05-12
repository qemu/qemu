/*
 * IMX6 System Reset Controller
 *
 * Copyright (C) 2012 NICTA
 * Updated by Jean-Christophe Dubois <jcd@tribudubois.net>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef IMX6_SRC_H
#define IMX6_SRC_H

#include "hw/sysbus.h"
#include "qemu/bitops.h"

#define SRC_SCR 0
#define SRC_SBMR1 1
#define SRC_SRSR 2
#define SRC_SISR 5
#define SRC_SIMR 6
#define SRC_SBMR2 7
#define SRC_GPR1 8
#define SRC_GPR2 9
#define SRC_GPR3 10
#define SRC_GPR4 11
#define SRC_GPR5 12
#define SRC_GPR6 13
#define SRC_GPR7 14
#define SRC_GPR8 15
#define SRC_GPR9 16
#define SRC_GPR10 17
#define SRC_MAX 18

/* SRC_SCR */
#define CORE3_ENABLE_SHIFT     24
#define CORE3_ENABLE_LENGTH    1
#define CORE2_ENABLE_SHIFT     23
#define CORE2_ENABLE_LENGTH    1
#define CORE1_ENABLE_SHIFT     22
#define CORE1_ENABLE_LENGTH    1
#define CORE3_RST_SHIFT        16
#define CORE3_RST_LENGTH       1
#define CORE2_RST_SHIFT        15
#define CORE2_RST_LENGTH       1
#define CORE1_RST_SHIFT        14
#define CORE1_RST_LENGTH       1
#define CORE0_RST_SHIFT        13
#define CORE0_RST_LENGTH       1
#define SW_IPU1_RST_SHIFT      3
#define SW_IPU1_RST_LENGTH     1
#define SW_IPU2_RST_SHIFT      12
#define SW_IPU2_RST_LENGTH     1
#define WARM_RST_ENABLE_SHIFT  0
#define WARM_RST_ENABLE_LENGTH 1

#define EXTRACT(value, name) extract32(value, name##_SHIFT, name##_LENGTH)

#define TYPE_IMX6_SRC "imx6.src"
#define IMX6_SRC(obj) OBJECT_CHECK(IMX6SRCState, (obj), TYPE_IMX6_SRC)

typedef struct IMX6SRCState {
    /* <private> */
    SysBusDevice parent_obj;

    /* <public> */
    MemoryRegion iomem;

    uint32_t regs[SRC_MAX];

} IMX6SRCState;

#endif /* IMX6_SRC_H */

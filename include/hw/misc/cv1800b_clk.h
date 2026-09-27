/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Sophgo CV1800B Clock Controller
 *
 * Copyright (c) 2026 Kuan-Wei Chiu <visitorckw@gmail.com>
 */

#ifndef HW_MISC_CV1800B_CLK_H
#define HW_MISC_CV1800B_CLK_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_CV1800B_CLK "cv1800b-clk"
OBJECT_DECLARE_SIMPLE_TYPE(CV1800BClkState, CV1800B_CLK)

#define CV1800B_CLK_IOSIZE 0x1000

struct CV1800BClkState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    uint32_t regs[CV1800B_CLK_IOSIZE / sizeof(uint32_t)];
};

#endif

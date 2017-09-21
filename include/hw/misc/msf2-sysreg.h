/*
 * Microsemi SmartFusion2 SYSREG
 *
 * Copyright (c) 2017 Subbaraya Sundeep <sundeep.lkml@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef HW_MSF2_SYSREG_H
#define HW_MSF2_SYSREG_H

#include "hw/sysbus.h"

enum {
    ESRAM_CR        = 0x00 / 4,
    ESRAM_MAX_LAT,
    DDR_CR,
    ENVM_CR,
    ENVM_REMAP_BASE_CR,
    ENVM_REMAP_FAB_CR,
    CC_CR,
    CC_REGION_CR,
    CC_LOCK_BASE_ADDR_CR,
    CC_FLUSH_INDX_CR,
    DDRB_BUF_TIMER_CR,
    DDRB_NB_ADDR_CR,
    DDRB_NB_SIZE_CR,
    DDRB_CR,

    SOFT_RESET_CR  = 0x48 / 4,
    M3_CR,

    GPIO_SYSRESET_SEL_CR = 0x58 / 4,

    MDDR_CR = 0x60 / 4,

    MSSDDR_PLL_STATUS_LOW_CR = 0x90 / 4,
    MSSDDR_PLL_STATUS_HIGH_CR,
    MSSDDR_FACC1_CR,
    MSSDDR_FACC2_CR,

    MSSDDR_PLL_STATUS = 0x150 / 4,
};

#define MSF2_SYSREG_MMIO_SIZE     0x300

#define TYPE_MSF2_SYSREG          "msf2-sysreg"
#define MSF2_SYSREG(obj)  OBJECT_CHECK(MSF2SysregState, (obj), TYPE_MSF2_SYSREG)

typedef struct MSF2SysregState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;

    uint8_t apb0div;
    uint8_t apb1div;

    uint32_t regs[MSF2_SYSREG_MMIO_SIZE / 4];
} MSF2SysregState;

#endif /* HW_MSF2_SYSREG_H */

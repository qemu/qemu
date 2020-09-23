/*
 * QEMU model of Xilinx I/O Module Interrupt Controller
 *
 * Copyright (c) 2014 Xilinx Inc.
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

#ifndef HW_INTC_XLNX_PMU_IOMOD_INTC_H
#define HW_INTC_XLNX_PMU_IOMOD_INTC_H

#include "hw/sysbus.h"
#include "hw/register.h"
#include "qom/object.h"

#define TYPE_XLNX_PMU_IO_INTC "xlnx.pmu_io_intc"

OBJECT_DECLARE_SIMPLE_TYPE(XlnxPMUIOIntc, XLNX_PMU_IO_INTC)

/* This is R_PIT3_CONTROL + 1 */
#define XLNXPMUIOINTC_R_MAX (0x78 + 1)

struct XlnxPMUIOIntc {
    SysBusDevice parent_obj;
    MemoryRegion iomem;

    qemu_irq parent_irq;

    struct {
        uint32_t intr_size;
        uint32_t level_edge;
        uint32_t positive;
    } cfg;

    uint32_t irq_raw;

    uint32_t regs[XLNXPMUIOINTC_R_MAX];
    RegisterInfo regs_info[XLNXPMUIOINTC_R_MAX];
};

#endif /* HW_INTC_XLNX_PMU_IOMOD_INTC_H */

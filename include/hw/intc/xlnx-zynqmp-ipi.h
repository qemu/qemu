/*
 * QEMU model of the IPI Inter Processor Interrupt block
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

#ifndef XLNX_ZYNQMP_IPI_H
#define XLNX_ZYNQMP_IPI_H

#include "hw/sysbus.h"
#include "hw/register.h"
#include "qom/object.h"

#define TYPE_XLNX_ZYNQMP_IPI "xlnx.zynqmp_ipi"

OBJECT_DECLARE_SIMPLE_TYPE(XlnxZynqMPIPI, XLNX_ZYNQMP_IPI)

/* This is R_IPI_IDR + 1 */
#define R_XLNX_ZYNQMP_IPI_MAX ((0x1c / 4) + 1)

#define NUM_IPIS 11

struct XlnxZynqMPIPI {
    /* Private */
    SysBusDevice parent_obj;

    /* Public */
    MemoryRegion iomem;
    qemu_irq irq;

    qemu_irq irq_trig_out[NUM_IPIS];
    qemu_irq irq_obs_out[NUM_IPIS];

    uint32_t regs[R_XLNX_ZYNQMP_IPI_MAX];
    RegisterInfo regs_info[R_XLNX_ZYNQMP_IPI_MAX];
};

#endif /* XLNX_ZYNQMP_IPI_H */

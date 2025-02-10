/*
 * QEMU model of the ZynqMP generic DMA
 *
 * Copyright (c) 2014 Xilinx Inc.
 * Copyright (c) 2018 FEIMTECH AB
 *
 * Written by Edgar E. Iglesias <edgar.iglesias@xilinx.com>,
 *            Francisco Iglesias <francisco.iglesias@feimtech.se>
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

#ifndef XLNX_ZDMA_H
#define XLNX_ZDMA_H

#include "hw/sysbus.h"
#include "hw/register.h"
#include "system/dma.h"
#include "qom/object.h"

#define ZDMA_R_MAX (0x204 / 4)

typedef enum {
    DISABLED = 0,
    ENABLED = 1,
    PAUSED = 2,
} XlnxZDMAState;

typedef union {
    struct {
        uint64_t addr;
        uint32_t size;
        uint32_t attr;
    };
    uint32_t words[4];
} XlnxZDMADescr;

struct XlnxZDMA {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    MemTxAttrs attr;
    MemoryRegion *dma_mr;
    AddressSpace dma_as;
    qemu_irq irq_zdma_ch_imr;

    struct {
        uint32_t bus_width;
    } cfg;

    XlnxZDMAState state;
    bool error;

    XlnxZDMADescr dsc_src;
    XlnxZDMADescr dsc_dst;

    uint32_t regs[ZDMA_R_MAX];
    RegisterInfo regs_info[ZDMA_R_MAX];

    /* We don't model the common bufs. Must be at least 16 bytes
       to model write only mode.  */
    uint8_t buf[2048];
};

#define TYPE_XLNX_ZDMA "xlnx.zdma"

OBJECT_DECLARE_SIMPLE_TYPE(XlnxZDMA, XLNX_ZDMA)

#endif /* XLNX_ZDMA_H */

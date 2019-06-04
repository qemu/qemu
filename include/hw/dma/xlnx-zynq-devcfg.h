/*
 * QEMU model of the Xilinx Devcfg Interface
 *
 * (C) 2011 PetaLogix Pty Ltd
 * (C) 2014 Xilinx Inc.
 * Written by Peter Crosthwaite <peter.crosthwaite@xilinx.com>
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

#ifndef XLNX_ZYNQ_DEVCFG_H
#define XLNX_ZYNQ_DEVCFG_H

#include "hw/register.h"
#include "hw/sysbus.h"

#define TYPE_XLNX_ZYNQ_DEVCFG "xlnx.ps7-dev-cfg"

#define XLNX_ZYNQ_DEVCFG(obj) \
    OBJECT_CHECK(XlnxZynqDevcfg, (obj), TYPE_XLNX_ZYNQ_DEVCFG)

#define XLNX_ZYNQ_DEVCFG_R_MAX (0x100 / 4)

#define XLNX_ZYNQ_DEVCFG_DMA_CMD_FIFO_LEN 10

typedef struct XlnxZynqDevcfgDMACmd {
    uint32_t src_addr;
    uint32_t dest_addr;
    uint32_t src_len;
    uint32_t dest_len;
} XlnxZynqDevcfgDMACmd;

typedef struct XlnxZynqDevcfg {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    qemu_irq irq;

    XlnxZynqDevcfgDMACmd dma_cmd_fifo[XLNX_ZYNQ_DEVCFG_DMA_CMD_FIFO_LEN];
    uint8_t dma_cmd_fifo_num;

    uint32_t regs[XLNX_ZYNQ_DEVCFG_R_MAX];
    RegisterInfo regs_info[XLNX_ZYNQ_DEVCFG_R_MAX];
} XlnxZynqDevcfg;

#endif

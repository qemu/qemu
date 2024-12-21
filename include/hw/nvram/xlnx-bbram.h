/*
 * QEMU model of the Xilinx BBRAM Battery Backed RAM
 *
 * Copyright (c) 2015-2021 Xilinx Inc.
 *
 * Written by Edgar E. Iglesias <edgari@xilinx.com>
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
#ifndef XLNX_BBRAM_H
#define XLNX_BBRAM_H

#include "system/block-backend.h"
#include "hw/qdev-core.h"
#include "hw/irq.h"
#include "hw/sysbus.h"
#include "hw/register.h"

#define RMAX_XLNX_BBRAM ((0x4c / 4) + 1)

#define TYPE_XLNX_BBRAM "xlnx.bbram-ctrl"
OBJECT_DECLARE_SIMPLE_TYPE(XlnxBBRam, XLNX_BBRAM);

struct XlnxBBRam {
    SysBusDevice parent_obj;
    qemu_irq irq_bbram;

    BlockBackend *blk;

    uint32_t crc_zpads;
    bool bbram8_wo;
    bool blk_ro;

    RegisterInfoArray *reg_array;
    uint32_t regs[RMAX_XLNX_BBRAM];
    RegisterInfo regs_info[RMAX_XLNX_BBRAM];
};

#endif

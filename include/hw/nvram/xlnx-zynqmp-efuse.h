/*
 * Copyright (c) 2021 Xilinx Inc.
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
#ifndef XLNX_ZYNQMP_EFUSE_H
#define XLNX_ZYNQMP_EFUSE_H

#include "hw/irq.h"
#include "hw/sysbus.h"
#include "hw/register.h"
#include "hw/nvram/xlnx-efuse.h"

#define XLNX_ZYNQMP_EFUSE_R_MAX ((0x10fc / 4) + 1)

#define TYPE_XLNX_ZYNQMP_EFUSE "xlnx-zynqmp-efuse"
OBJECT_DECLARE_SIMPLE_TYPE(XlnxZynqMPEFuse, XLNX_ZYNQMP_EFUSE);

struct XlnxZynqMPEFuse {
    SysBusDevice parent_obj;
    qemu_irq irq;

    XlnxEFuse *efuse;
    RegisterInfoArray *reg_array;
    uint32_t regs[XLNX_ZYNQMP_EFUSE_R_MAX];
    RegisterInfo regs_info[XLNX_ZYNQMP_EFUSE_R_MAX];
};

#endif

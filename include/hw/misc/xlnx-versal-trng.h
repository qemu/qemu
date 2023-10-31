/*
 * Non-crypto strength model of the True Random Number Generator
 * in the AMD/Xilinx Versal device family.
 *
 * Copyright (c) 2017-2020 Xilinx Inc.
 * Copyright (c) 2023 Advanced Micro Devices, Inc.
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
#ifndef XLNX_VERSAL_TRNG_H
#define XLNX_VERSAL_TRNG_H

#include "hw/irq.h"
#include "hw/sysbus.h"
#include "hw/register.h"

#define TYPE_XLNX_VERSAL_TRNG "xlnx.versal-trng"
OBJECT_DECLARE_SIMPLE_TYPE(XlnxVersalTRng, XLNX_VERSAL_TRNG);

#define RMAX_XLNX_VERSAL_TRNG ((0xf0 / 4) + 1)

typedef struct XlnxVersalTRng {
    SysBusDevice parent_obj;
    qemu_irq irq;
    GRand *prng;

    uint32_t hw_version;
    uint32_t forced_faults;

    uint32_t rand_count;
    uint64_t rand_reseed;

    uint64_t forced_prng_seed;
    uint64_t forced_prng_count;
    uint64_t tst_seed[2];

    uint32_t regs[RMAX_XLNX_VERSAL_TRNG];
    RegisterInfo regs_info[RMAX_XLNX_VERSAL_TRNG];
} XlnxVersalTRng;

#undef RMAX_XLNX_VERSAL_TRNG
#endif

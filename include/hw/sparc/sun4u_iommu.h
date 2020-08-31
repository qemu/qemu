/*
 * QEMU sun4u IOMMU emulation
 *
 * Copyright (c) 2006 Fabrice Bellard
 * Copyright (c) 2012,2013 Artyom Tarasenko
 * Copyright (c) 2017 Mark Cave-Ayland
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

#ifndef SUN4U_IOMMU_H
#define SUN4U_IOMMU_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define IOMMU_NREGS             3

struct IOMMUState {
    SysBusDevice parent_obj;

    AddressSpace iommu_as;
    IOMMUMemoryRegion iommu;

    MemoryRegion iomem;
    uint64_t regs[IOMMU_NREGS];
};
typedef struct IOMMUState IOMMUState;

#define TYPE_SUN4U_IOMMU "sun4u-iommu"
DECLARE_INSTANCE_CHECKER(IOMMUState, SUN4U_IOMMU,
                         TYPE_SUN4U_IOMMU)

#define TYPE_SUN4U_IOMMU_MEMORY_REGION "sun4u-iommu-memory-region"

#endif

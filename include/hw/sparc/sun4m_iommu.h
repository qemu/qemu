/*
 * QEMU Sun4m iommu emulation
 *
 * Copyright (c) 2003-2005 Fabrice Bellard
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

#ifndef SUN4M_IOMMU_H
#define SUN4M_IOMMU_H

#include "hw/sysbus.h"

#define IOMMU_NREGS         (4 * 4096 / 4)

typedef struct IOMMUState {
    SysBusDevice parent_obj;

    AddressSpace iommu_as;
    IOMMUMemoryRegion iommu;

    MemoryRegion iomem;
    uint32_t regs[IOMMU_NREGS];
    hwaddr iostart;
    qemu_irq irq;
    uint32_t version;
} IOMMUState;

#define TYPE_SUN4M_IOMMU "sun4m-iommu"
#define SUN4M_IOMMU(obj) OBJECT_CHECK(IOMMUState, (obj), TYPE_SUN4M_IOMMU)

#define TYPE_SUN4M_IOMMU_MEMORY_REGION "sun4m-iommu-memory-region"

#endif

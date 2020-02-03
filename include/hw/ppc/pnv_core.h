/*
 * QEMU PowerPC PowerNV CPU Core model
 *
 * Copyright (c) 2016, IBM Corporation.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef PPC_PNV_CORE_H
#define PPC_PNV_CORE_H

#include "hw/cpu/core.h"
#include "target/ppc/cpu.h"

#define TYPE_PNV_CORE "powernv-cpu-core"
#define PNV_CORE(obj) \
    OBJECT_CHECK(PnvCore, (obj), TYPE_PNV_CORE)
#define PNV_CORE_CLASS(klass) \
     OBJECT_CLASS_CHECK(PnvCoreClass, (klass), TYPE_PNV_CORE)
#define PNV_CORE_GET_CLASS(obj) \
     OBJECT_GET_CLASS(PnvCoreClass, (obj), TYPE_PNV_CORE)

typedef struct PnvChip PnvChip;

typedef struct PnvCore {
    /*< private >*/
    CPUCore parent_obj;

    /*< public >*/
    PowerPCCPU **threads;
    uint32_t pir;
    uint64_t hrmor;
    PnvChip *chip;

    MemoryRegion xscom_regs;
} PnvCore;

typedef struct PnvCoreClass {
    DeviceClass parent_class;

    const MemoryRegionOps *xscom_ops;
} PnvCoreClass;

#define PNV_CORE_TYPE_SUFFIX "-" TYPE_PNV_CORE
#define PNV_CORE_TYPE_NAME(cpu_model) cpu_model PNV_CORE_TYPE_SUFFIX

typedef struct PnvCPUState {
    Object *intc;
} PnvCPUState;

static inline PnvCPUState *pnv_cpu_state(PowerPCCPU *cpu)
{
    return (PnvCPUState *)cpu->machine_data;
}

#define TYPE_PNV_QUAD "powernv-cpu-quad"
#define PNV_QUAD(obj) \
    OBJECT_CHECK(PnvQuad, (obj), TYPE_PNV_QUAD)

typedef struct PnvQuad {
    DeviceState parent_obj;

    uint32_t id;
    MemoryRegion xscom_regs;
} PnvQuad;
#endif /* PPC_PNV_CORE_H */

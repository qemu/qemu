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
#ifndef _PPC_PNV_CORE_H
#define _PPC_PNV_CORE_H

#include "hw/cpu/core.h"

#define TYPE_PNV_CORE "powernv-cpu-core"
#define PNV_CORE(obj) \
    OBJECT_CHECK(PnvCore, (obj), TYPE_PNV_CORE)
#define PNV_CORE_CLASS(klass) \
     OBJECT_CLASS_CHECK(PnvCoreClass, (klass), TYPE_PNV_CORE)
#define PNV_CORE_GET_CLASS(obj) \
     OBJECT_GET_CLASS(PnvCoreClass, (obj), TYPE_PNV_CORE)

typedef struct PnvCore {
    /*< private >*/
    CPUCore parent_obj;

    /*< public >*/
    PowerPCCPU **threads;
    uint32_t pir;

    MemoryRegion xscom_regs;
} PnvCore;

typedef struct PnvCoreClass {
    DeviceClass parent_class;
} PnvCoreClass;

#define PNV_CORE_TYPE_SUFFIX "-" TYPE_PNV_CORE
#define PNV_CORE_TYPE_NAME(cpu_model) cpu_model PNV_CORE_TYPE_SUFFIX

#endif /* _PPC_PNV_CORE_H */

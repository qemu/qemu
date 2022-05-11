/*
 * Infineon tc27x SoC System emulation.
 *
 * Copyright (c) 2020 Andreas Konopik <andreas.konopik@efs-auto.de>
 * Copyright (c) 2020 David Brenken <david.brenken@efs-auto.de>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef TC27X_SOC_H
#define TC27X_SOC_H

#include "hw/sysbus.h"
#include "target/tricore/cpu.h"
#include "qom/object.h"

#define TYPE_TC27X_SOC ("tc27x-soc")
OBJECT_DECLARE_TYPE(TC27XSoCState, TC27XSoCClass, TC27X_SOC)

typedef struct TC27XSoCCPUMemState {

    MemoryRegion dspr;
    MemoryRegion pspr;

    MemoryRegion dcache;
    MemoryRegion dtag;
    MemoryRegion pcache;
    MemoryRegion ptag;

} TC27XSoCCPUMemState;

typedef struct TC27XSoCFlashMemState {

    MemoryRegion pflash0_c;
    MemoryRegion pflash1_c;
    MemoryRegion pflash0_u;
    MemoryRegion pflash1_u;
    MemoryRegion dflash0;
    MemoryRegion dflash1;
    MemoryRegion olda_c;
    MemoryRegion olda_u;
    MemoryRegion brom_c;
    MemoryRegion brom_u;
    MemoryRegion lmuram_c;
    MemoryRegion lmuram_u;
    MemoryRegion emem_c;
    MemoryRegion emem_u;

} TC27XSoCFlashMemState;

typedef struct TC27XSoCState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    TriCoreCPU cpu;

    MemoryRegion dsprX;
    MemoryRegion psprX;

    TC27XSoCCPUMemState cpu0mem;
    TC27XSoCCPUMemState cpu1mem;
    TC27XSoCCPUMemState cpu2mem;

    TC27XSoCFlashMemState flashmem;

} TC27XSoCState;

typedef struct MemmapEntry {
    hwaddr base;
    hwaddr size;
} MemmapEntry;

typedef struct TC27XSoCClass {
    DeviceClass parent_class;

    const char *name;
    const char *cpu_type;
    const MemmapEntry *memmap;
    uint32_t num_cpus;
} TC27XSoCClass;

enum {
    TC27XD_DSPR2,
    TC27XD_DCACHE2,
    TC27XD_DTAG2,
    TC27XD_PSPR2,
    TC27XD_PCACHE2,
    TC27XD_PTAG2,
    TC27XD_DSPR1,
    TC27XD_DCACHE1,
    TC27XD_DTAG1,
    TC27XD_PSPR1,
    TC27XD_PCACHE1,
    TC27XD_PTAG1,
    TC27XD_DSPR0,
    TC27XD_PSPR0,
    TC27XD_PCACHE0,
    TC27XD_PTAG0,
    TC27XD_PFLASH0_C,
    TC27XD_PFLASH1_C,
    TC27XD_OLDA_C,
    TC27XD_BROM_C,
    TC27XD_LMURAM_C,
    TC27XD_EMEM_C,
    TC27XD_PFLASH0_U,
    TC27XD_PFLASH1_U,
    TC27XD_DFLASH0,
    TC27XD_DFLASH1,
    TC27XD_OLDA_U,
    TC27XD_BROM_U,
    TC27XD_LMURAM_U,
    TC27XD_EMEM_U,
    TC27XD_PSPRX,
    TC27XD_DSPRX,
};

#endif

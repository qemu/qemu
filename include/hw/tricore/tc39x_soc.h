/*
 * Infineon TC39x SoC System emulation.
 *
 * Copyright (c) 2024
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#ifndef HW_TRICORE_TC39X_SOC_H
#define HW_TRICORE_TC39X_SOC_H

#include "hw/sysbus.h"
#include "target/tricore/cpu.h"
#include "hw/tricore/tc_stm.h"
#include "hw/tricore/tc_ir.h"
#include "qom/object.h"

#define TYPE_TC39X_SOC "tc39x-soc"
OBJECT_DECLARE_TYPE(TC39xSoCState, TC39xSoCClass, TC39X_SOC)

/*
 * TC39x Memory Map (based on TC397 User Manual)
 */
typedef enum {
    /* CPU0 Local Memory */
    TC39X_DSPR0,        /* 0x70000000 - Data Scratch-Pad RAM CPU0 (240KB) */
    TC39X_PSPR0,        /* 0x70100000 - Program Scratch-Pad RAM CPU0 (64KB) */
    TC39X_PCACHE0,      /* 0x70180000 - Program Cache CPU0 */
    TC39X_PTAG0,        /* 0x701C0000 - Program Cache Tag CPU0 */

    /* CPU1 Local Memory */
    TC39X_DSPR1,        /* 0x60000000 - Data Scratch-Pad RAM CPU1 (240KB) */
    TC39X_PSPR1,        /* 0x60100000 - Program Scratch-Pad RAM CPU1 (64KB) */
    TC39X_PCACHE1,      /* 0x60180000 - Program Cache CPU1 */
    TC39X_PTAG1,        /* 0x601C0000 - Program Cache Tag CPU1 */

    /* CPU2 Local Memory */
    TC39X_DSPR2,        /* 0x50000000 - Data Scratch-Pad RAM CPU2 (96KB) */
    TC39X_PSPR2,        /* 0x50100000 - Program Scratch-Pad RAM CPU2 (64KB) */
    TC39X_PCACHE2,      /* 0x50180000 - Program Cache CPU2 */
    TC39X_PTAG2,        /* 0x501C0000 - Program Cache Tag CPU2 */

    /* Flash Memory (Cached) */
    TC39X_PFLASH0_C,    /* 0x80000000 - Program Flash 0 Cached (3MB) */
    TC39X_PFLASH1_C,    /* 0x80300000 - Program Flash 1 Cached (3MB) */
    TC39X_PFLASH2_C,    /* 0x80600000 - Program Flash 2 Cached (3MB) */
    TC39X_PFLASH3_C,    /* 0x80900000 - Program Flash 3 Cached (3MB) */

    /* Flash Memory (Uncached) */
    TC39X_PFLASH0_U,    /* 0xA0000000 - Program Flash 0 Uncached */
    TC39X_PFLASH1_U,    /* 0xA0300000 - Program Flash 1 Uncached */
    TC39X_PFLASH2_U,    /* 0xA0600000 - Program Flash 2 Uncached */
    TC39X_PFLASH3_U,    /* 0xA0900000 - Program Flash 3 Uncached */

    /* Data Flash */
    TC39X_DFLASH0,      /* 0xAF000000 - Data Flash 0 */
    TC39X_DFLASH1,      /* 0xAF400000 - Data Flash 1 */

    /* Boot ROM */
    TC39X_BROM_C,       /* 0x8FFF8000 - Boot ROM Cached */
    TC39X_BROM_U,       /* 0xAFFF8000 - Boot ROM Uncached */

    /* LMU RAM */
    TC39X_LMURAM_C,     /* 0x90000000 - LMU RAM Cached (768KB) */
    TC39X_LMURAM_U,     /* 0xB0000000 - LMU RAM Uncached */

    /* DAM (Default Application Memory) */
    TC39X_DAM0,         /* 0xB00A0000 - DAM0 (128KB) */

    /* Local Addressing */
    TC39X_PSPRX,        /* 0xC0000000 - LOCAL.PSPR */
    TC39X_DSPRX,        /* 0xD0000000 - LOCAL.DSPR */

    /* Peripheral Memory */
    TC39X_STM0,         /* 0xF0001000 - System Timer 0 */
    TC39X_STM1,         /* 0xF0001100 - System Timer 1 */
    TC39X_STM2,         /* 0xF0001200 - System Timer 2 */
    TC39X_IR,           /* 0xF0038000 - Interrupt Router */

    TC39X_MEMMAP_SIZE
} TC39xMemoryMap;

typedef struct MemmapEntry {
    hwaddr base;
    hwaddr size;
} MemmapEntry;

typedef struct TC39xCpuMemState {
    MemoryRegion dspr;
    MemoryRegion pspr;
    MemoryRegion pcache;
    MemoryRegion ptag;
} TC39xCpuMemState;

typedef struct TC39xFlashMemState {
    MemoryRegion pflash0_c;
    MemoryRegion pflash1_c;
    MemoryRegion pflash2_c;
    MemoryRegion pflash3_c;
    MemoryRegion pflash0_u;
    MemoryRegion pflash1_u;
    MemoryRegion pflash2_u;
    MemoryRegion pflash3_u;
    MemoryRegion dflash0;
    MemoryRegion dflash1;
    MemoryRegion brom_c;
    MemoryRegion brom_u;
    MemoryRegion lmuram_c;
    MemoryRegion lmuram_u;
    MemoryRegion dam0;
} TC39xFlashMemState;

struct TC39xSoCState {
    SysBusDevice parent_obj;

    /* CPU */
    TriCoreCPU cpu;

    /* Memory regions */
    TC39xCpuMemState cpu0mem;
    TC39xCpuMemState cpu1mem;
    TC39xCpuMemState cpu2mem;
    TC39xFlashMemState flashmem;

    MemoryRegion psprX;     /* Local PSPR alias */
    MemoryRegion dsprX;     /* Local DSPR alias */

    /* Peripherals */
    TcStmState stm0;
    TcStmState stm1;
    TcStmState stm2;
    TcIrState ir;
};

struct TC39xSoCClass {
    SysBusDeviceClass parent_class;

    const char *name;
    const char *cpu_type;
    const MemmapEntry *memmap;
    uint32_t num_cpus;
};

#endif /* HW_TRICORE_TC39X_SOC_H */

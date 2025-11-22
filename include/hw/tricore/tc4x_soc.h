/*
 * Infineon TC4x SoC System emulation.
 *
 * Copyright (c) 2024 QEMU contributors
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef TC4X_SOC_H
#define TC4X_SOC_H

#include "hw/sysbus.h"
#include "target/tricore/cpu.h"
#include "qom/object.h"

#define TYPE_TC4X_SOC "tc4x-soc"
OBJECT_DECLARE_TYPE(TC4xSoCState, TC4xSoCClass, TC4X_SOC)

/*
 * TC4xx Memory sizes (TC4D7 variant - high-end)
 * Based on Infineon TC4Dx datasheet
 */
#define TC4X_DSPR_SIZE      (512 * KiB)   /* Per-core DSPR */
#define TC4X_PSPR_SIZE      (64 * KiB)    /* Per-core PSPR */
#define TC4X_DCACHE_SIZE    (16 * KiB)    /* Per-core D-Cache */
#define TC4X_PCACHE_SIZE    (32 * KiB)    /* Per-core P-Cache */
#define TC4X_PFLASH_SIZE    (8 * MiB)     /* Program Flash per bank */
#define TC4X_DFLASH_SIZE    (2 * MiB)     /* Data Flash */
#define TC4X_LMURAM_SIZE    (1 * MiB)     /* LMU RAM */
#define TC4X_BROM_SIZE      (64 * KiB)    /* Boot ROM */
#define TC4X_EMEM_SIZE      (4 * MiB)     /* EMEM */

/* Per-CPU memory regions */
typedef struct TC4xSoCCPUMemState {
    MemoryRegion dspr;
    MemoryRegion pspr;
    MemoryRegion dcache;
    MemoryRegion dtag;
    MemoryRegion pcache;
    MemoryRegion ptag;
} TC4xSoCCPUMemState;

/* Flash memory regions */
typedef struct TC4xSoCFlashMemState {
    MemoryRegion pflash0_c;   /* Program Flash Bank 0 - Cached */
    MemoryRegion pflash1_c;   /* Program Flash Bank 1 - Cached */
    MemoryRegion pflash2_c;   /* Program Flash Bank 2 - Cached */
    MemoryRegion pflash0_u;   /* Program Flash Bank 0 - Uncached */
    MemoryRegion pflash1_u;   /* Program Flash Bank 1 - Uncached */
    MemoryRegion pflash2_u;   /* Program Flash Bank 2 - Uncached */
    MemoryRegion dflash0;     /* Data Flash 0 */
    MemoryRegion dflash1;     /* Data Flash 1 */
    MemoryRegion brom_c;      /* Boot ROM - Cached */
    MemoryRegion brom_u;      /* Boot ROM - Uncached */
    MemoryRegion lmuram_c;    /* LMU RAM - Cached */
    MemoryRegion lmuram_u;    /* LMU RAM - Uncached */
    MemoryRegion emem_c;      /* EMEM - Cached */
    MemoryRegion emem_u;      /* EMEM - Uncached */
} TC4xSoCFlashMemState;

/* Forward declarations for peripheral devices */
typedef struct TCStmState TCStmState;
typedef struct TCIrState TCIrState;
typedef struct TCScuState TCScuState;
typedef struct TCWdtState TCWdtState;
typedef struct TCAsclinState TCAsclinState;

typedef struct TC4xSoCState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    TriCoreCPU cpu;

    /* Local memory aliases */
    MemoryRegion dsprX;
    MemoryRegion psprX;

    /* Per-CPU memory - TC4xx supports up to 6 cores */
    TC4xSoCCPUMemState cpu0mem;
    TC4xSoCCPUMemState cpu1mem;
    TC4xSoCCPUMemState cpu2mem;
    TC4xSoCCPUMemState cpu3mem;
    TC4xSoCCPUMemState cpu4mem;
    TC4xSoCCPUMemState cpu5mem;

    /* Flash memory */
    TC4xSoCFlashMemState flashmem;

    /* Peripherals */
    TCStmState *stm[6];       /* System Timer Modules (one per core) */
    TCIrState *ir;            /* Interrupt Router */
    TCScuState *scu;          /* System Control Unit */
    TCWdtState *wdt_cpu[6];   /* CPU Watchdogs */
    TCWdtState *wdt_safety;   /* Safety Watchdog */
    TCAsclinState *asclin[4]; /* ASCLIN UART modules */
} TC4xSoCState;

typedef struct MemmapEntry {
    hwaddr base;
    hwaddr size;
} MemmapEntry;

typedef struct TC4xSoCClass {
    DeviceClass parent_class;

    const char *name;
    const char *cpu_type;
    const MemmapEntry *memmap;
    uint32_t num_cpus;
} TC4xSoCClass;

/* TC4x Memory Map indices */
enum {
    /* CPU5 Local Memory (highest numbered core) */
    TC4X_DSPR5,
    TC4X_DCACHE5,
    TC4X_PSPR5,
    TC4X_PCACHE5,
    /* CPU4 Local Memory */
    TC4X_DSPR4,
    TC4X_DCACHE4,
    TC4X_PSPR4,
    TC4X_PCACHE4,
    /* CPU3 Local Memory */
    TC4X_DSPR3,
    TC4X_DCACHE3,
    TC4X_PSPR3,
    TC4X_PCACHE3,
    /* CPU2 Local Memory */
    TC4X_DSPR2,
    TC4X_DCACHE2,
    TC4X_PSPR2,
    TC4X_PCACHE2,
    /* CPU1 Local Memory */
    TC4X_DSPR1,
    TC4X_DCACHE1,
    TC4X_PSPR1,
    TC4X_PCACHE1,
    /* CPU0 Local Memory */
    TC4X_DSPR0,
    TC4X_DCACHE0,
    TC4X_PSPR0,
    TC4X_PCACHE0,
    /* Program Flash - Cached segment (0x8xxxxxxx) */
    TC4X_PFLASH0_C,
    TC4X_PFLASH1_C,
    TC4X_PFLASH2_C,
    TC4X_BROM_C,
    TC4X_LMURAM_C,
    TC4X_EMEM_C,
    /* Program Flash - Uncached segment (0xAxxxxxxx) */
    TC4X_PFLASH0_U,
    TC4X_PFLASH1_U,
    TC4X_PFLASH2_U,
    TC4X_DFLASH0,
    TC4X_DFLASH1,
    TC4X_BROM_U,
    TC4X_LMURAM_U,
    TC4X_EMEM_U,
    /* Local addressing windows */
    TC4X_PSPRX,
    TC4X_DSPRX,
    /* Peripheral base */
    TC4X_PERIPH_BASE,
};

/* Peripheral register addresses (offset from 0xF0000000) */
#define TC4X_STM0_BASE      0xF0001000
#define TC4X_STM1_BASE      0xF0001100
#define TC4X_STM2_BASE      0xF0001200
#define TC4X_STM3_BASE      0xF0001300
#define TC4X_STM4_BASE      0xF0001400
#define TC4X_STM5_BASE      0xF0001500
#define TC4X_ASCLIN0_BASE   0xF0000600
#define TC4X_ASCLIN1_BASE   0xF0000700
#define TC4X_ASCLIN2_BASE   0xF0000800
#define TC4X_ASCLIN3_BASE   0xF0000900
#define TC4X_SCU_BASE       0xF0036000
#define TC4X_IR_BASE        0xF0038000
#define TC4X_WDT_CPU0_BASE  0xF0036100
#define TC4X_WDT_CPU1_BASE  0xF0036104
#define TC4X_WDT_CPU2_BASE  0xF0036108
#define TC4X_WDT_CPU3_BASE  0xF003610C
#define TC4X_WDT_CPU4_BASE  0xF0036110
#define TC4X_WDT_CPU5_BASE  0xF0036114
#define TC4X_WDT_SAFETY_BASE 0xF00360F0

#endif /* TC4X_SOC_H */

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

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/sysbus.h"
#include "hw/loader.h"
#include "qemu/units.h"
#include "hw/misc/unimp.h"
#include "hw/tricore/tc4x_soc.h"

/*
 * TC4D7 Memory Map (high-end TC4xx variant)
 * Based on Infineon TC4Dx User Manual
 */
static const MemmapEntry tc4x_soc_memmap[] = {
    /* CPU5 Local Memory - 0x10000000 segment */
    [TC4X_DSPR5]     = { 0x10000000,            512 * KiB },
    [TC4X_DCACHE5]   = { 0x10080000,             16 * KiB },
    [TC4X_PSPR5]     = { 0x10100000,             64 * KiB },
    [TC4X_PCACHE5]   = { 0x10110000,             32 * KiB },
    /* CPU4 Local Memory - 0x20000000 segment */
    [TC4X_DSPR4]     = { 0x20000000,            512 * KiB },
    [TC4X_DCACHE4]   = { 0x20080000,             16 * KiB },
    [TC4X_PSPR4]     = { 0x20100000,             64 * KiB },
    [TC4X_PCACHE4]   = { 0x20110000,             32 * KiB },
    /* CPU3 Local Memory - 0x30000000 segment */
    [TC4X_DSPR3]     = { 0x30000000,            512 * KiB },
    [TC4X_DCACHE3]   = { 0x30080000,             16 * KiB },
    [TC4X_PSPR3]     = { 0x30100000,             64 * KiB },
    [TC4X_PCACHE3]   = { 0x30110000,             32 * KiB },
    /* CPU2 Local Memory - 0x50000000 segment */
    [TC4X_DSPR2]     = { 0x50000000,            512 * KiB },
    [TC4X_DCACHE2]   = { 0x50080000,             16 * KiB },
    [TC4X_PSPR2]     = { 0x50100000,             64 * KiB },
    [TC4X_PCACHE2]   = { 0x50110000,             32 * KiB },
    /* CPU1 Local Memory - 0x60000000 segment */
    [TC4X_DSPR1]     = { 0x60000000,            512 * KiB },
    [TC4X_DCACHE1]   = { 0x60080000,             16 * KiB },
    [TC4X_PSPR1]     = { 0x60100000,             64 * KiB },
    [TC4X_PCACHE1]   = { 0x60110000,             32 * KiB },
    /* CPU0 Local Memory - 0x70000000 segment */
    [TC4X_DSPR0]     = { 0x70000000,            512 * KiB },
    [TC4X_DCACHE0]   = { 0x70080000,             16 * KiB },
    [TC4X_PSPR0]     = { 0x70100000,             64 * KiB },
    [TC4X_PCACHE0]   = { 0x70110000,             32 * KiB },
    /* Program Flash - Cached (0x80000000) */
    [TC4X_PFLASH0_C] = { 0x80000000,              8 * MiB },
    [TC4X_PFLASH1_C] = { 0x80800000,              8 * MiB },
    [TC4X_PFLASH2_C] = { 0x81000000,              8 * MiB },
    [TC4X_BROM_C]    = { 0x8FFF8000,             32 * KiB },
    [TC4X_LMURAM_C]  = { 0x90000000,              1 * MiB },
    [TC4X_EMEM_C]    = { 0x99000000,              4 * MiB },
    /* Program Flash - Uncached (0xA0000000) */
    [TC4X_PFLASH0_U] = { 0xA0000000,                  0x0 },  /* Alias */
    [TC4X_PFLASH1_U] = { 0xA0800000,                  0x0 },  /* Alias */
    [TC4X_PFLASH2_U] = { 0xA1000000,                  0x0 },  /* Alias */
    [TC4X_DFLASH0]   = { 0xAF000000,              2 * MiB },
    [TC4X_DFLASH1]   = { 0xAF400000,            128 * KiB },
    [TC4X_BROM_U]    = { 0xAFFF8000,                  0x0 },  /* Alias */
    [TC4X_LMURAM_U]  = { 0xB0000000,                  0x0 },  /* Alias */
    [TC4X_EMEM_U]    = { 0xB9000000,                  0x0 },  /* Alias */
    /* Local addressing windows */
    [TC4X_PSPRX]     = { 0xC0000000,                  0x0 },  /* Alias to CPU0 */
    [TC4X_DSPRX]     = { 0xD0000000,                  0x0 },  /* Alias to CPU0 */
    /* Peripheral base */
    [TC4X_PERIPH_BASE] = { 0xF0000000,           16 * MiB },
};

/*
 * Initialize ROM region and map it into the memory map
 */
static void make_rom(MemoryRegion *mr, const char *name,
                     hwaddr base, hwaddr size)
{
    memory_region_init_rom(mr, NULL, name, size, &error_fatal);
    memory_region_add_subregion(get_system_memory(), base, mr);
}

/*
 * Initialize RAM region and map it into the memory map
 */
static void make_ram(MemoryRegion *mr, const char *name,
                     hwaddr base, hwaddr size)
{
    memory_region_init_ram(mr, NULL, name, size, &error_fatal);
    memory_region_add_subregion(get_system_memory(), base, mr);
}

/*
 * Create an alias of an entire original MemoryRegion @orig
 * located at @base in the memory map.
 */
static void make_alias(MemoryRegion *mr, const char *name,
                       MemoryRegion *orig, hwaddr base)
{
    memory_region_init_alias(mr, NULL, name, orig, 0,
                             memory_region_size(orig));
    memory_region_add_subregion(get_system_memory(), base, mr);
}

/*
 * Initialize Context Save Area (CSA) linked list in DSPR memory.
 * This is critical for FreeRTOS/Zephyr/AUTOSAR context switching.
 *
 * CSA pool: 256 contexts * 64 bytes = 16KB
 * Located at end of DSPR0 (0x70000000 + 512KB - 16KB = 0x7007C000)
 */
static void tc4x_init_csa(TC4xSoCState *s, const MemmapEntry *memmap)
{
    AddressSpace *cpu_as = &address_space_memory;
    hwaddr csa_base = memmap[TC4X_DSPR0].base + memmap[TC4X_DSPR0].size - (16 * KiB);
    uint32_t csa_count = 256;
    uint32_t i;
    uint32_t fcx;

    /*
     * Initialize CSA linked list
     * Each CSA is 64 bytes (16 words)
     * First word contains link to next CSA
     */
    for (i = 0; i < csa_count - 1; i++) {
        hwaddr csa_addr = csa_base + (i * 64);
        hwaddr next_addr = csa_base + ((i + 1) * 64);
        /* Link word format: [19:16] = segment, [15:0] = offset >> 6 */
        uint32_t link = ((next_addr >> 28) << 16) | ((next_addr >> 6) & 0xFFFF);
        address_space_stl(cpu_as, csa_addr, link,
                          MEMTXATTRS_UNSPECIFIED, NULL);
    }

    /* Last CSA points to NULL */
    hwaddr last_csa = csa_base + ((csa_count - 1) * 64);
    address_space_stl(cpu_as, last_csa, 0, MEMTXATTRS_UNSPECIFIED, NULL);

    /* Set FCX to point to first free CSA */
    fcx = ((csa_base >> 28) << 16) | ((csa_base >> 6) & 0xFFFF);
    s->cpu.env.FCX = fcx;

    /* Set LCX to point to last CSA (for underflow detection) */
    s->cpu.env.LCX = ((last_csa >> 28) << 16) | ((last_csa >> 6) & 0xFFFF);

    /* Initialize Interrupt Stack Pointer (ISP) at end of DSPR0 before CSA */
    s->cpu.env.ISP = csa_base - 0x10;

    /* Initialize Base Interrupt Vector (BIV) and Base Trap Vector (BTV) */
    s->cpu.env.BIV = memmap[TC4X_PFLASH0_C].base;  /* Default to flash start */
    s->cpu.env.BTV = memmap[TC4X_PFLASH0_C].base + 0x100;
}

static void tc4x_soc_init_memory_mapping(DeviceState *dev_soc)
{
    TC4xSoCState *s = TC4X_SOC(dev_soc);
    TC4xSoCClass *sc = TC4X_SOC_GET_CLASS(s);

    /* CPU0 Local Memory */
    make_ram(&s->cpu0mem.dspr, "CPU0.DSPR",
        sc->memmap[TC4X_DSPR0].base, sc->memmap[TC4X_DSPR0].size);
    make_ram(&s->cpu0mem.pspr, "CPU0.PSPR",
        sc->memmap[TC4X_PSPR0].base, sc->memmap[TC4X_PSPR0].size);
    make_ram(&s->cpu0mem.dcache, "CPU0.DCACHE",
        sc->memmap[TC4X_DCACHE0].base, sc->memmap[TC4X_DCACHE0].size);
    make_ram(&s->cpu0mem.pcache, "CPU0.PCACHE",
        sc->memmap[TC4X_PCACHE0].base, sc->memmap[TC4X_PCACHE0].size);

    /* CPU1 Local Memory */
    make_ram(&s->cpu1mem.dspr, "CPU1.DSPR",
        sc->memmap[TC4X_DSPR1].base, sc->memmap[TC4X_DSPR1].size);
    make_ram(&s->cpu1mem.pspr, "CPU1.PSPR",
        sc->memmap[TC4X_PSPR1].base, sc->memmap[TC4X_PSPR1].size);
    make_ram(&s->cpu1mem.dcache, "CPU1.DCACHE",
        sc->memmap[TC4X_DCACHE1].base, sc->memmap[TC4X_DCACHE1].size);
    make_ram(&s->cpu1mem.pcache, "CPU1.PCACHE",
        sc->memmap[TC4X_PCACHE1].base, sc->memmap[TC4X_PCACHE1].size);

    /* CPU2 Local Memory */
    make_ram(&s->cpu2mem.dspr, "CPU2.DSPR",
        sc->memmap[TC4X_DSPR2].base, sc->memmap[TC4X_DSPR2].size);
    make_ram(&s->cpu2mem.pspr, "CPU2.PSPR",
        sc->memmap[TC4X_PSPR2].base, sc->memmap[TC4X_PSPR2].size);
    make_ram(&s->cpu2mem.dcache, "CPU2.DCACHE",
        sc->memmap[TC4X_DCACHE2].base, sc->memmap[TC4X_DCACHE2].size);
    make_ram(&s->cpu2mem.pcache, "CPU2.PCACHE",
        sc->memmap[TC4X_PCACHE2].base, sc->memmap[TC4X_PCACHE2].size);

    /*
     * Local addressing windows (0xC/0xD segments)
     * Currently maps to CPU0 for single-core emulation
     */
    make_alias(&s->psprX, "LOCAL.PSPR", &s->cpu0mem.pspr,
        sc->memmap[TC4X_PSPRX].base);
    make_alias(&s->dsprX, "LOCAL.DSPR", &s->cpu0mem.dspr,
        sc->memmap[TC4X_DSPRX].base);

    /* Program Flash - Cached */
    make_ram(&s->flashmem.pflash0_c, "PF0",
        sc->memmap[TC4X_PFLASH0_C].base, sc->memmap[TC4X_PFLASH0_C].size);
    make_ram(&s->flashmem.pflash1_c, "PF1",
        sc->memmap[TC4X_PFLASH1_C].base, sc->memmap[TC4X_PFLASH1_C].size);
    make_ram(&s->flashmem.pflash2_c, "PF2",
        sc->memmap[TC4X_PFLASH2_C].base, sc->memmap[TC4X_PFLASH2_C].size);

    /* Data Flash */
    make_ram(&s->flashmem.dflash0, "DF0",
        sc->memmap[TC4X_DFLASH0].base, sc->memmap[TC4X_DFLASH0].size);
    make_ram(&s->flashmem.dflash1, "DF1",
        sc->memmap[TC4X_DFLASH1].base, sc->memmap[TC4X_DFLASH1].size);

    /* Boot ROM and LMU RAM */
    make_rom(&s->flashmem.brom_c, "BROM",
        sc->memmap[TC4X_BROM_C].base, sc->memmap[TC4X_BROM_C].size);
    make_ram(&s->flashmem.lmuram_c, "LMURAM",
        sc->memmap[TC4X_LMURAM_C].base, sc->memmap[TC4X_LMURAM_C].size);
    make_ram(&s->flashmem.emem_c, "EMEM",
        sc->memmap[TC4X_EMEM_C].base, sc->memmap[TC4X_EMEM_C].size);

    /* Uncached aliases (0xA/0xB segment) */
    make_alias(&s->flashmem.pflash0_u, "PF0.U", &s->flashmem.pflash0_c,
        sc->memmap[TC4X_PFLASH0_U].base);
    make_alias(&s->flashmem.pflash1_u, "PF1.U", &s->flashmem.pflash1_c,
        sc->memmap[TC4X_PFLASH1_U].base);
    make_alias(&s->flashmem.pflash2_u, "PF2.U", &s->flashmem.pflash2_c,
        sc->memmap[TC4X_PFLASH2_U].base);
    make_alias(&s->flashmem.brom_u, "BROM.U", &s->flashmem.brom_c,
        sc->memmap[TC4X_BROM_U].base);
    make_alias(&s->flashmem.lmuram_u, "LMURAM.U", &s->flashmem.lmuram_c,
        sc->memmap[TC4X_LMURAM_U].base);
    make_alias(&s->flashmem.emem_u, "EMEM.U", &s->flashmem.emem_c,
        sc->memmap[TC4X_EMEM_U].base);
}

static void tc4x_soc_realize(DeviceState *dev_soc, Error **errp)
{
    TC4xSoCState *s = TC4X_SOC(dev_soc);
    TC4xSoCClass *sc = TC4X_SOC_GET_CLASS(s);
    Error *err = NULL;

    /* Realize CPU */
    qdev_realize(DEVICE(&s->cpu), NULL, &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }

    /* Initialize memory mapping */
    tc4x_soc_init_memory_mapping(dev_soc);

    /* Initialize CSA pool and interrupt vectors for RTOS support */
    tc4x_init_csa(s, sc->memmap);

    /*
     * TODO: Initialize peripherals (STM, IR, SCU, WDT, ASCLIN)
     * These will be added in subsequent patches
     */

    /* Create unimplemented device for peripheral space to catch accesses */
    create_unimplemented_device("tc4x-periph", 0xF0000000, 16 * MiB);
}

static void tc4x_soc_init(Object *obj)
{
    TC4xSoCState *s = TC4X_SOC(obj);
    TC4xSoCClass *sc = TC4X_SOC_GET_CLASS(s);

    object_initialize_child(obj, "tc4x", &s->cpu, sc->cpu_type);
}

static void tc4x_soc_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = tc4x_soc_realize;
}

static void tc4d7_soc_class_init(ObjectClass *oc, const void *data)
{
    TC4xSoCClass *sc = TC4X_SOC_CLASS(oc);

    sc->name         = "tc4d7-soc";
    sc->cpu_type     = TRICORE_CPU_TYPE_NAME("tc4x");
    sc->memmap       = tc4x_soc_memmap;
    sc->num_cpus     = 6;  /* TC4D7 has 6 cores */
}

static const TypeInfo tc4x_soc_types[] = {
    {
        .name          = "tc4d7-soc",
        .parent        = TYPE_TC4X_SOC,
        .class_init    = tc4d7_soc_class_init,
    },
    {
        .name          = TYPE_TC4X_SOC,
        .parent        = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(TC4xSoCState),
        .instance_init = tc4x_soc_init,
        .class_size    = sizeof(TC4xSoCClass),
        .class_init    = tc4x_soc_class_init,
        .abstract      = true,
    },
};

DEFINE_TYPES(tc4x_soc_types)

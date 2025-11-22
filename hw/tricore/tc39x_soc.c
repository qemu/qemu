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

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/sysbus.h"
#include "hw/loader.h"
#include "qemu/units.h"
#include "hw/misc/unimp.h"

#include "hw/tricore/tc39x_soc.h"
#include "hw/tricore/triboard.h"

/*
 * TC39x Memory Map
 * Based on TC397 User Manual
 */
const MemmapEntry tc39x_soc_memmap[] = {
    /* CPU0 Local Memory */
    [TC39X_DSPR0]     = { 0x70000000,            240 * KiB },
    [TC39X_PSPR0]     = { 0x70100000,             64 * KiB },
    [TC39X_PCACHE0]   = { 0x70180000,             32 * KiB },
    [TC39X_PTAG0]     = { 0x701C0000,              0x1800 },

    /* CPU1 Local Memory */
    [TC39X_DSPR1]     = { 0x60000000,            240 * KiB },
    [TC39X_PSPR1]     = { 0x60100000,             64 * KiB },
    [TC39X_PCACHE1]   = { 0x60180000,             32 * KiB },
    [TC39X_PTAG1]     = { 0x601C0000,              0x1800 },

    /* CPU2 Local Memory */
    [TC39X_DSPR2]     = { 0x50000000,             96 * KiB },
    [TC39X_PSPR2]     = { 0x50100000,             64 * KiB },
    [TC39X_PCACHE2]   = { 0x50180000,             32 * KiB },
    [TC39X_PTAG2]     = { 0x501C0000,              0x1800 },

    /* Flash Memory (Cached) */
    [TC39X_PFLASH0_C] = { 0x80000000,              3 * MiB },
    [TC39X_PFLASH1_C] = { 0x80300000,              3 * MiB },
    [TC39X_PFLASH2_C] = { 0x80600000,              3 * MiB },
    [TC39X_PFLASH3_C] = { 0x80900000,              3 * MiB },

    /* Flash Memory (Uncached - aliases) */
    [TC39X_PFLASH0_U] = { 0xA0000000,                  0x0 },
    [TC39X_PFLASH1_U] = { 0xA0300000,                  0x0 },
    [TC39X_PFLASH2_U] = { 0xA0600000,                  0x0 },
    [TC39X_PFLASH3_U] = { 0xA0900000,                  0x0 },

    /* Data Flash */
    [TC39X_DFLASH0]   = { 0xAF000000,              2 * MiB },
    [TC39X_DFLASH1]   = { 0xAF400000,            128 * KiB },

    /* Boot ROM */
    [TC39X_BROM_C]    = { 0x8FFF8000,             32 * KiB },
    [TC39X_BROM_U]    = { 0xAFFF8000,                  0x0 },

    /* LMU RAM */
    [TC39X_LMURAM_C]  = { 0x90000000,            768 * KiB },
    [TC39X_LMURAM_U]  = { 0xB0000000,                  0x0 },

    /* DAM */
    [TC39X_DAM0]      = { 0xB00A0000,            128 * KiB },

    /* Local Addressing */
    [TC39X_PSPRX]     = { 0xC0000000,                  0x0 },
    [TC39X_DSPRX]     = { 0xD0000000,                  0x0 },

    /* Peripherals */
    [TC39X_STM0]      = { 0xF0001000,              0x100 },
    [TC39X_STM1]      = { 0xF0001100,              0x100 },
    [TC39X_STM2]      = { 0xF0001200,              0x100 },
    [TC39X_IR]        = { 0xF0038000,             0x4000 },
};

/*
 * Initialize ROM region and map it into memory
 */
static void make_rom(MemoryRegion *mr, const char *name,
                     hwaddr base, hwaddr size)
{
    memory_region_init_rom(mr, NULL, name, size, &error_fatal);
    memory_region_add_subregion(get_system_memory(), base, mr);
}

/*
 * Initialize RAM region and map it into memory
 */
static void make_ram(MemoryRegion *mr, const char *name,
                     hwaddr base, hwaddr size)
{
    memory_region_init_ram(mr, NULL, name, size, &error_fatal);
    memory_region_add_subregion(get_system_memory(), base, mr);
}

/*
 * Create an alias of a MemoryRegion at a different address
 */
static void make_alias(MemoryRegion *mr, const char *name,
                       MemoryRegion *orig, hwaddr base)
{
    memory_region_init_alias(mr, NULL, name, orig, 0,
                             memory_region_size(orig));
    memory_region_add_subregion(get_system_memory(), base, mr);
}

static void tc39x_soc_init_memory_mapping(DeviceState *dev_soc)
{
    TC39xSoCState *s = TC39X_SOC(dev_soc);
    TC39xSoCClass *sc = TC39X_SOC_GET_CLASS(s);

    /* CPU0 Local Memory */
    make_ram(&s->cpu0mem.dspr, "CPU0.DSPR",
        sc->memmap[TC39X_DSPR0].base, sc->memmap[TC39X_DSPR0].size);
    make_ram(&s->cpu0mem.pspr, "CPU0.PSPR",
        sc->memmap[TC39X_PSPR0].base, sc->memmap[TC39X_PSPR0].size);
    make_ram(&s->cpu0mem.pcache, "CPU0.PCACHE",
        sc->memmap[TC39X_PCACHE0].base, sc->memmap[TC39X_PCACHE0].size);
    make_ram(&s->cpu0mem.ptag, "CPU0.PTAG",
        sc->memmap[TC39X_PTAG0].base, sc->memmap[TC39X_PTAG0].size);

    /* CPU1 Local Memory */
    make_ram(&s->cpu1mem.dspr, "CPU1.DSPR",
        sc->memmap[TC39X_DSPR1].base, sc->memmap[TC39X_DSPR1].size);
    make_ram(&s->cpu1mem.pspr, "CPU1.PSPR",
        sc->memmap[TC39X_PSPR1].base, sc->memmap[TC39X_PSPR1].size);
    make_ram(&s->cpu1mem.pcache, "CPU1.PCACHE",
        sc->memmap[TC39X_PCACHE1].base, sc->memmap[TC39X_PCACHE1].size);
    make_ram(&s->cpu1mem.ptag, "CPU1.PTAG",
        sc->memmap[TC39X_PTAG1].base, sc->memmap[TC39X_PTAG1].size);

    /* CPU2 Local Memory */
    make_ram(&s->cpu2mem.dspr, "CPU2.DSPR",
        sc->memmap[TC39X_DSPR2].base, sc->memmap[TC39X_DSPR2].size);
    make_ram(&s->cpu2mem.pspr, "CPU2.PSPR",
        sc->memmap[TC39X_PSPR2].base, sc->memmap[TC39X_PSPR2].size);
    make_ram(&s->cpu2mem.pcache, "CPU2.PCACHE",
        sc->memmap[TC39X_PCACHE2].base, sc->memmap[TC39X_PCACHE2].size);
    make_ram(&s->cpu2mem.ptag, "CPU2.PTAG",
        sc->memmap[TC39X_PTAG2].base, sc->memmap[TC39X_PTAG2].size);

    /*
     * TriCore QEMU executes CPU0 only, so map LOCAL.PSPR/LOCAL.DSPR
     * exclusively onto CPU0's PSPR/DSPR.
     */
    make_alias(&s->psprX, "LOCAL.PSPR", &s->cpu0mem.pspr,
        sc->memmap[TC39X_PSPRX].base);
    make_alias(&s->dsprX, "LOCAL.DSPR", &s->cpu0mem.dspr,
        sc->memmap[TC39X_DSPRX].base);

    /* Program Flash (Cached) */
    make_ram(&s->flashmem.pflash0_c, "PF0",
        sc->memmap[TC39X_PFLASH0_C].base, sc->memmap[TC39X_PFLASH0_C].size);
    make_ram(&s->flashmem.pflash1_c, "PF1",
        sc->memmap[TC39X_PFLASH1_C].base, sc->memmap[TC39X_PFLASH1_C].size);
    make_ram(&s->flashmem.pflash2_c, "PF2",
        sc->memmap[TC39X_PFLASH2_C].base, sc->memmap[TC39X_PFLASH2_C].size);
    make_ram(&s->flashmem.pflash3_c, "PF3",
        sc->memmap[TC39X_PFLASH3_C].base, sc->memmap[TC39X_PFLASH3_C].size);

    /* Program Flash (Uncached - aliases to cached) */
    make_alias(&s->flashmem.pflash0_u, "PF0.U", &s->flashmem.pflash0_c,
        sc->memmap[TC39X_PFLASH0_U].base);
    make_alias(&s->flashmem.pflash1_u, "PF1.U", &s->flashmem.pflash1_c,
        sc->memmap[TC39X_PFLASH1_U].base);
    make_alias(&s->flashmem.pflash2_u, "PF2.U", &s->flashmem.pflash2_c,
        sc->memmap[TC39X_PFLASH2_U].base);
    make_alias(&s->flashmem.pflash3_u, "PF3.U", &s->flashmem.pflash3_c,
        sc->memmap[TC39X_PFLASH3_U].base);

    /* Data Flash */
    make_ram(&s->flashmem.dflash0, "DF0",
        sc->memmap[TC39X_DFLASH0].base, sc->memmap[TC39X_DFLASH0].size);
    make_ram(&s->flashmem.dflash1, "DF1",
        sc->memmap[TC39X_DFLASH1].base, sc->memmap[TC39X_DFLASH1].size);

    /* Boot ROM */
    make_rom(&s->flashmem.brom_c, "BROM",
        sc->memmap[TC39X_BROM_C].base, sc->memmap[TC39X_BROM_C].size);
    make_alias(&s->flashmem.brom_u, "BROM.U", &s->flashmem.brom_c,
        sc->memmap[TC39X_BROM_U].base);

    /* LMU RAM */
    make_ram(&s->flashmem.lmuram_c, "LMURAM",
        sc->memmap[TC39X_LMURAM_C].base, sc->memmap[TC39X_LMURAM_C].size);
    make_alias(&s->flashmem.lmuram_u, "LMURAM.U", &s->flashmem.lmuram_c,
        sc->memmap[TC39X_LMURAM_U].base);

    /* DAM */
    make_ram(&s->flashmem.dam0, "DAM0",
        sc->memmap[TC39X_DAM0].base, sc->memmap[TC39X_DAM0].size);
}

static void tc39x_soc_realize(DeviceState *dev_soc, Error **errp)
{
    TC39xSoCState *s = TC39X_SOC(dev_soc);
    TC39xSoCClass *sc = TC39X_SOC_GET_CLASS(s);
    Error *err = NULL;

    /* Initialize and realize CPU */
    qdev_realize(DEVICE(&s->cpu), NULL, &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }

    /* Initialize memory mapping */
    tc39x_soc_init_memory_mapping(dev_soc);

    /* Realize STM0 */
    object_property_set_link(OBJECT(&s->stm0), "cpu", OBJECT(&s->cpu), &err);
    sysbus_realize(SYS_BUS_DEVICE(&s->stm0), &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->stm0), 0,
                    sc->memmap[TC39X_STM0].base);

    /* Realize STM1 */
    sysbus_realize(SYS_BUS_DEVICE(&s->stm1), &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->stm1), 0,
                    sc->memmap[TC39X_STM1].base);

    /* Realize STM2 */
    sysbus_realize(SYS_BUS_DEVICE(&s->stm2), &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->stm2), 0,
                    sc->memmap[TC39X_STM2].base);

    /* Realize Interrupt Router */
    object_property_set_link(OBJECT(&s->ir), "cpu", OBJECT(&s->cpu), &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    sysbus_realize(SYS_BUS_DEVICE(&s->ir), &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->ir), 0,
                    sc->memmap[TC39X_IR].base);

    /* Connect STM0 interrupts to IR */
    /* STM0 CMP0 typically uses SRC index 4 (STM0SR0) */
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->stm0), 0,
                       qdev_get_gpio_in(DEVICE(&s->ir), 4));
    /* STM0 CMP1 typically uses SRC index 5 (STM0SR1) */
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->stm0), 1,
                       qdev_get_gpio_in(DEVICE(&s->ir), 5));
}

static void tc39x_soc_init(Object *obj)
{
    TC39xSoCState *s = TC39X_SOC(obj);
    TC39xSoCClass *sc = TC39X_SOC_GET_CLASS(s);

    object_initialize_child(obj, "cpu", &s->cpu, sc->cpu_type);

    /* Initialize STM timers */
    object_initialize_child(obj, "stm0", &s->stm0, TYPE_TC_STM);
    object_initialize_child(obj, "stm1", &s->stm1, TYPE_TC_STM);
    object_initialize_child(obj, "stm2", &s->stm2, TYPE_TC_STM);

    /* Initialize Interrupt Router */
    object_initialize_child(obj, "ir", &s->ir, TYPE_TC_IR);
}

static void tc39x_soc_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = tc39x_soc_realize;
}

static void tc397_soc_class_init(ObjectClass *oc, const void *data)
{
    TC39xSoCClass *sc = TC39X_SOC_CLASS(oc);

    sc->name         = "tc397-soc";
    sc->cpu_type     = TRICORE_CPU_TYPE_NAME("tc39x");
    sc->memmap       = tc39x_soc_memmap;
    sc->num_cpus     = 1;  /* Single core emulation for now */
}

static const TypeInfo tc39x_soc_types[] = {
    {
        .name          = "tc397-soc",
        .parent        = TYPE_TC39X_SOC,
        .class_init    = tc397_soc_class_init,
    }, {
        .name          = TYPE_TC39X_SOC,
        .parent        = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(TC39xSoCState),
        .instance_init = tc39x_soc_init,
        .class_size    = sizeof(TC39xSoCClass),
        .class_init    = tc39x_soc_class_init,
        .abstract      = true,
    },
};

DEFINE_TYPES(tc39x_soc_types)

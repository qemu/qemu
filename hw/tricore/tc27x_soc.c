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

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/sysbus.h"
#include "hw/loader.h"
#include "qemu/units.h"
#include "hw/misc/unimp.h"

#include "hw/tricore/tc27x_soc.h"
#include "hw/tricore/triboard.h"

const MemmapEntry tc27x_soc_memmap[] = {
    [TC27XD_DSPR2]     = { 0x50000000,            120 * KiB },
    [TC27XD_DCACHE2]   = { 0x5001E000,              8 * KiB },
    [TC27XD_DTAG2]     = { 0x500C0000,                0xC00 },
    [TC27XD_PSPR2]     = { 0x50100000,             32 * KiB },
    [TC27XD_PCACHE2]   = { 0x50108000,             16 * KiB },
    [TC27XD_PTAG2]     = { 0x501C0000,               0x1800 },
    [TC27XD_DSPR1]     = { 0x60000000,            120 * KiB },
    [TC27XD_DCACHE1]   = { 0x6001E000,              8 * KiB },
    [TC27XD_DTAG1]     = { 0x600C0000,                0xC00 },
    [TC27XD_PSPR1]     = { 0x60100000,             32 * KiB },
    [TC27XD_PCACHE1]   = { 0x60108000,             16 * KiB },
    [TC27XD_PTAG1]     = { 0x601C0000,               0x1800 },
    [TC27XD_DSPR0]     = { 0x70000000,            112 * KiB },
    [TC27XD_PSPR0]     = { 0x70100000,             24 * KiB },
    [TC27XD_PCACHE0]   = { 0x70106000,              8 * KiB },
    [TC27XD_PTAG0]     = { 0x701C0000,                0xC00 },
    [TC27XD_PFLASH0_C] = { 0x80000000,              2 * MiB },
    [TC27XD_PFLASH1_C] = { 0x80200000,              2 * MiB },
    [TC27XD_OLDA_C]    = { 0x8FE70000,             32 * KiB },
    [TC27XD_BROM_C]    = { 0x8FFF8000,             32 * KiB },
    [TC27XD_LMURAM_C]  = { 0x90000000,             32 * KiB },
    [TC27XD_EMEM_C]    = { 0x9F000000,              1 * MiB },
    [TC27XD_PFLASH0_U] = { 0xA0000000,                  0x0 },
    [TC27XD_PFLASH1_U] = { 0xA0200000,                  0x0 },
    [TC27XD_DFLASH0]   = { 0xAF000000,   1 * MiB + 16 * KiB },
    [TC27XD_DFLASH1]   = { 0xAF110000,             64 * KiB },
    [TC27XD_OLDA_U]    = { 0xAFE70000,                  0x0 },
    [TC27XD_BROM_U]    = { 0xAFFF8000,                  0x0 },
    [TC27XD_LMURAM_U]  = { 0xB0000000,                  0x0 },
    [TC27XD_EMEM_U]    = { 0xBF000000,                  0x0 },
    [TC27XD_PSPRX]     = { 0xC0000000,                  0x0 },
    [TC27XD_DSPRX]     = { 0xD0000000,                  0x0 },
};

/*
 * Initialize the auxiliary ROM region @mr and map it into
 * the memory map at @base.
 */
static void make_rom(MemoryRegion *mr, const char *name,
                     hwaddr base, hwaddr size)
{
    memory_region_init_rom(mr, NULL, name, size, &error_fatal);
    memory_region_add_subregion(get_system_memory(), base, mr);
}

/*
 * Initialize the auxiliary RAM region @mr and map it into
 * the memory map at @base.
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

static void tc27x_soc_init_memory_mapping(DeviceState *dev_soc)
{
    TC27XSoCState *s = TC27X_SOC(dev_soc);
    TC27XSoCClass *sc = TC27X_SOC_GET_CLASS(s);

    make_ram(&s->cpu0mem.dspr, "CPU0.DSPR",
        sc->memmap[TC27XD_DSPR0].base, sc->memmap[TC27XD_DSPR0].size);
    make_ram(&s->cpu0mem.pspr, "CPU0.PSPR",
        sc->memmap[TC27XD_PSPR0].base, sc->memmap[TC27XD_PSPR0].size);
    make_ram(&s->cpu1mem.dspr, "CPU1.DSPR",
        sc->memmap[TC27XD_DSPR1].base, sc->memmap[TC27XD_DSPR1].size);
    make_ram(&s->cpu1mem.pspr, "CPU1.PSPR",
        sc->memmap[TC27XD_PSPR1].base, sc->memmap[TC27XD_PSPR1].size);
    make_ram(&s->cpu2mem.dspr, "CPU2.DSPR",
        sc->memmap[TC27XD_DSPR2].base, sc->memmap[TC27XD_DSPR2].size);
    make_ram(&s->cpu2mem.pspr, "CPU2.PSPR",
        sc->memmap[TC27XD_PSPR2].base, sc->memmap[TC27XD_PSPR2].size);

    /* TODO: Control Cache mapping with Memory Test Unit (MTU) */
    make_ram(&s->cpu2mem.dcache, "CPU2.DCACHE",
        sc->memmap[TC27XD_DCACHE2].base, sc->memmap[TC27XD_DCACHE2].size);
    make_ram(&s->cpu2mem.dtag,   "CPU2.DTAG",
        sc->memmap[TC27XD_DTAG2].base, sc->memmap[TC27XD_DTAG2].size);
    make_ram(&s->cpu2mem.pcache, "CPU2.PCACHE",
        sc->memmap[TC27XD_PCACHE2].base, sc->memmap[TC27XD_PCACHE2].size);
    make_ram(&s->cpu2mem.ptag,   "CPU2.PTAG",
        sc->memmap[TC27XD_PTAG2].base, sc->memmap[TC27XD_PTAG2].size);

    make_ram(&s->cpu1mem.dcache, "CPU1.DCACHE",
        sc->memmap[TC27XD_DCACHE1].base, sc->memmap[TC27XD_DCACHE1].size);
    make_ram(&s->cpu1mem.dtag,   "CPU1.DTAG",
        sc->memmap[TC27XD_DTAG1].base, sc->memmap[TC27XD_DTAG1].size);
    make_ram(&s->cpu1mem.pcache, "CPU1.PCACHE",
        sc->memmap[TC27XD_PCACHE1].base, sc->memmap[TC27XD_PCACHE1].size);
    make_ram(&s->cpu1mem.ptag,   "CPU1.PTAG",
        sc->memmap[TC27XD_PTAG1].base, sc->memmap[TC27XD_PTAG1].size);

    make_ram(&s->cpu0mem.pcache, "CPU0.PCACHE",
        sc->memmap[TC27XD_PCACHE0].base, sc->memmap[TC27XD_PCACHE0].size);
    make_ram(&s->cpu0mem.ptag,   "CPU0.PTAG",
        sc->memmap[TC27XD_PTAG0].base, sc->memmap[TC27XD_PTAG0].size);

    /*
     * TriCore QEMU executes CPU0 only, thus it is sufficient to map
     * LOCAL.PSPR/LOCAL.DSPR exclusively onto PSPR0/DSPR0.
     */
    make_alias(&s->psprX, "LOCAL.PSPR", &s->cpu0mem.pspr,
        sc->memmap[TC27XD_PSPRX].base);
    make_alias(&s->dsprX, "LOCAL.DSPR", &s->cpu0mem.dspr,
        sc->memmap[TC27XD_DSPRX].base);

    make_ram(&s->flashmem.pflash0_c, "PF0",
        sc->memmap[TC27XD_PFLASH0_C].base, sc->memmap[TC27XD_PFLASH0_C].size);
    make_ram(&s->flashmem.pflash1_c, "PF1",
        sc->memmap[TC27XD_PFLASH1_C].base, sc->memmap[TC27XD_PFLASH1_C].size);
    make_ram(&s->flashmem.dflash0,   "DF0",
        sc->memmap[TC27XD_DFLASH0].base, sc->memmap[TC27XD_DFLASH0].size);
    make_ram(&s->flashmem.dflash1,   "DF1",
        sc->memmap[TC27XD_DFLASH1].base, sc->memmap[TC27XD_DFLASH1].size);
    make_ram(&s->flashmem.olda_c,    "OLDA",
        sc->memmap[TC27XD_OLDA_C].base, sc->memmap[TC27XD_OLDA_C].size);
    make_rom(&s->flashmem.brom_c,    "BROM",
        sc->memmap[TC27XD_BROM_C].base, sc->memmap[TC27XD_BROM_C].size);
    make_ram(&s->flashmem.lmuram_c,  "LMURAM",
        sc->memmap[TC27XD_LMURAM_C].base, sc->memmap[TC27XD_LMURAM_C].size);
    make_ram(&s->flashmem.emem_c,    "EMEM",
        sc->memmap[TC27XD_EMEM_C].base, sc->memmap[TC27XD_EMEM_C].size);

    make_alias(&s->flashmem.pflash0_u, "PF0.U",    &s->flashmem.pflash0_c,
        sc->memmap[TC27XD_PFLASH0_U].base);
    make_alias(&s->flashmem.pflash1_u, "PF1.U",    &s->flashmem.pflash1_c,
        sc->memmap[TC27XD_PFLASH1_U].base);
    make_alias(&s->flashmem.olda_u,    "OLDA.U",   &s->flashmem.olda_c,
        sc->memmap[TC27XD_OLDA_U].base);
    make_alias(&s->flashmem.brom_u,    "BROM.U",   &s->flashmem.brom_c,
        sc->memmap[TC27XD_BROM_U].base);
    make_alias(&s->flashmem.lmuram_u,  "LMURAM.U", &s->flashmem.lmuram_c,
        sc->memmap[TC27XD_LMURAM_U].base);
    make_alias(&s->flashmem.emem_u,    "EMEM.U",   &s->flashmem.emem_c,
        sc->memmap[TC27XD_EMEM_U].base);
}

static void tc27x_soc_realize(DeviceState *dev_soc, Error **errp)
{
    TC27XSoCState *s = TC27X_SOC(dev_soc);
    Error *err = NULL;

    qdev_realize(DEVICE(&s->cpu), NULL, &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }

    tc27x_soc_init_memory_mapping(dev_soc);
}

static void tc27x_soc_init(Object *obj)
{
    TC27XSoCState *s = TC27X_SOC(obj);
    TC27XSoCClass *sc = TC27X_SOC_GET_CLASS(s);

    object_initialize_child(obj, "tc27x", &s->cpu, sc->cpu_type);
}

static Property tc27x_soc_properties[] = {
    DEFINE_PROP_END_OF_LIST(),
};

static void tc27x_soc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = tc27x_soc_realize;
    device_class_set_props(dc, tc27x_soc_properties);
}

static void tc277d_soc_class_init(ObjectClass *oc, void *data)
{
    TC27XSoCClass *sc = TC27X_SOC_CLASS(oc);

    sc->name         = "tc277d-soc";
    sc->cpu_type     = TRICORE_CPU_TYPE_NAME("tc27x");
    sc->memmap       = tc27x_soc_memmap;
    sc->num_cpus     = 1;
}

static const TypeInfo tc27x_soc_types[] = {
    {
        .name          = "tc277d-soc",
        .parent        = TYPE_TC27X_SOC,
        .class_init    = tc277d_soc_class_init,
    }, {
        .name          = TYPE_TC27X_SOC,
        .parent        = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(TC27XSoCState),
        .instance_init = tc27x_soc_init,
        .class_size    = sizeof(TC27XSoCClass),
        .class_init    = tc27x_soc_class_init,
        .abstract      = true,
    },
};

DEFINE_TYPES(tc27x_soc_types)

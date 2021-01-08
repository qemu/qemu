/*
 * Nuvoton NPCM7xx Memory Controller stub
 *
 * Copyright 2020 Google LLC
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

#include "qemu/osdep.h"

#include "hw/mem/npcm7xx_mc.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/units.h"

#define NPCM7XX_MC_REGS_SIZE (4 * KiB)

static uint64_t npcm7xx_mc_read(void *opaque, hwaddr addr, unsigned int size)
{
    /*
     * If bits 8..11 @ offset 0 are not zero, the boot block thinks the memory
     * controller has already been initialized and will skip DDR training.
     */
    if (addr == 0) {
        return 0x100;
    }

    qemu_log_mask(LOG_UNIMP, "%s: mostly unimplemented\n", __func__);

    return 0;
}

static void npcm7xx_mc_write(void *opaque, hwaddr addr, uint64_t v,
                             unsigned int size)
{
    qemu_log_mask(LOG_UNIMP, "%s: mostly unimplemented\n", __func__);
}

static const MemoryRegionOps npcm7xx_mc_ops = {
    .read = npcm7xx_mc_read,
    .write = npcm7xx_mc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
};

static void npcm7xx_mc_realize(DeviceState *dev, Error **errp)
{
    NPCM7xxMCState *s = NPCM7XX_MC(dev);

    memory_region_init_io(&s->mmio, OBJECT(s), &npcm7xx_mc_ops, s, "regs",
                          NPCM7XX_MC_REGS_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->mmio);
}

static void npcm7xx_mc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "NPCM7xx Memory Controller stub";
    dc->realize = npcm7xx_mc_realize;
}

static const TypeInfo npcm7xx_mc_types[] = {
    {
        .name = TYPE_NPCM7XX_MC,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(NPCM7xxMCState),
        .class_init = npcm7xx_mc_class_init,
    },
};
DEFINE_TYPES(npcm7xx_mc_types);

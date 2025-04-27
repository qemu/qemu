/*
 * Nuvoton NPCM7xx Random Number Generator.
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

#include "hw/misc/npcm7xx_rng.h"
#include "migration/vmstate.h"
#include "qemu/bitops.h"
#include "qemu/guest-random.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/units.h"

#include "trace.h"

#define NPCM7XX_RNG_REGS_SIZE   (4 * KiB)

#define NPCM7XX_RNGCS           (0x00)
#define NPCM7XX_RNGCS_CLKP(rv)      extract32(rv, 2, 4)
#define NPCM7XX_RNGCS_DVALID        BIT(1)
#define NPCM7XX_RNGCS_RNGE          BIT(0)

#define NPCM7XX_RNGD            (0x04)
#define NPCM7XX_RNGMODE         (0x08)
#define NPCM7XX_RNGMODE_NORMAL      (0x02)

static bool npcm7xx_rng_is_enabled(NPCM7xxRNGState *s)
{
    return (s->rngcs & NPCM7XX_RNGCS_RNGE) &&
        (s->rngmode == NPCM7XX_RNGMODE_NORMAL);
}

static uint64_t npcm7xx_rng_read(void *opaque, hwaddr offset, unsigned size)
{
    NPCM7xxRNGState *s = opaque;
    uint64_t value = 0;

    switch (offset) {
    case NPCM7XX_RNGCS:
        /*
         * If the RNG is enabled, but we don't have any valid random data, try
         * obtaining some and update the DVALID bit accordingly.
         */
        if (!npcm7xx_rng_is_enabled(s)) {
            s->rngcs &= ~NPCM7XX_RNGCS_DVALID;
        } else if (!(s->rngcs & NPCM7XX_RNGCS_DVALID)) {
            uint8_t byte = 0;

            if (qemu_guest_getrandom(&byte, sizeof(byte), NULL) == 0) {
                s->rngd = byte;
                s->rngcs |= NPCM7XX_RNGCS_DVALID;
            }
        }
        value = s->rngcs;
        break;
    case NPCM7XX_RNGD:
        if (npcm7xx_rng_is_enabled(s) && s->rngcs & NPCM7XX_RNGCS_DVALID) {
            s->rngcs &= ~NPCM7XX_RNGCS_DVALID;
            value = s->rngd;
            s->rngd = 0;
        }
        break;
    case NPCM7XX_RNGMODE:
        value = s->rngmode;
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: read from invalid offset 0x%" HWADDR_PRIx "\n",
                      DEVICE(s)->canonical_path, offset);
        break;
    }

    trace_npcm7xx_rng_read(offset, value, size);

    return value;
}

static void npcm7xx_rng_write(void *opaque, hwaddr offset, uint64_t value,
                              unsigned size)
{
    NPCM7xxRNGState *s = opaque;

    trace_npcm7xx_rng_write(offset, value, size);

    switch (offset) {
    case NPCM7XX_RNGCS:
        s->rngcs &= NPCM7XX_RNGCS_DVALID;
        s->rngcs |= value & ~NPCM7XX_RNGCS_DVALID;
        break;
    case NPCM7XX_RNGD:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: write to read-only register @ 0x%" HWADDR_PRIx "\n",
                      DEVICE(s)->canonical_path, offset);
        break;
    case NPCM7XX_RNGMODE:
        s->rngmode = value;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: write to invalid offset 0x%" HWADDR_PRIx "\n",
                      DEVICE(s)->canonical_path, offset);
        break;
    }
}

static const MemoryRegionOps npcm7xx_rng_ops = {
    .read = npcm7xx_rng_read,
    .write = npcm7xx_rng_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
        .unaligned = false,
    },
};

static void npcm7xx_rng_enter_reset(Object *obj, ResetType type)
{
    NPCM7xxRNGState *s = NPCM7XX_RNG(obj);

    s->rngcs = 0;
    s->rngd = 0;
    s->rngmode = 0;
}

static void npcm7xx_rng_init(Object *obj)
{
    NPCM7xxRNGState *s = NPCM7XX_RNG(obj);

    memory_region_init_io(&s->iomem, obj, &npcm7xx_rng_ops, s, "regs",
                          NPCM7XX_RNG_REGS_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->iomem);
}

static const VMStateDescription vmstate_npcm7xx_rng = {
    .name = "npcm7xx-rng",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8(rngcs, NPCM7xxRNGState),
        VMSTATE_UINT8(rngd, NPCM7xxRNGState),
        VMSTATE_UINT8(rngmode, NPCM7xxRNGState),
        VMSTATE_END_OF_LIST(),
    },
};

static void npcm7xx_rng_class_init(ObjectClass *klass, const void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "NPCM7xx Random Number Generator";
    dc->vmsd = &vmstate_npcm7xx_rng;
    rc->phases.enter = npcm7xx_rng_enter_reset;
}

static const TypeInfo npcm7xx_rng_types[] = {
    {
        .name = TYPE_NPCM7XX_RNG,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(NPCM7xxRNGState),
        .class_init = npcm7xx_rng_class_init,
        .instance_init = npcm7xx_rng_init,
    },
};
DEFINE_TYPES(npcm7xx_rng_types);

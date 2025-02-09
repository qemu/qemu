/*
 * Nuvoton NPCM7xx/8xx System Global Control Registers.
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

#include "hw/misc/npcm_gcr.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/cutils.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/units.h"

#include "trace.h"

#define NPCM7XX_GCR_MIN_DRAM_SIZE   (128 * MiB)
#define NPCM7XX_GCR_MAX_DRAM_SIZE   (2 * GiB)

enum NPCM7xxGCRRegisters {
    NPCM7XX_GCR_PDID,
    NPCM7XX_GCR_PWRON,
    NPCM7XX_GCR_MFSEL1          = 0x0c / sizeof(uint32_t),
    NPCM7XX_GCR_MFSEL2,
    NPCM7XX_GCR_MISCPE,
    NPCM7XX_GCR_SPSWC           = 0x038 / sizeof(uint32_t),
    NPCM7XX_GCR_INTCR,
    NPCM7XX_GCR_INTSR,
    NPCM7XX_GCR_HIFCR           = 0x050 / sizeof(uint32_t),
    NPCM7XX_GCR_INTCR2          = 0x060 / sizeof(uint32_t),
    NPCM7XX_GCR_MFSEL3,
    NPCM7XX_GCR_SRCNT,
    NPCM7XX_GCR_RESSR,
    NPCM7XX_GCR_RLOCKR1,
    NPCM7XX_GCR_FLOCKR1,
    NPCM7XX_GCR_DSCNT,
    NPCM7XX_GCR_MDLR,
    NPCM7XX_GCR_SCRPAD3,
    NPCM7XX_GCR_SCRPAD2,
    NPCM7XX_GCR_DAVCLVLR        = 0x098 / sizeof(uint32_t),
    NPCM7XX_GCR_INTCR3,
    NPCM7XX_GCR_VSINTR          = 0x0ac / sizeof(uint32_t),
    NPCM7XX_GCR_MFSEL4,
    NPCM7XX_GCR_CPBPNTR         = 0x0c4 / sizeof(uint32_t),
    NPCM7XX_GCR_CPCTL           = 0x0d0 / sizeof(uint32_t),
    NPCM7XX_GCR_CP2BST,
    NPCM7XX_GCR_B2CPNT,
    NPCM7XX_GCR_CPPCTL,
    NPCM7XX_GCR_I2CSEGSEL,
    NPCM7XX_GCR_I2CSEGCTL,
    NPCM7XX_GCR_VSRCR,
    NPCM7XX_GCR_MLOCKR,
    NPCM7XX_GCR_SCRPAD          = 0x013c / sizeof(uint32_t),
    NPCM7XX_GCR_USB1PHYCTL,
    NPCM7XX_GCR_USB2PHYCTL,
};

static const uint32_t npcm7xx_cold_reset_values[NPCM7XX_GCR_NR_REGS] = {
    [NPCM7XX_GCR_PDID]          = 0x04a92750,   /* Poleg A1 */
    [NPCM7XX_GCR_MISCPE]        = 0x0000ffff,
    [NPCM7XX_GCR_SPSWC]         = 0x00000003,
    [NPCM7XX_GCR_INTCR]         = 0x0000035e,
    [NPCM7XX_GCR_HIFCR]         = 0x0000004e,
    [NPCM7XX_GCR_INTCR2]        = (1U << 19),   /* DDR initialized */
    [NPCM7XX_GCR_RESSR]         = 0x80000000,
    [NPCM7XX_GCR_DSCNT]         = 0x000000c0,
    [NPCM7XX_GCR_DAVCLVLR]      = 0x5a00f3cf,
    [NPCM7XX_GCR_SCRPAD]        = 0x00000008,
    [NPCM7XX_GCR_USB1PHYCTL]    = 0x034730e4,
    [NPCM7XX_GCR_USB2PHYCTL]    = 0x034730e4,
};

enum NPCM8xxGCRRegisters {
    NPCM8XX_GCR_PDID,
    NPCM8XX_GCR_PWRON,
    NPCM8XX_GCR_MISCPE          = 0x014 / sizeof(uint32_t),
    NPCM8XX_GCR_FLOCKR2         = 0x020 / sizeof(uint32_t),
    NPCM8XX_GCR_FLOCKR3,
    NPCM8XX_GCR_A35_MODE        = 0x034 / sizeof(uint32_t),
    NPCM8XX_GCR_SPSWC,
    NPCM8XX_GCR_INTCR,
    NPCM8XX_GCR_INTSR,
    NPCM8XX_GCR_HIFCR           = 0x050 / sizeof(uint32_t),
    NPCM8XX_GCR_INTCR2          = 0x060 / sizeof(uint32_t),
    NPCM8XX_GCR_SRCNT           = 0x068 / sizeof(uint32_t),
    NPCM8XX_GCR_RESSR,
    NPCM8XX_GCR_RLOCKR1,
    NPCM8XX_GCR_FLOCKR1,
    NPCM8XX_GCR_DSCNT,
    NPCM8XX_GCR_MDLR,
    NPCM8XX_GCR_SCRPAD_C        = 0x080 / sizeof(uint32_t),
    NPCM8XX_GCR_SCRPAD_B,
    NPCM8XX_GCR_DAVCLVLR        = 0x098 / sizeof(uint32_t),
    NPCM8XX_GCR_INTCR3,
    NPCM8XX_GCR_PCIRCTL         = 0x0a0 / sizeof(uint32_t),
    NPCM8XX_GCR_VSINTR,
    NPCM8XX_GCR_SD2SUR1         = 0x0b4 / sizeof(uint32_t),
    NPCM8XX_GCR_SD2SUR2,
    NPCM8XX_GCR_INTCR4          = 0x0c0 / sizeof(uint32_t),
    NPCM8XX_GCR_CPCTL           = 0x0d0 / sizeof(uint32_t),
    NPCM8XX_GCR_CP2BST,
    NPCM8XX_GCR_B2CPNT,
    NPCM8XX_GCR_CPPCTL,
    NPCM8XX_GCR_I2CSEGSEL       = 0x0e0 / sizeof(uint32_t),
    NPCM8XX_GCR_I2CSEGCTL,
    NPCM8XX_GCR_VSRCR,
    NPCM8XX_GCR_MLOCKR,
    NPCM8XX_GCR_SCRPAD          = 0x13c / sizeof(uint32_t),
    NPCM8XX_GCR_USB1PHYCTL,
    NPCM8XX_GCR_USB2PHYCTL,
    NPCM8XX_GCR_USB3PHYCTL,
    NPCM8XX_GCR_MFSEL1          = 0x260 / sizeof(uint32_t),
    NPCM8XX_GCR_MFSEL2,
    NPCM8XX_GCR_MFSEL3,
    NPCM8XX_GCR_MFSEL4,
    NPCM8XX_GCR_MFSEL5,
    NPCM8XX_GCR_MFSEL6,
    NPCM8XX_GCR_MFSEL7,
    NPCM8XX_GCR_MFSEL_LK1       = 0x280 / sizeof(uint32_t),
    NPCM8XX_GCR_MFSEL_LK2,
    NPCM8XX_GCR_MFSEL_LK3,
    NPCM8XX_GCR_MFSEL_LK4,
    NPCM8XX_GCR_MFSEL_LK5,
    NPCM8XX_GCR_MFSEL_LK6,
    NPCM8XX_GCR_MFSEL_LK7,
    NPCM8XX_GCR_MFSEL_SET1      = 0x2a0 / sizeof(uint32_t),
    NPCM8XX_GCR_MFSEL_SET2,
    NPCM8XX_GCR_MFSEL_SET3,
    NPCM8XX_GCR_MFSEL_SET4,
    NPCM8XX_GCR_MFSEL_SET5,
    NPCM8XX_GCR_MFSEL_SET6,
    NPCM8XX_GCR_MFSEL_SET7,
    NPCM8XX_GCR_MFSEL_CLR1      = 0x2c0 / sizeof(uint32_t),
    NPCM8XX_GCR_MFSEL_CLR2,
    NPCM8XX_GCR_MFSEL_CLR3,
    NPCM8XX_GCR_MFSEL_CLR4,
    NPCM8XX_GCR_MFSEL_CLR5,
    NPCM8XX_GCR_MFSEL_CLR6,
    NPCM8XX_GCR_MFSEL_CLR7,
    NPCM8XX_GCR_WD0RCRLK        = 0x400 / sizeof(uint32_t),
    NPCM8XX_GCR_WD1RCRLK,
    NPCM8XX_GCR_WD2RCRLK,
    NPCM8XX_GCR_SWRSTC1LK,
    NPCM8XX_GCR_SWRSTC2LK,
    NPCM8XX_GCR_SWRSTC3LK,
    NPCM8XX_GCR_TIPRSTCLK,
    NPCM8XX_GCR_CORSTCLK,
    NPCM8XX_GCR_WD0RCRBLK,
    NPCM8XX_GCR_WD1RCRBLK,
    NPCM8XX_GCR_WD2RCRBLK,
    NPCM8XX_GCR_SWRSTC1BLK,
    NPCM8XX_GCR_SWRSTC2BLK,
    NPCM8XX_GCR_SWRSTC3BLK,
    NPCM8XX_GCR_TIPRSTCBLK,
    NPCM8XX_GCR_CORSTCBLK,
    /* 64 scratch pad registers start here. 0xe00 ~ 0xefc */
    NPCM8XX_GCR_SCRPAD_00       = 0xe00 / sizeof(uint32_t),
    /* 32 semaphore registers start here. 0xf00 ~ 0xf7c */
    NPCM8XX_GCR_GP_SEMFR_00     = 0xf00 / sizeof(uint32_t),
    NPCM8XX_GCR_GP_SEMFR_31     = 0xf7c / sizeof(uint32_t),
};

static const uint32_t npcm8xx_cold_reset_values[NPCM8XX_GCR_NR_REGS] = {
    [NPCM8XX_GCR_PDID]          = 0x04a35850,   /* Arbel A1 */
    [NPCM8XX_GCR_MISCPE]        = 0x0000ffff,
    [NPCM8XX_GCR_A35_MODE]      = 0xfff4ff30,
    [NPCM8XX_GCR_SPSWC]         = 0x00000003,
    [NPCM8XX_GCR_INTCR]         = 0x0010035e,
    [NPCM8XX_GCR_HIFCR]         = 0x0000004e,
    [NPCM8XX_GCR_SD2SUR1]       = 0xfdc80000,
    [NPCM8XX_GCR_SD2SUR2]       = 0x5200b130,
    [NPCM8XX_GCR_INTCR2]        = (1U << 19),   /* DDR initialized */
    [NPCM8XX_GCR_RESSR]         = 0x80000000,
    [NPCM8XX_GCR_DAVCLVLR]      = 0x5a00f3cf,
    [NPCM8XX_GCR_INTCR3]        = 0x5e001002,
    [NPCM8XX_GCR_VSRCR]         = 0x00004800,
    [NPCM8XX_GCR_SCRPAD]        = 0x00000008,
    [NPCM8XX_GCR_USB1PHYCTL]    = 0x034730e4,
    [NPCM8XX_GCR_USB2PHYCTL]    = 0x034730e4,
    [NPCM8XX_GCR_USB3PHYCTL]    = 0x034730e4,
    /* All 32 semaphores should be initialized to 1. */
    [NPCM8XX_GCR_GP_SEMFR_00...NPCM8XX_GCR_GP_SEMFR_31] = 0x00000001,
};

static uint64_t npcm_gcr_read(void *opaque, hwaddr offset, unsigned size)
{
    uint32_t reg = offset / sizeof(uint32_t);
    NPCMGCRState *s = opaque;
    NPCMGCRClass *c = NPCM_GCR_GET_CLASS(s);
    uint64_t value;

    if (reg >= c->nr_regs) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: offset 0x%04" HWADDR_PRIx " out of range\n",
                      __func__, offset);
        return 0;
    }

    switch (size) {
    case 4:
        value = s->regs[reg];
        break;

    case 8:
        g_assert(!(reg & 1));
        value = deposit64(s->regs[reg], 32, 32, s->regs[reg + 1]);
        break;

    default:
        g_assert_not_reached();
    }

    trace_npcm_gcr_read(offset, value);
    return value;
}

static void npcm_gcr_write(void *opaque, hwaddr offset,
                              uint64_t v, unsigned size)
{
    uint32_t reg = offset / sizeof(uint32_t);
    NPCMGCRState *s = opaque;
    NPCMGCRClass *c = NPCM_GCR_GET_CLASS(s);
    uint32_t value = v;

    trace_npcm_gcr_write(offset, v);

    if (reg >= c->nr_regs) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: offset 0x%04" HWADDR_PRIx " out of range\n",
                      __func__, offset);
        return;
    }

    switch (size) {
    case 4:
        switch (reg) {
        case NPCM7XX_GCR_PDID:
        case NPCM7XX_GCR_PWRON:
        case NPCM7XX_GCR_INTSR:
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: register @ 0x%04" HWADDR_PRIx " is read-only\n",
                          __func__, offset);
            return;

        case NPCM7XX_GCR_RESSR:
        case NPCM7XX_GCR_CP2BST:
            /* Write 1 to clear */
            value = s->regs[reg] & ~value;
            break;

        case NPCM7XX_GCR_RLOCKR1:
        case NPCM7XX_GCR_MDLR:
            /* Write 1 to set */
            value |= s->regs[reg];
            break;
        };
        s->regs[reg] = value;
        break;

    case 8:
        g_assert(!(reg & 1));
        s->regs[reg] = value;
        s->regs[reg + 1] = extract64(v, 32, 32);
        break;

    default:
        g_assert_not_reached();
    }
}

static bool npcm_gcr_check_mem_op(void *opaque, hwaddr offset,
                                  unsigned size, bool is_write,
                                  MemTxAttrs attrs)
{
    NPCMGCRClass *c = NPCM_GCR_GET_CLASS(opaque);

    if (offset >= c->nr_regs * sizeof(uint32_t)) {
        return false;
    }

    switch (size) {
    case 4:
        return true;
    case 8:
        if (offset >= NPCM8XX_GCR_SCRPAD_00 * sizeof(uint32_t) &&
            offset < (NPCM8XX_GCR_NR_REGS - 1) * sizeof(uint32_t)) {
            return true;
        } else {
            return false;
        }
    default:
        return false;
    }
}

static const struct MemoryRegionOps npcm_gcr_ops = {
    .read       = npcm_gcr_read,
    .write      = npcm_gcr_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid      = {
        .min_access_size        = 4,
        .max_access_size        = 8,
        .accepts                = npcm_gcr_check_mem_op,
        .unaligned              = false,
    },
};

static void npcm7xx_gcr_enter_reset(Object *obj, ResetType type)
{
    NPCMGCRState *s = NPCM_GCR(obj);
    NPCMGCRClass *c = NPCM_GCR_GET_CLASS(obj);

    g_assert(sizeof(s->regs) >= sizeof(c->cold_reset_values));
    g_assert(sizeof(s->regs) >= c->nr_regs * sizeof(uint32_t));
    memcpy(s->regs, c->cold_reset_values, c->nr_regs * sizeof(uint32_t));
    /* These 3 registers are at the same location in both 7xx and 8xx. */
    s->regs[NPCM7XX_GCR_PWRON] = s->reset_pwron;
    s->regs[NPCM7XX_GCR_MDLR] = s->reset_mdlr;
    s->regs[NPCM7XX_GCR_INTCR3] = s->reset_intcr3;
}

static void npcm8xx_gcr_enter_reset(Object *obj, ResetType type)
{
    NPCMGCRState *s = NPCM_GCR(obj);
    NPCMGCRClass *c = NPCM_GCR_GET_CLASS(obj);

    memcpy(s->regs, c->cold_reset_values, c->nr_regs * sizeof(uint32_t));
    /* These 3 registers are at the same location in both 7xx and 8xx. */
    s->regs[NPCM8XX_GCR_PWRON] = s->reset_pwron;
    s->regs[NPCM8XX_GCR_MDLR] = s->reset_mdlr;
    s->regs[NPCM8XX_GCR_INTCR3] = s->reset_intcr3;
    s->regs[NPCM8XX_GCR_SCRPAD_B] = s->reset_scrpad_b;
}

static void npcm_gcr_realize(DeviceState *dev, Error **errp)
{
    ERRP_GUARD();
    NPCMGCRState *s = NPCM_GCR(dev);
    uint64_t dram_size;
    Object *obj;

    obj = object_property_get_link(OBJECT(dev), "dram-mr", errp);
    if (!obj) {
        error_prepend(errp, "%s: required dram-mr link not found: ", __func__);
        return;
    }
    dram_size = memory_region_size(MEMORY_REGION(obj));
    if (!is_power_of_2(dram_size) ||
        dram_size < NPCM7XX_GCR_MIN_DRAM_SIZE ||
        dram_size > NPCM7XX_GCR_MAX_DRAM_SIZE) {
        g_autofree char *sz = size_to_str(dram_size);
        g_autofree char *min_sz = size_to_str(NPCM7XX_GCR_MIN_DRAM_SIZE);
        g_autofree char *max_sz = size_to_str(NPCM7XX_GCR_MAX_DRAM_SIZE);
        error_setg(errp, "%s: unsupported DRAM size %s", __func__, sz);
        error_append_hint(errp,
                          "DRAM size must be a power of two between %s and %s,"
                          " inclusive.\n", min_sz, max_sz);
        return;
    }

    /* Power-on reset value */
    s->reset_intcr3 = 0x00001002;

    /*
     * The GMMAP (Graphics Memory Map) field is used by u-boot to detect the
     * DRAM size, and is normally initialized by the boot block as part of DRAM
     * training. However, since we don't have a complete emulation of the
     * memory controller and try to make it look like it has already been
     * initialized, the boot block will skip this initialization, and we need
     * to make sure this field is set correctly up front.
     *
     * WARNING: some versions of u-boot only looks at bits 8 and 9, so 2 GiB of
     * DRAM will be interpreted as 128 MiB.
     *
     * https://github.com/Nuvoton-Israel/u-boot/blob/2aef993bd2aafeb5408dbaad0f3ce099ee40c4aa/board/nuvoton/poleg/poleg.c#L244
     */
    s->reset_intcr3 |= ctz64(dram_size / NPCM7XX_GCR_MIN_DRAM_SIZE) << 8;

    /*
     * The boot block starting from 0.0.6 for NPCM8xx SoCs stores the DRAM size
     * in the SCRPAD2 registers. We need to set this field correctly since
     * the initialization is skipped as we mentioned above.
     * https://github.com/Nuvoton-Israel/u-boot/blob/npcm8mnx-v2019.01_tmp/board/nuvoton/arbel/arbel.c#L737
     */
    s->reset_scrpad_b = dram_size;
}

static void npcm_gcr_init(Object *obj)
{
    NPCMGCRState *s = NPCM_GCR(obj);

    memory_region_init_io(&s->iomem, obj, &npcm_gcr_ops, s,
                          TYPE_NPCM_GCR, 4 * KiB);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->iomem);
}

static const VMStateDescription vmstate_npcm_gcr = {
    .name = "npcm-gcr",
    .version_id = 2,
    .minimum_version_id = 2,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, NPCMGCRState, NPCM_GCR_MAX_NR_REGS),
        VMSTATE_END_OF_LIST(),
    },
};

static const Property npcm_gcr_properties[] = {
    DEFINE_PROP_UINT32("disabled-modules", NPCMGCRState, reset_mdlr, 0),
    DEFINE_PROP_UINT32("power-on-straps", NPCMGCRState, reset_pwron, 0),
};

static void npcm_gcr_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = npcm_gcr_realize;
    dc->vmsd = &vmstate_npcm_gcr;

    device_class_set_props(dc, npcm_gcr_properties);
}

static void npcm7xx_gcr_class_init(ObjectClass *klass, const void *data)
{
    NPCMGCRClass *c = NPCM_GCR_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->desc = "NPCM7xx System Global Control Registers";
    rc->phases.enter = npcm7xx_gcr_enter_reset;

    c->nr_regs = NPCM7XX_GCR_NR_REGS;
    c->cold_reset_values = npcm7xx_cold_reset_values;
    rc->phases.enter = npcm7xx_gcr_enter_reset;
}

static void npcm8xx_gcr_class_init(ObjectClass *klass, const void *data)
{
    NPCMGCRClass *c = NPCM_GCR_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->desc = "NPCM8xx System Global Control Registers";
    c->nr_regs = NPCM8XX_GCR_NR_REGS;
    c->cold_reset_values = npcm8xx_cold_reset_values;
    rc->phases.enter = npcm8xx_gcr_enter_reset;
}

static const TypeInfo npcm_gcr_info[] = {
    {
        .name               = TYPE_NPCM_GCR,
        .parent             = TYPE_SYS_BUS_DEVICE,
        .instance_size      = sizeof(NPCMGCRState),
        .instance_init      = npcm_gcr_init,
        .class_size         = sizeof(NPCMGCRClass),
        .class_init         = npcm_gcr_class_init,
        .abstract           = true,
    },
    {
        .name               = TYPE_NPCM7XX_GCR,
        .parent             = TYPE_NPCM_GCR,
        .class_init         = npcm7xx_gcr_class_init,
    },
    {
        .name               = TYPE_NPCM8XX_GCR,
        .parent             = TYPE_NPCM_GCR,
        .class_init         = npcm8xx_gcr_class_init,
    },
};
DEFINE_TYPES(npcm_gcr_info)

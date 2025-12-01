/*
 * Ingenic T41 CPM (Clock Power Management) and HARB (AHB Bus Controller)
 *
 * Copyright (c) 2024 OpenSensor Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file implements the CPM registers and HARB0 CPU ID register
 * needed for the /sbin/soc script to identify the SoC model.
 *
 * Memory Map:
 *   CPM:   0x10000000 - Clock Power Management
 *   HARB0: 0x13000000 - AHB0 Bus Controller (contains CPU ID at offset 0x2C)
 *   EFUSE: 0x13540000 - EFUSE/OTP registers
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "qemu/log.h"
#include "qom/object.h"

/* CPM Register offsets */
#define CPM_CPCCR           0x00    /* Clock Control Register */
#define CPM_CPCSR           0x34    /* Clock Status Register (CPPSR) */

/* HARB0 Register offsets */
#define HARB0_CPUID         0x2C    /* CPU ID Register */

/* T41NQ identification values */
#define T41_CPUID_RAW       0x00040000  /* cpuid = (raw >> 12) & 0xFFFF = 0x40 */

#define TYPE_INGENIC_CPM "ingenic-cpm"
OBJECT_DECLARE_SIMPLE_TYPE(IngenicCpmState, INGENIC_CPM)

struct IngenicCpmState {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    uint32_t cpccr;
    uint32_t cpcsr;
};

static uint64_t ingenic_cpm_read(void *opaque, hwaddr offset, unsigned size)
{
    IngenicCpmState *s = INGENIC_CPM(opaque);

    switch (offset) {
    case CPM_CPCCR:
        return s->cpccr;
    case CPM_CPCSR:
        return s->cpcsr;
    default:
        qemu_log_mask(LOG_UNIMP, "ingenic_cpm: read from unimpl offset 0x%"
                      HWADDR_PRIx "\n", offset);
        return 0;
    }
}

static void ingenic_cpm_write(void *opaque, hwaddr offset, uint64_t value,
                              unsigned size)
{
    IngenicCpmState *s = INGENIC_CPM(opaque);

    switch (offset) {
    case CPM_CPCCR:
        s->cpccr = value;
        break;
    case CPM_CPCSR:
        s->cpcsr = value;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "ingenic_cpm: write to unimpl offset 0x%"
                      HWADDR_PRIx "\n", offset);
        break;
    }
}

static const MemoryRegionOps ingenic_cpm_ops = {
    .read = ingenic_cpm_read,
    .write = ingenic_cpm_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void ingenic_cpm_realize(DeviceState *dev, Error **errp)
{
    IngenicCpmState *s = INGENIC_CPM(dev);

    memory_region_init_io(&s->iomem, OBJECT(s), &ingenic_cpm_ops, s,
                          "ingenic-cpm", 0x100);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->iomem);
}

static void ingenic_cpm_reset(DeviceState *dev)
{
    IngenicCpmState *s = INGENIC_CPM(dev);

    s->cpccr = 0x95800000;  /* Default clock configuration */
    s->cpcsr = 0x00000000;  /* Default status */
}

static void ingenic_cpm_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = ingenic_cpm_realize;
    device_class_set_legacy_reset(dc, ingenic_cpm_reset);
}

static const TypeInfo ingenic_cpm_info = {
    .name = TYPE_INGENIC_CPM,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IngenicCpmState),
    .class_init = ingenic_cpm_class_init,
};

/*
 * HARB0 - AHB0 Bus Controller with CPU ID
 */
#define TYPE_INGENIC_HARB0 "ingenic-harb0"
OBJECT_DECLARE_SIMPLE_TYPE(IngenicHarb0State, INGENIC_HARB0)

struct IngenicHarb0State {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    uint32_t cpuid;
};

static uint64_t ingenic_harb0_read(void *opaque, hwaddr offset, unsigned size)
{
    IngenicHarb0State *s = INGENIC_HARB0(opaque);

    switch (offset) {
    case HARB0_CPUID:
        return s->cpuid;
    default:
        return 0;
    }
}

static void ingenic_harb0_write(void *opaque, hwaddr offset, uint64_t value,
                                unsigned size)
{
    /* HARB0 registers are mostly read-only */
}

static const MemoryRegionOps ingenic_harb0_ops = {
    .read = ingenic_harb0_read,
    .write = ingenic_harb0_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void ingenic_harb0_realize(DeviceState *dev, Error **errp)
{
    IngenicHarb0State *s = INGENIC_HARB0(dev);

    memory_region_init_io(&s->iomem, OBJECT(s), &ingenic_harb0_ops, s,
                          "ingenic-harb0", 0x100);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->iomem);
}

static void ingenic_harb0_reset(DeviceState *dev)
{
    IngenicHarb0State *s = INGENIC_HARB0(dev);
    s->cpuid = T41_CPUID_RAW;  /* T41 CPU ID: (0x40000 >> 12) & 0xFFFF = 0x40 */
}

static void ingenic_harb0_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = ingenic_harb0_realize;
    device_class_set_legacy_reset(dc, ingenic_harb0_reset);
}

static const TypeInfo ingenic_harb0_info = {
    .name = TYPE_INGENIC_HARB0,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IngenicHarb0State),
    .class_init = ingenic_harb0_class_init,
};

/*
 * EFUSE - OTP/EFUSE Controller
 * Contains chip identification and serial number registers
 */
#define TYPE_INGENIC_EFUSE "ingenic-efuse"
OBJECT_DECLARE_SIMPLE_TYPE(IngenicEfuseState, INGENIC_EFUSE)

/* EFUSE Register offsets used by /sbin/soc */
#define EFUSE_SERIAL0       0x200   /* Serial number part 0 */
#define EFUSE_SERIAL1       0x204   /* Serial number part 1 */
#define EFUSE_SERIAL2       0x208   /* Serial number part 2 */
#define EFUSE_SUBRM         0x231   /* Subrom register */
#define EFUSE_TYPE1         0x238   /* Type1 register */
#define EFUSE_SERIAL3       0x23C   /* Serial number part 3 */
#define EFUSE_TYPE2         0x250   /* Type2 register */

/* T41NQ identification: type2 = 0xAAAA */
#define T41NQ_TYPE2         0xAAAA0000

struct IngenicEfuseState {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    uint32_t serial[4];
    uint32_t subrm;
    uint32_t type1;
    uint32_t type2;
};

static uint64_t ingenic_efuse_read(void *opaque, hwaddr offset, unsigned size)
{
    IngenicEfuseState *s = INGENIC_EFUSE(opaque);

    switch (offset) {
    case EFUSE_SERIAL0:
        return s->serial[0];
    case EFUSE_SERIAL1:
        return s->serial[1];
    case EFUSE_SERIAL2:
        return s->serial[2];
    case EFUSE_SERIAL3:
        return s->serial[3];
    case EFUSE_SUBRM:
        return s->subrm;
    case EFUSE_TYPE1:
        return s->type1;
    case EFUSE_TYPE2:
        return s->type2;
    default:
        return 0;
    }
}

static void ingenic_efuse_write(void *opaque, hwaddr offset, uint64_t value,
                                unsigned size)
{
    /* EFUSE is read-only */
}

static const MemoryRegionOps ingenic_efuse_ops = {
    .read = ingenic_efuse_read,
    .write = ingenic_efuse_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
    .impl = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static void ingenic_efuse_realize(DeviceState *dev, Error **errp)
{
    IngenicEfuseState *s = INGENIC_EFUSE(dev);

    memory_region_init_io(&s->iomem, OBJECT(s), &ingenic_efuse_ops, s,
                          "ingenic-efuse", 0x1000);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->iomem);
}

static void ingenic_efuse_reset(DeviceState *dev)
{
    IngenicEfuseState *s = INGENIC_EFUSE(dev);

    /* Set up T41NQ identification values */
    s->serial[0] = 0x12345678;  /* Fake serial number */
    s->serial[1] = 0x9ABCDEF0;
    s->serial[2] = 0x11223344;
    s->serial[3] = 0x55667788;
    s->subrm = 0x00000000;
    s->type1 = 0x00000000;
    s->type2 = T41NQ_TYPE2;     /* type2 = (raw >> 16) & 0xFFFF = 0xAAAA -> T41NQ */
}

static void ingenic_efuse_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = ingenic_efuse_realize;
    device_class_set_legacy_reset(dc, ingenic_efuse_reset);
}

static const TypeInfo ingenic_efuse_info = {
    .name = TYPE_INGENIC_EFUSE,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IngenicEfuseState),
    .class_init = ingenic_efuse_class_init,
};

static void ingenic_cpm_register_types(void)
{
    type_register_static(&ingenic_cpm_info);
    type_register_static(&ingenic_harb0_info);
    type_register_static(&ingenic_efuse_info);
}

type_init(ingenic_cpm_register_types)


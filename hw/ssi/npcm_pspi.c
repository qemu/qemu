/*
 * Nuvoton NPCM Peripheral SPI Module (PSPI)
 *
 * Copyright 2023 Google LLC
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

#include "hw/irq.h"
#include "hw/registerfields.h"
#include "hw/ssi/npcm_pspi.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/units.h"

#include "trace.h"

REG16(PSPI_DATA, 0x0)
REG16(PSPI_CTL1, 0x2)
    FIELD(PSPI_CTL1, SPIEN, 0,  1)
    FIELD(PSPI_CTL1, MOD,   2,  1)
    FIELD(PSPI_CTL1, EIR,   5,  1)
    FIELD(PSPI_CTL1, EIW,   6,  1)
    FIELD(PSPI_CTL1, SCM,   7,  1)
    FIELD(PSPI_CTL1, SCIDL, 8,  1)
    FIELD(PSPI_CTL1, SCDV,  9,  7)
REG16(PSPI_STAT, 0x4)
    FIELD(PSPI_STAT, BSY,  0,  1)
    FIELD(PSPI_STAT, RBF,  1,  1)

static void npcm_pspi_update_irq(NPCMPSPIState *s)
{
    int level = 0;

    /* Only fire IRQ when the module is enabled. */
    if (FIELD_EX16(s->regs[R_PSPI_CTL1], PSPI_CTL1, SPIEN)) {
        /* Update interrupt as BSY is cleared. */
        if ((!FIELD_EX16(s->regs[R_PSPI_STAT], PSPI_STAT, BSY)) &&
            FIELD_EX16(s->regs[R_PSPI_CTL1], PSPI_CTL1, EIW)) {
            level = 1;
        }

        /* Update interrupt as RBF is set. */
        if (FIELD_EX16(s->regs[R_PSPI_STAT], PSPI_STAT, RBF) &&
            FIELD_EX16(s->regs[R_PSPI_CTL1], PSPI_CTL1, EIR)) {
            level = 1;
        }
    }
    qemu_set_irq(s->irq, level);
}

static uint16_t npcm_pspi_read_data(NPCMPSPIState *s)
{
    uint16_t value = s->regs[R_PSPI_DATA];

    /* Clear stat bits as the value are read out. */
    s->regs[R_PSPI_STAT] = 0;

    return value;
}

static void npcm_pspi_write_data(NPCMPSPIState *s, uint16_t data)
{
    uint16_t value = 0;

    if (FIELD_EX16(s->regs[R_PSPI_CTL1], PSPI_CTL1, MOD)) {
        value = ssi_transfer(s->spi, extract16(data, 8, 8)) << 8;
    }
    value |= ssi_transfer(s->spi, extract16(data, 0, 8));
    s->regs[R_PSPI_DATA] = value;

    /* Mark data as available */
    s->regs[R_PSPI_STAT] = R_PSPI_STAT_BSY_MASK | R_PSPI_STAT_RBF_MASK;
}

/* Control register read handler. */
static uint64_t npcm_pspi_ctrl_read(void *opaque, hwaddr addr,
                                    unsigned int size)
{
    NPCMPSPIState *s = opaque;
    uint16_t value;

    switch (addr) {
    case A_PSPI_DATA:
        value = npcm_pspi_read_data(s);
        break;

    case A_PSPI_CTL1:
        value = s->regs[R_PSPI_CTL1];
        break;

    case A_PSPI_STAT:
        value = s->regs[R_PSPI_STAT];
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: write to invalid offset 0x%" PRIx64 "\n",
                      DEVICE(s)->canonical_path, addr);
        return 0;
    }
    trace_npcm_pspi_ctrl_read(DEVICE(s)->canonical_path, addr, value);
    npcm_pspi_update_irq(s);

    return value;
}

/* Control register write handler. */
static void npcm_pspi_ctrl_write(void *opaque, hwaddr addr, uint64_t v,
                                 unsigned int size)
{
    NPCMPSPIState *s = opaque;
    uint16_t value = v;

    trace_npcm_pspi_ctrl_write(DEVICE(s)->canonical_path, addr, value);

    switch (addr) {
    case A_PSPI_DATA:
        npcm_pspi_write_data(s, value);
        break;

    case A_PSPI_CTL1:
        s->regs[R_PSPI_CTL1] = value;
        break;

    case A_PSPI_STAT:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: write to read-only register PSPI_STAT: 0x%08"
                      PRIx64 "\n", DEVICE(s)->canonical_path, v);
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: write to invalid offset 0x%" PRIx64 "\n",
                      DEVICE(s)->canonical_path, addr);
        return;
    }
    npcm_pspi_update_irq(s);
}

static const MemoryRegionOps npcm_pspi_ctrl_ops = {
    .read = npcm_pspi_ctrl_read,
    .write = npcm_pspi_ctrl_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 2,
        .unaligned = false,
    },
    .impl = {
        .min_access_size = 2,
        .max_access_size = 2,
        .unaligned = false,
    },
};

static void npcm_pspi_enter_reset(Object *obj, ResetType type)
{
    NPCMPSPIState *s = NPCM_PSPI(obj);

    trace_npcm_pspi_enter_reset(DEVICE(obj)->canonical_path, type);
    memset(s->regs, 0, sizeof(s->regs));
}

static void npcm_pspi_realize(DeviceState *dev, Error **errp)
{
    NPCMPSPIState *s = NPCM_PSPI(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    Object *obj = OBJECT(dev);

    s->spi = ssi_create_bus(dev, "pspi");
    memory_region_init_io(&s->mmio, obj, &npcm_pspi_ctrl_ops, s,
                          "mmio", 4 * KiB);
    sysbus_init_mmio(sbd, &s->mmio);
    sysbus_init_irq(sbd, &s->irq);
}

static const VMStateDescription vmstate_npcm_pspi = {
    .name = "npcm-pspi",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (VMStateField[]) {
        VMSTATE_UINT16_ARRAY(regs, NPCMPSPIState, NPCM_PSPI_NR_REGS),
        VMSTATE_END_OF_LIST(),
    },
};


static void npcm_pspi_class_init(ObjectClass *klass, void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "NPCM Peripheral SPI Module";
    dc->realize = npcm_pspi_realize;
    dc->vmsd = &vmstate_npcm_pspi;
    rc->phases.enter = npcm_pspi_enter_reset;
}

static const TypeInfo npcm_pspi_types[] = {
    {
        .name = TYPE_NPCM_PSPI,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(NPCMPSPIState),
        .class_init = npcm_pspi_class_init,
    },
};
DEFINE_TYPES(npcm_pspi_types);

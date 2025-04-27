/*
 * Nuvoton NPCM7xx Flash Interface Unit (FIU)
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

#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/ssi/npcm7xx_fiu.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/units.h"

#include "trace.h"

/* Up to 128 MiB of flash may be accessed directly as memory. */
#define NPCM7XX_FIU_MAX_FLASH_WINDOW_SIZE (128 * MiB)

/* Each module has 4 KiB of register space. Only a fraction of it is used. */
#define NPCM7XX_FIU_CTRL_REGS_SIZE (4 * KiB)

/* 32-bit FIU register indices. */
enum NPCM7xxFIURegister {
    NPCM7XX_FIU_DRD_CFG,
    NPCM7XX_FIU_DWR_CFG,
    NPCM7XX_FIU_UMA_CFG,
    NPCM7XX_FIU_UMA_CTS,
    NPCM7XX_FIU_UMA_CMD,
    NPCM7XX_FIU_UMA_ADDR,
    NPCM7XX_FIU_PRT_CFG,
    NPCM7XX_FIU_UMA_DW0 = 0x0020 / sizeof(uint32_t),
    NPCM7XX_FIU_UMA_DW1,
    NPCM7XX_FIU_UMA_DW2,
    NPCM7XX_FIU_UMA_DW3,
    NPCM7XX_FIU_UMA_DR0,
    NPCM7XX_FIU_UMA_DR1,
    NPCM7XX_FIU_UMA_DR2,
    NPCM7XX_FIU_UMA_DR3,
    NPCM7XX_FIU_PRT_CMD0,
    NPCM7XX_FIU_PRT_CMD1,
    NPCM7XX_FIU_PRT_CMD2,
    NPCM7XX_FIU_PRT_CMD3,
    NPCM7XX_FIU_PRT_CMD4,
    NPCM7XX_FIU_PRT_CMD5,
    NPCM7XX_FIU_PRT_CMD6,
    NPCM7XX_FIU_PRT_CMD7,
    NPCM7XX_FIU_PRT_CMD8,
    NPCM7XX_FIU_PRT_CMD9,
    NPCM7XX_FIU_CFG = 0x78 / sizeof(uint32_t),
    NPCM7XX_FIU_REGS_END,
};

/* FIU_{DRD,DWR,UMA,PTR}_CFG cannot be written when this bit is set. */
#define NPCM7XX_FIU_CFG_LCK BIT(31)

/* Direct Read configuration register fields. */
#define FIU_DRD_CFG_ADDSIZ(rv) extract32(rv, 16, 2)
#define FIU_ADDSIZ_3BYTES 0
#define FIU_ADDSIZ_4BYTES 1
#define FIU_DRD_CFG_DBW(rv) extract32(rv, 12, 2)
#define FIU_DRD_CFG_ACCTYPE(rv) extract32(rv, 8, 2)
#define FIU_DRD_CFG_RDCMD(rv) extract32(rv, 0, 8)

/* Direct Write configuration register fields. */
#define FIU_DWR_CFG_ADDSIZ(rv) extract32(rv, 16, 2)
#define FIU_DWR_CFG_WRCMD(rv) extract32(rv, 0, 8)

/* User-Mode Access register fields. */

/* Command Mode Lock and the bits protected by it. */
#define FIU_UMA_CFG_CMMLCK BIT(30)
#define FIU_UMA_CFG_CMMLCK_MASK 0x00000403

#define FIU_UMA_CFG_RDATSIZ(rv) extract32(rv, 24, 5)
#define FIU_UMA_CFG_DBSIZ(rv) extract32(rv, 21, 3)
#define FIU_UMA_CFG_WDATSIZ(rv) extract32(rv, 16, 5)
#define FIU_UMA_CFG_ADDSIZ(rv) extract32(rv, 11, 3)
#define FIU_UMA_CFG_CMDSIZ(rv) extract32(rv, 10, 1)
#define FIU_UMA_CFG_DBPCK(rv) extract32(rv, 6, 2)

#define FIU_UMA_CTS_RDYIE BIT(25)
#define FIU_UMA_CTS_RDYST BIT(24)
#define FIU_UMA_CTS_SW_CS BIT(16)
#define FIU_UMA_CTS_DEV_NUM(rv) extract32(rv, 8, 2)
#define FIU_UMA_CTS_EXEC_DONE BIT(0)

/*
 * Returns the index of flash in the fiu->flash array. This corresponds to the
 * chip select ID of the flash.
 */
static unsigned npcm7xx_fiu_cs_index(NPCM7xxFIUState *fiu,
                                     NPCM7xxFIUFlash *flash)
{
    int index = flash - fiu->flash;

    g_assert(index >= 0 && index < fiu->cs_count);

    return index;
}

/* Assert the chip select specified in the UMA Control/Status Register. */
static void npcm7xx_fiu_select(NPCM7xxFIUState *s, unsigned cs_id)
{
    trace_npcm7xx_fiu_select(DEVICE(s)->canonical_path, cs_id);

    if (cs_id < s->cs_count) {
        qemu_irq_lower(s->cs_lines[cs_id]);
        s->active_cs = cs_id;
    } else {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: UMA to CS%d; this module has only %d chip selects",
                      DEVICE(s)->canonical_path, cs_id, s->cs_count);
        s->active_cs = -1;
    }
}

/* Deassert the currently active chip select. */
static void npcm7xx_fiu_deselect(NPCM7xxFIUState *s)
{
    if (s->active_cs < 0) {
        return;
    }

    trace_npcm7xx_fiu_deselect(DEVICE(s)->canonical_path, s->active_cs);

    qemu_irq_raise(s->cs_lines[s->active_cs]);
    s->active_cs = -1;
}

/* Direct flash memory read handler. */
static uint64_t npcm7xx_fiu_flash_read(void *opaque, hwaddr addr,
                                       unsigned int size)
{
    NPCM7xxFIUFlash *f = opaque;
    NPCM7xxFIUState *fiu = f->fiu;
    uint64_t value = 0;
    uint32_t drd_cfg;
    int dummy_cycles;
    int i;

    if (fiu->active_cs != -1) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: direct flash read with CS%d already active",
                      DEVICE(fiu)->canonical_path, fiu->active_cs);
    }

    npcm7xx_fiu_select(fiu, npcm7xx_fiu_cs_index(fiu, f));

    drd_cfg = fiu->regs[NPCM7XX_FIU_DRD_CFG];
    ssi_transfer(fiu->spi, FIU_DRD_CFG_RDCMD(drd_cfg));

    switch (FIU_DRD_CFG_ADDSIZ(drd_cfg)) {
    case FIU_ADDSIZ_4BYTES:
        ssi_transfer(fiu->spi, extract32(addr, 24, 8));
        /* fall through */
    case FIU_ADDSIZ_3BYTES:
        ssi_transfer(fiu->spi, extract32(addr, 16, 8));
        ssi_transfer(fiu->spi, extract32(addr, 8, 8));
        ssi_transfer(fiu->spi, extract32(addr, 0, 8));
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: bad address size %d\n",
                      DEVICE(fiu)->canonical_path, FIU_DRD_CFG_ADDSIZ(drd_cfg));
        break;
    }

    /* Flash chip model expects one transfer per dummy bit, not byte */
    dummy_cycles =
        (FIU_DRD_CFG_DBW(drd_cfg) * 8) >> FIU_DRD_CFG_ACCTYPE(drd_cfg);
    for (i = 0; i < dummy_cycles; i++) {
        ssi_transfer(fiu->spi, 0);
    }

    for (i = 0; i < size; i++) {
        value = deposit64(value, 8 * i, 8, ssi_transfer(fiu->spi, 0));
    }

    trace_npcm7xx_fiu_flash_read(DEVICE(fiu)->canonical_path, fiu->active_cs,
                                 addr, size, value);

    npcm7xx_fiu_deselect(fiu);

    return value;
}

/* Direct flash memory write handler. */
static void npcm7xx_fiu_flash_write(void *opaque, hwaddr addr, uint64_t v,
                                    unsigned int size)
{
    NPCM7xxFIUFlash *f = opaque;
    NPCM7xxFIUState *fiu = f->fiu;
    uint32_t dwr_cfg;
    unsigned cs_id;
    int i;

    if (fiu->active_cs != -1) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: direct flash write with CS%d already active",
                      DEVICE(fiu)->canonical_path, fiu->active_cs);
    }

    cs_id = npcm7xx_fiu_cs_index(fiu, f);
    trace_npcm7xx_fiu_flash_write(DEVICE(fiu)->canonical_path, cs_id, addr,
                                  size, v);
    npcm7xx_fiu_select(fiu, cs_id);

    dwr_cfg = fiu->regs[NPCM7XX_FIU_DWR_CFG];
    ssi_transfer(fiu->spi, FIU_DWR_CFG_WRCMD(dwr_cfg));

    switch (FIU_DWR_CFG_ADDSIZ(dwr_cfg)) {
    case FIU_ADDSIZ_4BYTES:
        ssi_transfer(fiu->spi, extract32(addr, 24, 8));
        /* fall through */
    case FIU_ADDSIZ_3BYTES:
        ssi_transfer(fiu->spi, extract32(addr, 16, 8));
        ssi_transfer(fiu->spi, extract32(addr, 8, 8));
        ssi_transfer(fiu->spi, extract32(addr, 0, 8));
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: bad address size %d\n",
                      DEVICE(fiu)->canonical_path, FIU_DWR_CFG_ADDSIZ(dwr_cfg));
        break;
    }

    for (i = 0; i < size; i++) {
        ssi_transfer(fiu->spi, extract64(v, i * 8, 8));
    }

    npcm7xx_fiu_deselect(fiu);
}

static const MemoryRegionOps npcm7xx_fiu_flash_ops = {
    .read = npcm7xx_fiu_flash_read,
    .write = npcm7xx_fiu_flash_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 8,
        .unaligned = true,
    },
};

/* Control register read handler. */
static uint64_t npcm7xx_fiu_ctrl_read(void *opaque, hwaddr addr,
                                      unsigned int size)
{
    hwaddr reg = addr / sizeof(uint32_t);
    NPCM7xxFIUState *s = opaque;
    uint32_t value;

    if (reg < NPCM7XX_FIU_NR_REGS) {
        value = s->regs[reg];
    } else {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: read from invalid offset 0x%" PRIx64 "\n",
                      DEVICE(s)->canonical_path, addr);
        value = 0;
    }

    trace_npcm7xx_fiu_ctrl_read(DEVICE(s)->canonical_path, addr, value);

    return value;
}

/* Send the specified number of address bytes from the UMA address register. */
static void send_address(SSIBus *spi, unsigned int addsiz, uint32_t addr)
{
    switch (addsiz) {
    case 4:
        ssi_transfer(spi, extract32(addr, 24, 8));
        /* fall through */
    case 3:
        ssi_transfer(spi, extract32(addr, 16, 8));
        /* fall through */
    case 2:
        ssi_transfer(spi, extract32(addr, 8, 8));
        /* fall through */
    case 1:
        ssi_transfer(spi, extract32(addr, 0, 8));
        /* fall through */
    case 0:
        break;
    }
}

/* Send the number of dummy bits specified in the UMA config register. */
static void send_dummy_bits(SSIBus *spi, uint32_t uma_cfg, uint32_t uma_cmd)
{
    unsigned int bits_per_clock = 1U << FIU_UMA_CFG_DBPCK(uma_cfg);
    unsigned int i;

    for (i = 0; i < FIU_UMA_CFG_DBSIZ(uma_cfg); i++) {
        /* Use bytes 0 and 1 first, then keep repeating byte 2 */
        unsigned int field = (i < 2) ? ((i + 1) * 8) : 24;
        unsigned int j;

        for (j = 0; j < 8; j += bits_per_clock) {
            ssi_transfer(spi, extract32(uma_cmd, field + j, bits_per_clock));
        }
    }
}

/* Perform a User-Mode Access transaction. */
static void npcm7xx_fiu_uma_transaction(NPCM7xxFIUState *s)
{
    uint32_t uma_cts = s->regs[NPCM7XX_FIU_UMA_CTS];
    uint32_t uma_cfg;
    unsigned int i;

    /* SW_CS means the CS is already forced low, so don't touch it. */
    if (uma_cts & FIU_UMA_CTS_SW_CS) {
        int cs_id = FIU_UMA_CTS_DEV_NUM(s->regs[NPCM7XX_FIU_UMA_CTS]);
        npcm7xx_fiu_select(s, cs_id);
    }

    /* Send command, if present. */
    uma_cfg = s->regs[NPCM7XX_FIU_UMA_CFG];
    if (FIU_UMA_CFG_CMDSIZ(uma_cfg) > 0) {
        ssi_transfer(s->spi, extract32(s->regs[NPCM7XX_FIU_UMA_CMD], 0, 8));
    }

    /* Send address, if present. */
    send_address(s->spi, FIU_UMA_CFG_ADDSIZ(uma_cfg),
                 s->regs[NPCM7XX_FIU_UMA_ADDR]);

    /* Write data, if present. */
    for (i = 0; i < FIU_UMA_CFG_WDATSIZ(uma_cfg); i++) {
        unsigned int reg =
            (i < 16) ? (NPCM7XX_FIU_UMA_DW0 + i / 4) : NPCM7XX_FIU_UMA_DW3;
        unsigned int field = (i % 4) * 8;

        ssi_transfer(s->spi, extract32(s->regs[reg], field, 8));
    }

    /* Send dummy bits, if present. */
    send_dummy_bits(s->spi, uma_cfg, s->regs[NPCM7XX_FIU_UMA_CMD]);

    /* Read data, if present. */
    for (i = 0; i < FIU_UMA_CFG_RDATSIZ(uma_cfg); i++) {
        unsigned int reg = NPCM7XX_FIU_UMA_DR0 + i / 4;
        unsigned int field = (i % 4) * 8;
        uint8_t c;

        c = ssi_transfer(s->spi, 0);
        if (reg <= NPCM7XX_FIU_UMA_DR3) {
            s->regs[reg] = deposit32(s->regs[reg], field, 8, c);
        }
    }

    /* Again, don't touch CS if the user is forcing it low. */
    if (uma_cts & FIU_UMA_CTS_SW_CS) {
        npcm7xx_fiu_deselect(s);
    }

    /* RDYST means a command has completed since it was cleared. */
    s->regs[NPCM7XX_FIU_UMA_CTS] |= FIU_UMA_CTS_RDYST;
    /* EXEC_DONE means Execute Command / Not Done, so clear it here. */
    s->regs[NPCM7XX_FIU_UMA_CTS] &= ~FIU_UMA_CTS_EXEC_DONE;
}

/* Control register write handler. */
static void npcm7xx_fiu_ctrl_write(void *opaque, hwaddr addr, uint64_t v,
                                   unsigned int size)
{
    hwaddr reg = addr / sizeof(uint32_t);
    NPCM7xxFIUState *s = opaque;
    uint32_t value = v;

    trace_npcm7xx_fiu_ctrl_write(DEVICE(s)->canonical_path, addr, value);

    switch (reg) {
    case NPCM7XX_FIU_UMA_CFG:
        if (s->regs[reg] & FIU_UMA_CFG_CMMLCK) {
            value &= ~FIU_UMA_CFG_CMMLCK_MASK;
            value |= (s->regs[reg] & FIU_UMA_CFG_CMMLCK_MASK);
        }
        /* fall through */
    case NPCM7XX_FIU_DRD_CFG:
    case NPCM7XX_FIU_DWR_CFG:
        if (s->regs[reg] & NPCM7XX_FIU_CFG_LCK) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: write to locked register @ 0x%" PRIx64 "\n",
                          DEVICE(s)->canonical_path, addr);
            return;
        }
        s->regs[reg] = value;
        break;

    case NPCM7XX_FIU_UMA_CTS:
        if (value & FIU_UMA_CTS_RDYST) {
            value &= ~FIU_UMA_CTS_RDYST;
        } else {
            value |= s->regs[reg] & FIU_UMA_CTS_RDYST;
        }
        if ((s->regs[reg] ^ value) & FIU_UMA_CTS_SW_CS) {
            if (value & FIU_UMA_CTS_SW_CS) {
                /*
                 * Don't drop CS if there's a transfer in progress, or we're
                 * about to start one.
                 */
                if (!((value | s->regs[reg]) & FIU_UMA_CTS_EXEC_DONE)) {
                    npcm7xx_fiu_deselect(s);
                }
            } else {
                int cs_id = FIU_UMA_CTS_DEV_NUM(s->regs[NPCM7XX_FIU_UMA_CTS]);
                npcm7xx_fiu_select(s, cs_id);
            }
        }
        s->regs[reg] = value | (s->regs[reg] & FIU_UMA_CTS_EXEC_DONE);
        if (value & FIU_UMA_CTS_EXEC_DONE) {
            npcm7xx_fiu_uma_transaction(s);
        }
        break;

    case NPCM7XX_FIU_UMA_DR0 ... NPCM7XX_FIU_UMA_DR3:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: write to read-only register @ 0x%" PRIx64 "\n",
                      DEVICE(s)->canonical_path, addr);
        return;

    case NPCM7XX_FIU_PRT_CFG:
    case NPCM7XX_FIU_PRT_CMD0 ... NPCM7XX_FIU_PRT_CMD9:
        qemu_log_mask(LOG_UNIMP, "%s: PRT is not implemented\n", __func__);
        break;

    case NPCM7XX_FIU_UMA_CMD:
    case NPCM7XX_FIU_UMA_ADDR:
    case NPCM7XX_FIU_UMA_DW0 ... NPCM7XX_FIU_UMA_DW3:
    case NPCM7XX_FIU_CFG:
        s->regs[reg] = value;
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: write to invalid offset 0x%" PRIx64 "\n",
                      DEVICE(s)->canonical_path, addr);
        return;
    }
}

static const MemoryRegionOps npcm7xx_fiu_ctrl_ops = {
    .read = npcm7xx_fiu_ctrl_read,
    .write = npcm7xx_fiu_ctrl_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
};

static void npcm7xx_fiu_enter_reset(Object *obj, ResetType type)
{
    NPCM7xxFIUState *s = NPCM7XX_FIU(obj);

    trace_npcm7xx_fiu_enter_reset(DEVICE(obj)->canonical_path, type);

    memset(s->regs, 0, sizeof(s->regs));

    s->regs[NPCM7XX_FIU_DRD_CFG] = 0x0300100b;
    s->regs[NPCM7XX_FIU_DWR_CFG] = 0x03000002;
    s->regs[NPCM7XX_FIU_UMA_CFG] = 0x00000400;
    s->regs[NPCM7XX_FIU_UMA_CTS] = 0x00010000;
    s->regs[NPCM7XX_FIU_UMA_CMD] = 0x0000000b;
    s->regs[NPCM7XX_FIU_PRT_CFG] = 0x00000400;
    s->regs[NPCM7XX_FIU_CFG] = 0x0000000b;
}

static void npcm7xx_fiu_hold_reset(Object *obj, ResetType type)
{
    NPCM7xxFIUState *s = NPCM7XX_FIU(obj);
    int i;

    trace_npcm7xx_fiu_hold_reset(DEVICE(obj)->canonical_path);

    for (i = 0; i < s->cs_count; i++) {
        qemu_irq_raise(s->cs_lines[i]);
    }
}

static void npcm7xx_fiu_realize(DeviceState *dev, Error **errp)
{
    NPCM7xxFIUState *s = NPCM7XX_FIU(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    int i;

    if (s->cs_count <= 0) {
        error_setg(errp, "%s: %d chip selects specified, need at least one",
                   dev->canonical_path, s->cs_count);
        return;
    }

    if (s->flash_size == 0) {
        error_setg(errp, "%s: flash size must be set", dev->canonical_path);
        return;
    }

    if (s->flash_size > NPCM7XX_FIU_MAX_FLASH_WINDOW_SIZE) {
        error_setg(errp, "%s: flash size should not exceed 128 MiB",
                   dev->canonical_path);
        return;
    }

    s->spi = ssi_create_bus(dev, "spi");
    s->cs_lines = g_new0(qemu_irq, s->cs_count);
    qdev_init_gpio_out_named(DEVICE(s), s->cs_lines, "cs", s->cs_count);
    s->flash = g_new0(NPCM7xxFIUFlash, s->cs_count);

    /*
     * Register the control registers region first. It may be followed by one
     * or more direct flash access regions.
     */
    memory_region_init_io(&s->mmio, OBJECT(s), &npcm7xx_fiu_ctrl_ops, s, "ctrl",
                          NPCM7XX_FIU_CTRL_REGS_SIZE);
    sysbus_init_mmio(sbd, &s->mmio);

    for (i = 0; i < s->cs_count; i++) {
        NPCM7xxFIUFlash *flash = &s->flash[i];
        flash->fiu = s;
        memory_region_init_io(&flash->direct_access, OBJECT(s),
                              &npcm7xx_fiu_flash_ops, &s->flash[i], "flash",
                              s->flash_size);
        sysbus_init_mmio(sbd, &flash->direct_access);
    }
}

static const VMStateDescription vmstate_npcm7xx_fiu = {
    .name = "npcm7xx-fiu",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (const VMStateField[]) {
        VMSTATE_INT32(active_cs, NPCM7xxFIUState),
        VMSTATE_UINT32_ARRAY(regs, NPCM7xxFIUState, NPCM7XX_FIU_NR_REGS),
        VMSTATE_END_OF_LIST(),
    },
};

static const Property npcm7xx_fiu_properties[] = {
    DEFINE_PROP_INT32("cs-count", NPCM7xxFIUState, cs_count, 0),
    DEFINE_PROP_SIZE("flash-size", NPCM7xxFIUState, flash_size, 0),
};

static void npcm7xx_fiu_class_init(ObjectClass *klass, const void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    QEMU_BUILD_BUG_ON(NPCM7XX_FIU_REGS_END > NPCM7XX_FIU_NR_REGS);

    dc->desc = "NPCM7xx Flash Interface Unit";
    dc->realize = npcm7xx_fiu_realize;
    dc->vmsd = &vmstate_npcm7xx_fiu;
    rc->phases.enter = npcm7xx_fiu_enter_reset;
    rc->phases.hold = npcm7xx_fiu_hold_reset;
    device_class_set_props(dc, npcm7xx_fiu_properties);
}

static const TypeInfo npcm7xx_fiu_types[] = {
    {
        .name = TYPE_NPCM7XX_FIU,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(NPCM7xxFIUState),
        .class_init = npcm7xx_fiu_class_init,
    },
};
DEFINE_TYPES(npcm7xx_fiu_types);

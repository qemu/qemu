/*
 * NPCM7xx SD-3.0 / eMMC-4.51 Host Controller
 *
 * Copyright (c) 2021 Google LLC
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

#include "hw/sd/npcm7xx_sdhci.h"
#include "migration/vmstate.h"
#include "sdhci-internal.h"
#include "qemu/log.h"

static uint64_t npcm7xx_sdhci_read(void *opaque, hwaddr addr, unsigned int size)
{
    NPCM7xxSDHCIState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case NPCM7XX_PRSTVALS_0:
    case NPCM7XX_PRSTVALS_1:
    case NPCM7XX_PRSTVALS_2:
    case NPCM7XX_PRSTVALS_3:
    case NPCM7XX_PRSTVALS_4:
    case NPCM7XX_PRSTVALS_5:
        val = s->regs.prstvals[(addr - NPCM7XX_PRSTVALS_0) / 2];
        break;
    case NPCM7XX_BOOTTOCTRL:
        val = s->regs.boottoctrl;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "SDHCI read of nonexistent reg: 0x%02"
                      HWADDR_PRIx, addr);
        break;
    }

    return val;
}

static void npcm7xx_sdhci_write(void *opaque, hwaddr addr, uint64_t val,
                                unsigned int size)
{
    NPCM7xxSDHCIState *s = opaque;

    switch (addr) {
    case NPCM7XX_BOOTTOCTRL:
        s->regs.boottoctrl = val;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "SDHCI write of nonexistent reg: 0x%02"
                      HWADDR_PRIx, addr);
        break;
    }
}

static bool npcm7xx_sdhci_check_mem_op(void *opaque, hwaddr addr,
                                       unsigned size, bool is_write,
                                       MemTxAttrs attrs)
{
    switch (addr) {
    case NPCM7XX_PRSTVALS_0:
    case NPCM7XX_PRSTVALS_1:
    case NPCM7XX_PRSTVALS_2:
    case NPCM7XX_PRSTVALS_3:
    case NPCM7XX_PRSTVALS_4:
    case NPCM7XX_PRSTVALS_5:
        /* RO Word */
        return !is_write && size == 2;
    case NPCM7XX_BOOTTOCTRL:
        /* R/W Dword */
        return size == 4;
    default:
        return false;
    }
}

static const MemoryRegionOps npcm7xx_sdhci_ops = {
    .read = npcm7xx_sdhci_read,
    .write = npcm7xx_sdhci_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
        .unaligned = false,
        .accepts = npcm7xx_sdhci_check_mem_op,
    },
};

static void npcm7xx_sdhci_realize(DeviceState *dev, Error **errp)
{
    NPCM7xxSDHCIState *s = NPCM7XX_SDHCI(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    SysBusDevice *sbd_sdhci = SYS_BUS_DEVICE(&s->sdhci);

    memory_region_init(&s->container, OBJECT(s),
                       "npcm7xx.sdhci-container", 0x1000);
    sysbus_init_mmio(sbd, &s->container);

    memory_region_init_io(&s->iomem, OBJECT(s), &npcm7xx_sdhci_ops, s,
                          TYPE_NPCM7XX_SDHCI, NPCM7XX_SDHCI_REGSIZE);
    memory_region_add_subregion_overlap(&s->container, NPCM7XX_PRSTVALS,
                                        &s->iomem, 1);

    sysbus_realize(sbd_sdhci, errp);
    memory_region_add_subregion(&s->container, 0,
                                sysbus_mmio_get_region(sbd_sdhci, 0));

    /* propagate irq and "sd-bus" from generic-sdhci */
    sysbus_pass_irq(sbd, sbd_sdhci);
    s->bus = qdev_get_child_bus(DEVICE(sbd_sdhci), "sd-bus");

    /* Set the read only preset values. */
    memset(s->regs.prstvals, 0, sizeof(s->regs.prstvals));
    s->regs.prstvals[0] = NPCM7XX_PRSTVALS_0_RESET;
    s->regs.prstvals[1] = NPCM7XX_PRSTVALS_1_RESET;
    s->regs.prstvals[3] = NPCM7XX_PRSTVALS_3_RESET;
}

static void npcm7xx_sdhci_reset(DeviceState *dev)
{
    NPCM7xxSDHCIState *s = NPCM7XX_SDHCI(dev);
    device_cold_reset(DEVICE(&s->sdhci));
    s->regs.boottoctrl = 0;

    s->sdhci.prnsts = NPCM7XX_PRSNTS_RESET;
    s->sdhci.blkgap = NPCM7XX_BLKGAP_RESET;
    s->sdhci.capareg = NPCM7XX_CAPAB_RESET;
    s->sdhci.maxcurr = NPCM7XX_MAXCURR_RESET;
    s->sdhci.version = NPCM7XX_HCVER_RESET;
}

static const VMStateDescription vmstate_npcm7xx_sdhci = {
    .name = TYPE_NPCM7XX_SDHCI,
    .version_id = 0,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(regs.boottoctrl, NPCM7xxSDHCIState),
        VMSTATE_END_OF_LIST(),
    },
};

static void npcm7xx_sdhci_class_init(ObjectClass *classp, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(classp);

    dc->desc = "NPCM7xx SD/eMMC Host Controller";
    dc->realize = npcm7xx_sdhci_realize;
    dc->reset = npcm7xx_sdhci_reset;
    dc->vmsd = &vmstate_npcm7xx_sdhci;
}

static void npcm7xx_sdhci_instance_init(Object *obj)
{
    NPCM7xxSDHCIState *s = NPCM7XX_SDHCI(obj);

    object_initialize_child(OBJECT(s), "generic-sdhci", &s->sdhci,
                            TYPE_SYSBUS_SDHCI);
}

static const TypeInfo npcm7xx_sdhci_types[] = {
    {
        .name           = TYPE_NPCM7XX_SDHCI,
        .parent         = TYPE_SYS_BUS_DEVICE,
        .instance_size  = sizeof(NPCM7xxSDHCIState),
        .instance_init  = npcm7xx_sdhci_instance_init,
        .class_init     = npcm7xx_sdhci_class_init,
    },
};

DEFINE_TYPES(npcm7xx_sdhci_types)

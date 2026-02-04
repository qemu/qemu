/*
 * ASPEED LTPI Controller
 *
 * Copyright (C) 2025 ASPEED Technology Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "migration/vmstate.h"
#include "hw/misc/aspeed_ltpi.h"

#define ASPEED_LTPI_CTRL_BASE   0x000
#define ASPEED_LTPI_PHY_BASE    0x200
#define ASPEED_LTPI_TOP_BASE    0x800

#define LTPI_CTRL_LINK_MNG 0x42
#define LTPI_PHY_MODE 0x0

static uint64_t aspeed_ltpi_top_read(void *opaque, hwaddr offset, unsigned size)
{
    AspeedLTPIState *s = opaque;
    uint32_t idx = offset >> 2;

    return s->top_regs[idx];
}

static void aspeed_ltpi_top_write(void *opaque, hwaddr offset,
                              uint64_t val, unsigned size)
{
    AspeedLTPIState *s = opaque;
    uint32_t idx = offset >> 2;

    switch (offset) {
    default:
        s->top_regs[idx] = (uint32_t)val;
        break;
    }
}

static const MemoryRegionOps aspeed_ltpi_top_ops = {
    .read = aspeed_ltpi_top_read,
    .write = aspeed_ltpi_top_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static uint64_t aspeed_ltpi_phy_read(void *opaque, hwaddr offset, unsigned size)
{
    AspeedLTPIState *s = opaque;
    uint32_t idx = offset >> 2;

    return s->phy_regs[idx];
}

static void aspeed_ltpi_phy_write(void *opaque, hwaddr offset,
                              uint64_t val, unsigned size)
{
    AspeedLTPIState *s = opaque;
    uint32_t idx = offset >> 2;

    switch (offset) {
    default:
        s->phy_regs[idx] = (uint32_t)val;
        break;
    }
}

static const MemoryRegionOps aspeed_ltpi_phy_ops = {
    .read = aspeed_ltpi_phy_read,
    .write = aspeed_ltpi_phy_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static uint64_t aspeed_ltpi_ctrl_read(void *opaque,
                                      hwaddr offset, unsigned size)
{
    AspeedLTPIState *s = opaque;
    uint32_t idx = offset >> 2;

    return s->ctrl_regs[idx];
}

static void aspeed_ltpi_ctrl_write(void *opaque, hwaddr offset,
                              uint64_t val, unsigned size)
{
    AspeedLTPIState *s = opaque;
    uint32_t idx = offset >> 2;

    switch (offset) {
    default:
        s->ctrl_regs[idx] = (uint32_t)val;
        break;
    }
}

static const MemoryRegionOps aspeed_ltpi_ctrl_ops = {
    .read = aspeed_ltpi_ctrl_read,
    .write = aspeed_ltpi_ctrl_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static void aspeed_ltpi_reset(DeviceState *dev)
{
    AspeedLTPIState *s = ASPEED_LTPI(dev);

    memset(s->ctrl_regs, 0, sizeof(s->ctrl_regs));
    memset(s->phy_regs, 0, sizeof(s->phy_regs));
    memset(s->top_regs, 0, sizeof(s->top_regs));
    /* set default values */
    s->ctrl_regs[LTPI_CTRL_LINK_MNG] = 0x11900007;
    s->phy_regs[LTPI_PHY_MODE] = 0x2;
}


static const VMStateDescription vmstate_aspeed_ltpi = {
    .name = TYPE_ASPEED_LTPI,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(ctrl_regs, AspeedLTPIState,
                             ASPEED_LTPI_CTRL_SIZE >> 2),
        VMSTATE_UINT32_ARRAY(phy_regs, AspeedLTPIState,
                             ASPEED_LTPI_PHY_SIZE >> 2),
        VMSTATE_UINT32_ARRAY(top_regs, AspeedLTPIState,
                             ASPEED_LTPI_TOP_SIZE >> 2),

        VMSTATE_END_OF_LIST()
    }
};

static void aspeed_ltpi_realize(DeviceState *dev, Error **errp)
{
    AspeedLTPIState *s = ASPEED_LTPI(dev);

    memory_region_init(&s->mmio, OBJECT(s), TYPE_ASPEED_LTPI,
                       ASPEED_LTPI_TOTAL_SIZE);

    memory_region_init_io(&s->mmio_ctrl, OBJECT(s),
                          &aspeed_ltpi_ctrl_ops, s,
                          "aspeed-ltpi-ctrl", ASPEED_LTPI_CTRL_SIZE);

    memory_region_init_io(&s->mmio_phy, OBJECT(s),
                          &aspeed_ltpi_phy_ops, s,
                          "aspeed-ltpi-phy", ASPEED_LTPI_PHY_SIZE);

    memory_region_init_io(&s->mmio_top, OBJECT(s),
                          &aspeed_ltpi_top_ops, s,
                          "aspeed-ltpi-top", ASPEED_LTPI_TOP_SIZE);

    memory_region_add_subregion(&s->mmio,
                                ASPEED_LTPI_CTRL_BASE, &s->mmio_ctrl);
    memory_region_add_subregion(&s->mmio,
                                ASPEED_LTPI_PHY_BASE, &s->mmio_phy);
    memory_region_add_subregion(&s->mmio,
                                ASPEED_LTPI_TOP_BASE, &s->mmio_top);

    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->mmio);
}

static void aspeed_ltpi_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->realize = aspeed_ltpi_realize;
    dc->vmsd = &vmstate_aspeed_ltpi;
    device_class_set_legacy_reset(dc, aspeed_ltpi_reset);
}

static const TypeInfo aspeed_ltpi_info = {
    .name          = TYPE_ASPEED_LTPI,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AspeedLTPIState),
    .class_init    = aspeed_ltpi_class_init,
};

static void aspeed_ltpi_register_types(void)
{
    type_register_static(&aspeed_ltpi_info);
}

type_init(aspeed_ltpi_register_types);

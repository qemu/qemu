/*
 * PRCM (Clock and Reset Controller) in Tenstorrent Atlantis SoC
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright 2026 Tenstorrent
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "hw/misc/tt_atlantis_prcm.h"
#include "trace.h"

#define TO_REG(offset) ((offset) >> 2)

#define PLL_LOCK_BIT        31
#define PLL_FBDIV_LSB       14
#define PLL_POSTDIV2_LSB    11
#define PLL_POSTDIV1_LSB    8
#define PLL_REFDIV_LSB      2
#define PLL_BYPASS_BIT      1

#define PLL_RESET_VAL(BYPASS, REFDIV, POSTDIV1, POSTDIV2, FBDIV, LOCK)         \
  ((LOCK << PLL_LOCK_BIT) | (FBDIV << PLL_FBDIV_LSB) |                         \
   (POSTDIV2 << PLL_POSTDIV2_LSB) | (POSTDIV1 << PLL_POSTDIV1_LSB) |           \
   (REFDIV << PLL_REFDIV_LSB) | (BYPASS << PLL_BYPASS_BIT))

#define RCPU_PLL_RCPU_CFG   TO_REG(0x000)
#define RCPU_PLL_NOCC_CFG   TO_REG(0x004)
#define RCPU_NOCC_CLK_CFG   TO_REG(0x008)
#define RCPU_RCPU_DIV_CFG   TO_REG(0x00c)
#define RCPU_RCPU_BLK_CG    TO_REG(0x014)
#define RCPU_LSIO_BLK_CG    TO_REG(0x018)
#define RCPU_RCPU_BLK_RST   TO_REG(0x01c)
#define RCPU_LSIO_BLK_RST   TO_REG(0x020)
#define RCPU_PLL_RCPU_EN    TO_REG(0x11c)
#define RCPU_PLL_NOCC_EN    TO_REG(0x120)
#define RCPU_PLL_MM_EN      TO_REG(0x124)
#define RCPU_PLL1_MM_EN     TO_REG(0x128)
#define RCPU_PLL_HSIO_EN    TO_REG(0x12c)
#define RCPU_PLL_PCIE_EN    TO_REG(0x138)
#define RCPU_PLL_I2S_EN     TO_REG(0x13c)
#define RCPU_PLL_MM_CFG     TO_REG(0x19c)
#define RCPU_PLL_HSIO_CFG   TO_REG(0x1a0)
#define RCPU_PLL_PCIE_CFG   TO_REG(0x1ac)
#define RCPU_BOOT_MODE_CFG  TO_REG(0x1b0)
#define RCPU_PLL_I2S_CFG    TO_REG(0x1f4)
#define RCPU_I2S_DIV_CFG    TO_REG(0x1f8)
#define RCPU_BUS_CFG        TO_REG(0x1fc)
#define RCPU_PLL1_MM_CFG    TO_REG(0x2a4)

#define HSIO_HSIO_DIV_CFG       TO_REG(0x00)
#define HSIO_HSIO_DIV_CFG1      TO_REG(0x04)
#define HSIO_HSIO_BLK_CG        TO_REG(0x08)
#define HSIO_HSIO_BLK_RST       TO_REG(0x0c)
#define HSIO_HSIO_GMAC_DIV_CFG  TO_REG(0x10)

#define PCIE_SUBS_RST_CTL   TO_REG(0x0)
#define PCIE_SUBS_CG_CTL    TO_REG(0x4)
#define PCIE_SUBS_CK_CTL    TO_REG(0x8)

#define MM_MM_CLK_GATE_CFG  TO_REG(0x008)
#define MM_MM_CLK_DIV_CFG   TO_REG(0x00c)
#define MM_MM_CLK_SEL_CFG   TO_REG(0x010)
#define MM_MM_RSTN          TO_REG(0x014)
#define MM_MM_CLK_DIV_CFG1  TO_REG(0x130)

#define DDRC_STAT           TO_REG(0x10014)
#define DDRC_DFISTAT        TO_REG(0x10514)
#define DDRC_SWSTAT         TO_REG(0x10c84)

#define PLL_EN              BIT(0)
#define RCPU_PLL_BUS_CG_EN  BIT(7)
#define HSIO_PLL_BUS_CG_EN  BIT(3)
#define PCIE_PLL_BUS_CG_EN  BIT(1)
#define NOCC_PLL_BUS_CG_EN  BIT(0)

#define RCPU_BOOT_MODE_UART      (0x0 << 4)
#define RCPU_BOOT_MODE_NOR       (0x1 << 4)
#define RCPU_BOOT_MODE_SD        (0x2 << 4)

#define RCPU_DOMAIN_SIZE   0x10000
#define HSIO_DOMAIN_SIZE   0x510
#define PCIE_DOMAIN_SIZE   0x100
#define MM_DOMAIN_SIZE     0x1000
#define DDRC0_DOMAIN_SIZE  0x101000
#define DDRC1_DOMAIN_SIZE  0x101000

static uint64_t tt_atlantis_prcm_read(void *opaque, hwaddr offset,
                                      unsigned size)
{
    TTAtlantisPRCMState *s = TT_ATLANTIS_PRCM(opaque);
    TTAtlantisPRCMClass *c = TT_ATLANTIS_PRCM_GET_CLASS(s);
    uint32_t data;

    if (offset >= c->regs_size) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Out-of-bounds read at offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
        return 0;
    }
    data = s->regs[TO_REG(offset)];

    trace_tt_atlantis_prcm_read(c->domain, offset, data);
    return data;
}

static void tt_atlantis_prcm_write(void *opaque, hwaddr offset,
                                   uint64_t data, unsigned size)
{
    TTAtlantisPRCMState *s = TT_ATLANTIS_PRCM(opaque);
    TTAtlantisPRCMClass *c = TT_ATLANTIS_PRCM_GET_CLASS(s);

    if (offset >= c->regs_size) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Out-of-bounds write at offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
        return;
    }

    trace_tt_atlantis_prcm_write(c->domain, offset, data);

    s->regs[TO_REG(offset)] = data;
}

static void tt_atlantis_prcm_rcpu_write(void *opaque, hwaddr offset,
                                        uint64_t data, unsigned size)
{
    TTAtlantisPRCMState *s = TT_ATLANTIS_PRCM(opaque);
    uint32_t pll_reg_offset;

    tt_atlantis_prcm_write(opaque, offset, data, size);

    switch (TO_REG(offset)) {
    case RCPU_PLL_RCPU_EN:
        pll_reg_offset = RCPU_PLL_RCPU_CFG;
        break;
    case RCPU_PLL_NOCC_EN:
        pll_reg_offset = RCPU_PLL_NOCC_CFG;
        break;
    case RCPU_PLL_HSIO_EN:
        pll_reg_offset = RCPU_PLL_HSIO_CFG;
        break;
    case RCPU_PLL_PCIE_EN:
        pll_reg_offset = RCPU_PLL_PCIE_CFG;
        break;
    case RCPU_PLL_MM_EN:
        pll_reg_offset = RCPU_PLL_MM_CFG;
        break;
    case RCPU_PLL1_MM_EN:
        pll_reg_offset = RCPU_PLL1_MM_CFG;
        break;
    case RCPU_PLL_I2S_EN:
        pll_reg_offset = RCPU_PLL_I2S_CFG;
        break;
    default:
        return;
    }

    if (data & PLL_EN) {
        s->regs[pll_reg_offset] |= BIT(PLL_LOCK_BIT);
    } else {
        s->regs[pll_reg_offset] &= ~BIT(PLL_LOCK_BIT);
    }
}

static const MemoryRegionOps tt_atlantis_prcm_ops = {
    .read = tt_atlantis_prcm_read,
    .write = tt_atlantis_prcm_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
};

static const MemoryRegionOps tt_atlantis_prcm_rcpu_ops = {
    .read = tt_atlantis_prcm_read,
    .write = tt_atlantis_prcm_rcpu_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
};

static void tt_atlantis_prcm_realize(DeviceState *dev, Error **errp)
{
    TTAtlantisPRCMState *s = TT_ATLANTIS_PRCM(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    TTAtlantisPRCMClass *c = TT_ATLANTIS_PRCM_GET_CLASS(s);

    assert(c->domain < TT_ATLANTIS_PRCM_DOMAIN_COUNT);

    s->regs = g_new0(uint32_t, TO_REG(c->regs_size));

    memory_region_init_io(&s->mmio, OBJECT(s), c->ops, s,
                          TYPE_TT_ATLANTIS_PRCM, c->regs_size);
    sysbus_init_mmio(sbd, &s->mmio);
}

static void tt_atlantis_prcm_reset_hold(Object *obj, ResetType type)
{
    TTAtlantisPRCMState *s = TT_ATLANTIS_PRCM(obj);
    TTAtlantisPRCMClass *c = TT_ATLANTIS_PRCM_GET_CLASS(s);

    memset(s->regs, 0, c->regs_size);
}

static void tt_atlantis_prcm_rcpu_reset_hold(Object *obj, ResetType type)
{
    TTAtlantisPRCMState *s = TT_ATLANTIS_PRCM(obj);
    tt_atlantis_prcm_reset_hold(obj, type);

    /* Reset values come from Datasheet */
    s->regs[RCPU_PLL_RCPU_CFG] = PLL_RESET_VAL(1, 0, 4, 0, 100, 1);
    s->regs[RCPU_PLL_NOCC_CFG] = PLL_RESET_VAL(0, 2, 2, 2, 267, 1);
    s->regs[RCPU_NOCC_CLK_CFG] = (0x25f31 << 1) | BIT(0);
    s->regs[RCPU_RCPU_DIV_CFG] = (0xb8c2 << 1);
    s->regs[RCPU_RCPU_BLK_CG] = 0;
    s->regs[RCPU_LSIO_BLK_CG] = 0;
    s->regs[RCPU_RCPU_BLK_RST] = 0xffffffff;
    s->regs[RCPU_LSIO_BLK_RST] = 0xffffffff;
    s->regs[RCPU_PLL_RCPU_EN] = PLL_EN;
    s->regs[RCPU_PLL_NOCC_EN] = PLL_EN;
    s->regs[RCPU_PLL_HSIO_EN] = 0;
    s->regs[RCPU_PLL_MM_EN] = 0;
    s->regs[RCPU_PLL1_MM_EN] = 0;
    s->regs[RCPU_PLL_I2S_EN] = 0;
    s->regs[RCPU_PLL_PCIE_EN] = 0;
    s->regs[RCPU_PLL_HSIO_CFG] = PLL_RESET_VAL(0, 1, 2, 1, 50, 0);
    s->regs[RCPU_PLL_PCIE_CFG] = PLL_RESET_VAL(0, 2, 3, 1, 250, 0);
    s->regs[RCPU_BOOT_MODE_CFG] = RCPU_BOOT_MODE_SD;
    s->regs[RCPU_PLL_MM_CFG] = PLL_RESET_VAL(0, 1, 2, 1, 50, 1);
    s->regs[RCPU_PLL1_MM_CFG] = PLL_RESET_VAL(0, 1, 2, 1, 50, 1);
    s->regs[RCPU_PLL_I2S_CFG] = PLL_RESET_VAL(0, 1, 6, 6, 33, 0);
    s->regs[RCPU_I2S_DIV_CFG] = 0xde69ad31;
    s->regs[RCPU_BUS_CFG] = NOCC_PLL_BUS_CG_EN | HSIO_PLL_BUS_CG_EN;
}

static void tt_atlantis_prcm_ddrc_reset_hold(Object *obj, ResetType type)
{
    TTAtlantisPRCMState *s = TT_ATLANTIS_PRCM(obj);
    tt_atlantis_prcm_reset_hold(obj, type);

    /* Reset values come from Datasheet */
    s->regs[DDRC_STAT] = 0x1;
    s->regs[DDRC_DFISTAT] = 0x1;
    s->regs[DDRC_SWSTAT] = 0x1;
}

static void tt_atlantis_prcm_hsio_reset_hold(Object *obj, ResetType type)
{
    TTAtlantisPRCMState *s = TT_ATLANTIS_PRCM(obj);
    tt_atlantis_prcm_reset_hold(obj, type);

    /* Reset values come from Datasheet */
    s->regs[HSIO_HSIO_DIV_CFG] = 0x20821731;
    s->regs[HSIO_HSIO_DIV_CFG1] = 0x8;
    s->regs[HSIO_HSIO_BLK_CG] = 0;
    s->regs[HSIO_HSIO_BLK_RST] = 0;
    s->regs[HSIO_HSIO_GMAC_DIV_CFG] = 0;
}

static void tt_atlantis_prcm_pcie_reset_hold(Object *obj, ResetType type)
{
    TTAtlantisPRCMState *s = TT_ATLANTIS_PRCM(obj);
    tt_atlantis_prcm_reset_hold(obj, type);

    /* Reset values come from Datasheet */
    s->regs[PCIE_SUBS_RST_CTL] = 0;
    s->regs[PCIE_SUBS_CG_CTL] = 0;
    s->regs[PCIE_SUBS_CK_CTL] = 0x44908;
}

static void tt_atlantis_prcm_mm_reset_hold(Object *obj, ResetType type)
{
    TTAtlantisPRCMState *s = TT_ATLANTIS_PRCM(obj);
    tt_atlantis_prcm_reset_hold(obj, type);

    /* Reset values come from Datasheet */
    s->regs[MM_MM_CLK_GATE_CFG] = 0;
    s->regs[MM_MM_CLK_DIV_CFG] = 0xddc8312;
    s->regs[MM_MM_CLK_SEL_CFG] = 0;
    s->regs[MM_MM_RSTN] = 0;
    s->regs[MM_MM_CLK_DIV_CFG1] = 0x333;
}

static void tt_atlantis_prcm_unrealize(DeviceState *dev)
{
    TTAtlantisPRCMState *s = TT_ATLANTIS_PRCM(dev);

    g_free(s->regs);
}

static void tt_atlantis_prcm_class_init(ObjectClass *klass,
                                        const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    TTAtlantisPRCMClass *c = TT_ATLANTIS_PRCM_CLASS(klass);

    dc->realize = tt_atlantis_prcm_realize;
    dc->unrealize = tt_atlantis_prcm_unrealize;
    dc->desc = "Tenstorrent Atlantis PRCM Controller";
    rc->phases.hold = tt_atlantis_prcm_reset_hold;
    c->ops = &tt_atlantis_prcm_ops;
}

static void tt_atlantis_prcm_rcpu_class_init(ObjectClass *klass,
                                             const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    TTAtlantisPRCMClass *c = TT_ATLANTIS_PRCM_CLASS(klass);

    dc->desc = "Tenstorrent Atlantis RCPU PRCM Controller";
    rc->phases.hold = tt_atlantis_prcm_rcpu_reset_hold;
    c->domain = TT_ATLANTIS_PRCM_DOMAIN_RCPU;
    c->regs_size = RCPU_DOMAIN_SIZE;
    c->ops = &tt_atlantis_prcm_rcpu_ops;
}

static void tt_atlantis_prcm_hsio_class_init(ObjectClass *klass,
                                             const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    TTAtlantisPRCMClass *c = TT_ATLANTIS_PRCM_CLASS(klass);

    dc->desc = "Tenstorrent Atlantis HSIO PRCM Controller";
    rc->phases.hold = tt_atlantis_prcm_hsio_reset_hold;
    c->domain = TT_ATLANTIS_PRCM_DOMAIN_HSIO;
    c->regs_size = HSIO_DOMAIN_SIZE;
}

static void tt_atlantis_prcm_pcie_class_init(ObjectClass *klass,
                                             const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    TTAtlantisPRCMClass *c = TT_ATLANTIS_PRCM_CLASS(klass);

    dc->desc = "Tenstorrent Atlantis PCIE PRCM Controller";
    rc->phases.hold = tt_atlantis_prcm_pcie_reset_hold;
    c->domain = TT_ATLANTIS_PRCM_DOMAIN_PCIE;
    c->regs_size = PCIE_DOMAIN_SIZE;
}

static void tt_atlantis_prcm_mm_class_init(ObjectClass *klass,
                                           const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    TTAtlantisPRCMClass *c = TT_ATLANTIS_PRCM_CLASS(klass);

    dc->desc = "Tenstorrent Atlantis MM PRCM Controller";
    rc->phases.hold = tt_atlantis_prcm_mm_reset_hold;
    c->domain = TT_ATLANTIS_PRCM_DOMAIN_MM;
    c->regs_size = MM_DOMAIN_SIZE;
}

static void tt_atlantis_prcm_ddrc0_class_init(ObjectClass *klass,
                                              const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    TTAtlantisPRCMClass *c = TT_ATLANTIS_PRCM_CLASS(klass);

    dc->desc = "Tenstorrent Atlantis DDR Memory Controller 0 PRCM";
    rc->phases.hold = tt_atlantis_prcm_ddrc_reset_hold;
    c->domain = TT_ATLANTIS_PRCM_DOMAIN_DDRC0;
    c->regs_size = DDRC0_DOMAIN_SIZE;
}

static void tt_atlantis_prcm_ddrc1_class_init(ObjectClass *klass,
                                              const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    TTAtlantisPRCMClass *c = TT_ATLANTIS_PRCM_CLASS(klass);

    dc->desc = "Tenstorrent Atlantis DDR Memory Controller 1 PRCM";
    rc->phases.hold = tt_atlantis_prcm_ddrc_reset_hold;
    c->domain = TT_ATLANTIS_PRCM_DOMAIN_DDRC1;
    c->regs_size = DDRC1_DOMAIN_SIZE;
}

static const TypeInfo tt_atlantis_prcm_types[] = {
    {
        .name = TYPE_TT_ATLANTIS_PRCM,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(TTAtlantisPRCMState),
        .class_init = tt_atlantis_prcm_class_init,
        .class_size = sizeof(TTAtlantisPRCMClass),
        .abstract = true,
    },
    {
        .name = TYPE_TT_ATLANTIS_PRCM_RCPU,
        .parent = TYPE_TT_ATLANTIS_PRCM,
        .class_init = tt_atlantis_prcm_rcpu_class_init,
    },
    {
        .name = TYPE_TT_ATLANTIS_PRCM_HSIO,
        .parent = TYPE_TT_ATLANTIS_PRCM,
        .class_init = tt_atlantis_prcm_hsio_class_init,
    },
    {
        .name = TYPE_TT_ATLANTIS_PRCM_PCIE,
        .parent = TYPE_TT_ATLANTIS_PRCM,
        .class_init = tt_atlantis_prcm_pcie_class_init,
    },
    {
        .name = TYPE_TT_ATLANTIS_PRCM_MM,
        .parent = TYPE_TT_ATLANTIS_PRCM,
        .class_init = tt_atlantis_prcm_mm_class_init,
    },
    {
        .name = TYPE_TT_ATLANTIS_PRCM_DDRC0,
        .parent = TYPE_TT_ATLANTIS_PRCM,
        .class_init = tt_atlantis_prcm_ddrc0_class_init,
    },
    {
        .name = TYPE_TT_ATLANTIS_PRCM_DDRC1,
        .parent = TYPE_TT_ATLANTIS_PRCM,
        .class_init = tt_atlantis_prcm_ddrc1_class_init,
    },
};

DEFINE_TYPES(tt_atlantis_prcm_types);

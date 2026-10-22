/*
 * Qualcomm QDSP6SS clock blocks
 *
 * Firmware brings the DSP's PLLs up by programming them, releasing reset,
 * selecting the RUN mode and then polling for lock; it then asks the root
 * clock generator to switch over to the new clock by setting UPDATE and
 * waiting for the hardware to clear it.  Neither the frequencies nor the
 * clock tree matter to the model: these devices only complete those
 * handshakes, and otherwise hold whatever is written to them.
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/core/resettable.h"
#include "hw/core/sysbus.h"
#include "hw/misc/qcom-qdsp6ss-clk.h"
#include "migration/vmstate.h"
#include "qom/object.h"

#define QCOM_CLK_REGS_SIZE 0x1000
#define QCOM_CLK_NUM_REGS (QCOM_CLK_REGS_SIZE / 4)

/* Lucid PLL registers */
#define PLL_MODE            0x00
#define PLL_OPMODE          0x38

#define PLL_MODE_OUTCTRL    BIT(0)
#define PLL_MODE_BYPASSNL   BIT(1)
#define PLL_MODE_RESET_N    BIT(2)
#define PLL_MODE_ACTIVE     BIT(30)
#define PLL_MODE_LOCK_DET   BIT(31)

#define PLL_OPMODE_MASK     0x7
#define PLL_OPMODE_RUN      0x1

/* RCG registers */
#define RCG_CMD             0x20

#define RCG_CMD_UPDATE      BIT(0)
#define RCG_CMD_ROOT_EN     BIT(1)
#define RCG_CMD_ROOT_OFF    BIT(31)

typedef struct QcomClkRegsState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    uint32_t regs[QCOM_CLK_NUM_REGS];
} QcomClkRegsState;

#define TYPE_QCOM_CLK_REGS "qcom-clk-regs"
OBJECT_DECLARE_SIMPLE_TYPE(QcomClkRegsState, QCOM_CLK_REGS)

static uint64_t qcom_clk_read(void *opaque, hwaddr offset, unsigned size)
{
    QcomClkRegsState *s = opaque;

    return s->regs[offset / 4];
}

static void qcom_lucid_pll_write(void *opaque, hwaddr offset, uint64_t value,
                                 unsigned size)
{
    QcomClkRegsState *s = opaque;

    s->regs[offset / 4] = value;
    if (offset == PLL_MODE || offset == PLL_OPMODE) {
        uint32_t mode = s->regs[PLL_MODE / 4];
        uint32_t opmode = s->regs[PLL_OPMODE / 4];

        /* The PLL locks once it is out of reset and running */
        if ((mode & PLL_MODE_RESET_N) &&
            (opmode & PLL_OPMODE_MASK) == PLL_OPMODE_RUN) {
            mode |= PLL_MODE_LOCK_DET | PLL_MODE_ACTIVE;
        } else {
            mode &= ~(PLL_MODE_LOCK_DET | PLL_MODE_ACTIVE);
        }
        s->regs[PLL_MODE / 4] = mode;
    }
}

static void qcom_rcg_write(void *opaque, hwaddr offset, uint64_t value,
                           unsigned size)
{
    QcomClkRegsState *s = opaque;

    s->regs[offset / 4] = value;
    if (offset == RCG_CMD) {
        /* The update completes at once; the root is on if it was enabled */
        value &= ~(RCG_CMD_UPDATE | RCG_CMD_ROOT_OFF);
        if (!(value & RCG_CMD_ROOT_EN)) {
            value |= RCG_CMD_ROOT_OFF;
        }
        s->regs[RCG_CMD / 4] = value;
    }
}

static const MemoryRegionOps qcom_lucid_pll_ops = {
    .read = qcom_clk_read,
    .write = qcom_lucid_pll_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static const MemoryRegionOps qcom_rcg_ops = {
    .read = qcom_clk_read,
    .write = qcom_rcg_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void qcom_clk_realize(DeviceState *dev, const MemoryRegionOps *ops,
                             const char *name)
{
    QcomClkRegsState *s = QCOM_CLK_REGS(dev);

    memory_region_init_io(&s->iomem, OBJECT(dev), ops, s, name,
                          QCOM_CLK_REGS_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->iomem);
}

static void qcom_lucid_pll_realize(DeviceState *dev, Error **errp)
{
    qcom_clk_realize(dev, &qcom_lucid_pll_ops, TYPE_QCOM_LUCID_PLL);
}

static void qcom_rcg_realize(DeviceState *dev, Error **errp)
{
    qcom_clk_realize(dev, &qcom_rcg_ops, TYPE_QCOM_RCG);
}

static void qcom_clk_reset_hold(Object *obj, ResetType type)
{
    QcomClkRegsState *s = QCOM_CLK_REGS(obj);

    memset(s->regs, 0, sizeof(s->regs));
}

static const VMStateDescription vmstate_qcom_clk = {
    .name = "qcom-qdsp6ss-clk",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, QcomClkRegsState, QCOM_CLK_NUM_REGS),
        VMSTATE_END_OF_LIST()
    }
};

static void qcom_clk_regs_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->vmsd = &vmstate_qcom_clk;
    RESETTABLE_CLASS(oc)->phases.hold = qcom_clk_reset_hold;
}

static void qcom_lucid_pll_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = qcom_lucid_pll_realize;
    dc->desc = "Qualcomm Lucid alpha PLL";
}

static void qcom_rcg_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = qcom_rcg_realize;
    dc->desc = "Qualcomm root clock generator";
}

static const TypeInfo qcom_qdsp6ss_clk_types[] = {
    {
        .name = TYPE_QCOM_CLK_REGS,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(QcomClkRegsState),
        .class_init = qcom_clk_regs_class_init,
        .abstract = true,
    },
    {
        .name = TYPE_QCOM_LUCID_PLL,
        .parent = TYPE_QCOM_CLK_REGS,
        .class_init = qcom_lucid_pll_class_init,
    },
    {
        .name = TYPE_QCOM_RCG,
        .parent = TYPE_QCOM_CLK_REGS,
        .class_init = qcom_rcg_class_init,
    },
};

DEFINE_TYPES(qcom_qdsp6ss_clk_types)

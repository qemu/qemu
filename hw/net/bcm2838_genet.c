/*
 * BCM2838 Gigabit Ethernet emulation
 *
 * Copyright (C) 2022 Ovchinnikov Vitalii <vitalii.ovchinnikov@auriga.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "net/eth.h"
#include "qapi/error.h"
#include "hw/core/irq.h"
#include "net/checksum.h"
#include "system/dma.h"
#include "hw/core/registerfields.h"
#include "hw/core/resettable.h"
#include "hw/net/bcm2838_genet.h"
#include "trace.h"

REG32(GENET_SYS_REV_CTRL,              0)
FIELD(GENET_SYS_REV_CTRL, GPHY_REV,    0, 16)
FIELD(GENET_SYS_REV_CTRL, MINOR_REV,   16, 4)
FIELD(GENET_SYS_REV_CTRL, RSVD_20_23,  20, 4)
FIELD(GENET_SYS_REV_CTRL, MAJOR_REV,   24, 4)
FIELD(GENET_SYS_REV_CTRL, RSVD_28_31,  28, 4)

REG32(GENET_INTRL_0,                0)
FIELD(GENET_INTRL_0, SCB,           0, 1)
FIELD(GENET_INTRL_0, EPHY,          1, 1)
FIELD(GENET_INTRL_0, PHY_DET_R,     2, 1)
FIELD(GENET_INTRL_0, PHY_DET_F,     3, 1)
FIELD(GENET_INTRL_0, LINK_UP,       4, 1)
FIELD(GENET_INTRL_0, LINK_DOWN,     5, 1)
FIELD(GENET_INTRL_0, UMAC,          6, 1)
FIELD(GENET_INTRL_0, UMAC_TSV,      7, 1)
FIELD(GENET_INTRL_0, TBUF_UNDERRUN, 8, 1)
FIELD(GENET_INTRL_0, RBUF_OVERFLOW, 9, 1)
FIELD(GENET_INTRL_0, HFB_SM,        10, 1)
FIELD(GENET_INTRL_0, HFB_MM,        11, 1)
FIELD(GENET_INTRL_0, MPD_R,         12, 1)
FIELD(GENET_INTRL_0, RXDMA_MBDONE,  13, 1)
FIELD(GENET_INTRL_0, RXDMA_PDONE,   14, 1)
FIELD(GENET_INTRL_0, RXDMA_BDONE,   15, 1)
FIELD(GENET_INTRL_0, TXDMA_MBDONE,  16, 1)
FIELD(GENET_INTRL_0, TXDMA_PDONE,   17, 1)
FIELD(GENET_INTRL_0, TXDMA_BDONE,   18, 1)
FIELD(GENET_INTRL_0, RSVD_19_22,    19, 4)
FIELD(GENET_INTRL_0, MDIO_DONE,     23, 1)
FIELD(GENET_INTRL_0, MDIO_ERROR,    24, 1)
FIELD(GENET_INTRL_0, RSVD_25_31,    25, 4)

REG32(GENET_INTRL_1,              0)
FIELD(GENET_INTRL_1, TX_INTRS,    0, 16)
FIELD(GENET_INTRL_1, RX_INTRS,    16, 16)


static uint64_t bcm2838_genet_read(void *opaque, hwaddr offset, unsigned size)
{
    uint64_t value = ~0;

    qemu_log_mask(
        LOG_GUEST_ERROR,
        "%s: out-of-range access, %u bytes @ offset 0x%04" PRIx64 "\n",
        __func__, size, offset);

    trace_bcm2838_genet_read(size, offset, value);
    return value;
}

static void bcm2838_genet_write(void *opaque, hwaddr offset, uint64_t value,
                                unsigned size) {
    qemu_log_mask(
        LOG_GUEST_ERROR,
        "%s: out-of-range access, %u bytes @ offset 0x%04" PRIx64 "\n",
        __func__, size, offset);
}

static const MemoryRegionOps bcm2838_genet_ops = {
    .read = bcm2838_genet_read,
    .write = bcm2838_genet_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {.min_access_size = 4, .max_access_size = 4},
    .valid = {.min_access_size = 4, .max_access_size = 4},
};


static void bcm2838_genet_realize(DeviceState *dev, Error **errp)
{
    BCM2838GenetState *s = BCM2838_GENET(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    /* Controller registers */
    memory_region_init_io(&s->regs_mr, OBJECT(s), &bcm2838_genet_ops, s,
                          "bcm2838_genet_regs", sizeof(s->regs));
    sysbus_init_mmio(sbd, &s->regs_mr);
}

static void bcm2838_genet_phy_reset(BCM2838GenetState *s)
{
    trace_bcm2838_genet_phy_reset("done");
}

static void bcm2838_genet_reset(Object *obj, ResetType type)
{
    BCM2838GenetState *s = BCM2838_GENET(obj);

    memset(&s->regs, 0x00, sizeof(s->regs));

    trace_bcm2838_genet_reset("done");

    bcm2838_genet_phy_reset(s);
}

static void bcm2838_genet_class_init(ObjectClass *class, const void *data)
{
    static ResettablePhases unused_parent_phases;
    DeviceClass *dc = DEVICE_CLASS(class);
    ResettableClass *rc = RESETTABLE_CLASS(class);

    dc->realize = bcm2838_genet_realize;
    resettable_class_set_parent_phases(rc, NULL, bcm2838_genet_reset, NULL,
                                       &unused_parent_phases);
}

static const TypeInfo bcm2838_genet_info = {
    .name       = TYPE_BCM2838_GENET,
    .parent     = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(BCM2838GenetState),
    .class_init = bcm2838_genet_class_init,
};

static void bcm2838_genet_register(void)
{
    type_register_static(&bcm2838_genet_info);
}

type_init(bcm2838_genet_register)

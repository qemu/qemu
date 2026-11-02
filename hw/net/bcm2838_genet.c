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
#include "hw/core/resettable.h"
#include "hw/net/bcm2838_genet.h"
#include "trace.h"


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

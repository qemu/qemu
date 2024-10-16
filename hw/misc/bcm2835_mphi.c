/*
 * BCM2835 SOC MPHI emulation
 *
 * Very basic emulation, only providing the FIQ interrupt needed to
 * allow the dwc-otg USB host controller driver in the Raspbian kernel
 * to function.
 *
 * Copyright (c) 2020 Paul Zimmerman <pauldzim@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/misc/bcm2835_mphi.h"
#include "migration/vmstate.h"
#include "qemu/error-report.h"
#include "qemu/log.h"
#include "qemu/main-loop.h"

static inline void mphi_raise_irq(BCM2835MphiState *s)
{
    qemu_set_irq(s->irq, 1);
}

static inline void mphi_lower_irq(BCM2835MphiState *s)
{
    qemu_set_irq(s->irq, 0);
}

static uint64_t mphi_reg_read(void *ptr, hwaddr addr, unsigned size)
{
    BCM2835MphiState *s = ptr;
    uint32_t val = 0;

    switch (addr) {
    case 0x28:  /* outdda */
        val = s->outdda;
        break;
    case 0x2c:  /* outddb */
        val = s->outddb;
        break;
    case 0x4c:  /* ctrl */
        val = s->ctrl;
        val |= 1 << 17;
        break;
    case 0x50:  /* intstat */
        val = s->intstat;
        break;
    case 0x1f0: /* swirq_set */
        val = s->swirq;
        break;
    case 0x1f4: /* swirq_clr */
        val = s->swirq;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "read from unknown register");
        break;
    }

    return val;
}

static void mphi_reg_write(void *ptr, hwaddr addr, uint64_t val, unsigned size)
{
    BCM2835MphiState *s = ptr;
    int do_irq = 0;

    switch (addr) {
    case 0x28:  /* outdda */
        s->outdda = val;
        break;
    case 0x2c:  /* outddb */
        s->outddb = val;
        if (val & (1 << 29)) {
            do_irq = 1;
        }
        break;
    case 0x4c:  /* ctrl */
        s->ctrl = val;
        if (val & (1 << 16)) {
            do_irq = -1;
        }
        break;
    case 0x50:  /* intstat */
        s->intstat = val;
        if (val & ((1 << 16) | (1 << 29))) {
            do_irq = -1;
        }
        break;
    case 0x1f0: /* swirq_set */
        s->swirq |= val;
        do_irq = 1;
        break;
    case 0x1f4: /* swirq_clr */
        s->swirq &= ~val;
        do_irq = -1;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "write to unknown register");
        return;
    }

    if (do_irq > 0) {
        mphi_raise_irq(s);
    } else if (do_irq < 0) {
        mphi_lower_irq(s);
    }
}

static const MemoryRegionOps mphi_mmio_ops = {
    .read = mphi_reg_read,
    .write = mphi_reg_write,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void mphi_reset(DeviceState *dev)
{
    BCM2835MphiState *s = BCM2835_MPHI(dev);

    s->outdda = 0;
    s->outddb = 0;
    s->ctrl = 0;
    s->intstat = 0;
    s->swirq = 0;
}

static void mphi_realize(DeviceState *dev, Error **errp)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    BCM2835MphiState *s = BCM2835_MPHI(dev);

    sysbus_init_irq(sbd, &s->irq);
}

static void mphi_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    BCM2835MphiState *s = BCM2835_MPHI(obj);

    memory_region_init_io(&s->iomem, obj, &mphi_mmio_ops, s, "mphi", MPHI_MMIO_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
}

const VMStateDescription vmstate_mphi_state = {
    .name = "mphi",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(outdda, BCM2835MphiState),
        VMSTATE_UINT32(outddb, BCM2835MphiState),
        VMSTATE_UINT32(ctrl, BCM2835MphiState),
        VMSTATE_UINT32(intstat, BCM2835MphiState),
        VMSTATE_UINT32(swirq, BCM2835MphiState),
        VMSTATE_END_OF_LIST()
    }
};

static void mphi_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = mphi_realize;
    device_class_set_legacy_reset(dc, mphi_reset);
    dc->vmsd = &vmstate_mphi_state;
}

static const TypeInfo bcm2835_mphi_type_info = {
    .name          = TYPE_BCM2835_MPHI,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(BCM2835MphiState),
    .instance_init = mphi_init,
    .class_init    = mphi_class_init,
};

static void bcm2835_mphi_register_types(void)
{
    type_register_static(&bcm2835_mphi_type_info);
}

type_init(bcm2835_mphi_register_types)

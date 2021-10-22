/*
 * Aspeed SD Host Controller
 * Eddie James <eajames@linux.ibm.com>
 *
 * Copyright (C) 2019 IBM Corp
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "hw/sd/aspeed_sdhci.h"
#include "qapi/error.h"
#include "hw/irq.h"
#include "migration/vmstate.h"
#include "hw/qdev-properties.h"
#include "trace.h"

#define ASPEED_SDHCI_INFO            0x00
#define  ASPEED_SDHCI_INFO_SLOT1     (1 << 17)
#define  ASPEED_SDHCI_INFO_SLOT0     (1 << 16)
#define  ASPEED_SDHCI_INFO_RESET     (1 << 0)
#define ASPEED_SDHCI_DEBOUNCE        0x04
#define  ASPEED_SDHCI_DEBOUNCE_RESET 0x00000005
#define ASPEED_SDHCI_BUS             0x08
#define ASPEED_SDHCI_SDIO_140        0x10
#define ASPEED_SDHCI_SDIO_148        0x18
#define ASPEED_SDHCI_SDIO_240        0x20
#define ASPEED_SDHCI_SDIO_248        0x28
#define ASPEED_SDHCI_WP_POL          0xec
#define ASPEED_SDHCI_CARD_DET        0xf0
#define ASPEED_SDHCI_IRQ_STAT        0xfc

#define TO_REG(addr) ((addr) / sizeof(uint32_t))

static uint64_t aspeed_sdhci_read(void *opaque, hwaddr addr, unsigned int size)
{
    uint32_t val = 0;
    AspeedSDHCIState *sdhci = opaque;

    switch (addr) {
    case ASPEED_SDHCI_SDIO_140:
        val = (uint32_t)sdhci->slots[0].capareg;
        break;
    case ASPEED_SDHCI_SDIO_148:
        val = (uint32_t)sdhci->slots[0].maxcurr;
        break;
    case ASPEED_SDHCI_SDIO_240:
        val = (uint32_t)sdhci->slots[1].capareg;
        break;
    case ASPEED_SDHCI_SDIO_248:
        val = (uint32_t)sdhci->slots[1].maxcurr;
        break;
    default:
        if (addr < ASPEED_SDHCI_REG_SIZE) {
            val = sdhci->regs[TO_REG(addr)];
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: Out-of-bounds read at 0x%" HWADDR_PRIx "\n",
                          __func__, addr);
        }
    }

    trace_aspeed_sdhci_read(addr, size, (uint64_t) val);

    return (uint64_t)val;
}

static void aspeed_sdhci_write(void *opaque, hwaddr addr, uint64_t val,
                               unsigned int size)
{
    AspeedSDHCIState *sdhci = opaque;

    trace_aspeed_sdhci_write(addr, size, val);

    switch (addr) {
    case ASPEED_SDHCI_INFO:
        /* The RESET bit automatically clears. */
        sdhci->regs[TO_REG(addr)] = (uint32_t)val & ~ASPEED_SDHCI_INFO_RESET;
        break;
    case ASPEED_SDHCI_SDIO_140:
        sdhci->slots[0].capareg = (uint64_t)(uint32_t)val;
        break;
    case ASPEED_SDHCI_SDIO_148:
        sdhci->slots[0].maxcurr = (uint64_t)(uint32_t)val;
        break;
    case ASPEED_SDHCI_SDIO_240:
        sdhci->slots[1].capareg = (uint64_t)(uint32_t)val;
        break;
    case ASPEED_SDHCI_SDIO_248:
        sdhci->slots[1].maxcurr = (uint64_t)(uint32_t)val;
        break;
    default:
        if (addr < ASPEED_SDHCI_REG_SIZE) {
            sdhci->regs[TO_REG(addr)] = (uint32_t)val;
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: Out-of-bounds write at 0x%" HWADDR_PRIx "\n",
                          __func__, addr);
        }
    }
}

static const MemoryRegionOps aspeed_sdhci_ops = {
    .read = aspeed_sdhci_read,
    .write = aspeed_sdhci_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};

static void aspeed_sdhci_set_irq(void *opaque, int n, int level)
{
    AspeedSDHCIState *sdhci = opaque;

    if (level) {
        sdhci->regs[TO_REG(ASPEED_SDHCI_IRQ_STAT)] |= BIT(n);

        qemu_irq_raise(sdhci->irq);
    } else {
        sdhci->regs[TO_REG(ASPEED_SDHCI_IRQ_STAT)] &= ~BIT(n);

        qemu_irq_lower(sdhci->irq);
    }
}

static void aspeed_sdhci_realize(DeviceState *dev, Error **errp)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    AspeedSDHCIState *sdhci = ASPEED_SDHCI(dev);

    /* Create input irqs for the slots */
    qdev_init_gpio_in_named_with_opaque(DEVICE(sbd), aspeed_sdhci_set_irq,
                                        sdhci, NULL, sdhci->num_slots);

    sysbus_init_irq(sbd, &sdhci->irq);
    memory_region_init_io(&sdhci->iomem, OBJECT(sdhci), &aspeed_sdhci_ops,
                          sdhci, TYPE_ASPEED_SDHCI, 0x1000);
    sysbus_init_mmio(sbd, &sdhci->iomem);

    for (int i = 0; i < sdhci->num_slots; ++i) {
        Object *sdhci_slot = OBJECT(&sdhci->slots[i]);
        SysBusDevice *sbd_slot = SYS_BUS_DEVICE(&sdhci->slots[i]);

        if (!object_property_set_int(sdhci_slot, "sd-spec-version", 2, errp)) {
            return;
        }

        if (!object_property_set_uint(sdhci_slot, "capareg",
                                      ASPEED_SDHCI_CAPABILITIES, errp)) {
            return;
        }

        if (!sysbus_realize(sbd_slot, errp)) {
            return;
        }

        sysbus_connect_irq(sbd_slot, 0, qdev_get_gpio_in(DEVICE(sbd), i));
        memory_region_add_subregion(&sdhci->iomem, (i + 1) * 0x100,
                                    &sdhci->slots[i].iomem);
    }
}

static void aspeed_sdhci_reset(DeviceState *dev)
{
    AspeedSDHCIState *sdhci = ASPEED_SDHCI(dev);

    memset(sdhci->regs, 0, ASPEED_SDHCI_REG_SIZE);

    sdhci->regs[TO_REG(ASPEED_SDHCI_INFO)] = ASPEED_SDHCI_INFO_SLOT0;
    if (sdhci->num_slots == 2) {
        sdhci->regs[TO_REG(ASPEED_SDHCI_INFO)] |= ASPEED_SDHCI_INFO_SLOT1;
    }
    sdhci->regs[TO_REG(ASPEED_SDHCI_DEBOUNCE)] = ASPEED_SDHCI_DEBOUNCE_RESET;
}

static const VMStateDescription vmstate_aspeed_sdhci = {
    .name = TYPE_ASPEED_SDHCI,
    .version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, AspeedSDHCIState, ASPEED_SDHCI_NUM_REGS),
        VMSTATE_END_OF_LIST(),
    },
};

static Property aspeed_sdhci_properties[] = {
    DEFINE_PROP_UINT8("num-slots", AspeedSDHCIState, num_slots, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void aspeed_sdhci_class_init(ObjectClass *classp, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(classp);

    dc->realize = aspeed_sdhci_realize;
    dc->reset = aspeed_sdhci_reset;
    dc->vmsd = &vmstate_aspeed_sdhci;
    device_class_set_props(dc, aspeed_sdhci_properties);
}

static TypeInfo aspeed_sdhci_info = {
    .name          = TYPE_ASPEED_SDHCI,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AspeedSDHCIState),
    .class_init    = aspeed_sdhci_class_init,
};

static void aspeed_sdhci_register_types(void)
{
    type_register_static(&aspeed_sdhci_info);
}

type_init(aspeed_sdhci_register_types)

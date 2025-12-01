/*
 * Ingenic MSC (MMC/SD Controller) stub emulation
 *
 * Copyright (c) 2024 OpenSensor Project
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This is a minimal stub to prevent kernel hangs waiting for MMC.
 * It reports no card present and clock stable to allow boot to continue.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "qom/object.h"

/* SDHCI register offsets */
#define SDHCI_COMMAND           0x0E
#define SDHCI_PRESENT_STATE     0x24
#define SDHCI_CLOCK_CONTROL     0x2C
#define SDHCI_SOFTWARE_RESET    0x2F
#define SDHCI_INT_STATUS        0x30
#define SDHCI_INT_ENABLE        0x34
#define SDHCI_SIGNAL_ENABLE     0x38
#define SDHCI_CAPABILITIES      0x40
#define SDHCI_CAPABILITIES_1    0x44
#define SDHCI_MAX_CURRENT       0x48
#define SDHCI_HOST_VERSION      0xFE

/* SDHCI_CLOCK_CONTROL bits */
#define SDHCI_CLOCK_INT_EN      0x0001
#define SDHCI_CLOCK_INT_STABLE  0x0002
#define SDHCI_CLOCK_CARD_EN     0x0004

/* SDHCI_PRESENT_STATE bits */
#define SDHCI_CARD_PRESENT      0x00010000

/* SDHCI_INT_STATUS bits */
#define SDHCI_INT_RESPONSE      0x00000001
#define SDHCI_INT_TIMEOUT       0x00010000
#define SDHCI_INT_ERROR         0x00008000

#define TYPE_INGENIC_MSC "ingenic-msc"
OBJECT_DECLARE_SIMPLE_TYPE(IngenicMSCState, INGENIC_MSC)

struct IngenicMSCState {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    qemu_irq irq;
    QEMUTimer *cmd_timer;

    uint32_t clock_control;
    uint32_t int_status;
    uint32_t int_enable;
    uint32_t signal_enable;
};

static void ingenic_msc_update_irq(IngenicMSCState *s)
{
    int level = (s->int_status & s->signal_enable) != 0;
    qemu_set_irq(s->irq, level);
}

static void ingenic_msc_cmd_complete(void *opaque)
{
    IngenicMSCState *s = INGENIC_MSC(opaque);

    /* Set timeout error - no card responding */
    s->int_status |= SDHCI_INT_TIMEOUT | SDHCI_INT_ERROR;
    ingenic_msc_update_irq(s);
}

static uint64_t ingenic_msc_read(void *opaque, hwaddr offset, unsigned size)
{
    IngenicMSCState *s = INGENIC_MSC(opaque);

    switch (offset) {
    case SDHCI_PRESENT_STATE:
        /* No card present - this prevents the driver from waiting */
        return 0;
    case SDHCI_CLOCK_CONTROL:
        /* Return clock stable if internal clock enabled */
        if (s->clock_control & SDHCI_CLOCK_INT_EN) {
            return s->clock_control | SDHCI_CLOCK_INT_STABLE;
        }
        return s->clock_control;
    case SDHCI_INT_STATUS:
        return s->int_status;
    case SDHCI_INT_ENABLE:
        return s->int_enable;
    case SDHCI_SIGNAL_ENABLE:
        return s->signal_enable;
    case SDHCI_CAPABILITIES:
        /* Basic capabilities: voltage 3.3V, high-speed, SDMA */
        return 0x01000011;
    case SDHCI_CAPABILITIES_1:
        return 0;
    case SDHCI_MAX_CURRENT:
        return 0x00000001;
    case SDHCI_HOST_VERSION:
        /* SDHCI spec version 3.0, vendor version 0 */
        return 0x0002;
    default:
        return 0;
    }
}

static void ingenic_msc_write(void *opaque, hwaddr offset,
                               uint64_t val, unsigned size)
{
    IngenicMSCState *s = INGENIC_MSC(opaque);

    switch (offset) {
    case SDHCI_COMMAND:
        /*
         * When a command is issued, schedule a timeout error after a short
         * delay. This allows the CPU to continue and prevents RCU stalls.
         * The timer will fire and generate the timeout interrupt.
         */
        timer_mod(s->cmd_timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                  1000000);  /* 1ms delay */
        break;
    case SDHCI_CLOCK_CONTROL:
        s->clock_control = val & 0xFFFF;
        break;
    case SDHCI_SOFTWARE_RESET:
        /* Reset clears interrupt status */
        if (val & 0x01) {  /* Reset all */
            s->int_status = 0;
            s->clock_control = 0;
            timer_del(s->cmd_timer);
            ingenic_msc_update_irq(s);
        }
        break;
    case SDHCI_INT_STATUS:
        /* Write 1 to clear */
        s->int_status &= ~val;
        ingenic_msc_update_irq(s);
        break;
    case SDHCI_INT_ENABLE:
        s->int_enable = val;
        break;
    case SDHCI_SIGNAL_ENABLE:
        s->signal_enable = val;
        ingenic_msc_update_irq(s);
        break;
    default:
        break;
    }
}

static const MemoryRegionOps ingenic_msc_ops = {
    .read = ingenic_msc_read,
    .write = ingenic_msc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static void ingenic_msc_realize(DeviceState *dev, Error **errp)
{
    IngenicMSCState *s = INGENIC_MSC(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    memory_region_init_io(&s->iomem, OBJECT(s), &ingenic_msc_ops, s,
                          "ingenic-msc", 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);

    s->cmd_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, ingenic_msc_cmd_complete, s);
}

static void ingenic_msc_reset(DeviceState *dev)
{
    IngenicMSCState *s = INGENIC_MSC(dev);
    s->clock_control = 0;
    s->int_status = 0;
    s->int_enable = 0;
    s->signal_enable = 0;
    timer_del(s->cmd_timer);
}

static void ingenic_msc_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->realize = ingenic_msc_realize;
    device_class_set_legacy_reset(dc, ingenic_msc_reset);
}

static const TypeInfo ingenic_msc_info = {
    .name          = TYPE_INGENIC_MSC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IngenicMSCState),
    .class_init    = ingenic_msc_class_init,
};

static void ingenic_msc_register_types(void)
{
    type_register_static(&ingenic_msc_info);
}

type_init(ingenic_msc_register_types)


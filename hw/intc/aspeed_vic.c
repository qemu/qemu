/*
 * ASPEED Interrupt Controller (New)
 *
 * Andrew Jeffery <andrew@aj.id.au>
 *
 * Copyright 2015, 2016 IBM Corp.
 *
 * This code is licensed under the GPL version 2 or later.  See
 * the COPYING file in the top-level directory.
 */

/* The hardware exposes two register sets, a legacy set and a 'new' set. The
 * model implements the 'new' register set, and logs warnings on accesses to
 * the legacy IO space.
 *
 * The hardware uses 32bit registers to manage 51 IRQs, with low and high
 * registers for each conceptual register. The device model's implementation
 * uses 64bit data types to store both low and high register values (in the one
 * member), but must cope with access offset values in multiples of 4 passed to
 * the callbacks. As such the read() and write() implementations process the
 * provided offset to understand whether the access is requesting the lower or
 * upper 32 bits of the 64bit member.
 *
 * Additionally, the "Interrupt Enable", "Edge Status" and "Software Interrupt"
 * fields have separate "enable"/"status" and "clear" registers, where set bits
 * are written to one or the other to change state (avoiding a
 * read-modify-write sequence).
 */

#include "qemu/osdep.h"
#include "hw/intc/aspeed_vic.h"
#include "hw/irq.h"
#include "migration/vmstate.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "trace.h"

#define AVIC_NEW_BASE_OFFSET 0x80

#define AVIC_L_MASK 0xFFFFFFFFU
#define AVIC_H_MASK 0x0007FFFFU
#define AVIC_EVENT_W_MASK (0x78000ULL << 32)

static void aspeed_vic_update(AspeedVICState *s)
{
    uint64_t new = (s->raw & s->enable);
    uint64_t flags;

    flags = new & s->select;
    trace_aspeed_vic_update_fiq(!!flags);
    qemu_set_irq(s->fiq, !!flags);

    flags = new & ~s->select;
    trace_aspeed_vic_update_irq(!!flags);
    qemu_set_irq(s->irq, !!flags);
}

static void aspeed_vic_set_irq(void *opaque, int irq, int level)
{
    uint64_t irq_mask;
    bool raise;
    AspeedVICState *s = (AspeedVICState *)opaque;

    if (irq > ASPEED_VIC_NR_IRQS) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Invalid interrupt number: %d\n",
                      __func__, irq);
        return;
    }

    trace_aspeed_vic_set_irq(irq, level);

    irq_mask = BIT(irq);
    if (s->sense & irq_mask) {
        /* level-triggered */
        if (s->event & irq_mask) {
            /* high-sensitive */
            raise = level;
        } else {
            /* low-sensitive */
            raise = !level;
        }
        s->raw = deposit64(s->raw, irq, 1, raise);
    } else {
        uint64_t old_level = s->level & irq_mask;

        /* edge-triggered */
        if (s->dual_edge & irq_mask) {
            raise = (!!old_level) != (!!level);
        } else {
            if (s->event & irq_mask) {
                /* rising-sensitive */
                raise = !old_level && level;
            } else {
                /* falling-sensitive */
                raise = old_level && !level;
            }
        }
        if (raise) {
            s->raw = deposit64(s->raw, irq, 1, raise);
        }
    }
    s->level = deposit64(s->level, irq, 1, level);
    aspeed_vic_update(s);
}

static uint64_t aspeed_vic_read(void *opaque, hwaddr offset, unsigned size)
{
    AspeedVICState *s = (AspeedVICState *)opaque;
    hwaddr n_offset;
    uint64_t val;
    bool high;

    if (offset < AVIC_NEW_BASE_OFFSET) {
        high = false;
        n_offset = offset;
    } else {
        high = !!(offset & 0x4);
        n_offset = (offset & ~0x4);
    }

    switch (n_offset) {
    case 0x80: /* IRQ Status */
    case 0x00:
        val = s->raw & ~s->select & s->enable;
        break;
    case 0x88: /* FIQ Status */
    case 0x04:
        val = s->raw & s->select & s->enable;
        break;
    case 0x90: /* Raw Interrupt Status */
    case 0x08:
        val = s->raw;
        break;
    case 0x98: /* Interrupt Selection */
    case 0x0c:
        val = s->select;
        break;
    case 0xa0: /* Interrupt Enable */
    case 0x10:
        val = s->enable;
        break;
    case 0xb0: /* Software Interrupt */
    case 0x18:
        val = s->trigger;
        break;
    case 0xc0: /* Interrupt Sensitivity */
    case 0x24:
        val = s->sense;
        break;
    case 0xc8: /* Interrupt Both Edge Trigger Control */
    case 0x28:
        val = s->dual_edge;
        break;
    case 0xd0: /* Interrupt Event */
    case 0x2c:
        val = s->event;
        break;
    case 0xe0: /* Edge Triggered Interrupt Status */
        val = s->raw & ~s->sense;
        break;
        /* Illegal */
    case 0xa8: /* Interrupt Enable Clear */
    case 0xb8: /* Software Interrupt Clear */
    case 0xd8: /* Edge Triggered Interrupt Clear */
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Read of write-only register with offset 0x%"
                      HWADDR_PRIx "\n", __func__, offset);
        val = 0;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad register at offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
        val = 0;
        break;
    }
    if (high) {
        val = extract64(val, 32, 19);
    } else {
        val = extract64(val, 0, 32);
    }
    trace_aspeed_vic_read(offset, size, val);
    return val;
}

static void aspeed_vic_write(void *opaque, hwaddr offset, uint64_t data,
                             unsigned size)
{
    AspeedVICState *s = (AspeedVICState *)opaque;
    hwaddr n_offset;
    bool high;

    if (offset < AVIC_NEW_BASE_OFFSET) {
        high = false;
        n_offset = offset;
    } else {
        high = !!(offset & 0x4);
        n_offset = (offset & ~0x4);
    }

    trace_aspeed_vic_write(offset, size, data);

    /* Given we have members using separate enable/clear registers, deposit64()
     * isn't quite the tool for the job. Instead, relocate the incoming bits to
     * the required bit offset based on the provided access address
     */
    if (high) {
        data &= AVIC_H_MASK;
        data <<= 32;
    } else {
        data &= AVIC_L_MASK;
    }

    switch (n_offset) {
    case 0x98: /* Interrupt Selection */
    case 0x0c:
        /* Register has deposit64() semantics - overwrite requested 32 bits */
        if (high) {
            s->select &= AVIC_L_MASK;
        } else {
            s->select &= ((uint64_t) AVIC_H_MASK) << 32;
        }
        s->select |= data;
        break;
    case 0xa0: /* Interrupt Enable */
    case 0x10:
        s->enable |= data;
        break;
    case 0xa8: /* Interrupt Enable Clear */
    case 0x14:
        s->enable &= ~data;
        break;
    case 0xb0: /* Software Interrupt */
    case 0x18:
        qemu_log_mask(LOG_UNIMP, "%s: Software interrupts unavailable. "
                      "IRQs requested: 0x%016" PRIx64 "\n", __func__, data);
        break;
    case 0xb8: /* Software Interrupt Clear */
    case 0x1c:
        qemu_log_mask(LOG_UNIMP, "%s: Software interrupts unavailable. "
                      "IRQs to be cleared: 0x%016" PRIx64 "\n", __func__, data);
        break;
    case 0xd0: /* Interrupt Event */
        /* Register has deposit64() semantics - overwrite the top four valid
         * IRQ bits, as only the top four IRQs (GPIOs) can change their event
         * type */
        if (high) {
            s->event &= ~AVIC_EVENT_W_MASK;
            s->event |= (data & AVIC_EVENT_W_MASK);
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "Ignoring invalid write to interrupt event register");
        }
        break;
    case 0xd8: /* Edge Triggered Interrupt Clear */
    case 0x38:
        s->raw &= ~(data & ~s->sense);
        break;
    case 0x80: /* IRQ Status */
    case 0x00:
    case 0x88: /* FIQ Status */
    case 0x04:
    case 0x90: /* Raw Interrupt Status */
    case 0x08:
    case 0xc0: /* Interrupt Sensitivity */
    case 0x24:
    case 0xc8: /* Interrupt Both Edge Trigger Control */
    case 0x28:
    case 0xe0: /* Edge Triggered Interrupt Status */
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Write of read-only register with offset 0x%"
                      HWADDR_PRIx "\n", __func__, offset);
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad register at offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
        break;
    }
    aspeed_vic_update(s);
}

static const MemoryRegionOps aspeed_vic_ops = {
    .read = aspeed_vic_read,
    .write = aspeed_vic_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .valid.unaligned = false,
};

static void aspeed_vic_reset(DeviceState *dev)
{
    AspeedVICState *s = ASPEED_VIC(dev);

    s->level = 0;
    s->raw = 0;
    s->select = 0;
    s->enable = 0;
    s->trigger = 0;
    s->sense = 0x1F07FFF8FFFFULL;
    s->dual_edge = 0xF800070000ULL;
    s->event = 0x5F07FFF8FFFFULL;
}

#define AVIC_IO_REGION_SIZE 0x20000

static void aspeed_vic_realize(DeviceState *dev, Error **errp)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    AspeedVICState *s = ASPEED_VIC(dev);

    memory_region_init_io(&s->iomem, OBJECT(s), &aspeed_vic_ops, s,
                          TYPE_ASPEED_VIC, AVIC_IO_REGION_SIZE);

    sysbus_init_mmio(sbd, &s->iomem);

    qdev_init_gpio_in(dev, aspeed_vic_set_irq, ASPEED_VIC_NR_IRQS);
    sysbus_init_irq(sbd, &s->irq);
    sysbus_init_irq(sbd, &s->fiq);
}

static const VMStateDescription vmstate_aspeed_vic = {
    .name = "aspeed.new-vic",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64(level, AspeedVICState),
        VMSTATE_UINT64(raw, AspeedVICState),
        VMSTATE_UINT64(select, AspeedVICState),
        VMSTATE_UINT64(enable, AspeedVICState),
        VMSTATE_UINT64(trigger, AspeedVICState),
        VMSTATE_UINT64(sense, AspeedVICState),
        VMSTATE_UINT64(dual_edge, AspeedVICState),
        VMSTATE_UINT64(event, AspeedVICState),
        VMSTATE_END_OF_LIST()
    }
};

static void aspeed_vic_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->realize = aspeed_vic_realize;
    dc->reset = aspeed_vic_reset;
    dc->desc = "ASPEED Interrupt Controller (New)";
    dc->vmsd = &vmstate_aspeed_vic;
}

static const TypeInfo aspeed_vic_info = {
    .name = TYPE_ASPEED_VIC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AspeedVICState),
    .class_init = aspeed_vic_class_init,
};

static void aspeed_vic_register_types(void)
{
    type_register_static(&aspeed_vic_info);
}

type_init(aspeed_vic_register_types);

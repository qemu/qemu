/*
 * ColdFire Interrupt Controller emulation.
 *
 * Copyright (c) 2007 CodeSourcery.
 *
 * This code is licensed under the GPL
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "qemu/log.h"
#include "cpu.h"
#include "hw/irq.h"
#include "hw/sysbus.h"
#include "hw/m68k/mcf.h"
#include "hw/qdev-properties.h"
#include "qom/object.h"

#define TYPE_MCF_INTC "mcf-intc"
OBJECT_DECLARE_SIMPLE_TYPE(mcf_intc_state, MCF_INTC)

struct mcf_intc_state {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    uint64_t ipr;
    uint64_t imr;
    uint64_t ifr;
    uint64_t enabled;
    uint8_t icr[64];
    M68kCPU *cpu;
    int active_vector;
};

static void mcf_intc_update(mcf_intc_state *s)
{
    uint64_t active;
    int i;
    int best;
    int best_level;

    active = (s->ipr | s->ifr) & s->enabled & ~s->imr;
    best_level = 0;
    best = 64;
    if (active) {
        for (i = 0; i < 64; i++) {
            if ((active & 1) != 0 && s->icr[i] >= best_level) {
                best_level = s->icr[i];
                best = i;
            }
            active >>= 1;
        }
    }
    s->active_vector = ((best == 64) ? 24 : (best + 64));
    m68k_set_irq_level(s->cpu, best_level, s->active_vector);
}

static uint64_t mcf_intc_read(void *opaque, hwaddr addr,
                              unsigned size)
{
    int offset;
    mcf_intc_state *s = (mcf_intc_state *)opaque;
    offset = addr & 0xff;
    if (offset >= 0x40 && offset < 0x80) {
        return s->icr[offset - 0x40];
    }
    switch (offset) {
    case 0x00:
        return (uint32_t)(s->ipr >> 32);
    case 0x04:
        return (uint32_t)s->ipr;
    case 0x08:
        return (uint32_t)(s->imr >> 32);
    case 0x0c:
        return (uint32_t)s->imr;
    case 0x10:
        return (uint32_t)(s->ifr >> 32);
    case 0x14:
        return (uint32_t)s->ifr;
    case 0xe0: /* SWIACK.  */
        return s->active_vector;
    case 0xe1: case 0xe2: case 0xe3: case 0xe4:
    case 0xe5: case 0xe6: case 0xe7:
        /* LnIACK */
        qemu_log_mask(LOG_UNIMP, "%s: LnIACK not implemented (offset 0x%02x)\n",
                      __func__, offset);
        /* fallthru */
    default:
        return 0;
    }
}

static void mcf_intc_write(void *opaque, hwaddr addr,
                           uint64_t val, unsigned size)
{
    int offset;
    mcf_intc_state *s = (mcf_intc_state *)opaque;
    offset = addr & 0xff;
    if (offset >= 0x40 && offset < 0x80) {
        int n = offset - 0x40;
        s->icr[n] = val;
        if (val == 0)
            s->enabled &= ~(1ull << n);
        else
            s->enabled |= (1ull << n);
        mcf_intc_update(s);
        return;
    }
    switch (offset) {
    case 0x00: case 0x04:
        /* Ignore IPR writes.  */
        return;
    case 0x08:
        s->imr = (s->imr & 0xffffffff) | ((uint64_t)val << 32);
        break;
    case 0x0c:
        s->imr = (s->imr & 0xffffffff00000000ull) | (uint32_t)val;
        break;
    case 0x1c:
        if (val & 0x40) {
            s->imr = ~0ull;
        } else {
            s->imr |= (0x1ull << (val & 0x3f));
        }
        break;
    case 0x1d:
        if (val & 0x40) {
            s->imr = 0ull;
        } else {
            s->imr &= ~(0x1ull << (val & 0x3f));
        }
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad offset 0x%02x\n",
                      __func__, offset);
        return;
    }
    mcf_intc_update(s);
}

static void mcf_intc_set_irq(void *opaque, int irq, int level)
{
    mcf_intc_state *s = (mcf_intc_state *)opaque;
    if (irq >= 64)
        return;
    if (level)
        s->ipr |= 1ull << irq;
    else
        s->ipr &= ~(1ull << irq);
    mcf_intc_update(s);
}

static void mcf_intc_reset(DeviceState *dev)
{
    mcf_intc_state *s = MCF_INTC(dev);

    s->imr = ~0ull;
    s->ipr = 0;
    s->ifr = 0;
    s->enabled = 0;
    memset(s->icr, 0, 64);
    s->active_vector = 24;
}

static const MemoryRegionOps mcf_intc_ops = {
    .read = mcf_intc_read,
    .write = mcf_intc_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void mcf_intc_instance_init(Object *obj)
{
    mcf_intc_state *s = MCF_INTC(obj);

    memory_region_init_io(&s->iomem, obj, &mcf_intc_ops, s, "mcf", 0x100);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static Property mcf_intc_properties[] = {
    DEFINE_PROP_LINK("m68k-cpu", mcf_intc_state, cpu,
                     TYPE_M68K_CPU, M68kCPU *),
    DEFINE_PROP_END_OF_LIST(),
};

static void mcf_intc_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    device_class_set_props(dc, mcf_intc_properties);
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    dc->reset = mcf_intc_reset;
}

static const TypeInfo mcf_intc_gate_info = {
    .name          = TYPE_MCF_INTC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(mcf_intc_state),
    .instance_init = mcf_intc_instance_init,
    .class_init    = mcf_intc_class_init,
};

static void mcf_intc_register_types(void)
{
    type_register_static(&mcf_intc_gate_info);
}

type_init(mcf_intc_register_types)

qemu_irq *mcf_intc_init(MemoryRegion *sysmem,
                        hwaddr base,
                        M68kCPU *cpu)
{
    DeviceState  *dev;

    dev = qdev_new(TYPE_MCF_INTC);
    object_property_set_link(OBJECT(dev), "m68k-cpu",
                             OBJECT(cpu), &error_abort);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    memory_region_add_subregion(sysmem, base,
                                sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0));

    return qemu_allocate_irqs(mcf_intc_set_irq, dev, 64);
}

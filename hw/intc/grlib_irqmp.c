/*
 * QEMU GRLIB IRQMP Emulator
 *
 * (Extended interrupt not supported)
 *
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2010-2024 AdaCore
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "hw/irq.h"
#include "hw/sysbus.h"

#include "hw/qdev-properties.h"
#include "hw/intc/grlib_irqmp.h"

#include "trace.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "qom/object.h"

#define IRQMP_MAX_CPU 16
#define IRQMP_REG_SIZE 256      /* Size of memory mapped registers */

/* Memory mapped register offsets */
#define LEVEL_OFFSET     0x00
#define PENDING_OFFSET   0x04
#define FORCE0_OFFSET    0x08
#define CLEAR_OFFSET     0x0C
#define MP_STATUS_OFFSET 0x10
#define BROADCAST_OFFSET 0x14
#define MASK_OFFSET      0x40
#define FORCE_OFFSET     0x80
#define EXTENDED_OFFSET  0xC0

/* Multiprocessor Status Register  */
#define MP_STATUS_CPU_STATUS_MASK ((1 << IRQMP_MAX_CPU)-2)
#define MP_STATUS_NCPU_SHIFT      28

#define MAX_PILS 16

OBJECT_DECLARE_SIMPLE_TYPE(IRQMP, GRLIB_IRQMP)

typedef struct IRQMPState IRQMPState;

struct IRQMP {
    SysBusDevice parent_obj;

    MemoryRegion iomem;

    unsigned int ncpus;
    IRQMPState *state;
    qemu_irq start_signal[IRQMP_MAX_CPU];
    qemu_irq irq[IRQMP_MAX_CPU];
};

struct IRQMPState {
    uint32_t level;
    uint32_t pending;
    uint32_t clear;
    uint32_t mpstatus;
    uint32_t broadcast;

    uint32_t mask[IRQMP_MAX_CPU];
    uint32_t force[IRQMP_MAX_CPU];
    uint32_t extended[IRQMP_MAX_CPU];

    IRQMP    *parent;
};

static void grlib_irqmp_check_irqs(IRQMPState *state)
{
    int i;

    assert(state != NULL);
    assert(state->parent != NULL);

    for (i = 0; i < state->parent->ncpus; i++) {
        uint32_t pend = (state->pending | state->force[i]) & state->mask[i];
        uint32_t level0 = pend & ~state->level;
        uint32_t level1 = pend &  state->level;

        trace_grlib_irqmp_check_irqs(state->pending, state->force[i],
                                     state->mask[i], level1, level0);

        /* Trigger level1 interrupt first and level0 if there is no level1 */
        qemu_set_irq(state->parent->irq[i], level1 ?: level0);
    }
}

static void grlib_irqmp_ack_mask(IRQMPState *state, unsigned int cpu,
                                 uint32_t mask)
{
    /* Clear registers */
    state->pending  &= ~mask;
    state->force[cpu] &= ~mask;

    grlib_irqmp_check_irqs(state);
}

void grlib_irqmp_ack(DeviceState *dev, unsigned int cpu, int intno)
{
    IRQMP        *irqmp = GRLIB_IRQMP(dev);
    IRQMPState   *state;
    uint32_t      mask;

    state = irqmp->state;
    assert(state != NULL);

    intno &= 15;
    mask = 1 << intno;

    trace_grlib_irqmp_ack(intno);

    grlib_irqmp_ack_mask(state, cpu, mask);
}

static void grlib_irqmp_set_irq(void *opaque, int irq, int level)
{
    IRQMP      *irqmp = GRLIB_IRQMP(opaque);
    IRQMPState *s;
    int         i = 0;

    s = irqmp->state;
    assert(s         != NULL);
    assert(s->parent != NULL);


    if (level) {
        trace_grlib_irqmp_set_irq(irq);

        if (s->broadcast & 1 << irq) {
            /* Broadcasted IRQ */
            for (i = 0; i < IRQMP_MAX_CPU; i++) {
                s->force[i] |= 1 << irq;
            }
        } else {
            s->pending |= 1 << irq;
        }
        grlib_irqmp_check_irqs(s);
    }
}

static uint64_t grlib_irqmp_read(void *opaque, hwaddr addr,
                                 unsigned size)
{
    IRQMP      *irqmp = opaque;
    IRQMPState *state;

    assert(irqmp != NULL);
    state = irqmp->state;
    assert(state != NULL);

    addr &= 0xff;

    /* global registers */
    switch (addr) {
    case LEVEL_OFFSET:
        return state->level;

    case PENDING_OFFSET:
        return state->pending;

    case FORCE0_OFFSET:
        /* This register is an "alias" for the force register of CPU 0 */
        return state->force[0];

    case CLEAR_OFFSET:
        /* Always read as 0 */
        return 0;

    case MP_STATUS_OFFSET:
        return state->mpstatus;

    case BROADCAST_OFFSET:
        return state->broadcast;

    default:
        break;
    }

    /* mask registers */
    if (addr >= MASK_OFFSET && addr < FORCE_OFFSET) {
        int cpu = (addr - MASK_OFFSET) / 4;
        assert(cpu >= 0 && cpu < IRQMP_MAX_CPU);

        return state->mask[cpu];
    }

    /* force registers */
    if (addr >= FORCE_OFFSET && addr < EXTENDED_OFFSET) {
        int cpu = (addr - FORCE_OFFSET) / 4;
        assert(cpu >= 0 && cpu < IRQMP_MAX_CPU);

        return state->force[cpu];
    }

    /* extended (not supported) */
    if (addr >= EXTENDED_OFFSET && addr < IRQMP_REG_SIZE) {
        int cpu = (addr - EXTENDED_OFFSET) / 4;
        assert(cpu >= 0 && cpu < IRQMP_MAX_CPU);

        return state->extended[cpu];
    }

    trace_grlib_irqmp_readl_unknown(addr);
    return 0;
}

static void grlib_irqmp_write(void *opaque, hwaddr addr,
                              uint64_t value, unsigned size)
{
    IRQMP *irqmp = opaque;
    IRQMPState *state;
    int i;

    assert(irqmp != NULL);
    state = irqmp->state;
    assert(state != NULL);

    addr &= 0xff;

    /* global registers */
    switch (addr) {
    case LEVEL_OFFSET:
        value &= 0xFFFF << 1; /* clean up the value */
        state->level = value;
        return;

    case PENDING_OFFSET:
        /* Read Only */
        return;

    case FORCE0_OFFSET:
        /* This register is an "alias" for the force register of CPU 0 */

        value &= 0xFFFE; /* clean up the value */
        state->force[0] = value;
        grlib_irqmp_check_irqs(irqmp->state);
        return;

    case CLEAR_OFFSET:
        value &= ~1; /* clean up the value */
        for (i = 0; i < irqmp->ncpus; i++) {
            grlib_irqmp_ack_mask(state, i, value);
        }
        return;

    case MP_STATUS_OFFSET:
        /*
         * Writing and reading operations are reversed for the CPU status.
         * Writing "1" will start the CPU, but reading "1" means that the CPU
         * is power-down.
         */
        value &= MP_STATUS_CPU_STATUS_MASK;
        for (i = 0; i < irqmp->ncpus; i++) {
            if ((value >> i) & 1) {
                qemu_set_irq(irqmp->start_signal[i], 1);
                state->mpstatus &= ~(1 << i);
            }
        }
        return;

    case BROADCAST_OFFSET:
        value &= 0xFFFE; /* clean up the value */
        state->broadcast = value;
        return;

    default:
        break;
    }

    /* mask registers */
    if (addr >= MASK_OFFSET && addr < FORCE_OFFSET) {
        int cpu = (addr - MASK_OFFSET) / 4;
        assert(cpu >= 0 && cpu < IRQMP_MAX_CPU);

        value &= ~1; /* clean up the value */
        state->mask[cpu] = value;
        grlib_irqmp_check_irqs(irqmp->state);
        return;
    }

    /* force registers */
    if (addr >= FORCE_OFFSET && addr < EXTENDED_OFFSET) {
        int cpu = (addr - FORCE_OFFSET) / 4;
        assert(cpu >= 0 && cpu < IRQMP_MAX_CPU);

        uint32_t force = value & 0xFFFE;
        uint32_t clear = (value >> 16) & 0xFFFE;
        uint32_t old   = state->force[cpu];

        state->force[cpu] = (old | force) & ~clear;
        grlib_irqmp_check_irqs(irqmp->state);
        return;
    }

    /* extended (not supported) */
    if (addr >= EXTENDED_OFFSET && addr < IRQMP_REG_SIZE) {
        int cpu = (addr - EXTENDED_OFFSET) / 4;
        assert(cpu >= 0 && cpu < IRQMP_MAX_CPU);

        value &= 0xF; /* clean up the value */
        state->extended[cpu] = value;
        return;
    }

    trace_grlib_irqmp_writel_unknown(addr, value);
}

static const MemoryRegionOps grlib_irqmp_ops = {
    .read = grlib_irqmp_read,
    .write = grlib_irqmp_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void grlib_irqmp_reset(DeviceState *d)
{
    IRQMP *irqmp = GRLIB_IRQMP(d);
    assert(irqmp->state != NULL);

    memset(irqmp->state, 0, sizeof *irqmp->state);
    irqmp->state->parent = irqmp;
    irqmp->state->mpstatus = ((irqmp->ncpus - 1) << MP_STATUS_NCPU_SHIFT) |
        ((1 << irqmp->ncpus) - 2);
}

static void grlib_irqmp_realize(DeviceState *dev, Error **errp)
{
    IRQMP *irqmp = GRLIB_IRQMP(dev);

    if ((!irqmp->ncpus) || (irqmp->ncpus > IRQMP_MAX_CPU)) {
        error_setg(errp, "Invalid ncpus properties: "
                   "%u, must be 0 < ncpus =< %u.", irqmp->ncpus,
                   IRQMP_MAX_CPU);
    }

    qdev_init_gpio_in(dev, grlib_irqmp_set_irq, MAX_PILS);

    /*
     * Transitionning from 0 to 1 starts the CPUs. The opposite can't
     * happen.
     */
    qdev_init_gpio_out_named(dev, irqmp->start_signal, "grlib-start-cpu",
                             IRQMP_MAX_CPU);
    qdev_init_gpio_out_named(dev, irqmp->irq, "grlib-irq", irqmp->ncpus);
    memory_region_init_io(&irqmp->iomem, OBJECT(dev), &grlib_irqmp_ops, irqmp,
                          "irqmp", IRQMP_REG_SIZE);

    irqmp->state = g_malloc0(sizeof *irqmp->state);

    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &irqmp->iomem);
}

static Property grlib_irqmp_properties[] = {
    DEFINE_PROP_UINT32("ncpus", IRQMP, ncpus, 1),
    DEFINE_PROP_END_OF_LIST(),
};

static void grlib_irqmp_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = grlib_irqmp_realize;
    dc->reset = grlib_irqmp_reset;
    device_class_set_props(dc, grlib_irqmp_properties);
}

static const TypeInfo grlib_irqmp_info = {
    .name          = TYPE_GRLIB_IRQMP,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IRQMP),
    .class_init    = grlib_irqmp_class_init,
};

static void grlib_irqmp_register_types(void)
{
    type_register_static(&grlib_irqmp_info);
}

type_init(grlib_irqmp_register_types)

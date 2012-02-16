/*
 * QEMU GRLIB IRQMP Emulator
 *
 * (Multiprocessor and extended interrupt not supported)
 *
 * Copyright (c) 2010-2011 AdaCore
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

#include "sysbus.h"
#include "cpu.h"

#include "grlib.h"

#include "trace.h"

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

typedef struct IRQMPState IRQMPState;

typedef struct IRQMP {
    SysBusDevice busdev;
    MemoryRegion iomem;

    void *set_pil_in;
    void *set_pil_in_opaque;

    IRQMPState *state;
} IRQMP;

struct IRQMPState {
    uint32_t level;
    uint32_t pending;
    uint32_t clear;
    uint32_t broadcast;

    uint32_t mask[IRQMP_MAX_CPU];
    uint32_t force[IRQMP_MAX_CPU];
    uint32_t extended[IRQMP_MAX_CPU];

    IRQMP    *parent;
};

static void grlib_irqmp_check_irqs(IRQMPState *state)
{
    uint32_t      pend   = 0;
    uint32_t      level0 = 0;
    uint32_t      level1 = 0;
    set_pil_in_fn set_pil_in;

    assert(state != NULL);
    assert(state->parent != NULL);

    /* IRQ for CPU 0 (no SMP support) */
    pend = (state->pending | state->force[0])
        & state->mask[0];

    level0 = pend & ~state->level;
    level1 = pend &  state->level;

    trace_grlib_irqmp_check_irqs(state->pending, state->force[0],
                                 state->mask[0], level1, level0);

    set_pil_in = (set_pil_in_fn)state->parent->set_pil_in;

    /* Trigger level1 interrupt first and level0 if there is no level1 */
    if (level1 != 0) {
        set_pil_in(state->parent->set_pil_in_opaque, level1);
    } else {
        set_pil_in(state->parent->set_pil_in_opaque, level0);
    }
}

void grlib_irqmp_ack(DeviceState *dev, int intno)
{
    SysBusDevice *sdev;
    IRQMP        *irqmp;
    IRQMPState   *state;
    uint32_t      mask;

    assert(dev != NULL);

    sdev = sysbus_from_qdev(dev);
    assert(sdev != NULL);

    irqmp = FROM_SYSBUS(typeof(*irqmp), sdev);
    assert(irqmp != NULL);

    state = irqmp->state;
    assert(state != NULL);

    intno &= 15;
    mask = 1 << intno;

    trace_grlib_irqmp_ack(intno);

    /* Clear registers */
    state->pending  &= ~mask;
    state->force[0] &= ~mask; /* Only CPU 0 (No SMP support) */

    grlib_irqmp_check_irqs(state);
}

void grlib_irqmp_set_irq(void *opaque, int irq, int level)
{
    IRQMP      *irqmp;
    IRQMPState *s;
    int         i = 0;

    assert(opaque != NULL);

    irqmp = FROM_SYSBUS(typeof(*irqmp), sysbus_from_qdev(opaque));
    assert(irqmp != NULL);

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

static uint64_t grlib_irqmp_read(void *opaque, target_phys_addr_t addr,
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
    case MP_STATUS_OFFSET:
        /* Always read as 0 */
        return 0;

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

static void grlib_irqmp_write(void *opaque, target_phys_addr_t addr,
                              uint64_t value, unsigned size)
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
        state->pending &= ~value;
        return;

    case MP_STATUS_OFFSET:
        /* Read Only (no SMP support) */
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
    IRQMP *irqmp = container_of(d, IRQMP, busdev.qdev);
    assert(irqmp        != NULL);
    assert(irqmp->state != NULL);

    memset(irqmp->state, 0, sizeof *irqmp->state);
    irqmp->state->parent = irqmp;
}

static int grlib_irqmp_init(SysBusDevice *dev)
{
    IRQMP *irqmp = FROM_SYSBUS(typeof(*irqmp), dev);

    assert(irqmp != NULL);

    /* Check parameters */
    if (irqmp->set_pil_in == NULL) {
        return -1;
    }

    memory_region_init_io(&irqmp->iomem, &grlib_irqmp_ops, irqmp,
                          "irqmp", IRQMP_REG_SIZE);

    irqmp->state = g_malloc0(sizeof *irqmp->state);

    sysbus_init_mmio(dev, &irqmp->iomem);

    return 0;
}

static Property grlib_irqmp_properties[] = {
    DEFINE_PROP_PTR("set_pil_in", IRQMP, set_pil_in),
    DEFINE_PROP_PTR("set_pil_in_opaque", IRQMP, set_pil_in_opaque),
    DEFINE_PROP_END_OF_LIST(),
};

static void grlib_irqmp_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);

    k->init = grlib_irqmp_init;
    dc->reset = grlib_irqmp_reset;
    dc->props = grlib_irqmp_properties;
}

static TypeInfo grlib_irqmp_info = {
    .name          = "grlib,irqmp",
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IRQMP),
    .class_init    = grlib_irqmp_class_init,
};

static void grlib_irqmp_register_types(void)
{
    type_register_static(&grlib_irqmp_info);
}

type_init(grlib_irqmp_register_types)

/*
 * ARM Generic/Distributed Interrupt Controller
 *
 * Copyright (c) 2006-2007 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licenced under the GPL.
 */

/* This file contains implementation code for the RealView EB interrupt
   controller, MPCore distributed interrupt controller and ARMv7-M
   Nested Vectored Interrupt Controller.  */

//#define DEBUG_GIC

#ifdef DEBUG_GIC
#define DPRINTF(fmt, ...) \
do { printf("arm_gic: " fmt , ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...) do {} while(0)
#endif

#ifdef NVIC
static const uint8_t gic_id[] =
{ 0x00, 0xb0, 0x1b, 0x00, 0x0d, 0xe0, 0x05, 0xb1 };
/* The NVIC has 16 internal vectors.  However these are not exposed
   through the normal GIC interface.  */
#define GIC_BASE_IRQ    32
#else
static const uint8_t gic_id[] =
{ 0x90, 0x13, 0x04, 0x00, 0x0d, 0xf0, 0x05, 0xb1 };
#define GIC_BASE_IRQ    0
#endif

typedef struct gic_irq_state
{
    /* ??? The documentation seems to imply the enable bits are global, even
       for per-cpu interrupts.  This seems strange.  */
    unsigned enabled:1;
    unsigned pending:NCPU;
    unsigned active:NCPU;
    unsigned level:NCPU;
    unsigned model:1; /* 0 = N:N, 1 = 1:N */
    unsigned trigger:1; /* nonzero = edge triggered.  */
} gic_irq_state;

#define ALL_CPU_MASK ((1 << NCPU) - 1)

#define GIC_SET_ENABLED(irq) s->irq_state[irq].enabled = 1
#define GIC_CLEAR_ENABLED(irq) s->irq_state[irq].enabled = 0
#define GIC_TEST_ENABLED(irq) s->irq_state[irq].enabled
#define GIC_SET_PENDING(irq, cm) s->irq_state[irq].pending |= (cm)
#define GIC_CLEAR_PENDING(irq, cm) s->irq_state[irq].pending &= ~(cm)
#define GIC_TEST_PENDING(irq, cm) ((s->irq_state[irq].pending & (cm)) != 0)
#define GIC_SET_ACTIVE(irq, cm) s->irq_state[irq].active |= (cm)
#define GIC_CLEAR_ACTIVE(irq, cm) s->irq_state[irq].active &= ~(cm)
#define GIC_TEST_ACTIVE(irq, cm) ((s->irq_state[irq].active & (cm)) != 0)
#define GIC_SET_MODEL(irq) s->irq_state[irq].model = 1
#define GIC_CLEAR_MODEL(irq) s->irq_state[irq].model = 0
#define GIC_TEST_MODEL(irq) s->irq_state[irq].model
#define GIC_SET_LEVEL(irq, cm) s->irq_state[irq].level = (cm)
#define GIC_CLEAR_LEVEL(irq, cm) s->irq_state[irq].level &= ~(cm)
#define GIC_TEST_LEVEL(irq, cm) ((s->irq_state[irq].level & (cm)) != 0)
#define GIC_SET_TRIGGER(irq) s->irq_state[irq].trigger = 1
#define GIC_CLEAR_TRIGGER(irq) s->irq_state[irq].trigger = 0
#define GIC_TEST_TRIGGER(irq) s->irq_state[irq].trigger
#define GIC_GET_PRIORITY(irq, cpu) \
  (((irq) < 32) ? s->priority1[irq][cpu] : s->priority2[(irq) - 32])
#ifdef NVIC
#define GIC_TARGET(irq) 1
#else
#define GIC_TARGET(irq) s->irq_target[irq]
#endif

typedef struct gic_state
{
    qemu_irq parent_irq[NCPU];
    int enabled;
    int cpu_enabled[NCPU];

    gic_irq_state irq_state[GIC_NIRQ];
#ifndef NVIC
    int irq_target[GIC_NIRQ];
#endif
    int priority1[32][NCPU];
    int priority2[GIC_NIRQ - 32];
    int last_active[GIC_NIRQ][NCPU];

    int priority_mask[NCPU];
    int running_irq[NCPU];
    int running_priority[NCPU];
    int current_pending[NCPU];

    qemu_irq *in;
#ifdef NVIC
    void *nvic;
#endif
} gic_state;

/* TODO: Many places that call this routine could be optimized.  */
/* Update interrupt status after enabled or pending bits have been changed.  */
static void gic_update(gic_state *s)
{
    int best_irq;
    int best_prio;
    int irq;
    int level;
    int cpu;
    int cm;

    for (cpu = 0; cpu < NCPU; cpu++) {
        cm = 1 << cpu;
        s->current_pending[cpu] = 1023;
        if (!s->enabled || !s->cpu_enabled[cpu]) {
	    qemu_irq_lower(s->parent_irq[cpu]);
            return;
        }
        best_prio = 0x100;
        best_irq = 1023;
        for (irq = 0; irq < GIC_NIRQ; irq++) {
            if (GIC_TEST_ENABLED(irq) && GIC_TEST_PENDING(irq, cm)) {
                if (GIC_GET_PRIORITY(irq, cpu) < best_prio) {
                    best_prio = GIC_GET_PRIORITY(irq, cpu);
                    best_irq = irq;
                }
            }
        }
        level = 0;
        if (best_prio <= s->priority_mask[cpu]) {
            s->current_pending[cpu] = best_irq;
            if (best_prio < s->running_priority[cpu]) {
                DPRINTF("Raised pending IRQ %d\n", best_irq);
                level = 1;
            }
        }
        qemu_set_irq(s->parent_irq[cpu], level);
    }
}

static void __attribute__((unused))
gic_set_pending_private(gic_state *s, int cpu, int irq)
{
    int cm = 1 << cpu;

    if (GIC_TEST_PENDING(irq, cm))
        return;

    DPRINTF("Set %d pending cpu %d\n", irq, cpu);
    GIC_SET_PENDING(irq, cm);
    gic_update(s);
}

/* Process a change in an external IRQ input.  */
static void gic_set_irq(void *opaque, int irq, int level)
{
    gic_state *s = (gic_state *)opaque;
    /* The first external input line is internal interrupt 32.  */
    irq += 32;
    if (level == GIC_TEST_LEVEL(irq, ALL_CPU_MASK))
        return;

    if (level) {
        GIC_SET_LEVEL(irq, ALL_CPU_MASK);
        if (GIC_TEST_TRIGGER(irq) || GIC_TEST_ENABLED(irq)) {
            DPRINTF("Set %d pending mask %x\n", irq, GIC_TARGET(irq));
            GIC_SET_PENDING(irq, GIC_TARGET(irq));
        }
    } else {
        GIC_CLEAR_LEVEL(irq, ALL_CPU_MASK);
    }
    gic_update(s);
}

static void gic_set_running_irq(gic_state *s, int cpu, int irq)
{
    s->running_irq[cpu] = irq;
    if (irq == 1023) {
        s->running_priority[cpu] = 0x100;
    } else {
        s->running_priority[cpu] = GIC_GET_PRIORITY(irq, cpu);
    }
    gic_update(s);
}

static uint32_t gic_acknowledge_irq(gic_state *s, int cpu)
{
    int new_irq;
    int cm = 1 << cpu;
    new_irq = s->current_pending[cpu];
    if (new_irq == 1023
            || GIC_GET_PRIORITY(new_irq, cpu) >= s->running_priority[cpu]) {
        DPRINTF("ACK no pending IRQ\n");
        return 1023;
    }
    s->last_active[new_irq][cpu] = s->running_irq[cpu];
    /* Clear pending flags for both level and edge triggered interrupts.
       Level triggered IRQs will be reasserted once they become inactive.  */
    GIC_CLEAR_PENDING(new_irq, GIC_TEST_MODEL(new_irq) ? ALL_CPU_MASK : cm);
    gic_set_running_irq(s, cpu, new_irq);
    DPRINTF("ACK %d\n", new_irq);
    return new_irq;
}

static void gic_complete_irq(gic_state * s, int cpu, int irq)
{
    int update = 0;
    int cm = 1 << cpu;
    DPRINTF("EOI %d\n", irq);
    if (s->running_irq[cpu] == 1023)
        return; /* No active IRQ.  */
    if (irq != 1023) {
        /* Mark level triggered interrupts as pending if they are still
           raised.  */
        if (!GIC_TEST_TRIGGER(irq) && GIC_TEST_ENABLED(irq)
                && GIC_TEST_LEVEL(irq, cm) && (GIC_TARGET(irq) & cm) != 0) {
            DPRINTF("Set %d pending mask %x\n", irq, cm);
            GIC_SET_PENDING(irq, cm);
            update = 1;
        }
    }
    if (irq != s->running_irq[cpu]) {
        /* Complete an IRQ that is not currently running.  */
        int tmp = s->running_irq[cpu];
        while (s->last_active[tmp][cpu] != 1023) {
            if (s->last_active[tmp][cpu] == irq) {
                s->last_active[tmp][cpu] = s->last_active[irq][cpu];
                break;
            }
            tmp = s->last_active[tmp][cpu];
        }
        if (update) {
            gic_update(s);
        }
    } else {
        /* Complete the current running IRQ.  */
        gic_set_running_irq(s, cpu, s->last_active[s->running_irq[cpu]][cpu]);
    }
}

static uint32_t gic_dist_readb(void *opaque, target_phys_addr_t offset)
{
    gic_state *s = (gic_state *)opaque;
    uint32_t res;
    int irq;
    int i;
    int cpu;
    int cm;
    int mask;

    cpu = gic_get_current_cpu();
    cm = 1 << cpu;
    if (offset < 0x100) {
#ifndef NVIC
        if (offset == 0)
            return s->enabled;
        if (offset == 4)
            return ((GIC_NIRQ / 32) - 1) | ((NCPU - 1) << 5);
        if (offset < 0x08)
            return 0;
#endif
        goto bad_reg;
    } else if (offset < 0x200) {
        /* Interrupt Set/Clear Enable.  */
        if (offset < 0x180)
            irq = (offset - 0x100) * 8;
        else
            irq = (offset - 0x180) * 8;
        irq += GIC_BASE_IRQ;
        if (irq >= GIC_NIRQ)
            goto bad_reg;
        res = 0;
        for (i = 0; i < 8; i++) {
            if (GIC_TEST_ENABLED(irq + i)) {
                res |= (1 << i);
            }
        }
    } else if (offset < 0x300) {
        /* Interrupt Set/Clear Pending.  */
        if (offset < 0x280)
            irq = (offset - 0x200) * 8;
        else
            irq = (offset - 0x280) * 8;
        irq += GIC_BASE_IRQ;
        if (irq >= GIC_NIRQ)
            goto bad_reg;
        res = 0;
        mask = (irq < 32) ?  cm : ALL_CPU_MASK;
        for (i = 0; i < 8; i++) {
            if (GIC_TEST_PENDING(irq + i, mask)) {
                res |= (1 << i);
            }
        }
    } else if (offset < 0x400) {
        /* Interrupt Active.  */
        irq = (offset - 0x300) * 8 + GIC_BASE_IRQ;
        if (irq >= GIC_NIRQ)
            goto bad_reg;
        res = 0;
        mask = (irq < 32) ?  cm : ALL_CPU_MASK;
        for (i = 0; i < 8; i++) {
            if (GIC_TEST_ACTIVE(irq + i, mask)) {
                res |= (1 << i);
            }
        }
    } else if (offset < 0x800) {
        /* Interrupt Priority.  */
        irq = (offset - 0x400) + GIC_BASE_IRQ;
        if (irq >= GIC_NIRQ)
            goto bad_reg;
        res = GIC_GET_PRIORITY(irq, cpu);
#ifndef NVIC
    } else if (offset < 0xc00) {
        /* Interrupt CPU Target.  */
        irq = (offset - 0x800) + GIC_BASE_IRQ;
        if (irq >= GIC_NIRQ)
            goto bad_reg;
        if (irq >= 29 && irq <= 31) {
            res = cm;
        } else {
            res = GIC_TARGET(irq);
        }
    } else if (offset < 0xf00) {
        /* Interrupt Configuration.  */
        irq = (offset - 0xc00) * 2 + GIC_BASE_IRQ;
        if (irq >= GIC_NIRQ)
            goto bad_reg;
        res = 0;
        for (i = 0; i < 4; i++) {
            if (GIC_TEST_MODEL(irq + i))
                res |= (1 << (i * 2));
            if (GIC_TEST_TRIGGER(irq + i))
                res |= (2 << (i * 2));
        }
#endif
    } else if (offset < 0xfe0) {
        goto bad_reg;
    } else /* offset >= 0xfe0 */ {
        if (offset & 3) {
            res = 0;
        } else {
            res = gic_id[(offset - 0xfe0) >> 2];
        }
    }
    return res;
bad_reg:
    hw_error("gic_dist_readb: Bad offset %x\n", (int)offset);
    return 0;
}

static uint32_t gic_dist_readw(void *opaque, target_phys_addr_t offset)
{
    uint32_t val;
    val = gic_dist_readb(opaque, offset);
    val |= gic_dist_readb(opaque, offset + 1) << 8;
    return val;
}

static uint32_t gic_dist_readl(void *opaque, target_phys_addr_t offset)
{
    uint32_t val;
#ifdef NVIC
    gic_state *s = (gic_state *)opaque;
    uint32_t addr;
    addr = offset;
    if (addr < 0x100 || addr > 0xd00)
        return nvic_readl(s->nvic, addr);
#endif
    val = gic_dist_readw(opaque, offset);
    val |= gic_dist_readw(opaque, offset + 2) << 16;
    return val;
}

static void gic_dist_writeb(void *opaque, target_phys_addr_t offset,
                            uint32_t value)
{
    gic_state *s = (gic_state *)opaque;
    int irq;
    int i;
    int cpu;

    cpu = gic_get_current_cpu();
    if (offset < 0x100) {
#ifdef NVIC
        goto bad_reg;
#else
        if (offset == 0) {
            s->enabled = (value & 1);
            DPRINTF("Distribution %sabled\n", s->enabled ? "En" : "Dis");
        } else if (offset < 4) {
            /* ignored.  */
        } else {
            goto bad_reg;
        }
#endif
    } else if (offset < 0x180) {
        /* Interrupt Set Enable.  */
        irq = (offset - 0x100) * 8 + GIC_BASE_IRQ;
        if (irq >= GIC_NIRQ)
            goto bad_reg;
        if (irq < 16)
          value = 0xff;
        for (i = 0; i < 8; i++) {
            if (value & (1 << i)) {
                int mask = (irq < 32) ? (1 << cpu) : GIC_TARGET(irq);
                if (!GIC_TEST_ENABLED(irq + i))
                    DPRINTF("Enabled IRQ %d\n", irq + i);
                GIC_SET_ENABLED(irq + i);
                /* If a raised level triggered IRQ enabled then mark
                   is as pending.  */
                if (GIC_TEST_LEVEL(irq + i, mask)
                        && !GIC_TEST_TRIGGER(irq + i)) {
                    DPRINTF("Set %d pending mask %x\n", irq + i, mask);
                    GIC_SET_PENDING(irq + i, mask);
                }
            }
        }
    } else if (offset < 0x200) {
        /* Interrupt Clear Enable.  */
        irq = (offset - 0x180) * 8 + GIC_BASE_IRQ;
        if (irq >= GIC_NIRQ)
            goto bad_reg;
        if (irq < 16)
          value = 0;
        for (i = 0; i < 8; i++) {
            if (value & (1 << i)) {
                if (GIC_TEST_ENABLED(irq + i))
                    DPRINTF("Disabled IRQ %d\n", irq + i);
                GIC_CLEAR_ENABLED(irq + i);
            }
        }
    } else if (offset < 0x280) {
        /* Interrupt Set Pending.  */
        irq = (offset - 0x200) * 8 + GIC_BASE_IRQ;
        if (irq >= GIC_NIRQ)
            goto bad_reg;
        if (irq < 16)
          irq = 0;

        for (i = 0; i < 8; i++) {
            if (value & (1 << i)) {
                GIC_SET_PENDING(irq + i, GIC_TARGET(irq));
            }
        }
    } else if (offset < 0x300) {
        /* Interrupt Clear Pending.  */
        irq = (offset - 0x280) * 8 + GIC_BASE_IRQ;
        if (irq >= GIC_NIRQ)
            goto bad_reg;
        for (i = 0; i < 8; i++) {
            /* ??? This currently clears the pending bit for all CPUs, even
               for per-CPU interrupts.  It's unclear whether this is the
               corect behavior.  */
            if (value & (1 << i)) {
                GIC_CLEAR_PENDING(irq + i, ALL_CPU_MASK);
            }
        }
    } else if (offset < 0x400) {
        /* Interrupt Active.  */
        goto bad_reg;
    } else if (offset < 0x800) {
        /* Interrupt Priority.  */
        irq = (offset - 0x400) + GIC_BASE_IRQ;
        if (irq >= GIC_NIRQ)
            goto bad_reg;
        if (irq < 32) {
            s->priority1[irq][cpu] = value;
        } else {
            s->priority2[irq - 32] = value;
        }
#ifndef NVIC
    } else if (offset < 0xc00) {
        /* Interrupt CPU Target.  */
        irq = (offset - 0x800) + GIC_BASE_IRQ;
        if (irq >= GIC_NIRQ)
            goto bad_reg;
        if (irq < 29)
            value = 0;
        else if (irq < 32)
            value = ALL_CPU_MASK;
        s->irq_target[irq] = value & ALL_CPU_MASK;
    } else if (offset < 0xf00) {
        /* Interrupt Configuration.  */
        irq = (offset - 0xc00) * 4 + GIC_BASE_IRQ;
        if (irq >= GIC_NIRQ)
            goto bad_reg;
        if (irq < 32)
            value |= 0xaa;
        for (i = 0; i < 4; i++) {
            if (value & (1 << (i * 2))) {
                GIC_SET_MODEL(irq + i);
            } else {
                GIC_CLEAR_MODEL(irq + i);
            }
            if (value & (2 << (i * 2))) {
                GIC_SET_TRIGGER(irq + i);
            } else {
                GIC_CLEAR_TRIGGER(irq + i);
            }
        }
#endif
    } else {
        /* 0xf00 is only handled for 32-bit writes.  */
        goto bad_reg;
    }
    gic_update(s);
    return;
bad_reg:
    hw_error("gic_dist_writeb: Bad offset %x\n", (int)offset);
}

static void gic_dist_writew(void *opaque, target_phys_addr_t offset,
                            uint32_t value)
{
    gic_dist_writeb(opaque, offset, value & 0xff);
    gic_dist_writeb(opaque, offset + 1, value >> 8);
}

static void gic_dist_writel(void *opaque, target_phys_addr_t offset,
                            uint32_t value)
{
    gic_state *s = (gic_state *)opaque;
#ifdef NVIC
    uint32_t addr;
    addr = offset;
    if (addr < 0x100 || (addr > 0xd00 && addr != 0xf00)) {
        nvic_writel(s->nvic, addr, value);
        return;
    }
#endif
    if (offset == 0xf00) {
        int cpu;
        int irq;
        int mask;

        cpu = gic_get_current_cpu();
        irq = value & 0x3ff;
        switch ((value >> 24) & 3) {
        case 0:
            mask = (value >> 16) & ALL_CPU_MASK;
            break;
        case 1:
            mask = 1 << cpu;
            break;
        case 2:
            mask = ALL_CPU_MASK ^ (1 << cpu);
            break;
        default:
            DPRINTF("Bad Soft Int target filter\n");
            mask = ALL_CPU_MASK;
            break;
        }
        GIC_SET_PENDING(irq, mask);
        gic_update(s);
        return;
    }
    gic_dist_writew(opaque, offset, value & 0xffff);
    gic_dist_writew(opaque, offset + 2, value >> 16);
}

static CPUReadMemoryFunc *gic_dist_readfn[] = {
   gic_dist_readb,
   gic_dist_readw,
   gic_dist_readl
};

static CPUWriteMemoryFunc *gic_dist_writefn[] = {
   gic_dist_writeb,
   gic_dist_writew,
   gic_dist_writel
};

#ifndef NVIC
static uint32_t gic_cpu_read(gic_state *s, int cpu, int offset)
{
    switch (offset) {
    case 0x00: /* Control */
        return s->cpu_enabled[cpu];
    case 0x04: /* Priority mask */
        return s->priority_mask[cpu];
    case 0x08: /* Binary Point */
        /* ??? Not implemented.  */
        return 0;
    case 0x0c: /* Acknowledge */
        return gic_acknowledge_irq(s, cpu);
    case 0x14: /* Runing Priority */
        return s->running_priority[cpu];
    case 0x18: /* Highest Pending Interrupt */
        return s->current_pending[cpu];
    default:
        hw_error("gic_cpu_read: Bad offset %x\n", (int)offset);
        return 0;
    }
}

static void gic_cpu_write(gic_state *s, int cpu, int offset, uint32_t value)
{
    switch (offset) {
    case 0x00: /* Control */
        s->cpu_enabled[cpu] = (value & 1);
        DPRINTF("CPU %sabled\n", s->cpu_enabled ? "En" : "Dis");
        break;
    case 0x04: /* Priority mask */
        s->priority_mask[cpu] = (value & 0xff);
        break;
    case 0x08: /* Binary Point */
        /* ??? Not implemented.  */
        break;
    case 0x10: /* End Of Interrupt */
        return gic_complete_irq(s, cpu, value & 0x3ff);
    default:
        hw_error("gic_cpu_write: Bad offset %x\n", (int)offset);
        return;
    }
    gic_update(s);
}
#endif

static void gic_reset(gic_state *s)
{
    int i;
    memset(s->irq_state, 0, GIC_NIRQ * sizeof(gic_irq_state));
    for (i = 0 ; i < NCPU; i++) {
        s->priority_mask[i] = 0xf0;
        s->current_pending[i] = 1023;
        s->running_irq[i] = 1023;
        s->running_priority[i] = 0x100;
#ifdef NVIC
        /* The NVIC doesn't have per-cpu interfaces, so enable by default.  */
        s->cpu_enabled[i] = 1;
#else
        s->cpu_enabled[i] = 0;
#endif
    }
    for (i = 0; i < 16; i++) {
        GIC_SET_ENABLED(i);
        GIC_SET_TRIGGER(i);
    }
#ifdef NVIC
    /* The NVIC is always enabled.  */
    s->enabled = 1;
#else
    s->enabled = 0;
#endif
}

static void gic_save(QEMUFile *f, void *opaque)
{
    gic_state *s = (gic_state *)opaque;
    int i;
    int j;

    qemu_put_be32(f, s->enabled);
    for (i = 0; i < NCPU; i++) {
        qemu_put_be32(f, s->cpu_enabled[i]);
#ifndef NVIC
        qemu_put_be32(f, s->irq_target[i]);
#endif
        for (j = 0; j < 32; j++)
            qemu_put_be32(f, s->priority1[j][i]);
        for (j = 0; j < GIC_NIRQ; j++)
            qemu_put_be32(f, s->last_active[j][i]);
        qemu_put_be32(f, s->priority_mask[i]);
        qemu_put_be32(f, s->running_irq[i]);
        qemu_put_be32(f, s->running_priority[i]);
        qemu_put_be32(f, s->current_pending[i]);
    }
    for (i = 0; i < GIC_NIRQ - 32; i++) {
        qemu_put_be32(f, s->priority2[i]);
    }
    for (i = 0; i < GIC_NIRQ; i++) {
        qemu_put_byte(f, s->irq_state[i].enabled);
        qemu_put_byte(f, s->irq_state[i].pending);
        qemu_put_byte(f, s->irq_state[i].active);
        qemu_put_byte(f, s->irq_state[i].level);
        qemu_put_byte(f, s->irq_state[i].model);
        qemu_put_byte(f, s->irq_state[i].trigger);
    }
}

static int gic_load(QEMUFile *f, void *opaque, int version_id)
{
    gic_state *s = (gic_state *)opaque;
    int i;
    int j;

    if (version_id != 1)
        return -EINVAL;

    s->enabled = qemu_get_be32(f);
    for (i = 0; i < NCPU; i++) {
        s->cpu_enabled[i] = qemu_get_be32(f);
#ifndef NVIC
        s->irq_target[i] = qemu_get_be32(f);
#endif
        for (j = 0; j < 32; j++)
            s->priority1[j][i] = qemu_get_be32(f);
        for (j = 0; j < GIC_NIRQ; j++)
            s->last_active[j][i] = qemu_get_be32(f);
        s->priority_mask[i] = qemu_get_be32(f);
        s->running_irq[i] = qemu_get_be32(f);
        s->running_priority[i] = qemu_get_be32(f);
        s->current_pending[i] = qemu_get_be32(f);
    }
    for (i = 0; i < GIC_NIRQ - 32; i++) {
        s->priority2[i] = qemu_get_be32(f);
    }
    for (i = 0; i < GIC_NIRQ; i++) {
        s->irq_state[i].enabled = qemu_get_byte(f);
        s->irq_state[i].pending = qemu_get_byte(f);
        s->irq_state[i].active = qemu_get_byte(f);
        s->irq_state[i].level = qemu_get_byte(f);
        s->irq_state[i].model = qemu_get_byte(f);
        s->irq_state[i].trigger = qemu_get_byte(f);
    }

    return 0;
}

static gic_state *gic_init(uint32_t dist_base, qemu_irq *parent_irq)
{
    gic_state *s;
    int iomemtype;
    int i;

    s = (gic_state *)qemu_mallocz(sizeof(gic_state));
    s->in = qemu_allocate_irqs(gic_set_irq, s, GIC_NIRQ);
    for (i = 0; i < NCPU; i++) {
        s->parent_irq[i] = parent_irq[i];
    }
    iomemtype = cpu_register_io_memory(0, gic_dist_readfn,
                                       gic_dist_writefn, s);
    cpu_register_physical_memory(dist_base, 0x00001000,
                                 iomemtype);
    gic_reset(s);
    register_savevm("arm_gic", -1, 1, gic_save, gic_load, s);
    return s;
}

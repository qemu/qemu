/* 
 * ARM AMBA Generic/Distributed Interrupt Controller
 *
 * Copyright (c) 2006 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licenced under the GPL.
 */

/* TODO: Some variants of this controller can handle multiple CPUs.
   Currently only single CPU operation is implemented.  */

#include "vl.h"
#include "arm_pic.h"

//#define DEBUG_GIC

#ifdef DEBUG_GIC
#define DPRINTF(fmt, args...) \
do { printf("arm_gic: " fmt , (int)s->base, ##args); } while (0)
#else
#define DPRINTF(fmt, args...) do {} while(0)
#endif

/* Distributed interrupt controller.  */

static const uint8_t gic_id[] =
{ 0x90, 0x13, 0x04, 0x00, 0x0d, 0xf0, 0x05, 0xb1 };

#define GIC_NIRQ 96

typedef struct gic_irq_state
{
    unsigned enabled:1;
    unsigned pending:1;
    unsigned active:1;
    unsigned level:1;
    unsigned model:1; /* 0 = 1:N, 1 = N:N */
    unsigned trigger:1; /* nonzero = edge triggered.  */
} gic_irq_state;

#define GIC_SET_ENABLED(irq) s->irq_state[irq].enabled = 1
#define GIC_CLEAR_ENABLED(irq) s->irq_state[irq].enabled = 0
#define GIC_TEST_ENABLED(irq) s->irq_state[irq].enabled
#define GIC_SET_PENDING(irq) s->irq_state[irq].pending = 1
#define GIC_CLEAR_PENDING(irq) s->irq_state[irq].pending = 0
#define GIC_TEST_PENDING(irq) s->irq_state[irq].pending
#define GIC_SET_ACTIVE(irq) s->irq_state[irq].active = 1
#define GIC_CLEAR_ACTIVE(irq) s->irq_state[irq].active = 0
#define GIC_TEST_ACTIVE(irq) s->irq_state[irq].active
#define GIC_SET_MODEL(irq) s->irq_state[irq].model = 1
#define GIC_CLEAR_MODEL(irq) s->irq_state[irq].model = 0
#define GIC_TEST_MODEL(irq) s->irq_state[irq].model
#define GIC_SET_LEVEL(irq) s->irq_state[irq].level = 1
#define GIC_CLEAR_LEVEL(irq) s->irq_state[irq].level = 0
#define GIC_TEST_LEVEL(irq) s->irq_state[irq].level
#define GIC_SET_TRIGGER(irq) s->irq_state[irq].trigger = 1
#define GIC_CLEAR_TRIGGER(irq) s->irq_state[irq].trigger = 0
#define GIC_TEST_TRIGGER(irq) s->irq_state[irq].trigger

typedef struct gic_state
{
    arm_pic_handler handler;
    uint32_t base;
    void *parent;
    int parent_irq;
    int enabled;
    int cpu_enabled;

    gic_irq_state irq_state[GIC_NIRQ];
    int irq_target[GIC_NIRQ];
    int priority[GIC_NIRQ];
    int last_active[GIC_NIRQ];

    int priority_mask;
    int running_irq;
    int running_priority;
    int current_pending;
} gic_state;

/* TODO: Many places that call this routine could be optimized.  */
/* Update interrupt status after enabled or pending bits have been changed.  */
static void gic_update(gic_state *s)
{
    int best_irq;
    int best_prio;
    int irq;

    s->current_pending = 1023;
    if (!s->enabled || !s->cpu_enabled) {
        pic_set_irq_new(s->parent, s->parent_irq, 0);
        return;
    }
    best_prio = 0x100;
    best_irq = 1023;
    for (irq = 0; irq < 96; irq++) {
        if (GIC_TEST_ENABLED(irq) && GIC_TEST_PENDING(irq)) {
            if (s->priority[irq] < best_prio) {
                best_prio = s->priority[irq];
                best_irq = irq;
            }
        }
    }
    if (best_prio > s->priority_mask) {
        pic_set_irq_new(s->parent, s->parent_irq, 0);
    } else {
        s->current_pending = best_irq;
        if (best_prio < s->running_priority) {
            DPRINTF("Raised pending IRQ %d\n", best_irq);
            pic_set_irq_new(s->parent, s->parent_irq, 1);
        }
    }
}

static void gic_set_irq(void *opaque, int irq, int level)
{
    gic_state *s = (gic_state *)opaque;
    /* The first external input line is internal interrupt 32.  */
    irq += 32;
    if (level == GIC_TEST_LEVEL(irq)) 
        return;

    if (level) {
        GIC_SET_LEVEL(irq);
        if (GIC_TEST_TRIGGER(irq) || GIC_TEST_ENABLED(irq)) {
            DPRINTF("Set %d pending\n", irq);
            GIC_SET_PENDING(irq);
        }
    } else {
        GIC_CLEAR_LEVEL(irq);
    }
    gic_update(s);
}

static void gic_set_running_irq(gic_state *s, int irq)
{
    s->running_irq = irq;
    s->running_priority = s->priority[irq];
    gic_update(s);
}

static uint32_t gic_acknowledge_irq(gic_state *s)
{
    int new_irq;
    new_irq = s->current_pending;
    if (new_irq == 1023 || s->priority[new_irq] >= s->running_priority) {
        DPRINTF("ACK no pending IRQ\n");
        return 1023;
    }
    pic_set_irq_new(s->parent, s->parent_irq, 0);
    s->last_active[new_irq] = s->running_irq;
    /* For level triggered interrupts we clear the pending bit while
       the interrupt is active.  */
    GIC_CLEAR_PENDING(new_irq);
    gic_set_running_irq(s, new_irq);
    DPRINTF("ACK %d\n", new_irq);
    return new_irq;
}

static void gic_complete_irq(gic_state * s, int irq)
{
    int update = 0;
    DPRINTF("EIO %d\n", irq);
    if (s->running_irq == 1023)
        return; /* No active IRQ.  */
    if (irq != 1023) {
        /* Mark level triggered interrupts as pending if they are still
           raised.  */
        if (!GIC_TEST_TRIGGER(irq) && GIC_TEST_ENABLED(irq)
                && GIC_TEST_LEVEL(irq)) {
            GIC_SET_PENDING(irq);
            update = 1;
        }
    }
    if (irq != s->running_irq) {
        /* Complete an IRQ that is not currently running.  */
        int tmp = s->running_irq;
        while (s->last_active[tmp] != 1023) {
            if (s->last_active[tmp] == irq) {
                s->last_active[tmp] = s->last_active[irq];
                break;
            }
            tmp = s->last_active[tmp];
        }
        if (update) {
            gic_update(s);
        }
    } else {
        /* Complete the current running IRQ.  */
        gic_set_running_irq(s, s->last_active[s->running_irq]);
    }
}

static uint32_t gic_dist_readb(void *opaque, target_phys_addr_t offset)
{
    gic_state *s = (gic_state *)opaque;
    uint32_t res;
    int irq;
    int i;

    offset -= s->base + 0x1000;
    if (offset < 0x100) {
        if (offset == 0)
            return s->enabled;
        if (offset == 4)
            return (GIC_NIRQ / 32) - 1;
        if (offset < 0x08)
            return 0;
        goto bad_reg;
    } else if (offset < 0x200) {
        /* Interrupt Set/Clear Enable.  */
        if (offset < 0x180)
            irq = (offset - 0x100) * 8;
        else
            irq = (offset - 0x180) * 8;
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
        if (irq >= GIC_NIRQ)
            goto bad_reg;
        res = 0;
        for (i = 0; i < 8; i++) {
            if (GIC_TEST_PENDING(irq + i)) {
                res |= (1 << i);
            }
        }
    } else if (offset < 0x400) {
        /* Interrupt Active.  */
        irq = (offset - 0x300) * 8;
        if (irq >= GIC_NIRQ)
            goto bad_reg;
        res = 0;
        for (i = 0; i < 8; i++) {
            if (GIC_TEST_ACTIVE(irq + i)) {
                res |= (1 << i);
            }
        }
    } else if (offset < 0x800) {
        /* Interrupt Priority.  */
        irq = offset - 0x400;
        if (irq >= GIC_NIRQ)
            goto bad_reg;
        res = s->priority[irq];
    } else if (offset < 0xc00) {
        /* Interrupt CPU Target.  */
        irq = offset - 0x800;
        if (irq >= GIC_NIRQ)
            goto bad_reg;
        res = s->irq_target[irq];
    } else if (offset < 0xf00) {
        /* Interrupt Configuration.  */
        irq = (offset - 0xc00) * 2;
        if (irq >= GIC_NIRQ)
            goto bad_reg;
        res = 0;
        for (i = 0; i < 4; i++) {
            if (GIC_TEST_MODEL(irq + i))
                res |= (1 << (i * 2));
            if (GIC_TEST_TRIGGER(irq + i))
                res |= (2 << (i * 2));
        }
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
    cpu_abort (cpu_single_env, "gic_dist_readb: Bad offset %x\n", offset);
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

    offset -= s->base + 0x1000;
    if (offset < 0x100) {
        if (offset == 0) {
            s->enabled = (value & 1);
            DPRINTF("Distribution %sabled\n", s->enabled ? "En" : "Dis");
        } else if (offset < 4) {
            /* ignored.  */
        } else {
            goto bad_reg;
        }
    } else if (offset < 0x180) {
        /* Interrupt Set Enable.  */
        irq = (offset - 0x100) * 8;
        if (irq >= GIC_NIRQ)
            goto bad_reg;
        for (i = 0; i < 8; i++) {
            if (value & (1 << i)) {
                if (!GIC_TEST_ENABLED(irq + i))
                    DPRINTF("Enabled IRQ %d\n", irq + i);
                GIC_SET_ENABLED(irq + i);
                /* If a raised level triggered IRQ enabled then mark
                   is as pending.  */
                if (GIC_TEST_LEVEL(irq + i) && !GIC_TEST_TRIGGER(irq + i))
                    GIC_SET_PENDING(irq + i);
            }
        }
    } else if (offset < 0x200) {
        /* Interrupt Clear Enable.  */
        irq = (offset - 0x180) * 8;
        if (irq >= GIC_NIRQ)
            goto bad_reg;
        for (i = 0; i < 8; i++) {
            if (value & (1 << i)) {
                if (GIC_TEST_ENABLED(irq + i))
                    DPRINTF("Disabled IRQ %d\n", irq + i);
                GIC_CLEAR_ENABLED(irq + i);
            }
        }
    } else if (offset < 0x280) {
        /* Interrupt Set Pending.  */
        irq = (offset - 0x200) * 8;
        if (irq >= GIC_NIRQ)
            goto bad_reg;
        for (i = 0; i < 8; i++) {
            if (value & (1 << i)) {
                GIC_SET_PENDING(irq + i);
            }
        }
    } else if (offset < 0x300) {
        /* Interrupt Clear Pending.  */
        irq = (offset - 0x280) * 8;
        if (irq >= GIC_NIRQ)
            goto bad_reg;
        for (i = 0; i < 8; i++) {
            if (value & (1 << i)) {
                GIC_CLEAR_PENDING(irq + i);
            }
        }
    } else if (offset < 0x400) {
        /* Interrupt Active.  */
        goto bad_reg;
    } else if (offset < 0x800) {
        /* Interrupt Priority.  */
        irq = offset - 0x400;
        if (irq >= GIC_NIRQ)
            goto bad_reg;
        s->priority[irq] = value;
    } else if (offset < 0xc00) {
        /* Interrupt CPU Target.  */
        irq = offset - 0x800;
        if (irq >= GIC_NIRQ)
            goto bad_reg;
        s->irq_target[irq] = value;
    } else if (offset < 0xf00) {
        /* Interrupt Configuration.  */
        irq = (offset - 0xc00) * 2;
        if (irq >= GIC_NIRQ)
            goto bad_reg;
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
    } else {
        /* 0xf00 is only handled for word writes.  */
        goto bad_reg;
    }
    gic_update(s);
    return;
bad_reg:
    cpu_abort (cpu_single_env, "gic_dist_writeb: Bad offset %x\n", offset);
}

static void gic_dist_writew(void *opaque, target_phys_addr_t offset,
                            uint32_t value)
{
    gic_state *s = (gic_state *)opaque;
    if (offset - s->base == 0xf00) {
        GIC_SET_PENDING(value & 0x3ff);
        gic_update(s);
        return;
    }
    gic_dist_writeb(opaque, offset, value & 0xff);
    gic_dist_writeb(opaque, offset + 1, value >> 8);
}

static void gic_dist_writel(void *opaque, target_phys_addr_t offset,
                            uint32_t value)
{
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

static uint32_t gic_cpu_read(void *opaque, target_phys_addr_t offset)
{
    gic_state *s = (gic_state *)opaque;
    offset -= s->base;
    switch (offset) {
    case 0x00: /* Control */
        return s->cpu_enabled;
    case 0x04: /* Priority mask */
        return s->priority_mask;
    case 0x08: /* Binary Point */
        /* ??? Not implemented.  */
        return 0;
    case 0x0c: /* Acknowledge */
        return gic_acknowledge_irq(s);
    case 0x14: /* Runing Priority */
        return s->running_priority;
    case 0x18: /* Highest Pending Interrupt */
        return s->current_pending;
    default:
        cpu_abort (cpu_single_env, "gic_cpu_writeb: Bad offset %x\n", offset);
        return 0;
    }
}

static void gic_cpu_write(void *opaque, target_phys_addr_t offset,
                          uint32_t value)
{
    gic_state *s = (gic_state *)opaque;
    offset -= s->base;
    switch (offset) {
    case 0x00: /* Control */
        s->cpu_enabled = (value & 1);
        DPRINTF("CPU %sabled\n", s->cpu_enabled ? "En" : "Dis");
        break;
    case 0x04: /* Priority mask */
        s->priority_mask = (value & 0x3ff);
        break;
    case 0x08: /* Binary Point */
        /* ??? Not implemented.  */
        break;
    case 0x10: /* End Of Interrupt */
        return gic_complete_irq(s, value & 0x3ff);
    default:
        cpu_abort (cpu_single_env, "gic_cpu_writeb: Bad offset %x\n", offset);
        return;
    }
    gic_update(s);
}

static CPUReadMemoryFunc *gic_cpu_readfn[] = {
   gic_cpu_read,
   gic_cpu_read,
   gic_cpu_read
};

static CPUWriteMemoryFunc *gic_cpu_writefn[] = {
   gic_cpu_write,
   gic_cpu_write,
   gic_cpu_write
};

static void gic_reset(gic_state *s)
{
    int i;
    memset(s->irq_state, 0, GIC_NIRQ * sizeof(gic_irq_state));
    s->priority_mask = 0xf0;
    s->current_pending = 1023;
    s->running_irq = 1023;
    s->running_priority = 0x100;
    for (i = 0; i < 15; i++) {
        GIC_SET_ENABLED(i);
        GIC_SET_TRIGGER(i);
    }
    s->enabled = 0;
    s->cpu_enabled = 0;
}

void *arm_gic_init(uint32_t base, void *parent, int parent_irq)
{
    gic_state *s;
    int iomemtype;

    s = (gic_state *)qemu_mallocz(sizeof(gic_state));
    if (!s)
        return NULL;
    s->handler = gic_set_irq;
    s->parent = parent;
    s->parent_irq = parent_irq;
    if (base != 0xffffffff) {
        iomemtype = cpu_register_io_memory(0, gic_cpu_readfn,
                                           gic_cpu_writefn, s);
        cpu_register_physical_memory(base, 0x00000fff, iomemtype);
        iomemtype = cpu_register_io_memory(0, gic_dist_readfn,
                                           gic_dist_writefn, s);
        cpu_register_physical_memory(base + 0x1000, 0x00000fff, iomemtype);
        s->base = base;
    } else {
        s->base = 0;
    }
    gic_reset(s);
    return s;
}

/*
 * ARM Nested Vectored Interrupt Controller
 *
 * Copyright (c) 2006-2007 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licenced under the GPL.
 *
 * The ARMv7M System controller is fairly tightly tied in with the
 * NVIC.  Much of that is also implemented here.
 */

#include "hw.h"
#include "qemu-timer.h"
#include "arm-misc.h"

/* 32 internal lines (16 used for system exceptions) plus 64 external
   interrupt lines.  */
#define GIC_NIRQ 96
#define NCPU 1
#define NVIC 1

/* Only a single "CPU" interface is present.  */
static inline int
gic_get_current_cpu(void)
{
    return 0;
}

static uint32_t nvic_readl(void *opaque, uint32_t offset);
static void nvic_writel(void *opaque, uint32_t offset, uint32_t value);

#include "arm_gic.c"

typedef struct {
    struct {
        uint32_t control;
        uint32_t reload;
        int64_t tick;
        QEMUTimer *timer;
    } systick;
    gic_state *gic;
} nvic_state;

/* qemu timers run at 1GHz.   We want something closer to 1MHz.  */
#define SYSTICK_SCALE 1000ULL

#define SYSTICK_ENABLE    (1 << 0)
#define SYSTICK_TICKINT   (1 << 1)
#define SYSTICK_CLKSOURCE (1 << 2)
#define SYSTICK_COUNTFLAG (1 << 16)

/* Conversion factor from qemu timer to SysTick frequencies.  */
static inline int64_t systick_scale(nvic_state *s)
{
    if (s->systick.control & SYSTICK_CLKSOURCE)
        return system_clock_scale;
    else
        return 1000;
}

static void systick_reload(nvic_state *s, int reset)
{
    if (reset)
        s->systick.tick = qemu_get_clock(vm_clock);
    s->systick.tick += (s->systick.reload + 1) * systick_scale(s);
    qemu_mod_timer(s->systick.timer, s->systick.tick);
}

static void systick_timer_tick(void * opaque)
{
    nvic_state *s = (nvic_state *)opaque;
    s->systick.control |= SYSTICK_COUNTFLAG;
    if (s->systick.control & SYSTICK_TICKINT) {
        /* Trigger the interrupt.  */
        armv7m_nvic_set_pending(s, ARMV7M_EXCP_SYSTICK);
    }
    if (s->systick.reload == 0) {
        s->systick.control &= ~SYSTICK_ENABLE;
    } else {
        systick_reload(s, 0);
    }
}

/* The external routines use the hardware vector numbering, ie. the first
   IRQ is #16.  The internal GIC routines use #32 as the first IRQ.  */
void armv7m_nvic_set_pending(void *opaque, int irq)
{
    nvic_state *s = (nvic_state *)opaque;
    if (irq >= 16)
        irq += 16;
    gic_set_pending_private(s->gic, 0, irq);
}

/* Make pending IRQ active.  */
int armv7m_nvic_acknowledge_irq(void *opaque)
{
    nvic_state *s = (nvic_state *)opaque;
    uint32_t irq;

    irq = gic_acknowledge_irq(s->gic, 0);
    if (irq == 1023)
        cpu_abort(cpu_single_env, "Interrupt but no vector\n");
    if (irq >= 32)
        irq -= 16;
    return irq;
}

void armv7m_nvic_complete_irq(void *opaque, int irq)
{
    nvic_state *s = (nvic_state *)opaque;
    if (irq >= 16)
        irq += 16;
    gic_complete_irq(s->gic, 0, irq);
}

static uint32_t nvic_readl(void *opaque, uint32_t offset)
{
    nvic_state *s = (nvic_state *)opaque;
    uint32_t val;
    int irq;

    switch (offset) {
    case 4: /* Interrupt Control Type.  */
        return (GIC_NIRQ / 32) - 1;
    case 0x10: /* SysTick Control and Status.  */
        val = s->systick.control;
        s->systick.control &= ~SYSTICK_COUNTFLAG;
        return val;
    case 0x14: /* SysTick Reload Value.  */
        return s->systick.reload;
    case 0x18: /* SysTick Current Value.  */
        {
            int64_t t;
            if ((s->systick.control & SYSTICK_ENABLE) == 0)
                return 0;
            t = qemu_get_clock(vm_clock);
            if (t >= s->systick.tick)
                return 0;
            val = ((s->systick.tick - (t + 1)) / systick_scale(s)) + 1;
            /* The interrupt in triggered when the timer reaches zero.
               However the counter is not reloaded until the next clock
               tick.  This is a hack to return zero during the first tick.  */
            if (val > s->systick.reload)
                val = 0;
            return val;
        }
    case 0x1c: /* SysTick Calibration Value.  */
        return 10000;
    case 0xd00: /* CPUID Base.  */
        return cpu_single_env->cp15.c0_cpuid;
    case 0xd04: /* Interrypt Control State.  */
        /* VECTACTIVE */
        val = s->gic->running_irq[0];
        if (val == 1023) {
            val = 0;
        } else if (val >= 32) {
            val -= 16;
        }
        /* RETTOBASE */
        if (s->gic->running_irq[0] == 1023
                || s->gic->last_active[s->gic->running_irq[0]][0] == 1023) {
            val |= (1 << 11);
        }
        /* VECTPENDING */
        if (s->gic->current_pending[0] != 1023)
            val |= (s->gic->current_pending[0] << 12);
        /* ISRPENDING */
        for (irq = 32; irq < GIC_NIRQ; irq++) {
            if (s->gic->irq_state[irq].pending) {
                val |= (1 << 22);
                break;
            }
        }
        /* PENDSTSET */
        if (s->gic->irq_state[ARMV7M_EXCP_SYSTICK].pending)
            val |= (1 << 26);
        /* PENDSVSET */
        if (s->gic->irq_state[ARMV7M_EXCP_PENDSV].pending)
            val |= (1 << 28);
        /* NMIPENDSET */
        if (s->gic->irq_state[ARMV7M_EXCP_NMI].pending)
            val |= (1 << 31);
        return val;
    case 0xd08: /* Vector Table Offset.  */
        return cpu_single_env->v7m.vecbase;
    case 0xd0c: /* Application Interrupt/Reset Control.  */
        return 0xfa05000;
    case 0xd10: /* System Control.  */
        /* TODO: Implement SLEEPONEXIT.  */
        return 0;
    case 0xd14: /* Configuration Control.  */
        /* TODO: Implement Configuration Control bits.  */
        return 0;
    case 0xd18: case 0xd1c: case 0xd20: /* System Handler Priority.  */
        irq = offset - 0xd14;
        val = 0;
        val = s->gic->priority1[irq++][0];
        val = s->gic->priority1[irq++][0] << 8;
        val = s->gic->priority1[irq++][0] << 16;
        val = s->gic->priority1[irq][0] << 24;
        return val;
    case 0xd24: /* System Handler Status.  */
        val = 0;
        if (s->gic->irq_state[ARMV7M_EXCP_MEM].active) val |= (1 << 0);
        if (s->gic->irq_state[ARMV7M_EXCP_BUS].active) val |= (1 << 1);
        if (s->gic->irq_state[ARMV7M_EXCP_USAGE].active) val |= (1 << 3);
        if (s->gic->irq_state[ARMV7M_EXCP_SVC].active) val |= (1 << 7);
        if (s->gic->irq_state[ARMV7M_EXCP_DEBUG].active) val |= (1 << 8);
        if (s->gic->irq_state[ARMV7M_EXCP_PENDSV].active) val |= (1 << 10);
        if (s->gic->irq_state[ARMV7M_EXCP_SYSTICK].active) val |= (1 << 11);
        if (s->gic->irq_state[ARMV7M_EXCP_USAGE].pending) val |= (1 << 12);
        if (s->gic->irq_state[ARMV7M_EXCP_MEM].pending) val |= (1 << 13);
        if (s->gic->irq_state[ARMV7M_EXCP_BUS].pending) val |= (1 << 14);
        if (s->gic->irq_state[ARMV7M_EXCP_SVC].pending) val |= (1 << 15);
        if (s->gic->irq_state[ARMV7M_EXCP_MEM].enabled) val |= (1 << 16);
        if (s->gic->irq_state[ARMV7M_EXCP_BUS].enabled) val |= (1 << 17);
        if (s->gic->irq_state[ARMV7M_EXCP_USAGE].enabled) val |= (1 << 18);
        return val;
    case 0xd28: /* Configurable Fault Status.  */
        /* TODO: Implement Fault Status.  */
        cpu_abort(cpu_single_env,
                  "Not implemented: Configurable Fault Status.");
        return 0;
    case 0xd2c: /* Hard Fault Status.  */
    case 0xd30: /* Debug Fault Status.  */
    case 0xd34: /* Mem Manage Address.  */
    case 0xd38: /* Bus Fault Address.  */
    case 0xd3c: /* Aux Fault Status.  */
        /* TODO: Implement fault status registers.  */
        goto bad_reg;
    case 0xd40: /* PFR0.  */
        return 0x00000030;
    case 0xd44: /* PRF1.  */
        return 0x00000200;
    case 0xd48: /* DFR0.  */
        return 0x00100000;
    case 0xd4c: /* AFR0.  */
        return 0x00000000;
    case 0xd50: /* MMFR0.  */
        return 0x00000030;
    case 0xd54: /* MMFR1.  */
        return 0x00000000;
    case 0xd58: /* MMFR2.  */
        return 0x00000000;
    case 0xd5c: /* MMFR3.  */
        return 0x00000000;
    case 0xd60: /* ISAR0.  */
        return 0x01141110;
    case 0xd64: /* ISAR1.  */
        return 0x02111000;
    case 0xd68: /* ISAR2.  */
        return 0x21112231;
    case 0xd6c: /* ISAR3.  */
        return 0x01111110;
    case 0xd70: /* ISAR4.  */
        return 0x01310102;
    /* TODO: Implement debug registers.  */
    default:
    bad_reg:
        cpu_abort(cpu_single_env, "NVIC: Bad read offset 0x%x\n", offset);
    }
}

static void nvic_writel(void *opaque, uint32_t offset, uint32_t value)
{
    nvic_state *s = (nvic_state *)opaque;
    uint32_t oldval;
    switch (offset) {
    case 0x10: /* SysTick Control and Status.  */
        oldval = s->systick.control;
        s->systick.control &= 0xfffffff8;
        s->systick.control |= value & 7;
        if ((oldval ^ value) & SYSTICK_ENABLE) {
            int64_t now = qemu_get_clock(vm_clock);
            if (value & SYSTICK_ENABLE) {
                if (s->systick.tick) {
                    s->systick.tick += now;
                    qemu_mod_timer(s->systick.timer, s->systick.tick);
                } else {
                    systick_reload(s, 1);
                }
            } else {
                qemu_del_timer(s->systick.timer);
                s->systick.tick -= now;
                if (s->systick.tick < 0)
                  s->systick.tick = 0;
            }
        } else if ((oldval ^ value) & SYSTICK_CLKSOURCE) {
            /* This is a hack. Force the timer to be reloaded
               when the reference clock is changed.  */
            systick_reload(s, 1);
        }
        break;
    case 0x14: /* SysTick Reload Value.  */
        s->systick.reload = value;
        break;
    case 0x18: /* SysTick Current Value.  Writes reload the timer.  */
        systick_reload(s, 1);
        s->systick.control &= ~SYSTICK_COUNTFLAG;
        break;
    case 0xd04: /* Interrupt Control State.  */
        if (value & (1 << 31)) {
            armv7m_nvic_set_pending(s, ARMV7M_EXCP_NMI);
        }
        if (value & (1 << 28)) {
            armv7m_nvic_set_pending(s, ARMV7M_EXCP_PENDSV);
        } else if (value & (1 << 27)) {
            s->gic->irq_state[ARMV7M_EXCP_PENDSV].pending = 0;
            gic_update(s->gic);
        }
        if (value & (1 << 26)) {
            armv7m_nvic_set_pending(s, ARMV7M_EXCP_SYSTICK);
        } else if (value & (1 << 25)) {
            s->gic->irq_state[ARMV7M_EXCP_SYSTICK].pending = 0;
            gic_update(s->gic);
        }
        break;
    case 0xd08: /* Vector Table Offset.  */
        cpu_single_env->v7m.vecbase = value & 0xffffff80;
        break;
    case 0xd0c: /* Application Interrupt/Reset Control.  */
        if ((value >> 16) == 0x05fa) {
            if (value & 2) {
                cpu_abort(cpu_single_env, "VECTCLRACTIVE not implemented");
            }
            if (value & 5) {
                cpu_abort(cpu_single_env, "System reset");
            }
        }
        break;
    case 0xd10: /* System Control.  */
    case 0xd14: /* Configuration Control.  */
        /* TODO: Implement control registers.  */
        goto bad_reg;
    case 0xd18: case 0xd1c: case 0xd20: /* System Handler Priority.  */
        {
            int irq;
            irq = offset - 0xd14;
            s->gic->priority1[irq++][0] = value & 0xff;
            s->gic->priority1[irq++][0] = (value >> 8) & 0xff;
            s->gic->priority1[irq++][0] = (value >> 16) & 0xff;
            s->gic->priority1[irq][0] = (value >> 24) & 0xff;
            gic_update(s->gic);
        }
        break;
    case 0xd24: /* System Handler Control.  */
        /* TODO: Real hardware allows you to set/clear the active bits
           under some circumstances.  We don't implement this.  */
        s->gic->irq_state[ARMV7M_EXCP_MEM].enabled = (value & (1 << 16)) != 0;
        s->gic->irq_state[ARMV7M_EXCP_BUS].enabled = (value & (1 << 17)) != 0;
        s->gic->irq_state[ARMV7M_EXCP_USAGE].enabled = (value & (1 << 18)) != 0;
        break;
    case 0xd28: /* Configurable Fault Status.  */
    case 0xd2c: /* Hard Fault Status.  */
    case 0xd30: /* Debug Fault Status.  */
    case 0xd34: /* Mem Manage Address.  */
    case 0xd38: /* Bus Fault Address.  */
    case 0xd3c: /* Aux Fault Status.  */
        goto bad_reg;
    default:
    bad_reg:
        cpu_abort(cpu_single_env, "NVIC: Bad write offset 0x%x\n", offset);
    }
}

static void nvic_save(QEMUFile *f, void *opaque)
{
    nvic_state *s = (nvic_state *)opaque;

    qemu_put_be32(f, s->systick.control);
    qemu_put_be32(f, s->systick.reload);
    qemu_put_be64(f, s->systick.tick);
    qemu_put_timer(f, s->systick.timer);
}

static int nvic_load(QEMUFile *f, void *opaque, int version_id)
{
    nvic_state *s = (nvic_state *)opaque;

    if (version_id != 1)
        return -EINVAL;

    s->systick.control = qemu_get_be32(f);
    s->systick.reload = qemu_get_be32(f);
    s->systick.tick = qemu_get_be64(f);
    qemu_get_timer(f, s->systick.timer);

    return 0;
}

qemu_irq *armv7m_nvic_init(CPUState *env)
{
    nvic_state *s;
    qemu_irq *parent;

    parent = arm_pic_init_cpu(env);
    s = (nvic_state *)qemu_mallocz(sizeof(nvic_state));
    s->gic = gic_init(0xe000e000, &parent[ARM_PIC_CPU_IRQ]);
    s->gic->nvic = s;
    s->systick.timer = qemu_new_timer(vm_clock, systick_timer_tick, s);
    if (env->v7m.nvic)
        cpu_abort(env, "CPU can only have one NVIC\n");
    env->v7m.nvic = s;
    register_savevm("armv7m_nvic", -1, 1, nvic_save, nvic_load, s);
    return s->gic->in;
}

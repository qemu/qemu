/*
 * QEMU Sun4u/Sun4v System Emulator common routines
 *
 * Copyright (c) 2005 Fabrice Bellard
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
#include "cpu.h"
#include "hw/char/serial.h"
#include "hw/sparc/sparc64.h"
#include "qemu/timer.h"


//#define DEBUG_IRQ
//#define DEBUG_TIMER

#ifdef DEBUG_IRQ
#define CPUIRQ_DPRINTF(fmt, ...)                                \
    do { printf("CPUIRQ: " fmt , ## __VA_ARGS__); } while (0)
#else
#define CPUIRQ_DPRINTF(fmt, ...)
#endif

#ifdef DEBUG_TIMER
#define TIMER_DPRINTF(fmt, ...)                                  \
    do { printf("TIMER: " fmt , ## __VA_ARGS__); } while (0)
#else
#define TIMER_DPRINTF(fmt, ...)
#endif

#define TICK_MAX             0x7fffffffffffffffULL

void cpu_check_irqs(CPUSPARCState *env)
{
    CPUState *cs;
    uint32_t pil = env->pil_in |
                  (env->softint & ~(SOFTINT_TIMER | SOFTINT_STIMER));

    /* We should be holding the BQL before we mess with IRQs */
    g_assert(qemu_mutex_iothread_locked());

    /* TT_IVEC has a higher priority (16) than TT_EXTINT (31..17) */
    if (env->ivec_status & 0x20) {
        return;
    }
    cs = CPU(sparc_env_get_cpu(env));
    /* check if TM or SM in SOFTINT are set
       setting these also causes interrupt 14 */
    if (env->softint & (SOFTINT_TIMER | SOFTINT_STIMER)) {
        pil |= 1 << 14;
    }

    /* The bit corresponding to psrpil is (1<< psrpil), the next bit
       is (2 << psrpil). */
    if (pil < (2 << env->psrpil)) {
        if (cs->interrupt_request & CPU_INTERRUPT_HARD) {
            CPUIRQ_DPRINTF("Reset CPU IRQ (current interrupt %x)\n",
                           env->interrupt_index);
            env->interrupt_index = 0;
            cpu_reset_interrupt(cs, CPU_INTERRUPT_HARD);
        }
        return;
    }

    if (cpu_interrupts_enabled(env)) {

        unsigned int i;

        for (i = 15; i > env->psrpil; i--) {
            if (pil & (1 << i)) {
                int old_interrupt = env->interrupt_index;
                int new_interrupt = TT_EXTINT | i;

                if (unlikely(env->tl > 0 && cpu_tsptr(env)->tt > new_interrupt
                  && ((cpu_tsptr(env)->tt & 0x1f0) == TT_EXTINT))) {
                    CPUIRQ_DPRINTF("Not setting CPU IRQ: TL=%d "
                                   "current %x >= pending %x\n",
                                   env->tl, cpu_tsptr(env)->tt, new_interrupt);
                } else if (old_interrupt != new_interrupt) {
                    env->interrupt_index = new_interrupt;
                    CPUIRQ_DPRINTF("Set CPU IRQ %d old=%x new=%x\n", i,
                                   old_interrupt, new_interrupt);
                    cpu_interrupt(cs, CPU_INTERRUPT_HARD);
                }
                break;
            }
        }
    } else if (cs->interrupt_request & CPU_INTERRUPT_HARD) {
        CPUIRQ_DPRINTF("Interrupts disabled, pil=%08x pil_in=%08x softint=%08x "
                       "current interrupt %x\n",
                       pil, env->pil_in, env->softint, env->interrupt_index);
        env->interrupt_index = 0;
        cpu_reset_interrupt(cs, CPU_INTERRUPT_HARD);
    }
}

static void cpu_kick_irq(SPARCCPU *cpu)
{
    CPUState *cs = CPU(cpu);
    CPUSPARCState *env = &cpu->env;

    cs->halted = 0;
    cpu_check_irqs(env);
    qemu_cpu_kick(cs);
}

void sparc64_cpu_set_ivec_irq(void *opaque, int irq, int level)
{
    SPARCCPU *cpu = opaque;
    CPUSPARCState *env = &cpu->env;
    CPUState *cs;

    if (level) {
        if (!(env->ivec_status & 0x20)) {
            CPUIRQ_DPRINTF("Raise IVEC IRQ %d\n", irq);
            cs = CPU(cpu);
            cs->halted = 0;
            env->interrupt_index = TT_IVEC;
            env->ivec_status |= 0x20;
            env->ivec_data[0] = (0x1f << 6) | irq;
            env->ivec_data[1] = 0;
            env->ivec_data[2] = 0;
            cpu_interrupt(cs, CPU_INTERRUPT_HARD);
        }
    } else {
        if (env->ivec_status & 0x20) {
            CPUIRQ_DPRINTF("Lower IVEC IRQ %d\n", irq);
            cs = CPU(cpu);
            env->ivec_status &= ~0x20;
            cpu_reset_interrupt(cs, CPU_INTERRUPT_HARD);
        }
    }
}

typedef struct ResetData {
    SPARCCPU *cpu;
    uint64_t prom_addr;
} ResetData;

static CPUTimer *cpu_timer_create(const char *name, SPARCCPU *cpu,
                                  QEMUBHFunc *cb, uint32_t frequency,
                                  uint64_t disabled_mask, uint64_t npt_mask)
{
    CPUTimer *timer = g_malloc0(sizeof(CPUTimer));

    timer->name = name;
    timer->frequency = frequency;
    timer->disabled_mask = disabled_mask;
    timer->npt_mask = npt_mask;

    timer->disabled = 1;
    timer->npt = 1;
    timer->clock_offset = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

    timer->qtimer = timer_new_ns(QEMU_CLOCK_VIRTUAL, cb, cpu);

    return timer;
}

static void cpu_timer_reset(CPUTimer *timer)
{
    timer->disabled = 1;
    timer->clock_offset = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

    timer_del(timer->qtimer);
}

static void main_cpu_reset(void *opaque)
{
    ResetData *s = (ResetData *)opaque;
    CPUSPARCState *env = &s->cpu->env;
    static unsigned int nr_resets;

    cpu_reset(CPU(s->cpu));

    cpu_timer_reset(env->tick);
    cpu_timer_reset(env->stick);
    cpu_timer_reset(env->hstick);

    env->gregs[1] = 0; /* Memory start */
    env->gregs[2] = ram_size; /* Memory size */
    env->gregs[3] = 0; /* Machine description XXX */
    if (nr_resets++ == 0) {
        /* Power on reset */
        env->pc = s->prom_addr + 0x20ULL;
    } else {
        env->pc = s->prom_addr + 0x40ULL;
    }
    env->npc = env->pc + 4;
}

static void tick_irq(void *opaque)
{
    SPARCCPU *cpu = opaque;
    CPUSPARCState *env = &cpu->env;

    CPUTimer *timer = env->tick;

    if (timer->disabled) {
        CPUIRQ_DPRINTF("tick_irq: softint disabled\n");
        return;
    } else {
        CPUIRQ_DPRINTF("tick: fire\n");
    }

    env->softint |= SOFTINT_TIMER;
    cpu_kick_irq(cpu);
}

static void stick_irq(void *opaque)
{
    SPARCCPU *cpu = opaque;
    CPUSPARCState *env = &cpu->env;

    CPUTimer *timer = env->stick;

    if (timer->disabled) {
        CPUIRQ_DPRINTF("stick_irq: softint disabled\n");
        return;
    } else {
        CPUIRQ_DPRINTF("stick: fire\n");
    }

    env->softint |= SOFTINT_STIMER;
    cpu_kick_irq(cpu);
}

static void hstick_irq(void *opaque)
{
    SPARCCPU *cpu = opaque;
    CPUSPARCState *env = &cpu->env;

    CPUTimer *timer = env->hstick;

    if (timer->disabled) {
        CPUIRQ_DPRINTF("hstick_irq: softint disabled\n");
        return;
    } else {
        CPUIRQ_DPRINTF("hstick: fire\n");
    }

    env->softint |= SOFTINT_STIMER;
    cpu_kick_irq(cpu);
}

static int64_t cpu_to_timer_ticks(int64_t cpu_ticks, uint32_t frequency)
{
    return muldiv64(cpu_ticks, NANOSECONDS_PER_SECOND, frequency);
}

static uint64_t timer_to_cpu_ticks(int64_t timer_ticks, uint32_t frequency)
{
    return muldiv64(timer_ticks, frequency, NANOSECONDS_PER_SECOND);
}

void cpu_tick_set_count(CPUTimer *timer, uint64_t count)
{
    uint64_t real_count = count & ~timer->npt_mask;
    uint64_t npt_bit = count & timer->npt_mask;

    int64_t vm_clock_offset = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) -
                    cpu_to_timer_ticks(real_count, timer->frequency);

    TIMER_DPRINTF("%s set_count count=0x%016lx (npt %s) p=%p\n",
                  timer->name, real_count,
                  timer->npt ? "disabled" : "enabled", timer);

    timer->npt = npt_bit ? 1 : 0;
    timer->clock_offset = vm_clock_offset;
}

uint64_t cpu_tick_get_count(CPUTimer *timer)
{
    uint64_t real_count = timer_to_cpu_ticks(
                    qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) - timer->clock_offset,
                    timer->frequency);

    TIMER_DPRINTF("%s get_count count=0x%016lx (npt %s) p=%p\n",
           timer->name, real_count,
           timer->npt ? "disabled" : "enabled", timer);

    if (timer->npt) {
        real_count |= timer->npt_mask;
    }

    return real_count;
}

void cpu_tick_set_limit(CPUTimer *timer, uint64_t limit)
{
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

    uint64_t real_limit = limit & ~timer->disabled_mask;
    timer->disabled = (limit & timer->disabled_mask) ? 1 : 0;

    int64_t expires = cpu_to_timer_ticks(real_limit, timer->frequency) +
                    timer->clock_offset;

    if (expires < now) {
        expires = now + 1;
    }

    TIMER_DPRINTF("%s set_limit limit=0x%016lx (%s) p=%p "
                  "called with limit=0x%016lx at 0x%016lx (delta=0x%016lx)\n",
                  timer->name, real_limit,
                  timer->disabled ? "disabled" : "enabled",
                  timer, limit,
                  timer_to_cpu_ticks(now - timer->clock_offset,
                                     timer->frequency),
                  timer_to_cpu_ticks(expires - now, timer->frequency));

    if (!real_limit) {
        TIMER_DPRINTF("%s set_limit limit=ZERO - not starting timer\n",
                timer->name);
        timer_del(timer->qtimer);
    } else if (timer->disabled) {
        timer_del(timer->qtimer);
    } else {
        timer_mod(timer->qtimer, expires);
    }
}

SPARCCPU *sparc64_cpu_devinit(const char *cpu_model,
                              const char *default_cpu_model, uint64_t prom_addr)
{
    SPARCCPU *cpu;
    CPUSPARCState *env;
    ResetData *reset_info;

    uint32_t   tick_frequency = 100 * 1000000;
    uint32_t  stick_frequency = 100 * 1000000;
    uint32_t hstick_frequency = 100 * 1000000;

    if (cpu_model == NULL) {
        cpu_model = default_cpu_model;
    }
    cpu = cpu_sparc_init(cpu_model);
    if (cpu == NULL) {
        fprintf(stderr, "Unable to find Sparc CPU definition\n");
        exit(1);
    }
    env = &cpu->env;

    env->tick = cpu_timer_create("tick", cpu, tick_irq,
                                  tick_frequency, TICK_INT_DIS,
                                  TICK_NPT_MASK);

    env->stick = cpu_timer_create("stick", cpu, stick_irq,
                                   stick_frequency, TICK_INT_DIS,
                                   TICK_NPT_MASK);

    env->hstick = cpu_timer_create("hstick", cpu, hstick_irq,
                                    hstick_frequency, TICK_INT_DIS,
                                    TICK_NPT_MASK);

    reset_info = g_malloc0(sizeof(ResetData));
    reset_info->cpu = cpu;
    reset_info->prom_addr = prom_addr;
    qemu_register_reset(main_cpu_reset, reset_info);

    return cpu;
}

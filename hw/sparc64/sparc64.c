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
#include "qapi/error.h"
#include "cpu.h"
#include "hw/boards.h"
#include "hw/sparc/sparc64.h"
#include "qemu/timer.h"
#include "system/reset.h"
#include "trace.h"


#define TICK_MAX             0x7fffffffffffffffULL

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
            trace_sparc64_cpu_ivec_raise_irq(irq);
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
            trace_sparc64_cpu_ivec_lower_irq(irq);
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
    CPUTimer *timer = g_new0(CPUTimer, 1);

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
    env->gregs[2] = current_machine->ram_size; /* Memory size */
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
        trace_sparc64_cpu_tick_irq_disabled();
        return;
    } else {
        trace_sparc64_cpu_tick_irq_fire();
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
        trace_sparc64_cpu_stick_irq_disabled();
        return;
    } else {
        trace_sparc64_cpu_stick_irq_fire();
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
        trace_sparc64_cpu_hstick_irq_disabled();
        return;
    } else {
        trace_sparc64_cpu_hstick_irq_fire();
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

    trace_sparc64_cpu_tick_set_count(timer->name, real_count,
                                     timer->npt ? "disabled" : "enabled",
                                     timer);

    timer->npt = npt_bit ? 1 : 0;
    timer->clock_offset = vm_clock_offset;
}

uint64_t cpu_tick_get_count(CPUTimer *timer)
{
    uint64_t real_count = timer_to_cpu_ticks(
                    qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) - timer->clock_offset,
                    timer->frequency);

    trace_sparc64_cpu_tick_get_count(timer->name, real_count,
                                     timer->npt ? "disabled" : "enabled",
                                     timer);

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

    trace_sparc64_cpu_tick_set_limit(timer->name, real_limit,
                                     timer->disabled ? "disabled" : "enabled",
                                     timer, limit,
                                     timer_to_cpu_ticks(
                                         now - timer->clock_offset,
                                         timer->frequency
                                     ),
                                     timer_to_cpu_ticks(
                                         expires - now, timer->frequency
                                     ));

    if (!real_limit) {
        trace_sparc64_cpu_tick_set_limit_zero(timer->name);
        timer_del(timer->qtimer);
    } else if (timer->disabled) {
        timer_del(timer->qtimer);
    } else {
        timer_mod(timer->qtimer, expires);
    }
}

SPARCCPU *sparc64_cpu_devinit(const char *cpu_type, uint64_t prom_addr)
{
    SPARCCPU *cpu;
    CPUSPARCState *env;
    ResetData *reset_info;

    uint32_t   tick_frequency = 100 * 1000000;
    uint32_t  stick_frequency = 100 * 1000000;
    uint32_t hstick_frequency = 100 * 1000000;

    cpu = SPARC_CPU(object_new(cpu_type));
    qdev_init_gpio_in_named(DEVICE(cpu), sparc64_cpu_set_ivec_irq,
                            "ivec-irq", IVEC_MAX);
    qdev_realize(DEVICE(cpu), NULL, &error_fatal);
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

    reset_info = g_new0(ResetData, 1);
    reset_info->cpu = cpu;
    reset_info->prom_addr = prom_addr;
    qemu_register_reset(main_cpu_reset, reset_info);

    return cpu;
}

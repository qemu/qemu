/*
 * QEMU OpenRISC timer support
 *
 * Copyright (c) 2011-2012 Jia Liu <proljc@gmail.com>
 *                         Zhizhou Zhang <etouzh@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "migration/vmstate.h"
#include "qemu/timer.h"
#include "system/reset.h"

#define TIMER_PERIOD 50 /* 50 ns period for 20 MHz timer */

/* Tick Timer global state to allow all cores to be in sync */
typedef struct OR1KTimerState {
    uint32_t ttcr;
    uint32_t ttcr_offset;
    uint64_t clk_offset;
} OR1KTimerState;

static OR1KTimerState *or1k_timer;

void cpu_openrisc_count_set(OpenRISCCPU *cpu, uint32_t val)
{
    or1k_timer->ttcr = val;
    or1k_timer->ttcr_offset = val;
    or1k_timer->clk_offset = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
}

uint32_t cpu_openrisc_count_get(OpenRISCCPU *cpu)
{
    return or1k_timer->ttcr;
}

/* Add elapsed ticks to ttcr */
void cpu_openrisc_count_update(OpenRISCCPU *cpu)
{
    uint64_t now;

    if (!cpu->env.is_counting) {
        return;
    }
    now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    or1k_timer->ttcr = or1k_timer->ttcr_offset +
        DIV_ROUND_UP(now - or1k_timer->clk_offset, TIMER_PERIOD);
}

/* Update the next timeout time as difference between ttmr and ttcr */
void cpu_openrisc_timer_update(OpenRISCCPU *cpu)
{
    uint32_t wait;
    uint64_t now, next;

    if (!cpu->env.is_counting) {
        return;
    }

    cpu_openrisc_count_update(cpu);
    now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

    if ((cpu->env.ttmr & TTMR_TP) <= (or1k_timer->ttcr & TTMR_TP)) {
        wait = TTMR_TP - (or1k_timer->ttcr & TTMR_TP) + 1;
        wait += cpu->env.ttmr & TTMR_TP;
    } else {
        wait = (cpu->env.ttmr & TTMR_TP) - (or1k_timer->ttcr & TTMR_TP);
    }
    next = now + (uint64_t)wait * TIMER_PERIOD;
    timer_mod(cpu->env.timer, next);
}

void cpu_openrisc_count_start(OpenRISCCPU *cpu)
{
    cpu->env.is_counting = 1;
    cpu_openrisc_count_update(cpu);
}

void cpu_openrisc_count_stop(OpenRISCCPU *cpu)
{
    timer_del(cpu->env.timer);
    cpu_openrisc_count_update(cpu);
    cpu->env.is_counting = 0;
}

static void openrisc_timer_cb(void *opaque)
{
    OpenRISCCPU *cpu = opaque;

    if ((cpu->env.ttmr & TTMR_IE) &&
         timer_expired(cpu->env.timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL))) {
        CPUState *cs = CPU(cpu);

        cpu->env.ttmr |= TTMR_IP;
        cpu_set_interrupt(cs, CPU_INTERRUPT_TIMER);
    }

    switch (cpu->env.ttmr & TTMR_M) {
    case TIMER_NONE:
        break;
    case TIMER_INTR:
        /* Zero the count by applying a negative offset to the counter */
        or1k_timer->ttcr_offset -= (cpu->env.ttmr & TTMR_TP);
        break;
    case TIMER_SHOT:
        cpu_openrisc_count_stop(cpu);
        break;
    case TIMER_CONT:
        break;
    }

    cpu_openrisc_timer_update(cpu);
    qemu_cpu_kick(CPU(cpu));
}

/* Reset the per CPU counter state. */
static void openrisc_count_reset(void *opaque)
{
    OpenRISCCPU *cpu = opaque;

    if (cpu->env.is_counting) {
        cpu_openrisc_count_stop(cpu);
    }
    cpu->env.ttmr = 0x00000000;
}

/* Reset the global timer state. */
static void openrisc_timer_reset(void *opaque)
{
    OpenRISCCPU *cpu = opaque;
    cpu_openrisc_count_set(cpu, 0);
}

static const VMStateDescription vmstate_or1k_timer = {
    .name = "or1k_timer",
    .version_id = 2,
    .minimum_version_id = 2,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(ttcr, OR1KTimerState),
        VMSTATE_UINT32(ttcr_offset, OR1KTimerState),
        VMSTATE_UINT64(clk_offset, OR1KTimerState),
        VMSTATE_END_OF_LIST()
    }
};

void cpu_openrisc_clock_init(OpenRISCCPU *cpu)
{
    cpu->env.timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, &openrisc_timer_cb, cpu);

    qemu_register_reset(openrisc_count_reset, cpu);
    if (or1k_timer == NULL) {
        or1k_timer = g_new0(OR1KTimerState, 1);
        qemu_register_reset(openrisc_timer_reset, cpu);
        vmstate_register(NULL, 0, &vmstate_or1k_timer, or1k_timer);
    }
}

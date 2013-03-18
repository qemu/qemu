/*
 * QEMU OpenRISC timer support
 *
 * Copyright (c) 2011-2012 Jia Liu <proljc@gmail.com>
 *                         Zhizhou Zhang <etouzh@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "cpu.h"
#include "hw/hw.h"
#include "qemu/timer.h"

#define TIMER_FREQ    (20 * 1000 * 1000)    /* 20MHz */

/* The time when TTCR changes */
static uint64_t last_clk;
static int is_counting;

void cpu_openrisc_count_update(OpenRISCCPU *cpu)
{
    uint64_t now, next;
    uint32_t wait;

    now = qemu_get_clock_ns(vm_clock);
    if (!is_counting) {
        qemu_del_timer(cpu->env.timer);
        last_clk = now;
        return;
    }

    cpu->env.ttcr += (uint32_t)muldiv64(now - last_clk, TIMER_FREQ,
                                        get_ticks_per_sec());
    last_clk = now;

    if ((cpu->env.ttmr & TTMR_TP) <= (cpu->env.ttcr & TTMR_TP)) {
        wait = TTMR_TP - (cpu->env.ttcr & TTMR_TP) + 1;
        wait += cpu->env.ttmr & TTMR_TP;
    } else {
        wait = (cpu->env.ttmr & TTMR_TP) - (cpu->env.ttcr & TTMR_TP);
    }

    next = now + muldiv64(wait, get_ticks_per_sec(), TIMER_FREQ);
    qemu_mod_timer(cpu->env.timer, next);
}

void cpu_openrisc_count_start(OpenRISCCPU *cpu)
{
    is_counting = 1;
    cpu_openrisc_count_update(cpu);
}

void cpu_openrisc_count_stop(OpenRISCCPU *cpu)
{
    is_counting = 0;
    cpu_openrisc_count_update(cpu);
}

static void openrisc_timer_cb(void *opaque)
{
    OpenRISCCPU *cpu = opaque;

    if ((cpu->env.ttmr & TTMR_IE) &&
         qemu_timer_expired(cpu->env.timer, qemu_get_clock_ns(vm_clock))) {
        CPUState *cs = CPU(cpu);

        cpu->env.ttmr |= TTMR_IP;
        cs->interrupt_request |= CPU_INTERRUPT_TIMER;
    }

    switch (cpu->env.ttmr & TTMR_M) {
    case TIMER_NONE:
        break;
    case TIMER_INTR:
        cpu->env.ttcr = 0;
        cpu_openrisc_count_start(cpu);
        break;
    case TIMER_SHOT:
        cpu_openrisc_count_stop(cpu);
        break;
    case TIMER_CONT:
        cpu_openrisc_count_start(cpu);
        break;
    }
}

void cpu_openrisc_clock_init(OpenRISCCPU *cpu)
{
    cpu->env.timer = qemu_new_timer_ns(vm_clock, &openrisc_timer_cb, cpu);
    cpu->env.ttmr = 0x00000000;
    cpu->env.ttcr = 0x00000000;
}

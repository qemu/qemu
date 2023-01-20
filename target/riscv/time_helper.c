/*
 * RISC-V timer helper implementation.
 *
 * Copyright (c) 2022 Rivos Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "cpu_bits.h"
#include "time_helper.h"
#include "hw/intc/riscv_aclint.h"

static void riscv_vstimer_cb(void *opaque)
{
    RISCVCPU *cpu = opaque;
    CPURISCVState *env = &cpu->env;
    env->vstime_irq = 1;
    riscv_cpu_update_mip(cpu, 0, BOOL_TO_MASK(1));
}

static void riscv_stimer_cb(void *opaque)
{
    RISCVCPU *cpu = opaque;
    riscv_cpu_update_mip(cpu, MIP_STIP, BOOL_TO_MASK(1));
}

/*
 * Called when timecmp is written to update the QEMU timer or immediately
 * trigger timer interrupt if mtimecmp <= current timer value.
 */
void riscv_timer_write_timecmp(RISCVCPU *cpu, QEMUTimer *timer,
                               uint64_t timecmp, uint64_t delta,
                               uint32_t timer_irq)
{
    uint64_t diff, ns_diff, next;
    CPURISCVState *env = &cpu->env;
    RISCVAclintMTimerState *mtimer = env->rdtime_fn_arg;
    uint32_t timebase_freq = mtimer->timebase_freq;
    uint64_t rtc_r = env->rdtime_fn(env->rdtime_fn_arg) + delta;

    if (timecmp <= rtc_r) {
        /*
         * If we're setting an stimecmp value in the "past",
         * immediately raise the timer interrupt
         */
        if (timer_irq == MIP_VSTIP) {
            env->vstime_irq = 1;
            riscv_cpu_update_mip(cpu, 0, BOOL_TO_MASK(1));
        } else {
            riscv_cpu_update_mip(cpu, MIP_STIP, BOOL_TO_MASK(1));
        }
        return;
    }

    /* Clear the [VS|S]TIP bit in mip */
    if (timer_irq == MIP_VSTIP) {
        env->vstime_irq = 0;
        riscv_cpu_update_mip(cpu, 0, BOOL_TO_MASK(0));
    } else {
        riscv_cpu_update_mip(cpu, timer_irq, BOOL_TO_MASK(0));
    }

    /* otherwise, set up the future timer interrupt */
    diff = timecmp - rtc_r;
    /* back to ns (note args switched in muldiv64) */
    ns_diff = muldiv64(diff, NANOSECONDS_PER_SECOND, timebase_freq);

    /*
     * check if ns_diff overflowed and check if the addition would potentially
     * overflow
     */
    if ((NANOSECONDS_PER_SECOND > timebase_freq && ns_diff < diff) ||
        ns_diff > INT64_MAX) {
        next = INT64_MAX;
    } else {
        /*
         * as it is very unlikely qemu_clock_get_ns will return a value
         * greater than INT64_MAX, no additional check is needed for an
         * unsigned integer overflow.
         */
        next = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + ns_diff;
        /*
         * if ns_diff is INT64_MAX next may still be outside the range
         * of a signed integer.
         */
        next = MIN(next, INT64_MAX);
    }

    timer_mod(timer, next);
}

void riscv_timer_init(RISCVCPU *cpu)
{
    CPURISCVState *env;

    if (!cpu) {
        return;
    }

    env = &cpu->env;
    env->stimer = timer_new_ns(QEMU_CLOCK_VIRTUAL, &riscv_stimer_cb, cpu);
    env->stimecmp = 0;

    env->vstimer = timer_new_ns(QEMU_CLOCK_VIRTUAL, &riscv_vstimer_cb, cpu);
    env->vstimecmp = 0;
}

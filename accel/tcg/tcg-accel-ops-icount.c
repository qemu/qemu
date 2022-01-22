/*
 * QEMU TCG Single Threaded vCPUs implementation using instruction counting
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
 * Copyright (c) 2014 Red Hat Inc.
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
#include "qemu-common.h"
#include "sysemu/tcg.h"
#include "sysemu/replay.h"
#include "sysemu/cpu-timers.h"
#include "qemu/main-loop.h"
#include "qemu/guest-random.h"
#include "exec/exec-all.h"

#include "tcg-accel-ops.h"
#include "tcg-accel-ops-icount.h"
#include "tcg-accel-ops-rr.h"

static int64_t icount_get_limit(void)
{
    int64_t deadline;

    if (replay_mode != REPLAY_MODE_PLAY) {
        /*
         * Include all the timers, because they may need an attention.
         * Too long CPU execution may create unnecessary delay in UI.
         */
        deadline = qemu_clock_deadline_ns_all(QEMU_CLOCK_VIRTUAL,
                                              QEMU_TIMER_ATTR_ALL);
        /* Check realtime timers, because they help with input processing */
        deadline = qemu_soonest_timeout(deadline,
                qemu_clock_deadline_ns_all(QEMU_CLOCK_REALTIME,
                                           QEMU_TIMER_ATTR_ALL));

        /*
         * Maintain prior (possibly buggy) behaviour where if no deadline
         * was set (as there is no QEMU_CLOCK_VIRTUAL timer) or it is more than
         * INT32_MAX nanoseconds ahead, we still use INT32_MAX
         * nanoseconds.
         */
        if ((deadline < 0) || (deadline > INT32_MAX)) {
            deadline = INT32_MAX;
        }

        return icount_round(deadline);
    } else {
        return replay_get_instructions();
    }
}

static void icount_notify_aio_contexts(void)
{
    /* Wake up other AioContexts.  */
    qemu_clock_notify(QEMU_CLOCK_VIRTUAL);
    qemu_clock_run_timers(QEMU_CLOCK_VIRTUAL);
}

void icount_handle_deadline(void)
{
    assert(qemu_in_vcpu_thread());
    int64_t deadline = qemu_clock_deadline_ns_all(QEMU_CLOCK_VIRTUAL,
                                                  QEMU_TIMER_ATTR_ALL);

    /*
     * Instructions, interrupts, and exceptions are processed in cpu-exec.
     * Don't interrupt cpu thread, when these events are waiting
     * (i.e., there is no checkpoint)
     */
    if (deadline == 0
        && (replay_mode != REPLAY_MODE_PLAY || replay_has_checkpoint())) {
        icount_notify_aio_contexts();
    }
}

void icount_prepare_for_run(CPUState *cpu)
{
    int insns_left;

    /*
     * These should always be cleared by icount_process_data after
     * each vCPU execution. However u16.high can be raised
     * asynchronously by cpu_exit/cpu_interrupt/tcg_handle_interrupt
     */
    g_assert(cpu_neg(cpu)->icount_decr.u16.low == 0);
    g_assert(cpu->icount_extra == 0);

    cpu->icount_budget = icount_get_limit();
    insns_left = MIN(0xffff, cpu->icount_budget);
    cpu_neg(cpu)->icount_decr.u16.low = insns_left;
    cpu->icount_extra = cpu->icount_budget - insns_left;

    replay_mutex_lock();

    if (cpu->icount_budget == 0 && replay_has_checkpoint()) {
        icount_notify_aio_contexts();
    }
}

void icount_process_data(CPUState *cpu)
{
    /* Account for executed instructions */
    icount_update(cpu);

    /* Reset the counters */
    cpu_neg(cpu)->icount_decr.u16.low = 0;
    cpu->icount_extra = 0;
    cpu->icount_budget = 0;

    replay_account_executed_instructions();

    replay_mutex_unlock();
}

void icount_handle_interrupt(CPUState *cpu, int mask)
{
    int old_mask = cpu->interrupt_request;

    tcg_handle_interrupt(cpu, mask);
    if (qemu_cpu_is_self(cpu) &&
        !cpu->can_do_io
        && (mask & ~old_mask) != 0) {
        cpu_abort(cpu, "Raised interrupt while not in I/O function");
    }
}

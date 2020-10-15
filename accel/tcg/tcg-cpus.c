/*
 * QEMU TCG vCPU common functionality
 *
 * Functionality common to all TCG vCPU variants: mttcg, rr and icount.
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
#include "qemu/main-loop.h"
#include "qemu/guest-random.h"
#include "exec/exec-all.h"
#include "hw/boards.h"

#include "tcg-cpus.h"
#include "tcg-cpus-mttcg.h"
#include "tcg-cpus-rr.h"

/* common functionality among all TCG variants */

void tcg_start_vcpu_thread(CPUState *cpu)
{
    char thread_name[VCPU_THREAD_NAME_SIZE];
    static QemuCond *single_tcg_halt_cond;
    static QemuThread *single_tcg_cpu_thread;
    static int tcg_region_inited;

    assert(tcg_enabled());
    /*
     * Initialize TCG regions--once. Now is a good time, because:
     * (1) TCG's init context, prologue and target globals have been set up.
     * (2) qemu_tcg_mttcg_enabled() works now (TCG init code runs before the
     *     -accel flag is processed, so the check doesn't work then).
     */
    if (!tcg_region_inited) {
        tcg_region_inited = 1;
        tcg_region_init();
        parallel_cpus = qemu_tcg_mttcg_enabled() && current_machine->smp.max_cpus > 1;
    }

    if (qemu_tcg_mttcg_enabled() || !single_tcg_cpu_thread) {
        cpu->thread = g_malloc0(sizeof(QemuThread));
        cpu->halt_cond = g_malloc0(sizeof(QemuCond));
        qemu_cond_init(cpu->halt_cond);

        if (qemu_tcg_mttcg_enabled()) {
            /* create a thread per vCPU with TCG (MTTCG) */
            snprintf(thread_name, VCPU_THREAD_NAME_SIZE, "CPU %d/TCG",
                 cpu->cpu_index);

            qemu_thread_create(cpu->thread, thread_name, tcg_cpu_thread_fn,
                               cpu, QEMU_THREAD_JOINABLE);

        } else {
            /* share a single thread for all cpus with TCG */
            snprintf(thread_name, VCPU_THREAD_NAME_SIZE, "ALL CPUs/TCG");
            qemu_thread_create(cpu->thread, thread_name,
                               tcg_rr_cpu_thread_fn,
                               cpu, QEMU_THREAD_JOINABLE);

            single_tcg_halt_cond = cpu->halt_cond;
            single_tcg_cpu_thread = cpu->thread;
        }
#ifdef _WIN32
        cpu->hThread = qemu_thread_get_handle(cpu->thread);
#endif
    } else {
        /* For non-MTTCG cases we share the thread */
        cpu->thread = single_tcg_cpu_thread;
        cpu->halt_cond = single_tcg_halt_cond;
        cpu->thread_id = first_cpu->thread_id;
        cpu->can_do_io = 1;
        cpu->created = true;
    }
}

void qemu_tcg_destroy_vcpu(CPUState *cpu)
{
    cpu_thread_signal_destroyed(cpu);
}

int tcg_cpu_exec(CPUState *cpu)
{
    int ret;
#ifdef CONFIG_PROFILER
    int64_t ti;
#endif
    assert(tcg_enabled());
#ifdef CONFIG_PROFILER
    ti = profile_getclock();
#endif
    cpu_exec_start(cpu);
    ret = cpu_exec(cpu);
    cpu_exec_end(cpu);
#ifdef CONFIG_PROFILER
    qatomic_set(&tcg_ctx->prof.cpu_exec_time,
                tcg_ctx->prof.cpu_exec_time + profile_getclock() - ti);
#endif
    return ret;
}

/* mask must never be zero, except for A20 change call */
void tcg_handle_interrupt(CPUState *cpu, int mask)
{
    g_assert(qemu_mutex_iothread_locked());

    cpu->interrupt_request |= mask;

    /*
     * If called from iothread context, wake the target cpu in
     * case its halted.
     */
    if (!qemu_cpu_is_self(cpu)) {
        qemu_cpu_kick(cpu);
    } else {
        qatomic_set(&cpu_neg(cpu)->icount_decr.u16.high, -1);
    }
}

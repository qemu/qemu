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

#include "tcg-accel-ops.h"
#include "tcg-accel-ops-mttcg.h"
#include "tcg-accel-ops-rr.h"
#include "tcg-accel-ops-icount.h"

/* common functionality among all TCG variants */

void tcg_cpus_destroy(CPUState *cpu)
{
    cpu_thread_signal_destroyed(cpu);
}

int tcg_cpus_exec(CPUState *cpu)
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

static void tcg_accel_ops_init(AccelOpsClass *ops)
{
    if (qemu_tcg_mttcg_enabled()) {
        ops->create_vcpu_thread = mttcg_start_vcpu_thread;
        ops->kick_vcpu_thread = mttcg_kick_vcpu_thread;
        ops->handle_interrupt = tcg_handle_interrupt;
    } else if (icount_enabled()) {
        ops->create_vcpu_thread = rr_start_vcpu_thread;
        ops->kick_vcpu_thread = rr_kick_vcpu_thread;
        ops->handle_interrupt = icount_handle_interrupt;
        ops->get_virtual_clock = icount_get;
        ops->get_elapsed_ticks = icount_get;
    } else {
        ops->create_vcpu_thread = rr_start_vcpu_thread;
        ops->kick_vcpu_thread = rr_kick_vcpu_thread;
        ops->handle_interrupt = tcg_handle_interrupt;
    }
}

static void tcg_accel_ops_class_init(ObjectClass *oc, void *data)
{
    AccelOpsClass *ops = ACCEL_OPS_CLASS(oc);

    ops->ops_init = tcg_accel_ops_init;
}

static const TypeInfo tcg_accel_ops_type = {
    .name = ACCEL_OPS_NAME("tcg"),

    .parent = TYPE_ACCEL_OPS,
    .class_init = tcg_accel_ops_class_init,
    .abstract = true,
};

static void tcg_accel_ops_register_types(void)
{
    type_register_static(&tcg_accel_ops_type);
}
type_init(tcg_accel_ops_register_types);

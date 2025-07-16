/*
 * Copyright 2008 IBM Corporation
 *           2008 Red Hat, Inc.
 * Copyright 2011 Intel Corporation
 * Copyright 2016 Veertu, Inc.
 * Copyright 2017 The Android Open Source Project
 *
 * QEMU Hypervisor.framework support
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * This file contain code under public domain from the hvdos project:
 * https://github.com/mist64/hvdos
 *
 * Parts Copyright (c) 2011 NetApp, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY NETAPP, INC ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL NETAPP, INC OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "qemu/osdep.h"
#include "qemu/guest-random.h"
#include "qemu/main-loop.h"
#include "qemu/queue.h"
#include "gdbstub/enums.h"
#include "exec/cpu-common.h"
#include "hw/core/cpu.h"
#include "accel/accel-cpu-ops.h"
#include "system/cpus.h"
#include "system/hvf.h"
#include "system/hvf_int.h"
#include <mach/mach_time.h>

HVFState *hvf_state;

/* Memory slots */

hvf_slot *hvf_find_overlap_slot(uint64_t start, uint64_t size)
{
    hvf_slot *slot;
    int x;
    for (x = 0; x < hvf_state->num_slots; ++x) {
        slot = &hvf_state->slots[x];
        if (slot->size && start < (slot->start + slot->size) &&
            (start + size) > slot->start) {
            return slot;
        }
    }
    return NULL;
}

static void do_hvf_cpu_synchronize_state(CPUState *cpu, run_on_cpu_data arg)
{
    if (!cpu->vcpu_dirty) {
        hvf_get_registers(cpu);
        cpu->vcpu_dirty = true;
    }
}

static void hvf_cpu_synchronize_state(CPUState *cpu)
{
    if (!cpu->vcpu_dirty) {
        run_on_cpu(cpu, do_hvf_cpu_synchronize_state, RUN_ON_CPU_NULL);
    }
}

static void do_hvf_cpu_synchronize_set_dirty(CPUState *cpu,
                                             run_on_cpu_data arg)
{
    /* QEMU state is the reference, push it to HVF now and on next entry */
    cpu->vcpu_dirty = true;
}

static void hvf_cpu_synchronize_post_reset(CPUState *cpu)
{
    run_on_cpu(cpu, do_hvf_cpu_synchronize_set_dirty, RUN_ON_CPU_NULL);
}

static void hvf_cpu_synchronize_post_init(CPUState *cpu)
{
    run_on_cpu(cpu, do_hvf_cpu_synchronize_set_dirty, RUN_ON_CPU_NULL);
}

static void hvf_cpu_synchronize_pre_loadvm(CPUState *cpu)
{
    run_on_cpu(cpu, do_hvf_cpu_synchronize_set_dirty, RUN_ON_CPU_NULL);
}

static void dummy_signal(int sig)
{
}

static void do_hvf_get_vcpu_exec_time(CPUState *cpu, run_on_cpu_data arg)
{
    int r = hv_vcpu_get_exec_time(cpu->accel->fd, arg.host_ptr);
    assert_hvf_ok(r);
}

static void hvf_vcpu_destroy(CPUState *cpu)
{
    hv_return_t ret = hv_vcpu_destroy(cpu->accel->fd);
    assert_hvf_ok(ret);

    hvf_arch_vcpu_destroy(cpu);
    g_free(cpu->accel);
    cpu->accel = NULL;
}

static int hvf_init_vcpu(CPUState *cpu)
{
    int r;

    cpu->accel = g_new0(AccelCPUState, 1);

    /* init cpu signals */
    struct sigaction sigact;

    memset(&sigact, 0, sizeof(sigact));
    sigact.sa_handler = dummy_signal;
    sigaction(SIG_IPI, &sigact, NULL);

    pthread_sigmask(SIG_BLOCK, NULL, &cpu->accel->unblock_ipi_mask);
    sigdelset(&cpu->accel->unblock_ipi_mask, SIG_IPI);

#ifdef __aarch64__
    r = hv_vcpu_create(&cpu->accel->fd,
                       (hv_vcpu_exit_t **)&cpu->accel->exit, NULL);
#else
    r = hv_vcpu_create(&cpu->accel->fd, HV_VCPU_DEFAULT);
#endif
    assert_hvf_ok(r);
    cpu->vcpu_dirty = true;

    cpu->accel->guest_debug_enabled = false;

    return hvf_arch_init_vcpu(cpu);
}

/*
 * The HVF-specific vCPU thread function. This one should only run when the host
 * CPU supports the VMX "unrestricted guest" feature.
 */
static void *hvf_cpu_thread_fn(void *arg)
{
    CPUState *cpu = arg;

    int r;

    assert(hvf_enabled());

    rcu_register_thread();

    bql_lock();
    qemu_thread_get_self(cpu->thread);

    cpu->thread_id = qemu_get_thread_id();
    current_cpu = cpu;

    hvf_init_vcpu(cpu);

    /* signal CPU creation */
    cpu_thread_signal_created(cpu);
    qemu_guest_random_seed_thread_part2(cpu->random_seed);

    do {
        if (cpu_can_run(cpu)) {
            r = hvf_vcpu_exec(cpu);
            if (r == EXCP_DEBUG) {
                cpu_handle_guest_debug(cpu);
            }
        }
        qemu_wait_io_event(cpu);
    } while (!cpu->unplug || cpu_can_run(cpu));

    hvf_vcpu_destroy(cpu);
    cpu_thread_signal_destroyed(cpu);
    bql_unlock();
    rcu_unregister_thread();
    return NULL;
}

static void hvf_start_vcpu_thread(CPUState *cpu)
{
    char thread_name[VCPU_THREAD_NAME_SIZE];

    /*
     * HVF currently does not support TCG, and only runs in
     * unrestricted-guest mode.
     */
    assert(hvf_enabled());

    snprintf(thread_name, VCPU_THREAD_NAME_SIZE, "CPU %d/HVF",
             cpu->cpu_index);
    qemu_thread_create(cpu->thread, thread_name, hvf_cpu_thread_fn,
                       cpu, QEMU_THREAD_JOINABLE);
}

struct hvf_sw_breakpoint *hvf_find_sw_breakpoint(CPUState *cpu, vaddr pc)
{
    struct hvf_sw_breakpoint *bp;

    QTAILQ_FOREACH(bp, &hvf_state->hvf_sw_breakpoints, entry) {
        if (bp->pc == pc) {
            return bp;
        }
    }
    return NULL;
}

int hvf_sw_breakpoints_active(CPUState *cpu)
{
    return !QTAILQ_EMPTY(&hvf_state->hvf_sw_breakpoints);
}

static void do_hvf_update_guest_debug(CPUState *cpu, run_on_cpu_data arg)
{
    hvf_arch_update_guest_debug(cpu);
}

int hvf_update_guest_debug(CPUState *cpu)
{
    run_on_cpu(cpu, do_hvf_update_guest_debug, RUN_ON_CPU_NULL);
    return 0;
}

static int hvf_insert_breakpoint(CPUState *cpu, int type, vaddr addr, vaddr len)
{
    struct hvf_sw_breakpoint *bp;
    int err;

    if (type == GDB_BREAKPOINT_SW) {
        bp = hvf_find_sw_breakpoint(cpu, addr);
        if (bp) {
            bp->use_count++;
            return 0;
        }

        bp = g_new(struct hvf_sw_breakpoint, 1);
        bp->pc = addr;
        bp->use_count = 1;
        err = hvf_arch_insert_sw_breakpoint(cpu, bp);
        if (err) {
            g_free(bp);
            return err;
        }

        QTAILQ_INSERT_HEAD(&hvf_state->hvf_sw_breakpoints, bp, entry);
    } else {
        err = hvf_arch_insert_hw_breakpoint(addr, len, type);
        if (err) {
            return err;
        }
    }

    CPU_FOREACH(cpu) {
        err = hvf_update_guest_debug(cpu);
        if (err) {
            return err;
        }
    }
    return 0;
}

static int hvf_remove_breakpoint(CPUState *cpu, int type, vaddr addr, vaddr len)
{
    struct hvf_sw_breakpoint *bp;
    int err;

    if (type == GDB_BREAKPOINT_SW) {
        bp = hvf_find_sw_breakpoint(cpu, addr);
        if (!bp) {
            return -ENOENT;
        }

        if (bp->use_count > 1) {
            bp->use_count--;
            return 0;
        }

        err = hvf_arch_remove_sw_breakpoint(cpu, bp);
        if (err) {
            return err;
        }

        QTAILQ_REMOVE(&hvf_state->hvf_sw_breakpoints, bp, entry);
        g_free(bp);
    } else {
        err = hvf_arch_remove_hw_breakpoint(addr, len, type);
        if (err) {
            return err;
        }
    }

    CPU_FOREACH(cpu) {
        err = hvf_update_guest_debug(cpu);
        if (err) {
            return err;
        }
    }
    return 0;
}

static void hvf_remove_all_breakpoints(CPUState *cpu)
{
    struct hvf_sw_breakpoint *bp, *next;
    CPUState *tmpcpu;

    QTAILQ_FOREACH_SAFE(bp, &hvf_state->hvf_sw_breakpoints, entry, next) {
        if (hvf_arch_remove_sw_breakpoint(cpu, bp) != 0) {
            /* Try harder to find a CPU that currently sees the breakpoint. */
            CPU_FOREACH(tmpcpu)
            {
                if (hvf_arch_remove_sw_breakpoint(tmpcpu, bp) == 0) {
                    break;
                }
            }
        }
        QTAILQ_REMOVE(&hvf_state->hvf_sw_breakpoints, bp, entry);
        g_free(bp);
    }
    hvf_arch_remove_all_hw_breakpoints();

    CPU_FOREACH(cpu) {
        hvf_update_guest_debug(cpu);
    }
}

static void hvf_get_vcpu_stats(CPUState *cpu, GString *buf)
{
    uint64_t time_mach; /* units of mach_absolute_time() */

    run_on_cpu(cpu, do_hvf_get_vcpu_exec_time, RUN_ON_CPU_HOST_PTR(&time_mach));

    mach_timebase_info_data_t timebase;
    mach_timebase_info(&timebase);
    uint64_t time_ns = time_mach * timebase.numer / timebase.denom;

    g_string_append_printf(buf, "HVF cumulative execution time: %llu.%.3llus\n",
                                 time_ns / 1000000000,
                                (time_ns % 1000000000) / 1000000);
}

static void hvf_accel_ops_class_init(ObjectClass *oc, const void *data)
{
    AccelOpsClass *ops = ACCEL_OPS_CLASS(oc);

    ops->create_vcpu_thread = hvf_start_vcpu_thread;
    ops->kick_vcpu_thread = hvf_kick_vcpu_thread;
    ops->handle_interrupt = generic_handle_interrupt;

    ops->synchronize_post_reset = hvf_cpu_synchronize_post_reset;
    ops->synchronize_post_init = hvf_cpu_synchronize_post_init;
    ops->synchronize_state = hvf_cpu_synchronize_state;
    ops->synchronize_pre_loadvm = hvf_cpu_synchronize_pre_loadvm;

    ops->insert_breakpoint = hvf_insert_breakpoint;
    ops->remove_breakpoint = hvf_remove_breakpoint;
    ops->remove_all_breakpoints = hvf_remove_all_breakpoints;
    ops->update_guest_debug = hvf_update_guest_debug;
    ops->supports_guest_debug = hvf_arch_supports_guest_debug;

    ops->get_vcpu_stats = hvf_get_vcpu_stats;
};

static const TypeInfo hvf_accel_ops_type = {
    .name = ACCEL_OPS_NAME("hvf"),

    .parent = TYPE_ACCEL_OPS,
    .class_init = hvf_accel_ops_class_init,
    .abstract = true,
};

static void hvf_accel_ops_register_types(void)
{
    type_register_static(&hvf_accel_ops_type);
}

type_init(hvf_accel_ops_register_types);

/*
 * Copyright (c) 2018-2019 Maxime Villard, All rights reserved.
 *
 * NetBSD Virtual Machine Monitor (NVMM) accelerator for QEMU.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "system/kvm_int.h"
#include "qemu/main-loop.h"
#include "system/accel-ops.h"
#include "system/cpus.h"
#include "qemu/guest-random.h"

#include "system/nvmm.h"
#include "nvmm-accel-ops.h"

static void *qemu_nvmm_cpu_thread_fn(void *arg)
{
    CPUState *cpu = arg;
    int r;

    assert(nvmm_enabled());

    rcu_register_thread();

    bql_lock();
    qemu_thread_get_self(cpu->thread);
    cpu->thread_id = qemu_get_thread_id();
    current_cpu = cpu;

    r = nvmm_init_vcpu(cpu);
    if (r < 0) {
        fprintf(stderr, "nvmm_init_vcpu failed: %s\n", strerror(-r));
        exit(1);
    }

    /* signal CPU creation */
    cpu_thread_signal_created(cpu);
    qemu_guest_random_seed_thread_part2(cpu->random_seed);

    do {
        if (cpu_can_run(cpu)) {
            r = nvmm_vcpu_exec(cpu);
            if (r == EXCP_DEBUG) {
                cpu_handle_guest_debug(cpu);
            }
        }
        while (cpu_thread_is_idle(cpu)) {
            qemu_cond_wait_bql(cpu->halt_cond);
        }
        qemu_wait_io_event_common(cpu);
    } while (!cpu->unplug || cpu_can_run(cpu));

    nvmm_destroy_vcpu(cpu);
    cpu_thread_signal_destroyed(cpu);
    bql_unlock();
    rcu_unregister_thread();
    return NULL;
}

static void nvmm_start_vcpu_thread(CPUState *cpu)
{
    char thread_name[VCPU_THREAD_NAME_SIZE];

    snprintf(thread_name, VCPU_THREAD_NAME_SIZE, "CPU %d/NVMM",
             cpu->cpu_index);
    qemu_thread_create(cpu->thread, thread_name, qemu_nvmm_cpu_thread_fn,
                       cpu, QEMU_THREAD_JOINABLE);
}

/*
 * Abort the call to run the virtual processor by another thread, and to
 * return the control to that thread.
 */
static void nvmm_kick_vcpu_thread(CPUState *cpu)
{
    cpu->exit_request = 1;
    cpus_kick_thread(cpu);
}

static void nvmm_accel_ops_class_init(ObjectClass *oc, void *data)
{
    AccelOpsClass *ops = ACCEL_OPS_CLASS(oc);

    ops->create_vcpu_thread = nvmm_start_vcpu_thread;
    ops->kick_vcpu_thread = nvmm_kick_vcpu_thread;

    ops->synchronize_post_reset = nvmm_cpu_synchronize_post_reset;
    ops->synchronize_post_init = nvmm_cpu_synchronize_post_init;
    ops->synchronize_state = nvmm_cpu_synchronize_state;
    ops->synchronize_pre_loadvm = nvmm_cpu_synchronize_pre_loadvm;
}

static const TypeInfo nvmm_accel_ops_type = {
    .name = ACCEL_OPS_NAME("nvmm"),

    .parent = TYPE_ACCEL_OPS,
    .class_init = nvmm_accel_ops_class_init,
    .abstract = true,
};

static void nvmm_accel_ops_register_types(void)
{
    type_register_static(&nvmm_accel_ops_type);
}
type_init(nvmm_accel_ops_register_types);

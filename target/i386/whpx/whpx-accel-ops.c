/*
 * QEMU Windows Hypervisor Platform accelerator (WHPX)
 *
 * Copyright Microsoft Corp. 2017
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "system/kvm_int.h"
#include "qemu/main-loop.h"
#include "system/cpus.h"
#include "qemu/guest-random.h"

#include "system/whpx.h"
#include "whpx-internal.h"
#include "whpx-accel-ops.h"

static void *whpx_cpu_thread_fn(void *arg)
{
    CPUState *cpu = arg;
    int r;

    rcu_register_thread();

    bql_lock();
    qemu_thread_get_self(cpu->thread);
    cpu->thread_id = qemu_get_thread_id();
    current_cpu = cpu;

    r = whpx_init_vcpu(cpu);
    if (r < 0) {
        fprintf(stderr, "whpx_init_vcpu failed: %s\n", strerror(-r));
        exit(1);
    }

    /* signal CPU creation */
    cpu_thread_signal_created(cpu);
    qemu_guest_random_seed_thread_part2(cpu->random_seed);

    do {
        if (cpu_can_run(cpu)) {
            r = whpx_vcpu_exec(cpu);
            if (r == EXCP_DEBUG) {
                cpu_handle_guest_debug(cpu);
            }
        }
        while (cpu_thread_is_idle(cpu)) {
            qemu_cond_wait_bql(cpu->halt_cond);
        }
        qemu_wait_io_event_common(cpu);
    } while (!cpu->unplug || cpu_can_run(cpu));

    whpx_destroy_vcpu(cpu);
    cpu_thread_signal_destroyed(cpu);
    bql_unlock();
    rcu_unregister_thread();
    return NULL;
}

static void whpx_start_vcpu_thread(CPUState *cpu)
{
    char thread_name[VCPU_THREAD_NAME_SIZE];

    snprintf(thread_name, VCPU_THREAD_NAME_SIZE, "CPU %d/WHPX",
             cpu->cpu_index);
    qemu_thread_create(cpu->thread, thread_name, whpx_cpu_thread_fn,
                       cpu, QEMU_THREAD_JOINABLE);
}

static void whpx_kick_vcpu_thread(CPUState *cpu)
{
    if (!qemu_cpu_is_self(cpu)) {
        whpx_vcpu_kick(cpu);
    }
}

static bool whpx_vcpu_thread_is_idle(CPUState *cpu)
{
    return !whpx_apic_in_platform();
}

static void whpx_accel_ops_class_init(ObjectClass *oc, void *data)
{
    AccelOpsClass *ops = ACCEL_OPS_CLASS(oc);

    ops->create_vcpu_thread = whpx_start_vcpu_thread;
    ops->kick_vcpu_thread = whpx_kick_vcpu_thread;
    ops->cpu_thread_is_idle = whpx_vcpu_thread_is_idle;

    ops->synchronize_post_reset = whpx_cpu_synchronize_post_reset;
    ops->synchronize_post_init = whpx_cpu_synchronize_post_init;
    ops->synchronize_state = whpx_cpu_synchronize_state;
    ops->synchronize_pre_loadvm = whpx_cpu_synchronize_pre_loadvm;
    ops->synchronize_pre_resume = whpx_cpu_synchronize_pre_resume;
}

static const TypeInfo whpx_accel_ops_type = {
    .name = ACCEL_OPS_NAME("whpx"),

    .parent = TYPE_ACCEL_OPS,
    .class_init = whpx_accel_ops_class_init,
    .abstract = true,
};

static void whpx_accel_ops_register_types(void)
{
    type_register_static(&whpx_accel_ops_type);
}
type_init(whpx_accel_ops_register_types);

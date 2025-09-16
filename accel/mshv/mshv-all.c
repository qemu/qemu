/*
 * QEMU MSHV support
 *
 * Copyright Microsoft, Corp. 2025
 *
 * Authors:
 *  Ziqiao Zhou       <ziqiaozhou@microsoft.com>
 *  Magnus Kulke      <magnuskulke@microsoft.com>
 *  Jinank Jain       <jinankjain@microsoft.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/event_notifier.h"
#include "qemu/module.h"
#include "qemu/main-loop.h"
#include "hw/boards.h"

#include "hw/hyperv/hvhdk.h"
#include "hw/hyperv/hvhdk_mini.h"
#include "hw/hyperv/hvgdk.h"
#include "linux/mshv.h"

#include "qemu/accel.h"
#include "qemu/guest-random.h"
#include "accel/accel-ops.h"
#include "accel/accel-cpu-ops.h"
#include "system/cpus.h"
#include "system/runstate.h"
#include "system/accel-blocker.h"
#include "system/address-spaces.h"
#include "system/mshv.h"
#include "system/mshv_int.h"
#include "system/reset.h"
#include "trace.h"
#include <err.h>
#include <stdint.h>
#include <sys/ioctl.h>

#define TYPE_MSHV_ACCEL ACCEL_CLASS_NAME("mshv")

DECLARE_INSTANCE_CHECKER(MshvState, MSHV_STATE, TYPE_MSHV_ACCEL)

bool mshv_allowed;

MshvState *mshv_state;

static void mem_region_add(MemoryListener *listener,
                           MemoryRegionSection *section)
{
    MshvMemoryListener *mml;
    mml = container_of(listener, MshvMemoryListener, listener);
    memory_region_ref(section->mr);
    mshv_set_phys_mem(mml, section, true);
}

static void mem_region_del(MemoryListener *listener,
                           MemoryRegionSection *section)
{
    MshvMemoryListener *mml;
    mml = container_of(listener, MshvMemoryListener, listener);
    mshv_set_phys_mem(mml, section, false);
    memory_region_unref(section->mr);
}

static MemoryListener mshv_memory_listener = {
    .name = "mshv",
    .priority = MEMORY_LISTENER_PRIORITY_ACCEL,
    .region_add = mem_region_add,
    .region_del = mem_region_del,
};

static MemoryListener mshv_io_listener = {
    .name = "mshv", .priority = MEMORY_LISTENER_PRIORITY_DEV_BACKEND,
    /* MSHV does not support PIO eventfd */
};

static void register_mshv_memory_listener(MshvState *s, MshvMemoryListener *mml,
                                          AddressSpace *as, int as_id,
                                          const char *name)
{
    int i;

    mml->listener = mshv_memory_listener;
    mml->listener.name = name;
    memory_listener_register(&mml->listener, as);
    for (i = 0; i < s->nr_as; ++i) {
        if (!s->as[i].as) {
            s->as[i].as = as;
            s->as[i].ml = mml;
            break;
        }
    }
}

static int mshv_init(AccelState *as, MachineState *ms)
{
    MshvState *s;
    s = MSHV_STATE(as);

    accel_blocker_init();

    s->vm = 0;

    s->nr_as = 1;
    s->as = g_new0(MshvAddressSpace, s->nr_as);

    mshv_state = s;

    register_mshv_memory_listener(s, &s->memory_listener, &address_space_memory,
                                  0, "mshv-memory");
    memory_listener_register(&mshv_io_listener, &address_space_io);

    return 0;
}

static void mshv_start_vcpu_thread(CPUState *cpu)
{
    error_report("unimplemented");
    abort();
}

static void mshv_cpu_synchronize_post_init(CPUState *cpu)
{
    error_report("unimplemented");
    abort();
}

static void mshv_cpu_synchronize_post_reset(CPUState *cpu)
{
    error_report("unimplemented");
    abort();
}

static void mshv_cpu_synchronize_pre_loadvm(CPUState *cpu)
{
    error_report("unimplemented");
    abort();
}

static void mshv_cpu_synchronize(CPUState *cpu)
{
    error_report("unimplemented");
    abort();
}

static bool mshv_cpus_are_resettable(void)
{
    error_report("unimplemented");
    abort();
}

static void mshv_accel_class_init(ObjectClass *oc, const void *data)
{
    AccelClass *ac = ACCEL_CLASS(oc);

    ac->name = "MSHV";
    ac->init_machine = mshv_init;
    ac->allowed = &mshv_allowed;
}

static void mshv_accel_instance_init(Object *obj)
{
    MshvState *s = MSHV_STATE(obj);

    s->vm = 0;
}

static const TypeInfo mshv_accel_type = {
    .name = TYPE_MSHV_ACCEL,
    .parent = TYPE_ACCEL,
    .instance_init = mshv_accel_instance_init,
    .class_init = mshv_accel_class_init,
    .instance_size = sizeof(MshvState),
};

static void mshv_accel_ops_class_init(ObjectClass *oc, const void *data)
{
    AccelOpsClass *ops = ACCEL_OPS_CLASS(oc);

    ops->create_vcpu_thread = mshv_start_vcpu_thread;
    ops->synchronize_post_init = mshv_cpu_synchronize_post_init;
    ops->synchronize_post_reset = mshv_cpu_synchronize_post_reset;
    ops->synchronize_state = mshv_cpu_synchronize;
    ops->synchronize_pre_loadvm = mshv_cpu_synchronize_pre_loadvm;
    ops->cpus_are_resettable = mshv_cpus_are_resettable;
    ops->handle_interrupt = generic_handle_interrupt;
}

static const TypeInfo mshv_accel_ops_type = {
    .name = ACCEL_OPS_NAME("mshv"),
    .parent = TYPE_ACCEL_OPS,
    .class_init = mshv_accel_ops_class_init,
    .abstract = true,
};

static void mshv_type_init(void)
{
    type_register_static(&mshv_accel_type);
    type_register_static(&mshv_accel_ops_type);
}

type_init(mshv_type_init);

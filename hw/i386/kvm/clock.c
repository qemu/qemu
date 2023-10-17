/*
 * QEMU KVM support, paravirtual clock device
 *
 * Copyright (C) 2011 Siemens AG
 *
 * Authors:
 *  Jan Kiszka        <jan.kiszka@siemens.com>
 *
 * This work is licensed under the terms of the GNU GPL version 2.
 * See the COPYING file in the top-level directory.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/host-utils.h"
#include "qemu/module.h"
#include "sysemu/kvm.h"
#include "sysemu/runstate.h"
#include "sysemu/hw_accel.h"
#include "kvm/kvm_i386.h"
#include "migration/vmstate.h"
#include "hw/sysbus.h"
#include "hw/i386/kvm/clock.h"
#include "hw/qdev-properties.h"
#include "qapi/error.h"

#include <linux/kvm.h>
#include "standard-headers/asm-x86/kvm_para.h"
#include "qom/object.h"

#define TYPE_KVM_CLOCK "kvmclock"
OBJECT_DECLARE_SIMPLE_TYPE(KVMClockState, KVM_CLOCK)

struct KVMClockState {
    /*< private >*/
    SysBusDevice busdev;
    /*< public >*/

    uint64_t clock;
    bool clock_valid;

    /* whether the 'clock' value was obtained in the 'paused' state */
    bool runstate_paused;

    /* whether machine type supports reliable KVM_GET_CLOCK */
    bool mach_use_reliable_get_clock;

    /* whether the 'clock' value was obtained in a host with
     * reliable KVM_GET_CLOCK */
    bool clock_is_reliable;
};

struct pvclock_vcpu_time_info {
    uint32_t   version;
    uint32_t   pad0;
    uint64_t   tsc_timestamp;
    uint64_t   system_time;
    uint32_t   tsc_to_system_mul;
    int8_t     tsc_shift;
    uint8_t    flags;
    uint8_t    pad[2];
} __attribute__((__packed__)); /* 32 bytes */

static uint64_t kvmclock_current_nsec(KVMClockState *s)
{
    CPUState *cpu = first_cpu;
    CPUX86State *env = cpu_env(cpu);
    hwaddr kvmclock_struct_pa;
    uint64_t migration_tsc = env->tsc;
    struct pvclock_vcpu_time_info time;
    uint64_t delta;
    uint64_t nsec_lo;
    uint64_t nsec_hi;
    uint64_t nsec;

    cpu_synchronize_state(cpu);

    if (!(env->system_time_msr & 1ULL)) {
        /* KVM clock not active */
        return 0;
    }

    kvmclock_struct_pa = env->system_time_msr & ~1ULL;
    cpu_physical_memory_read(kvmclock_struct_pa, &time, sizeof(time));

    assert(time.tsc_timestamp <= migration_tsc);
    delta = migration_tsc - time.tsc_timestamp;
    if (time.tsc_shift < 0) {
        delta >>= -time.tsc_shift;
    } else {
        delta <<= time.tsc_shift;
    }

    mulu64(&nsec_lo, &nsec_hi, delta, time.tsc_to_system_mul);
    nsec = (nsec_lo >> 32) | (nsec_hi << 32);
    return nsec + time.system_time;
}

static void kvm_update_clock(KVMClockState *s)
{
    struct kvm_clock_data data;
    int ret;

    ret = kvm_vm_ioctl(kvm_state, KVM_GET_CLOCK, &data);
    if (ret < 0) {
        fprintf(stderr, "KVM_GET_CLOCK failed: %s\n", strerror(-ret));
                abort();
    }
    s->clock = data.clock;

    /* If kvm_has_adjust_clock_stable() is false, KVM_GET_CLOCK returns
     * essentially CLOCK_MONOTONIC plus a guest-specific adjustment.  This
     * can drift from the TSC-based value that is computed by the guest,
     * so we need to go through kvmclock_current_nsec().  If
     * kvm_has_adjust_clock_stable() is true, and the flags contain
     * KVM_CLOCK_TSC_STABLE, then KVM_GET_CLOCK returns a TSC-based value
     * and kvmclock_current_nsec() is not necessary.
     *
     * Here, however, we need not check KVM_CLOCK_TSC_STABLE.  This is because:
     *
     * - if the host has disabled the kvmclock master clock, the guest already
     *   has protection against time going backwards.  This "safety net" is only
     *   absent when kvmclock is stable;
     *
     * - therefore, we can replace a check like
     *
     *       if last KVM_GET_CLOCK was not reliable then
     *               read from memory
     *
     *   with
     *
     *       if last KVM_GET_CLOCK was not reliable && masterclock is enabled
     *               read from memory
     *
     * However:
     *
     * - if kvm_has_adjust_clock_stable() returns false, the left side is
     *   always true (KVM_GET_CLOCK is never reliable), and the right side is
     *   unknown (because we don't have data.flags).  We must assume it's true
     *   and read from memory.
     *
     * - if kvm_has_adjust_clock_stable() returns true, the result of the &&
     *   is always false (masterclock is enabled iff KVM_GET_CLOCK is reliable)
     *
     * So we can just use this instead:
     *
     *       if !kvm_has_adjust_clock_stable() then
     *               read from memory
     */
    s->clock_is_reliable = kvm_has_adjust_clock_stable();
}

static void do_kvmclock_ctrl(CPUState *cpu, run_on_cpu_data data)
{
    int ret = kvm_vcpu_ioctl(cpu, KVM_KVMCLOCK_CTRL, 0);

    if (ret && ret != -EINVAL) {
        fprintf(stderr, "%s: %s\n", __func__, strerror(-ret));
    }
}

static void kvmclock_vm_state_change(void *opaque, bool running,
                                     RunState state)
{
    KVMClockState *s = opaque;
    CPUState *cpu;
    int cap_clock_ctrl = kvm_check_extension(kvm_state, KVM_CAP_KVMCLOCK_CTRL);
    int ret;

    if (running) {
        struct kvm_clock_data data = {};

        /*
         * If the host where s->clock was read did not support reliable
         * KVM_GET_CLOCK, read kvmclock value from memory.
         */
        if (!s->clock_is_reliable) {
            uint64_t pvclock_via_mem = kvmclock_current_nsec(s);
            /* We can't rely on the saved clock value, just discard it */
            if (pvclock_via_mem) {
                s->clock = pvclock_via_mem;
            }
        }

        s->clock_valid = false;

        data.clock = s->clock;
        ret = kvm_vm_ioctl(kvm_state, KVM_SET_CLOCK, &data);
        if (ret < 0) {
            fprintf(stderr, "KVM_SET_CLOCK failed: %s\n", strerror(-ret));
            abort();
        }

        if (!cap_clock_ctrl) {
            return;
        }
        CPU_FOREACH(cpu) {
            run_on_cpu(cpu, do_kvmclock_ctrl, RUN_ON_CPU_NULL);
        }
    } else {

        if (s->clock_valid) {
            return;
        }

        s->runstate_paused = runstate_check(RUN_STATE_PAUSED);

        kvm_synchronize_all_tsc();

        kvm_update_clock(s);
        /*
         * If the VM is stopped, declare the clock state valid to
         * avoid re-reading it on next vmsave (which would return
         * a different value). Will be reset when the VM is continued.
         */
        s->clock_valid = true;
    }
}

static void kvmclock_realize(DeviceState *dev, Error **errp)
{
    KVMClockState *s = KVM_CLOCK(dev);

    if (!kvm_enabled()) {
        error_setg(errp, "kvmclock device requires KVM");
        return;
    }

    kvm_update_clock(s);

    qemu_add_vm_change_state_handler(kvmclock_vm_state_change, s);
}

static bool kvmclock_clock_is_reliable_needed(void *opaque)
{
    KVMClockState *s = opaque;

    return s->mach_use_reliable_get_clock;
}

static const VMStateDescription kvmclock_reliable_get_clock = {
    .name = "kvmclock/clock_is_reliable",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = kvmclock_clock_is_reliable_needed,
    .fields = (VMStateField[]) {
        VMSTATE_BOOL(clock_is_reliable, KVMClockState),
        VMSTATE_END_OF_LIST()
    }
};

/*
 * When migrating, assume the source has an unreliable
 * KVM_GET_CLOCK unless told otherwise.
 */
static int kvmclock_pre_load(void *opaque)
{
    KVMClockState *s = opaque;

    s->clock_is_reliable = false;

    return 0;
}

/*
 * When migrating a running guest, read the clock just
 * before migration, so that the guest clock counts
 * during the events between:
 *
 *  * vm_stop()
 *  *
 *  * pre_save()
 *
 *  This reduces kvmclock difference on migration from 5s
 *  to 0.1s (when max_downtime == 5s), because sending the
 *  final pages of memory (which happens between vm_stop()
 *  and pre_save()) takes max_downtime.
 */
static int kvmclock_pre_save(void *opaque)
{
    KVMClockState *s = opaque;

    if (!s->runstate_paused) {
        kvm_update_clock(s);
    }

    return 0;
}

static const VMStateDescription kvmclock_vmsd = {
    .name = "kvmclock",
    .version_id = 1,
    .minimum_version_id = 1,
    .pre_load = kvmclock_pre_load,
    .pre_save = kvmclock_pre_save,
    .fields = (VMStateField[]) {
        VMSTATE_UINT64(clock, KVMClockState),
        VMSTATE_END_OF_LIST()
    },
    .subsections = (const VMStateDescription * []) {
        &kvmclock_reliable_get_clock,
        NULL
    }
};

static Property kvmclock_properties[] = {
    DEFINE_PROP_BOOL("x-mach-use-reliable-get-clock", KVMClockState,
                      mach_use_reliable_get_clock, true),
    DEFINE_PROP_END_OF_LIST(),
};

static void kvmclock_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = kvmclock_realize;
    dc->vmsd = &kvmclock_vmsd;
    device_class_set_props(dc, kvmclock_properties);
}

static const TypeInfo kvmclock_info = {
    .name          = TYPE_KVM_CLOCK,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(KVMClockState),
    .class_init    = kvmclock_class_init,
};

/* Note: Must be called after VCPU initialization. */
void kvmclock_create(bool create_always)
{
    X86CPU *cpu = X86_CPU(first_cpu);

    assert(kvm_enabled());
    if (create_always ||
        cpu->env.features[FEAT_KVM] & ((1ULL << KVM_FEATURE_CLOCKSOURCE) |
                                       (1ULL << KVM_FEATURE_CLOCKSOURCE2))) {
        sysbus_create_simple(TYPE_KVM_CLOCK, -1, NULL);
    }
}

static void kvmclock_register_types(void)
{
    type_register_static(&kvmclock_info);
}

type_init(kvmclock_register_types)

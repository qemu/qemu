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

#include "qemu-common.h"
#include "qemu/host-utils.h"
#include "sysemu/sysemu.h"
#include "sysemu/kvm.h"
#include "sysemu/cpus.h"
#include "hw/sysbus.h"
#include "hw/kvm/clock.h"

#include <linux/kvm.h>
#include <linux/kvm_para.h>

#define TYPE_KVM_CLOCK "kvmclock"
#define KVM_CLOCK(obj) OBJECT_CHECK(KVMClockState, (obj), TYPE_KVM_CLOCK)

typedef struct KVMClockState {
    /*< private >*/
    SysBusDevice busdev;
    /*< public >*/

    uint64_t clock;
    bool clock_valid;
} KVMClockState;

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
    CPUX86State *env = cpu->env_ptr;
    hwaddr kvmclock_struct_pa = env->system_time_msr & ~1ULL;
    uint64_t migration_tsc = env->tsc;
    struct pvclock_vcpu_time_info time;
    uint64_t delta;
    uint64_t nsec_lo;
    uint64_t nsec_hi;
    uint64_t nsec;

    if (!(env->system_time_msr & 1ULL)) {
        /* KVM clock not active */
        return 0;
    }

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

static void kvmclock_vm_state_change(void *opaque, int running,
                                     RunState state)
{
    KVMClockState *s = opaque;
    CPUState *cpu;
    int cap_clock_ctrl = kvm_check_extension(kvm_state, KVM_CAP_KVMCLOCK_CTRL);
    int ret;

    if (running) {
        struct kvm_clock_data data;
        uint64_t time_at_migration = kvmclock_current_nsec(s);

        s->clock_valid = false;

	/* We can't rely on the migrated clock value, just discard it */
	if (time_at_migration) {
	        s->clock = time_at_migration;
	}

        data.clock = s->clock;
        data.flags = 0;
        ret = kvm_vm_ioctl(kvm_state, KVM_SET_CLOCK, &data);
        if (ret < 0) {
            fprintf(stderr, "KVM_SET_CLOCK failed: %s\n", strerror(ret));
            abort();
        }

        if (!cap_clock_ctrl) {
            return;
        }
        CPU_FOREACH(cpu) {
            ret = kvm_vcpu_ioctl(cpu, KVM_KVMCLOCK_CTRL, 0);
            if (ret) {
                if (ret != -EINVAL) {
                    fprintf(stderr, "%s: %s\n", __func__, strerror(-ret));
                }
                return;
            }
        }
    } else {
        struct kvm_clock_data data;
        int ret;

        if (s->clock_valid) {
            return;
        }

        cpu_synchronize_all_states();
        ret = kvm_vm_ioctl(kvm_state, KVM_GET_CLOCK, &data);
        if (ret < 0) {
            fprintf(stderr, "KVM_GET_CLOCK failed: %s\n", strerror(ret));
            abort();
        }
        s->clock = data.clock;

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

    qemu_add_vm_change_state_handler(kvmclock_vm_state_change, s);
}

static const VMStateDescription kvmclock_vmsd = {
    .name = "kvmclock",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT64(clock, KVMClockState),
        VMSTATE_END_OF_LIST()
    }
};

static void kvmclock_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = kvmclock_realize;
    dc->vmsd = &kvmclock_vmsd;
}

static const TypeInfo kvmclock_info = {
    .name          = TYPE_KVM_CLOCK,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(KVMClockState),
    .class_init    = kvmclock_class_init,
};

/* Note: Must be called after VCPU initialization. */
void kvmclock_create(void)
{
    X86CPU *cpu = X86_CPU(first_cpu);

    if (kvm_enabled() &&
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

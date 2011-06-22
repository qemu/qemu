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
 */

#include "qemu-common.h"
#include "sysemu.h"
#include "sysbus.h"
#include "kvm.h"
#include "kvmclock.h"

#include <linux/kvm.h>
#include <linux/kvm_para.h>

typedef struct KVMClockState {
    SysBusDevice busdev;
    uint64_t clock;
    bool clock_valid;
} KVMClockState;

static void kvmclock_pre_save(void *opaque)
{
    KVMClockState *s = opaque;
    struct kvm_clock_data data;
    int ret;

    if (s->clock_valid) {
        return;
    }
    ret = kvm_vm_ioctl(kvm_state, KVM_GET_CLOCK, &data);
    if (ret < 0) {
        fprintf(stderr, "KVM_GET_CLOCK failed: %s\n", strerror(ret));
        data.clock = 0;
    }
    s->clock = data.clock;
    /*
     * If the VM is stopped, declare the clock state valid to avoid re-reading
     * it on next vmsave (which would return a different value). Will be reset
     * when the VM is continued.
     */
    s->clock_valid = !vm_running;
}

static int kvmclock_post_load(void *opaque, int version_id)
{
    KVMClockState *s = opaque;
    struct kvm_clock_data data;

    data.clock = s->clock;
    data.flags = 0;
    return kvm_vm_ioctl(kvm_state, KVM_SET_CLOCK, &data);
}

static void kvmclock_vm_state_change(void *opaque, int running, int reason)
{
    KVMClockState *s = opaque;

    if (running) {
        s->clock_valid = false;
    }
}

static int kvmclock_init(SysBusDevice *dev)
{
    KVMClockState *s = FROM_SYSBUS(KVMClockState, dev);

    qemu_add_vm_change_state_handler(kvmclock_vm_state_change, s);
    return 0;
}

static const VMStateDescription kvmclock_vmsd = {
    .name = "kvmclock",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .pre_save = kvmclock_pre_save,
    .post_load = kvmclock_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_UINT64(clock, KVMClockState),
        VMSTATE_END_OF_LIST()
    }
};

static SysBusDeviceInfo kvmclock_info = {
    .qdev.name = "kvmclock",
    .qdev.size = sizeof(KVMClockState),
    .qdev.vmsd = &kvmclock_vmsd,
    .qdev.no_user = 1,
    .init = kvmclock_init,
};

/* Note: Must be called after VCPU initialization. */
void kvmclock_create(void)
{
    if (kvm_enabled() &&
        first_cpu->cpuid_kvm_features & ((1ULL << KVM_FEATURE_CLOCKSOURCE)
#ifdef KVM_FEATURE_CLOCKSOURCE2
        || (1ULL << KVM_FEATURE_CLOCKSOURCE2)
#endif
    )) {
        sysbus_create_simple("kvmclock", -1, NULL);
    }
}

static void kvmclock_register_device(void)
{
    if (kvm_enabled()) {
        sysbus_register_withprop(&kvmclock_info);
    }
}

device_init(kvmclock_register_device);

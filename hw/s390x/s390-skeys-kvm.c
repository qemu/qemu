/*
 * s390 storage key device
 *
 * Copyright 2015 IBM Corp.
 * Author(s): Jason J. Herne <jjherne@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#include "qemu/osdep.h"
#include "hw/s390x/storage-keys.h"
#include "sysemu/kvm.h"
#include "qemu/error-report.h"
#include "qemu/module.h"

static bool kvm_s390_skeys_are_enabled(S390SKeysState *ss)
{
    S390SKeysClass *skeyclass = S390_SKEYS_GET_CLASS(ss);
    uint8_t single_key;
    int r;

    r = skeyclass->get_skeys(ss, 0, 1, &single_key);
    if (r != 0 && r != KVM_S390_GET_SKEYS_NONE) {
        error_report("S390_GET_KEYS error %d", r);
    }
    return (r == 0);
}

static int kvm_s390_skeys_get(S390SKeysState *ss, uint64_t start_gfn,
                              uint64_t count, uint8_t *keys)
{
    struct kvm_s390_skeys args = {
        .start_gfn = start_gfn,
        .count = count,
        .skeydata_addr = (__u64)keys
    };

    return kvm_vm_ioctl(kvm_state, KVM_S390_GET_SKEYS, &args);
}

static int kvm_s390_skeys_set(S390SKeysState *ss, uint64_t start_gfn,
                              uint64_t count, uint8_t *keys)
{
    struct kvm_s390_skeys args = {
        .start_gfn = start_gfn,
        .count = count,
        .skeydata_addr = (__u64)keys
    };

    return kvm_vm_ioctl(kvm_state, KVM_S390_SET_SKEYS, &args);
}

static void kvm_s390_skeys_class_init(ObjectClass *oc, void *data)
{
    S390SKeysClass *skeyclass = S390_SKEYS_CLASS(oc);
    DeviceClass *dc = DEVICE_CLASS(oc);

    skeyclass->skeys_are_enabled = kvm_s390_skeys_are_enabled;
    skeyclass->get_skeys = kvm_s390_skeys_get;
    skeyclass->set_skeys = kvm_s390_skeys_set;

    /* Reason: Internal device (only one skeys device for the whole memory) */
    dc->user_creatable = false;
}

static const TypeInfo kvm_s390_skeys_info = {
    .name          = TYPE_KVM_S390_SKEYS,
    .parent        = TYPE_S390_SKEYS,
    .instance_size = sizeof(S390SKeysState),
    .class_init    = kvm_s390_skeys_class_init,
    .class_size    = sizeof(S390SKeysClass),
};

static void kvm_s390_skeys_register_types(void)
{
    type_register_static(&kvm_s390_skeys_info);
}

type_init(kvm_s390_skeys_register_types)

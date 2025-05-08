/*
 * x86-specific confidential guest methods.
 *
 * Copyright (c) 2024 Red Hat Inc.
 *
 * Authors:
 *  Paolo Bonzini <pbonzini@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#ifndef TARGET_I386_CG_H
#define TARGET_I386_CG_H

#include "qom/object.h"

#include "system/confidential-guest-support.h"

#define TYPE_X86_CONFIDENTIAL_GUEST "x86-confidential-guest"

OBJECT_DECLARE_TYPE(X86ConfidentialGuest,
                    X86ConfidentialGuestClass,
                    X86_CONFIDENTIAL_GUEST)

struct X86ConfidentialGuest {
    /* <private> */
    ConfidentialGuestSupport parent_obj;
};

/**
 * X86ConfidentialGuestClass:
 *
 * Class to be implemented by confidential-guest-support concrete objects
 * for the x86 target.
 */
struct X86ConfidentialGuestClass {
    /* <private> */
    ConfidentialGuestSupportClass parent;

    /* <public> */
    int (*kvm_type)(X86ConfidentialGuest *cg);
    void (*cpu_instance_init)(X86ConfidentialGuest *cg, CPUState *cpu);
    uint32_t (*adjust_cpuid_features)(X86ConfidentialGuest *cg, uint32_t feature,
                                      uint32_t index, int reg, uint32_t value);
    int (*check_features)(X86ConfidentialGuest *cg, CPUState *cs);
};

/**
 * x86_confidential_guest_kvm_type:
 *
 * Calls #X86ConfidentialGuestClass.kvm_type() callback.
 */
static inline int x86_confidential_guest_kvm_type(X86ConfidentialGuest *cg)
{
    X86ConfidentialGuestClass *klass = X86_CONFIDENTIAL_GUEST_GET_CLASS(cg);

    if (klass->kvm_type) {
        return klass->kvm_type(cg);
    } else {
        return 0;
    }
}

static inline void x86_confidential_guest_cpu_instance_init(X86ConfidentialGuest *cg,
                                                            CPUState *cpu)
{
    X86ConfidentialGuestClass *klass = X86_CONFIDENTIAL_GUEST_GET_CLASS(cg);

    if (klass->cpu_instance_init) {
        klass->cpu_instance_init(cg, cpu);
    }
}

/**
 * x86_confidential_guest_adjust_cpuid_features:
 *
 * Adjust the supported features from a confidential guest's CPUID values,
 * returns the adjusted value.  There are bits being removed that are not
 * supported by the confidential computing firmware or bits being added that
 * are forcibly exposed to guest by the confidential computing firmware.
 */
static inline int x86_confidential_guest_adjust_cpuid_features(X86ConfidentialGuest *cg,
                                                             uint32_t feature, uint32_t index,
                                                             int reg, uint32_t value)
{
    X86ConfidentialGuestClass *klass = X86_CONFIDENTIAL_GUEST_GET_CLASS(cg);

    if (klass->adjust_cpuid_features) {
        return klass->adjust_cpuid_features(cg, feature, index, reg, value);
    } else {
        return value;
    }
}

static inline int x86_confidential_guest_check_features(X86ConfidentialGuest *cg,
                                                        CPUState *cs)
{
    X86ConfidentialGuestClass *klass = X86_CONFIDENTIAL_GUEST_GET_CLASS(cg);

    if (klass->check_features) {
        return klass->check_features(cg, cs);
    }

    return 0;
}

#endif

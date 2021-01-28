/*
 * Stubs for CONFIG_HYPERV=n
 *
 * Copyright (c) 2015-2018 Virtuozzo International GmbH.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "hyperv.h"

#ifdef CONFIG_KVM
int kvm_hv_handle_exit(X86CPU *cpu, struct kvm_hyperv_exit *exit)
{
    switch (exit->type) {
    case KVM_EXIT_HYPERV_SYNIC:
        if (!hyperv_feat_enabled(cpu, HYPERV_FEAT_SYNIC)) {
            return -1;
        }

        /*
         * Tracking the changes in the MSRs is unnecessary as there are no
         * users for them beside save/load, which is handled nicely by the
         * generic MSR save/load code
         */
        return 0;
    case KVM_EXIT_HYPERV_HCALL:
        exit->u.hcall.result = HV_STATUS_INVALID_HYPERCALL_CODE;
        return 0;
    default:
        return -1;
    }
}
#endif

int hyperv_x86_synic_add(X86CPU *cpu)
{
    return -ENOSYS;
}

void hyperv_x86_synic_reset(X86CPU *cpu)
{
}

void hyperv_x86_synic_update(X86CPU *cpu)
{
}

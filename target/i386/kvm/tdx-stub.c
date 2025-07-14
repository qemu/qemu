/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "qemu/osdep.h"

#include "tdx.h"

int tdx_pre_create_vcpu(CPUState *cpu, Error **errp)
{
    return -EINVAL;
}

int tdx_parse_tdvf(void *flash_ptr, int size)
{
    return -EINVAL;
}

int tdx_handle_report_fatal_error(X86CPU *cpu, struct kvm_run *run)
{
    return -EINVAL;
}

void tdx_handle_get_quote(X86CPU *cpu, struct kvm_run *run)
{
}

void tdx_handle_get_tdvmcall_info(X86CPU *cpu, struct kvm_run *run)
{
}

void tdx_handle_setup_event_notify_interrupt(X86CPU *cpu, struct kvm_run *run)
{
}

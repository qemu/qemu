/*
 * QEMU KVM Hyper-V support
 *
 * Copyright (C) 2015 Andrey Smetanin <asmetanin@virtuozzo.com>
 *
 * Authors:
 *  Andrey Smetanin <asmetanin@virtuozzo.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "hyperv.h"
#include "hw/hyperv/hyperv.h"
#include "hyperv-proto.h"

int hyperv_x86_synic_add(X86CPU *cpu)
{
    hyperv_synic_add(CPU(cpu));
    return 0;
}

void hyperv_x86_synic_reset(X86CPU *cpu)
{
    hyperv_synic_reset(CPU(cpu));
}

void hyperv_x86_synic_update(X86CPU *cpu)
{
    CPUX86State *env = &cpu->env;
    bool enable = env->msr_hv_synic_control & HV_SYNIC_ENABLE;
    hwaddr msg_page_addr = (env->msr_hv_synic_msg_page & HV_SIMP_ENABLE) ?
        (env->msr_hv_synic_msg_page & TARGET_PAGE_MASK) : 0;
    hwaddr event_page_addr = (env->msr_hv_synic_evt_page & HV_SIEFP_ENABLE) ?
        (env->msr_hv_synic_evt_page & TARGET_PAGE_MASK) : 0;
    hyperv_synic_update(CPU(cpu), enable, msg_page_addr, event_page_addr);
}

int kvm_hv_handle_exit(X86CPU *cpu, struct kvm_hyperv_exit *exit)
{
    CPUX86State *env = &cpu->env;

    switch (exit->type) {
    case KVM_EXIT_HYPERV_SYNIC:
        if (!cpu->hyperv_synic) {
            return -1;
        }

        /*
         * For now just track changes in SynIC control and msg/evt pages msr's.
         * When SynIC messaging/events processing will be added in future
         * here we will do messages queues flushing and pages remapping.
         */
        switch (exit->u.synic.msr) {
        case HV_X64_MSR_SCONTROL:
            env->msr_hv_synic_control = exit->u.synic.control;
            break;
        case HV_X64_MSR_SIMP:
            env->msr_hv_synic_msg_page = exit->u.synic.msg_page;
            break;
        case HV_X64_MSR_SIEFP:
            env->msr_hv_synic_evt_page = exit->u.synic.evt_page;
            break;
        default:
            return -1;
        }

        hyperv_x86_synic_update(cpu);

        return 0;
    case KVM_EXIT_HYPERV_HCALL: {
        uint16_t code;

        code  = exit->u.hcall.input & 0xffff;
        switch (code) {
        case HV_POST_MESSAGE:
        case HV_SIGNAL_EVENT:
        default:
            exit->u.hcall.result = HV_STATUS_INVALID_HYPERCALL_CODE;
            return 0;
        }
    }
    default:
        return -1;
    }
}

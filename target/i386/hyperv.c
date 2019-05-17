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
#include "qemu/main-loop.h"
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

static void async_synic_update(CPUState *cs, run_on_cpu_data data)
{
    qemu_mutex_lock_iothread();
    hyperv_x86_synic_update(X86_CPU(cs));
    qemu_mutex_unlock_iothread();
}

int kvm_hv_handle_exit(X86CPU *cpu, struct kvm_hyperv_exit *exit)
{
    CPUX86State *env = &cpu->env;

    switch (exit->type) {
    case KVM_EXIT_HYPERV_SYNIC:
        if (!hyperv_feat_enabled(cpu, HYPERV_FEAT_SYNIC)) {
            return -1;
        }

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

        /*
         * this will run in this cpu thread before it returns to KVM, but in a
         * safe environment (i.e. when all cpus are quiescent) -- this is
         * necessary because memory hierarchy is being changed
         */
        async_safe_run_on_cpu(CPU(cpu), async_synic_update, RUN_ON_CPU_NULL);

        return 0;
    case KVM_EXIT_HYPERV_HCALL: {
        uint16_t code = exit->u.hcall.input & 0xffff;
        bool fast = exit->u.hcall.input & HV_HYPERCALL_FAST;
        uint64_t param = exit->u.hcall.params[0];

        switch (code) {
        case HV_POST_MESSAGE:
            exit->u.hcall.result = hyperv_hcall_post_message(param, fast);
            break;
        case HV_SIGNAL_EVENT:
            exit->u.hcall.result = hyperv_hcall_signal_event(param, fast);
            break;
        default:
            exit->u.hcall.result = HV_STATUS_INVALID_HYPERCALL_CODE;
        }
        return 0;
    }
    default:
        return -1;
    }
}

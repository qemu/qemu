/*
 * QEMU MSHV support
 *
 * Copyright Microsoft, Corp. 2025
 *
 * Authors: Ziqiao Zhou   <ziqiaozhou@microsoft.com>
 *          Magnus Kulke  <magnuskulke@microsoft.com>
 *          Jinank Jain   <jinankjain@microsoft.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/typedefs.h"

#include "system/mshv.h"
#include "system/mshv_int.h"
#include "system/address-spaces.h"
#include "linux/mshv.h"
#include "hw/hyperv/hvgdk.h"
#include "hw/hyperv/hvgdk_mini.h"
#include "hw/hyperv/hvhdk_mini.h"

#include "cpu.h"
#include "emulate/x86_decode.h"
#include "emulate/x86_emu.h"
#include "emulate/x86_flags.h"

#include "trace-accel_mshv.h"
#include "trace.h"

int mshv_store_regs(CPUState *cpu)
{
    error_report("unimplemented");
    abort();
}

int mshv_load_regs(CPUState *cpu)
{
    error_report("unimplemented");
    abort();
}

int mshv_arch_put_registers(const CPUState *cpu)
{
    error_report("unimplemented");
    abort();
}

void mshv_arch_amend_proc_features(
    union hv_partition_synthetic_processor_features *features)
{
    features->access_guest_idle_reg = 1;
}

int mshv_run_vcpu(int vm_fd, CPUState *cpu, hv_message *msg, MshvVmExit *exit)
{
    error_report("unimplemented");
    abort();
}

void mshv_remove_vcpu(int vm_fd, int cpu_fd)
{
    error_report("unimplemented");
    abort();
}

int mshv_create_vcpu(int vm_fd, uint8_t vp_index, int *cpu_fd)
{
    error_report("unimplemented");
    abort();
}

void mshv_init_mmio_emu(void)
{
    error_report("unimplemented");
    abort();
}

void mshv_arch_init_vcpu(CPUState *cpu)
{
    X86CPU *x86_cpu = X86_CPU(cpu);
    CPUX86State *env = &x86_cpu->env;

    env->emu_mmio_buf = g_new(char, 4096);
}

void mshv_arch_destroy_vcpu(CPUState *cpu)
{
    X86CPU *x86_cpu = X86_CPU(cpu);
    CPUX86State *env = &x86_cpu->env;

    g_clear_pointer(&env->emu_mmio_buf, g_free);
}

/*
 * Default Microsoft Hypervisor behavior for unimplemented MSR is to send a
 * fault to the guest if it tries to access it. It is possible to override
 * this behavior with a more suitable option i.e., ignore writes from the guest
 * and return zero in attempt to read unimplemented.
 */
static int set_unimplemented_msr_action(int vm_fd)
{
    struct hv_input_set_partition_property in = {0};
    struct mshv_root_hvcall args = {0};

    in.property_code  = HV_PARTITION_PROPERTY_UNIMPLEMENTED_MSR_ACTION;
    in.property_value = HV_UNIMPLEMENTED_MSR_ACTION_IGNORE_WRITE_READ_ZERO;

    args.code   = HVCALL_SET_PARTITION_PROPERTY;
    args.in_sz  = sizeof(in);
    args.in_ptr = (uint64_t)&in;

    trace_mshv_hvcall_args("unimplemented_msr_action", args.code, args.in_sz);

    int ret = mshv_hvcall(vm_fd, &args);
    if (ret < 0) {
        error_report("Failed to set unimplemented MSR action");
        return -1;
    }
    return 0;
}

int mshv_arch_post_init_vm(int vm_fd)
{
    int ret;

    ret = set_unimplemented_msr_action(vm_fd);
    if (ret < 0) {
        error_report("Failed to set unimplemented MSR action");
    }

    return ret;
}

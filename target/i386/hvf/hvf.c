/* Copyright 2008 IBM Corporation
 *           2008 Red Hat, Inc.
 * Copyright 2011 Intel Corporation
 * Copyright 2016 Veertu, Inc.
 * Copyright 2017 The Android Open Source Project
 *
 * QEMU Hypervisor.framework support
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * This file contain code under public domain from the hvdos project:
 * https://github.com/mist64/hvdos
 *
 * Parts Copyright (c) 2011 NetApp, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY NETAPP, INC ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL NETAPP, INC OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qemu/error-report.h"

#include "sysemu/hvf.h"
#include "sysemu/hvf_int.h"
#include "sysemu/runstate.h"
#include "sysemu/cpus.h"
#include "hvf-i386.h"
#include "vmcs.h"
#include "vmx.h"
#include "x86.h"
#include "x86_descr.h"
#include "x86_mmu.h"
#include "x86_decode.h"
#include "x86_emu.h"
#include "x86_task.h"
#include "x86hvf.h"

#include <Hypervisor/hv.h>
#include <Hypervisor/hv_vmx.h>
#include <sys/sysctl.h>

#include "hw/i386/apic_internal.h"
#include "qemu/main-loop.h"
#include "qemu/accel.h"
#include "target/i386/cpu.h"

void vmx_update_tpr(CPUState *cpu)
{
    /* TODO: need integrate APIC handling */
    X86CPU *x86_cpu = X86_CPU(cpu);
    int tpr = cpu_get_apic_tpr(x86_cpu->apic_state) << 4;
    int irr = apic_get_highest_priority_irr(x86_cpu->apic_state);

    wreg(cpu->hvf->fd, HV_X86_TPR, tpr);
    if (irr == -1) {
        wvmcs(cpu->hvf->fd, VMCS_TPR_THRESHOLD, 0);
    } else {
        wvmcs(cpu->hvf->fd, VMCS_TPR_THRESHOLD, (irr > tpr) ? tpr >> 4 :
              irr >> 4);
    }
}

static void update_apic_tpr(CPUState *cpu)
{
    X86CPU *x86_cpu = X86_CPU(cpu);
    int tpr = rreg(cpu->hvf->fd, HV_X86_TPR) >> 4;
    cpu_set_apic_tpr(x86_cpu->apic_state, tpr);
}

#define VECTORING_INFO_VECTOR_MASK     0xff

void hvf_handle_io(CPUArchState *env, uint16_t port, void *buffer,
                  int direction, int size, int count)
{
    int i;
    uint8_t *ptr = buffer;

    for (i = 0; i < count; i++) {
        address_space_rw(&address_space_io, port, MEMTXATTRS_UNSPECIFIED,
                         ptr, size,
                         direction);
        ptr += size;
    }
}

static bool ept_emulation_fault(hvf_slot *slot, uint64_t gpa, uint64_t ept_qual)
{
    int read, write;

    /* EPT fault on an instruction fetch doesn't make sense here */
    if (ept_qual & EPT_VIOLATION_INST_FETCH) {
        return false;
    }

    /* EPT fault must be a read fault or a write fault */
    read = ept_qual & EPT_VIOLATION_DATA_READ ? 1 : 0;
    write = ept_qual & EPT_VIOLATION_DATA_WRITE ? 1 : 0;
    if ((read | write) == 0) {
        return false;
    }

    if (write && slot) {
        if (slot->flags & HVF_SLOT_LOG) {
            memory_region_set_dirty(slot->region, gpa - slot->start, 1);
            hv_vm_protect((hv_gpaddr_t)slot->start, (size_t)slot->size,
                          HV_MEMORY_READ | HV_MEMORY_WRITE);
        }
    }

    /*
     * The EPT violation must have been caused by accessing a
     * guest-physical address that is a translation of a guest-linear
     * address.
     */
    if ((ept_qual & EPT_VIOLATION_GLA_VALID) == 0 ||
        (ept_qual & EPT_VIOLATION_XLAT_VALID) == 0) {
        return false;
    }

    if (!slot) {
        return true;
    }
    if (!memory_region_is_ram(slot->region) &&
        !(read && memory_region_is_romd(slot->region))) {
        return true;
    }
    return false;
}

void hvf_arch_vcpu_destroy(CPUState *cpu)
{
    X86CPU *x86_cpu = X86_CPU(cpu);
    CPUX86State *env = &x86_cpu->env;

    g_free(env->hvf_mmio_buf);
}

static void init_tsc_freq(CPUX86State *env)
{
    size_t length;
    uint64_t tsc_freq;

    if (env->tsc_khz != 0) {
        return;
    }

    length = sizeof(uint64_t);
    if (sysctlbyname("machdep.tsc.frequency", &tsc_freq, &length, NULL, 0)) {
        return;
    }
    env->tsc_khz = tsc_freq / 1000;  /* Hz to KHz */
}

static void init_apic_bus_freq(CPUX86State *env)
{
    size_t length;
    uint64_t bus_freq;

    if (env->apic_bus_freq != 0) {
        return;
    }

    length = sizeof(uint64_t);
    if (sysctlbyname("hw.busfrequency", &bus_freq, &length, NULL, 0)) {
        return;
    }
    env->apic_bus_freq = bus_freq;
}

static inline bool tsc_is_known(CPUX86State *env)
{
    return env->tsc_khz != 0;
}

static inline bool apic_bus_freq_is_known(CPUX86State *env)
{
    return env->apic_bus_freq != 0;
}

void hvf_kick_vcpu_thread(CPUState *cpu)
{
    cpus_kick_thread(cpu);
}

int hvf_arch_init(void)
{
    return 0;
}

int hvf_arch_init_vcpu(CPUState *cpu)
{
    X86CPU *x86cpu = X86_CPU(cpu);
    CPUX86State *env = &x86cpu->env;

    init_emu();
    init_decoder();

    hvf_state->hvf_caps = g_new0(struct hvf_vcpu_caps, 1);
    env->hvf_mmio_buf = g_new(char, 4096);

    if (x86cpu->vmware_cpuid_freq) {
        init_tsc_freq(env);
        init_apic_bus_freq(env);

        if (!tsc_is_known(env) || !apic_bus_freq_is_known(env)) {
            error_report("vmware-cpuid-freq: feature couldn't be enabled");
        }
    }

    if (hv_vmx_read_capability(HV_VMX_CAP_PINBASED,
        &hvf_state->hvf_caps->vmx_cap_pinbased)) {
        abort();
    }
    if (hv_vmx_read_capability(HV_VMX_CAP_PROCBASED,
        &hvf_state->hvf_caps->vmx_cap_procbased)) {
        abort();
    }
    if (hv_vmx_read_capability(HV_VMX_CAP_PROCBASED2,
        &hvf_state->hvf_caps->vmx_cap_procbased2)) {
        abort();
    }
    if (hv_vmx_read_capability(HV_VMX_CAP_ENTRY,
        &hvf_state->hvf_caps->vmx_cap_entry)) {
        abort();
    }

    /* set VMCS control fields */
    wvmcs(cpu->hvf->fd, VMCS_PIN_BASED_CTLS,
          cap2ctrl(hvf_state->hvf_caps->vmx_cap_pinbased,
          VMCS_PIN_BASED_CTLS_EXTINT |
          VMCS_PIN_BASED_CTLS_NMI |
          VMCS_PIN_BASED_CTLS_VNMI));
    wvmcs(cpu->hvf->fd, VMCS_PRI_PROC_BASED_CTLS,
          cap2ctrl(hvf_state->hvf_caps->vmx_cap_procbased,
          VMCS_PRI_PROC_BASED_CTLS_HLT |
          VMCS_PRI_PROC_BASED_CTLS_MWAIT |
          VMCS_PRI_PROC_BASED_CTLS_TSC_OFFSET |
          VMCS_PRI_PROC_BASED_CTLS_TPR_SHADOW) |
          VMCS_PRI_PROC_BASED_CTLS_SEC_CONTROL);
    wvmcs(cpu->hvf->fd, VMCS_SEC_PROC_BASED_CTLS,
          cap2ctrl(hvf_state->hvf_caps->vmx_cap_procbased2,
                   VMCS_PRI_PROC_BASED2_CTLS_APIC_ACCESSES));

    wvmcs(cpu->hvf->fd, VMCS_ENTRY_CTLS, cap2ctrl(hvf_state->hvf_caps->vmx_cap_entry,
          0));
    wvmcs(cpu->hvf->fd, VMCS_EXCEPTION_BITMAP, 0); /* Double fault */

    wvmcs(cpu->hvf->fd, VMCS_TPR_THRESHOLD, 0);

    x86cpu = X86_CPU(cpu);
    x86cpu->env.xsave_buf_len = 4096;
    x86cpu->env.xsave_buf = qemu_memalign(4096, x86cpu->env.xsave_buf_len);

    /*
     * The allocated storage must be large enough for all of the
     * possible XSAVE state components.
     */
    assert(hvf_get_supported_cpuid(0xd, 0, R_ECX) <= x86cpu->env.xsave_buf_len);

    hv_vcpu_enable_native_msr(cpu->hvf->fd, MSR_STAR, 1);
    hv_vcpu_enable_native_msr(cpu->hvf->fd, MSR_LSTAR, 1);
    hv_vcpu_enable_native_msr(cpu->hvf->fd, MSR_CSTAR, 1);
    hv_vcpu_enable_native_msr(cpu->hvf->fd, MSR_FMASK, 1);
    hv_vcpu_enable_native_msr(cpu->hvf->fd, MSR_FSBASE, 1);
    hv_vcpu_enable_native_msr(cpu->hvf->fd, MSR_GSBASE, 1);
    hv_vcpu_enable_native_msr(cpu->hvf->fd, MSR_KERNELGSBASE, 1);
    hv_vcpu_enable_native_msr(cpu->hvf->fd, MSR_TSC_AUX, 1);
    hv_vcpu_enable_native_msr(cpu->hvf->fd, MSR_IA32_TSC, 1);
    hv_vcpu_enable_native_msr(cpu->hvf->fd, MSR_IA32_SYSENTER_CS, 1);
    hv_vcpu_enable_native_msr(cpu->hvf->fd, MSR_IA32_SYSENTER_EIP, 1);
    hv_vcpu_enable_native_msr(cpu->hvf->fd, MSR_IA32_SYSENTER_ESP, 1);

    return 0;
}

static void hvf_store_events(CPUState *cpu, uint32_t ins_len, uint64_t idtvec_info)
{
    X86CPU *x86_cpu = X86_CPU(cpu);
    CPUX86State *env = &x86_cpu->env;

    env->exception_nr = -1;
    env->exception_pending = 0;
    env->exception_injected = 0;
    env->interrupt_injected = -1;
    env->nmi_injected = false;
    env->ins_len = 0;
    env->has_error_code = false;
    if (idtvec_info & VMCS_IDT_VEC_VALID) {
        switch (idtvec_info & VMCS_IDT_VEC_TYPE) {
        case VMCS_IDT_VEC_HWINTR:
        case VMCS_IDT_VEC_SWINTR:
            env->interrupt_injected = idtvec_info & VMCS_IDT_VEC_VECNUM;
            break;
        case VMCS_IDT_VEC_NMI:
            env->nmi_injected = true;
            break;
        case VMCS_IDT_VEC_HWEXCEPTION:
        case VMCS_IDT_VEC_SWEXCEPTION:
            env->exception_nr = idtvec_info & VMCS_IDT_VEC_VECNUM;
            env->exception_injected = 1;
            break;
        case VMCS_IDT_VEC_PRIV_SWEXCEPTION:
        default:
            abort();
        }
        if ((idtvec_info & VMCS_IDT_VEC_TYPE) == VMCS_IDT_VEC_SWEXCEPTION ||
            (idtvec_info & VMCS_IDT_VEC_TYPE) == VMCS_IDT_VEC_SWINTR) {
            env->ins_len = ins_len;
        }
        if (idtvec_info & VMCS_IDT_VEC_ERRCODE_VALID) {
            env->has_error_code = true;
            env->error_code = rvmcs(cpu->hvf->fd, VMCS_IDT_VECTORING_ERROR);
        }
    }
    if ((rvmcs(cpu->hvf->fd, VMCS_GUEST_INTERRUPTIBILITY) &
        VMCS_INTERRUPTIBILITY_NMI_BLOCKING)) {
        env->hflags2 |= HF2_NMI_MASK;
    } else {
        env->hflags2 &= ~HF2_NMI_MASK;
    }
    if (rvmcs(cpu->hvf->fd, VMCS_GUEST_INTERRUPTIBILITY) &
         (VMCS_INTERRUPTIBILITY_STI_BLOCKING |
         VMCS_INTERRUPTIBILITY_MOVSS_BLOCKING)) {
        env->hflags |= HF_INHIBIT_IRQ_MASK;
    } else {
        env->hflags &= ~HF_INHIBIT_IRQ_MASK;
    }
}

static void hvf_cpu_x86_cpuid(CPUX86State *env, uint32_t index, uint32_t count,
                              uint32_t *eax, uint32_t *ebx,
                              uint32_t *ecx, uint32_t *edx)
{
    /*
     * A wrapper extends cpu_x86_cpuid with 0x40000000 and 0x40000010 leafs,
     * leafs 0x40000001-0x4000000F are filled with zeros
     * Provides vmware-cpuid-freq support to hvf
     *
     * Note: leaf 0x40000000 not exposes HVF,
     * leaving hypervisor signature empty
     */

    if (index < 0x40000000 || index > 0x40000010 ||
        !tsc_is_known(env) || !apic_bus_freq_is_known(env)) {

        cpu_x86_cpuid(env, index, count, eax, ebx, ecx, edx);
        return;
    }

    switch (index) {
    case 0x40000000:
        *eax = 0x40000010;    /* Max available cpuid leaf */
        *ebx = 0;             /* Leave signature empty */
        *ecx = 0;
        *edx = 0;
        break;
    case 0x40000010:
        *eax = env->tsc_khz;
        *ebx = env->apic_bus_freq / 1000; /* Hz to KHz */
        *ecx = 0;
        *edx = 0;
        break;
    default:
        *eax = 0;
        *ebx = 0;
        *ecx = 0;
        *edx = 0;
        break;
    }
}

int hvf_vcpu_exec(CPUState *cpu)
{
    X86CPU *x86_cpu = X86_CPU(cpu);
    CPUX86State *env = &x86_cpu->env;
    int ret = 0;
    uint64_t rip = 0;

    if (hvf_process_events(cpu)) {
        return EXCP_HLT;
    }

    do {
        if (cpu->vcpu_dirty) {
            hvf_put_registers(cpu);
            cpu->vcpu_dirty = false;
        }

        if (hvf_inject_interrupts(cpu)) {
            return EXCP_INTERRUPT;
        }
        vmx_update_tpr(cpu);

        qemu_mutex_unlock_iothread();
        if (!cpu_is_bsp(X86_CPU(cpu)) && cpu->halted) {
            qemu_mutex_lock_iothread();
            return EXCP_HLT;
        }

        hv_return_t r  = hv_vcpu_run(cpu->hvf->fd);
        assert_hvf_ok(r);

        /* handle VMEXIT */
        uint64_t exit_reason = rvmcs(cpu->hvf->fd, VMCS_EXIT_REASON);
        uint64_t exit_qual = rvmcs(cpu->hvf->fd, VMCS_EXIT_QUALIFICATION);
        uint32_t ins_len = (uint32_t)rvmcs(cpu->hvf->fd,
                                           VMCS_EXIT_INSTRUCTION_LENGTH);

        uint64_t idtvec_info = rvmcs(cpu->hvf->fd, VMCS_IDT_VECTORING_INFO);

        hvf_store_events(cpu, ins_len, idtvec_info);
        rip = rreg(cpu->hvf->fd, HV_X86_RIP);
        env->eflags = rreg(cpu->hvf->fd, HV_X86_RFLAGS);

        qemu_mutex_lock_iothread();

        update_apic_tpr(cpu);
        current_cpu = cpu;

        ret = 0;
        switch (exit_reason) {
        case EXIT_REASON_HLT: {
            macvm_set_rip(cpu, rip + ins_len);
            if (!((cpu->interrupt_request & CPU_INTERRUPT_HARD) &&
                (env->eflags & IF_MASK))
                && !(cpu->interrupt_request & CPU_INTERRUPT_NMI) &&
                !(idtvec_info & VMCS_IDT_VEC_VALID)) {
                cpu->halted = 1;
                ret = EXCP_HLT;
                break;
            }
            ret = EXCP_INTERRUPT;
            break;
        }
        case EXIT_REASON_MWAIT: {
            ret = EXCP_INTERRUPT;
            break;
        }
        /* Need to check if MMIO or unmapped fault */
        case EXIT_REASON_EPT_FAULT:
        {
            hvf_slot *slot;
            uint64_t gpa = rvmcs(cpu->hvf->fd, VMCS_GUEST_PHYSICAL_ADDRESS);

            if (((idtvec_info & VMCS_IDT_VEC_VALID) == 0) &&
                ((exit_qual & EXIT_QUAL_NMIUDTI) != 0)) {
                vmx_set_nmi_blocking(cpu);
            }

            slot = hvf_find_overlap_slot(gpa, 1);
            /* mmio */
            if (ept_emulation_fault(slot, gpa, exit_qual)) {
                struct x86_decode decode;

                load_regs(cpu);
                decode_instruction(env, &decode);
                exec_instruction(env, &decode);
                store_regs(cpu);
                break;
            }
            break;
        }
        case EXIT_REASON_INOUT:
        {
            uint32_t in = (exit_qual & 8) != 0;
            uint32_t size =  (exit_qual & 7) + 1;
            uint32_t string =  (exit_qual & 16) != 0;
            uint32_t port =  exit_qual >> 16;
            /*uint32_t rep = (exit_qual & 0x20) != 0;*/

            if (!string && in) {
                uint64_t val = 0;
                load_regs(cpu);
                hvf_handle_io(env, port, &val, 0, size, 1);
                if (size == 1) {
                    AL(env) = val;
                } else if (size == 2) {
                    AX(env) = val;
                } else if (size == 4) {
                    RAX(env) = (uint32_t)val;
                } else {
                    RAX(env) = (uint64_t)val;
                }
                env->eip += ins_len;
                store_regs(cpu);
                break;
            } else if (!string && !in) {
                RAX(env) = rreg(cpu->hvf->fd, HV_X86_RAX);
                hvf_handle_io(env, port, &RAX(env), 1, size, 1);
                macvm_set_rip(cpu, rip + ins_len);
                break;
            }
            struct x86_decode decode;

            load_regs(cpu);
            decode_instruction(env, &decode);
            assert(ins_len == decode.len);
            exec_instruction(env, &decode);
            store_regs(cpu);

            break;
        }
        case EXIT_REASON_CPUID: {
            uint32_t rax = (uint32_t)rreg(cpu->hvf->fd, HV_X86_RAX);
            uint32_t rbx = (uint32_t)rreg(cpu->hvf->fd, HV_X86_RBX);
            uint32_t rcx = (uint32_t)rreg(cpu->hvf->fd, HV_X86_RCX);
            uint32_t rdx = (uint32_t)rreg(cpu->hvf->fd, HV_X86_RDX);

            if (rax == 1) {
                /* CPUID1.ecx.OSXSAVE needs to know CR4 */
                env->cr[4] = rvmcs(cpu->hvf->fd, VMCS_GUEST_CR4);
            }
            hvf_cpu_x86_cpuid(env, rax, rcx, &rax, &rbx, &rcx, &rdx);

            wreg(cpu->hvf->fd, HV_X86_RAX, rax);
            wreg(cpu->hvf->fd, HV_X86_RBX, rbx);
            wreg(cpu->hvf->fd, HV_X86_RCX, rcx);
            wreg(cpu->hvf->fd, HV_X86_RDX, rdx);

            macvm_set_rip(cpu, rip + ins_len);
            break;
        }
        case EXIT_REASON_XSETBV: {
            X86CPU *x86_cpu = X86_CPU(cpu);
            CPUX86State *env = &x86_cpu->env;
            uint32_t eax = (uint32_t)rreg(cpu->hvf->fd, HV_X86_RAX);
            uint32_t ecx = (uint32_t)rreg(cpu->hvf->fd, HV_X86_RCX);
            uint32_t edx = (uint32_t)rreg(cpu->hvf->fd, HV_X86_RDX);

            if (ecx) {
                macvm_set_rip(cpu, rip + ins_len);
                break;
            }
            env->xcr0 = ((uint64_t)edx << 32) | eax;
            wreg(cpu->hvf->fd, HV_X86_XCR0, env->xcr0 | 1);
            macvm_set_rip(cpu, rip + ins_len);
            break;
        }
        case EXIT_REASON_INTR_WINDOW:
            vmx_clear_int_window_exiting(cpu);
            ret = EXCP_INTERRUPT;
            break;
        case EXIT_REASON_NMI_WINDOW:
            vmx_clear_nmi_window_exiting(cpu);
            ret = EXCP_INTERRUPT;
            break;
        case EXIT_REASON_EXT_INTR:
            /* force exit and allow io handling */
            ret = EXCP_INTERRUPT;
            break;
        case EXIT_REASON_RDMSR:
        case EXIT_REASON_WRMSR:
        {
            load_regs(cpu);
            if (exit_reason == EXIT_REASON_RDMSR) {
                simulate_rdmsr(cpu);
            } else {
                simulate_wrmsr(cpu);
            }
            env->eip += ins_len;
            store_regs(cpu);
            break;
        }
        case EXIT_REASON_CR_ACCESS: {
            int cr;
            int reg;

            load_regs(cpu);
            cr = exit_qual & 15;
            reg = (exit_qual >> 8) & 15;

            switch (cr) {
            case 0x0: {
                macvm_set_cr0(cpu->hvf->fd, RRX(env, reg));
                break;
            }
            case 4: {
                macvm_set_cr4(cpu->hvf->fd, RRX(env, reg));
                break;
            }
            case 8: {
                X86CPU *x86_cpu = X86_CPU(cpu);
                if (exit_qual & 0x10) {
                    RRX(env, reg) = cpu_get_apic_tpr(x86_cpu->apic_state);
                } else {
                    int tpr = RRX(env, reg);
                    cpu_set_apic_tpr(x86_cpu->apic_state, tpr);
                    ret = EXCP_INTERRUPT;
                }
                break;
            }
            default:
                error_report("Unrecognized CR %d", cr);
                abort();
            }
            env->eip += ins_len;
            store_regs(cpu);
            break;
        }
        case EXIT_REASON_APIC_ACCESS: { /* TODO */
            struct x86_decode decode;

            load_regs(cpu);
            decode_instruction(env, &decode);
            exec_instruction(env, &decode);
            store_regs(cpu);
            break;
        }
        case EXIT_REASON_TPR: {
            ret = 1;
            break;
        }
        case EXIT_REASON_TASK_SWITCH: {
            uint64_t vinfo = rvmcs(cpu->hvf->fd, VMCS_IDT_VECTORING_INFO);
            x68_segment_selector sel = {.sel = exit_qual & 0xffff};
            vmx_handle_task_switch(cpu, sel, (exit_qual >> 30) & 0x3,
             vinfo & VMCS_INTR_VALID, vinfo & VECTORING_INFO_VECTOR_MASK, vinfo
             & VMCS_INTR_T_MASK);
            break;
        }
        case EXIT_REASON_TRIPLE_FAULT: {
            qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
            ret = EXCP_INTERRUPT;
            break;
        }
        case EXIT_REASON_RDPMC:
            wreg(cpu->hvf->fd, HV_X86_RAX, 0);
            wreg(cpu->hvf->fd, HV_X86_RDX, 0);
            macvm_set_rip(cpu, rip + ins_len);
            break;
        case VMX_REASON_VMCALL:
            env->exception_nr = EXCP0D_GPF;
            env->exception_injected = 1;
            env->has_error_code = true;
            env->error_code = 0;
            break;
        default:
            error_report("%llx: unhandled exit %llx", rip, exit_reason);
        }
    } while (ret == 0);

    return ret;
}

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
#include "qemu/error-report.h"
#include "qemu/memalign.h"
#include "qapi/error.h"
#include "migration/blocker.h"

#include "system/hvf.h"
#include "system/hvf_int.h"
#include "system/runstate.h"
#include "system/cpus.h"
#include "hvf-i386.h"
#include "vmcs.h"
#include "vmx.h"
#include "emulate/x86.h"
#include "x86_descr.h"
#include "emulate/x86_flags.h"
#include "x86_mmu.h"
#include "emulate/x86_decode.h"
#include "emulate/x86_emu.h"
#include "x86_task.h"
#include "x86hvf.h"

#include <Hypervisor/hv.h>
#include <Hypervisor/hv_vmx.h>
#include <sys/sysctl.h>

#include "hw/i386/apic_internal.h"
#include "qemu/main-loop.h"
#include "qemu/accel.h"
#include "target/i386/cpu.h"

static Error *invtsc_mig_blocker;

void vmx_update_tpr(CPUState *cpu)
{
    /* TODO: need integrate APIC handling */
    X86CPU *x86_cpu = X86_CPU(cpu);
    int tpr = cpu_get_apic_tpr(x86_cpu->apic_state) << 4;
    int irr = apic_get_highest_priority_irr(x86_cpu->apic_state);

    wreg(cpu->accel->fd, HV_X86_TPR, tpr);
    if (irr == -1) {
        wvmcs(cpu->accel->fd, VMCS_TPR_THRESHOLD, 0);
    } else {
        wvmcs(cpu->accel->fd, VMCS_TPR_THRESHOLD, (irr > tpr) ? tpr >> 4 :
              irr >> 4);
    }
}

static void update_apic_tpr(CPUState *cpu)
{
    X86CPU *x86_cpu = X86_CPU(cpu);
    int tpr = rreg(cpu->accel->fd, HV_X86_TPR) >> 4;
    cpu_set_apic_tpr(x86_cpu->apic_state, tpr);
}

#define VECTORING_INFO_VECTOR_MASK     0xff

void hvf_handle_io(CPUState *env, uint16_t port, void *buffer,
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
            uint64_t dirty_page_start = gpa & ~(TARGET_PAGE_SIZE - 1u);
            memory_region_set_dirty(slot->region, gpa - slot->start, 1);
            hv_vm_protect(dirty_page_start, TARGET_PAGE_SIZE,
                          HV_MEMORY_READ | HV_MEMORY_WRITE | HV_MEMORY_EXEC);
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

    g_free(env->emu_mmio_buf);
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
    hv_vcpu_interrupt(&cpu->accel->fd, 1);
}

int hvf_arch_init(void)
{
    return 0;
}

hv_return_t hvf_arch_vm_create(MachineState *ms, uint32_t pa_range)
{
    return hv_vm_create(HV_VM_DEFAULT);
}

static void hvf_read_segment_descriptor(CPUState *s, struct x86_segment_descriptor *desc,
                                        X86Seg seg)
{
    struct vmx_segment vmx_segment;
    vmx_read_segment_descriptor(s, &vmx_segment, seg);
    vmx_segment_to_x86_descriptor(s, &vmx_segment, desc);
}

static void hvf_read_mem(CPUState *cpu, void *data, target_ulong gva, int bytes)
{
    vmx_read_mem(cpu, data, gva, bytes);
}

static void hvf_write_mem(CPUState *cpu, void *data, target_ulong gva, int bytes)
{
    vmx_write_mem(cpu, gva, data, bytes);
}

static const struct x86_emul_ops hvf_x86_emul_ops = {
    .read_mem = hvf_read_mem,
    .write_mem = hvf_write_mem,
    .read_segment_descriptor = hvf_read_segment_descriptor,
    .handle_io = hvf_handle_io,
    .simulate_rdmsr = hvf_simulate_rdmsr,
    .simulate_wrmsr = hvf_simulate_wrmsr,
};

int hvf_arch_init_vcpu(CPUState *cpu)
{
    X86CPU *x86cpu = X86_CPU(cpu);
    CPUX86State *env = &x86cpu->env;
    Error *local_err = NULL;
    int r;
    uint64_t reqCap;

    init_emu(&hvf_x86_emul_ops);
    init_decoder();

    if (hvf_state->hvf_caps == NULL) {
        hvf_state->hvf_caps = g_new0(struct hvf_vcpu_caps, 1);
    }
    env->emu_mmio_buf = g_new(char, 4096);

    if (x86cpu->vmware_cpuid_freq) {
        init_tsc_freq(env);
        init_apic_bus_freq(env);

        if (!tsc_is_known(env) || !apic_bus_freq_is_known(env)) {
            error_report("vmware-cpuid-freq: feature couldn't be enabled");
        }
    }

    if ((env->features[FEAT_8000_0007_EDX] & CPUID_APM_INVTSC) &&
        invtsc_mig_blocker == NULL) {
        error_setg(&invtsc_mig_blocker,
                   "State blocked by non-migratable CPU device (invtsc flag)");
        r = migrate_add_blocker(&invtsc_mig_blocker, &local_err);
        if (r < 0) {
            error_report_err(local_err);
            return r;
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
    wvmcs(cpu->accel->fd, VMCS_PIN_BASED_CTLS,
          cap2ctrl(hvf_state->hvf_caps->vmx_cap_pinbased,
                   VMCS_PIN_BASED_CTLS_EXTINT |
                   VMCS_PIN_BASED_CTLS_NMI |
                   VMCS_PIN_BASED_CTLS_VNMI));
    wvmcs(cpu->accel->fd, VMCS_PRI_PROC_BASED_CTLS,
          cap2ctrl(hvf_state->hvf_caps->vmx_cap_procbased,
                   VMCS_PRI_PROC_BASED_CTLS_HLT |
                   VMCS_PRI_PROC_BASED_CTLS_MWAIT |
                   VMCS_PRI_PROC_BASED_CTLS_TSC_OFFSET |
                   VMCS_PRI_PROC_BASED_CTLS_TPR_SHADOW) |
          VMCS_PRI_PROC_BASED_CTLS_SEC_CONTROL);

    reqCap = VMCS_PRI_PROC_BASED2_CTLS_APIC_ACCESSES;

    /* Is RDTSCP support in CPUID?  If so, enable it in the VMCS. */
    if (hvf_get_supported_cpuid(0x80000001, 0, R_EDX) & CPUID_EXT2_RDTSCP) {
        reqCap |= VMCS_PRI_PROC_BASED2_CTLS_RDTSCP;
    }

    wvmcs(cpu->accel->fd, VMCS_SEC_PROC_BASED_CTLS,
          cap2ctrl(hvf_state->hvf_caps->vmx_cap_procbased2, reqCap));

    wvmcs(cpu->accel->fd, VMCS_ENTRY_CTLS,
          cap2ctrl(hvf_state->hvf_caps->vmx_cap_entry, 0));
    wvmcs(cpu->accel->fd, VMCS_EXCEPTION_BITMAP, 0); /* Double fault */

    wvmcs(cpu->accel->fd, VMCS_TPR_THRESHOLD, 0);

    x86cpu = X86_CPU(cpu);
    x86cpu->env.xsave_buf_len = 4096;
    x86cpu->env.xsave_buf = qemu_memalign(4096, x86cpu->env.xsave_buf_len);

    /*
     * The allocated storage must be large enough for all of the
     * possible XSAVE state components.
     */
    assert(hvf_get_supported_cpuid(0xd, 0, R_ECX) <= x86cpu->env.xsave_buf_len);

    hv_vcpu_enable_native_msr(cpu->accel->fd, MSR_STAR, 1);
    hv_vcpu_enable_native_msr(cpu->accel->fd, MSR_LSTAR, 1);
    hv_vcpu_enable_native_msr(cpu->accel->fd, MSR_CSTAR, 1);
    hv_vcpu_enable_native_msr(cpu->accel->fd, MSR_FMASK, 1);
    hv_vcpu_enable_native_msr(cpu->accel->fd, MSR_FSBASE, 1);
    hv_vcpu_enable_native_msr(cpu->accel->fd, MSR_GSBASE, 1);
    hv_vcpu_enable_native_msr(cpu->accel->fd, MSR_KERNELGSBASE, 1);
    hv_vcpu_enable_native_msr(cpu->accel->fd, MSR_TSC_AUX, 1);
    hv_vcpu_enable_native_msr(cpu->accel->fd, MSR_IA32_TSC, 1);
    hv_vcpu_enable_native_msr(cpu->accel->fd, MSR_IA32_SYSENTER_CS, 1);
    hv_vcpu_enable_native_msr(cpu->accel->fd, MSR_IA32_SYSENTER_EIP, 1);
    hv_vcpu_enable_native_msr(cpu->accel->fd, MSR_IA32_SYSENTER_ESP, 1);

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
            env->error_code = rvmcs(cpu->accel->fd, VMCS_IDT_VECTORING_ERROR);
        }
    }
    if ((rvmcs(cpu->accel->fd, VMCS_GUEST_INTERRUPTIBILITY) &
        VMCS_INTERRUPTIBILITY_NMI_BLOCKING)) {
        env->hflags2 |= HF2_NMI_MASK;
    } else {
        env->hflags2 &= ~HF2_NMI_MASK;
    }
    if (rvmcs(cpu->accel->fd, VMCS_GUEST_INTERRUPTIBILITY) &
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

void hvf_load_regs(CPUState *cs)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;

    int i = 0;
    RRX(env, R_EAX) = rreg(cs->accel->fd, HV_X86_RAX);
    RRX(env, R_EBX) = rreg(cs->accel->fd, HV_X86_RBX);
    RRX(env, R_ECX) = rreg(cs->accel->fd, HV_X86_RCX);
    RRX(env, R_EDX) = rreg(cs->accel->fd, HV_X86_RDX);
    RRX(env, R_ESI) = rreg(cs->accel->fd, HV_X86_RSI);
    RRX(env, R_EDI) = rreg(cs->accel->fd, HV_X86_RDI);
    RRX(env, R_ESP) = rreg(cs->accel->fd, HV_X86_RSP);
    RRX(env, R_EBP) = rreg(cs->accel->fd, HV_X86_RBP);
    for (i = 8; i < 16; i++) {
        RRX(env, i) = rreg(cs->accel->fd, HV_X86_RAX + i);
    }

    env->eflags = rreg(cs->accel->fd, HV_X86_RFLAGS);
    rflags_to_lflags(env);
    env->eip = rreg(cs->accel->fd, HV_X86_RIP);
}

void hvf_store_regs(CPUState *cs)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;

    int i = 0;
    wreg(cs->accel->fd, HV_X86_RAX, RAX(env));
    wreg(cs->accel->fd, HV_X86_RBX, RBX(env));
    wreg(cs->accel->fd, HV_X86_RCX, RCX(env));
    wreg(cs->accel->fd, HV_X86_RDX, RDX(env));
    wreg(cs->accel->fd, HV_X86_RSI, RSI(env));
    wreg(cs->accel->fd, HV_X86_RDI, RDI(env));
    wreg(cs->accel->fd, HV_X86_RBP, RBP(env));
    wreg(cs->accel->fd, HV_X86_RSP, RSP(env));
    for (i = 8; i < 16; i++) {
        wreg(cs->accel->fd, HV_X86_RAX + i, RRX(env, i));
    }

    lflags_to_rflags(env);
    wreg(cs->accel->fd, HV_X86_RFLAGS, env->eflags);
    macvm_set_rip(cs, env->eip);
}

void hvf_simulate_rdmsr(CPUState *cs)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;
    uint32_t msr = ECX(env);
    uint64_t val = 0;

    switch (msr) {
    case MSR_IA32_TSC:
        val = rdtscp() + rvmcs(cs->accel->fd, VMCS_TSC_OFFSET);
        break;
    case MSR_IA32_APICBASE:
        val = cpu_get_apic_base(cpu->apic_state);
        break;
    case MSR_APIC_START ... MSR_APIC_END: {
        int ret;
        int index = (uint32_t)env->regs[R_ECX] - MSR_APIC_START;

        ret = apic_msr_read(index, &val);
        if (ret < 0) {
            x86_emul_raise_exception(env, EXCP0D_GPF, 0);
        }

        break;
    }
    case MSR_IA32_UCODE_REV:
        val = cpu->ucode_rev;
        break;
    case MSR_EFER:
        val = rvmcs(cs->accel->fd, VMCS_GUEST_IA32_EFER);
        break;
    case MSR_FSBASE:
        val = rvmcs(cs->accel->fd, VMCS_GUEST_FS_BASE);
        break;
    case MSR_GSBASE:
        val = rvmcs(cs->accel->fd, VMCS_GUEST_GS_BASE);
        break;
    case MSR_KERNELGSBASE:
        val = rvmcs(cs->accel->fd, VMCS_HOST_FS_BASE);
        break;
    case MSR_STAR:
        abort();
        break;
    case MSR_LSTAR:
        abort();
        break;
    case MSR_CSTAR:
        abort();
        break;
    case MSR_IA32_MISC_ENABLE:
        val = env->msr_ia32_misc_enable;
        break;
    case MSR_MTRRphysBase(0):
    case MSR_MTRRphysBase(1):
    case MSR_MTRRphysBase(2):
    case MSR_MTRRphysBase(3):
    case MSR_MTRRphysBase(4):
    case MSR_MTRRphysBase(5):
    case MSR_MTRRphysBase(6):
    case MSR_MTRRphysBase(7):
        val = env->mtrr_var[(ECX(env) - MSR_MTRRphysBase(0)) / 2].base;
        break;
    case MSR_MTRRphysMask(0):
    case MSR_MTRRphysMask(1):
    case MSR_MTRRphysMask(2):
    case MSR_MTRRphysMask(3):
    case MSR_MTRRphysMask(4):
    case MSR_MTRRphysMask(5):
    case MSR_MTRRphysMask(6):
    case MSR_MTRRphysMask(7):
        val = env->mtrr_var[(ECX(env) - MSR_MTRRphysMask(0)) / 2].mask;
        break;
    case MSR_MTRRfix64K_00000:
        val = env->mtrr_fixed[0];
        break;
    case MSR_MTRRfix16K_80000:
    case MSR_MTRRfix16K_A0000:
        val = env->mtrr_fixed[ECX(env) - MSR_MTRRfix16K_80000 + 1];
        break;
    case MSR_MTRRfix4K_C0000:
    case MSR_MTRRfix4K_C8000:
    case MSR_MTRRfix4K_D0000:
    case MSR_MTRRfix4K_D8000:
    case MSR_MTRRfix4K_E0000:
    case MSR_MTRRfix4K_E8000:
    case MSR_MTRRfix4K_F0000:
    case MSR_MTRRfix4K_F8000:
        val = env->mtrr_fixed[ECX(env) - MSR_MTRRfix4K_C0000 + 3];
        break;
    case MSR_MTRRdefType:
        val = env->mtrr_deftype;
        break;
    case MSR_CORE_THREAD_COUNT:
        val = cpu_x86_get_msr_core_thread_count(cpu);
        break;
    default:
        /* fprintf(stderr, "%s: unknown msr 0x%x\n", __func__, msr); */
        val = 0;
        break;
    }

    RAX(env) = (uint32_t)val;
    RDX(env) = (uint32_t)(val >> 32);
}

void hvf_simulate_wrmsr(CPUState *cs)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;
    uint32_t msr = ECX(env);
    uint64_t data = ((uint64_t)EDX(env) << 32) | EAX(env);

    switch (msr) {
    case MSR_IA32_TSC:
        break;
    case MSR_IA32_APICBASE: {
        int r;

        r = cpu_set_apic_base(cpu->apic_state, data);
        if (r < 0) {
            x86_emul_raise_exception(env, EXCP0D_GPF, 0);
        }

        break;
    }
    case MSR_APIC_START ... MSR_APIC_END: {
        int ret;
        int index = (uint32_t)env->regs[R_ECX] - MSR_APIC_START;

        ret = apic_msr_write(index, data);
        if (ret < 0) {
            x86_emul_raise_exception(env, EXCP0D_GPF, 0);
        }

        break;
    }
    case MSR_FSBASE:
        wvmcs(cs->accel->fd, VMCS_GUEST_FS_BASE, data);
        break;
    case MSR_GSBASE:
        wvmcs(cs->accel->fd, VMCS_GUEST_GS_BASE, data);
        break;
    case MSR_KERNELGSBASE:
        wvmcs(cs->accel->fd, VMCS_HOST_FS_BASE, data);
        break;
    case MSR_STAR:
        abort();
        break;
    case MSR_LSTAR:
        abort();
        break;
    case MSR_CSTAR:
        abort();
        break;
    case MSR_EFER:
        /*printf("new efer %llx\n", EFER(cs));*/
        wvmcs(cs->accel->fd, VMCS_GUEST_IA32_EFER, data);
        if (data & MSR_EFER_NXE) {
            hv_vcpu_invalidate_tlb(cs->accel->fd);
        }
        break;
    case MSR_MTRRphysBase(0):
    case MSR_MTRRphysBase(1):
    case MSR_MTRRphysBase(2):
    case MSR_MTRRphysBase(3):
    case MSR_MTRRphysBase(4):
    case MSR_MTRRphysBase(5):
    case MSR_MTRRphysBase(6):
    case MSR_MTRRphysBase(7):
        env->mtrr_var[(ECX(env) - MSR_MTRRphysBase(0)) / 2].base = data;
        break;
    case MSR_MTRRphysMask(0):
    case MSR_MTRRphysMask(1):
    case MSR_MTRRphysMask(2):
    case MSR_MTRRphysMask(3):
    case MSR_MTRRphysMask(4):
    case MSR_MTRRphysMask(5):
    case MSR_MTRRphysMask(6):
    case MSR_MTRRphysMask(7):
        env->mtrr_var[(ECX(env) - MSR_MTRRphysMask(0)) / 2].mask = data;
        break;
    case MSR_MTRRfix64K_00000:
        env->mtrr_fixed[ECX(env) - MSR_MTRRfix64K_00000] = data;
        break;
    case MSR_MTRRfix16K_80000:
    case MSR_MTRRfix16K_A0000:
        env->mtrr_fixed[ECX(env) - MSR_MTRRfix16K_80000 + 1] = data;
        break;
    case MSR_MTRRfix4K_C0000:
    case MSR_MTRRfix4K_C8000:
    case MSR_MTRRfix4K_D0000:
    case MSR_MTRRfix4K_D8000:
    case MSR_MTRRfix4K_E0000:
    case MSR_MTRRfix4K_E8000:
    case MSR_MTRRfix4K_F0000:
    case MSR_MTRRfix4K_F8000:
        env->mtrr_fixed[ECX(env) - MSR_MTRRfix4K_C0000 + 3] = data;
        break;
    case MSR_MTRRdefType:
        env->mtrr_deftype = data;
        break;
    default:
        break;
    }

    /* Related to support known hypervisor interface */
    /* if (g_hypervisor_iface)
         g_hypervisor_iface->wrmsr_handler(cs, msr, data);

    printf("write msr %llx\n", RCX(cs));*/
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
        if (cpu->accel->dirty) {
            hvf_put_registers(cpu);
            cpu->accel->dirty = false;
        }

        if (hvf_inject_interrupts(cpu)) {
            return EXCP_INTERRUPT;
        }
        vmx_update_tpr(cpu);

        bql_unlock();
        if (!cpu_is_bsp(X86_CPU(cpu)) && cpu->halted) {
            bql_lock();
            return EXCP_HLT;
        }

        hv_return_t r = hv_vcpu_run_until(cpu->accel->fd, HV_DEADLINE_FOREVER);
        assert_hvf_ok(r);

        /* handle VMEXIT */
        uint64_t exit_reason = rvmcs(cpu->accel->fd, VMCS_EXIT_REASON);
        uint64_t exit_qual = rvmcs(cpu->accel->fd, VMCS_EXIT_QUALIFICATION);
        uint32_t ins_len = (uint32_t)rvmcs(cpu->accel->fd,
                                           VMCS_EXIT_INSTRUCTION_LENGTH);

        uint64_t idtvec_info = rvmcs(cpu->accel->fd, VMCS_IDT_VECTORING_INFO);

        hvf_store_events(cpu, ins_len, idtvec_info);
        rip = rreg(cpu->accel->fd, HV_X86_RIP);
        env->eflags = rreg(cpu->accel->fd, HV_X86_RFLAGS);

        bql_lock();

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
            uint64_t gpa = rvmcs(cpu->accel->fd, VMCS_GUEST_PHYSICAL_ADDRESS);

            if (((idtvec_info & VMCS_IDT_VEC_VALID) == 0) &&
                ((exit_qual & EXIT_QUAL_NMIUDTI) != 0)) {
                vmx_set_nmi_blocking(cpu);
            }

            slot = hvf_find_overlap_slot(gpa, 1);
            /* mmio */
            if (ept_emulation_fault(slot, gpa, exit_qual)) {
                struct x86_decode decode;

                hvf_load_regs(cpu);
                decode_instruction(env, &decode);
                exec_instruction(env, &decode);
                hvf_store_regs(cpu);
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
                hvf_load_regs(cpu);
                hvf_handle_io(env_cpu(env), port, &val, 0, size, 1);
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
                hvf_store_regs(cpu);
                break;
            } else if (!string && !in) {
                RAX(env) = rreg(cpu->accel->fd, HV_X86_RAX);
                hvf_handle_io(env_cpu(env), port, &RAX(env), 1, size, 1);
                macvm_set_rip(cpu, rip + ins_len);
                break;
            }
            struct x86_decode decode;

            hvf_load_regs(cpu);
            decode_instruction(env, &decode);
            assert(ins_len == decode.len);
            exec_instruction(env, &decode);
            hvf_store_regs(cpu);

            break;
        }
        case EXIT_REASON_CPUID: {
            uint32_t rax = (uint32_t)rreg(cpu->accel->fd, HV_X86_RAX);
            uint32_t rbx = (uint32_t)rreg(cpu->accel->fd, HV_X86_RBX);
            uint32_t rcx = (uint32_t)rreg(cpu->accel->fd, HV_X86_RCX);
            uint32_t rdx = (uint32_t)rreg(cpu->accel->fd, HV_X86_RDX);

            if (rax == 1) {
                /* CPUID1.ecx.OSXSAVE needs to know CR4 */
                env->cr[4] = rvmcs(cpu->accel->fd, VMCS_GUEST_CR4);
            }
            hvf_cpu_x86_cpuid(env, rax, rcx, &rax, &rbx, &rcx, &rdx);

            wreg(cpu->accel->fd, HV_X86_RAX, rax);
            wreg(cpu->accel->fd, HV_X86_RBX, rbx);
            wreg(cpu->accel->fd, HV_X86_RCX, rcx);
            wreg(cpu->accel->fd, HV_X86_RDX, rdx);

            macvm_set_rip(cpu, rip + ins_len);
            break;
        }
        case EXIT_REASON_XSETBV: {
            uint32_t eax = (uint32_t)rreg(cpu->accel->fd, HV_X86_RAX);
            uint32_t ecx = (uint32_t)rreg(cpu->accel->fd, HV_X86_RCX);
            uint32_t edx = (uint32_t)rreg(cpu->accel->fd, HV_X86_RDX);

            if (ecx) {
                macvm_set_rip(cpu, rip + ins_len);
                break;
            }
            env->xcr0 = ((uint64_t)edx << 32) | eax;
            wreg(cpu->accel->fd, HV_X86_XCR0, env->xcr0 | 1);
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
            hvf_load_regs(cpu);
            if (exit_reason == EXIT_REASON_RDMSR) {
                hvf_simulate_rdmsr(cpu);
            } else {
                hvf_simulate_wrmsr(cpu);
            }
            env->eip += ins_len;
            hvf_store_regs(cpu);
            break;
        }
        case EXIT_REASON_CR_ACCESS: {
            int cr;
            int reg;

            hvf_load_regs(cpu);
            cr = exit_qual & 15;
            reg = (exit_qual >> 8) & 15;

            switch (cr) {
            case 0x0: {
                macvm_set_cr0(cpu->accel->fd, RRX(env, reg));
                break;
            }
            case 4: {
                macvm_set_cr4(cpu->accel->fd, RRX(env, reg));
                break;
            }
            case 8: {
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
            hvf_store_regs(cpu);
            break;
        }
        case EXIT_REASON_APIC_ACCESS: { /* TODO */
            struct x86_decode decode;

            hvf_load_regs(cpu);
            decode_instruction(env, &decode);
            exec_instruction(env, &decode);
            hvf_store_regs(cpu);
            break;
        }
        case EXIT_REASON_TPR: {
            ret = 1;
            break;
        }
        case EXIT_REASON_TASK_SWITCH: {
            uint64_t vinfo = rvmcs(cpu->accel->fd, VMCS_IDT_VECTORING_INFO);
            x86_segment_selector sel = {.sel = exit_qual & 0xffff};
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
            wreg(cpu->accel->fd, HV_X86_RAX, 0);
            wreg(cpu->accel->fd, HV_X86_RDX, 0);
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

int hvf_arch_insert_sw_breakpoint(CPUState *cpu, struct hvf_sw_breakpoint *bp)
{
    return -ENOSYS;
}

int hvf_arch_remove_sw_breakpoint(CPUState *cpu, struct hvf_sw_breakpoint *bp)
{
    return -ENOSYS;
}

int hvf_arch_insert_hw_breakpoint(vaddr addr, vaddr len, int type)
{
    return -ENOSYS;
}

int hvf_arch_remove_hw_breakpoint(vaddr addr, vaddr len, int type)
{
    return -ENOSYS;
}

void hvf_arch_remove_all_hw_breakpoints(void)
{
}

void hvf_arch_update_guest_debug(CPUState *cpu)
{
}

bool hvf_arch_supports_guest_debug(void)
{
    return false;
}

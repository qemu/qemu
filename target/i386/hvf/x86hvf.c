/*
 * Copyright (c) 2003-2008 Fabrice Bellard
 * Copyright (C) 2016 Veertu Inc,
 * Copyright (C) 2017 Google Inc,
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"

#include "qemu-common.h"
#include "x86hvf.h"
#include "vmx.h"
#include "vmcs.h"
#include "cpu.h"
#include "x86_descr.h"
#include "x86_decode.h"

#include "hw/i386/apic_internal.h"

#include <Hypervisor/hv.h>
#include <Hypervisor/hv_vmx.h>

void hvf_set_segment(struct CPUState *cpu, struct vmx_segment *vmx_seg,
                     SegmentCache *qseg, bool is_tr)
{
    vmx_seg->sel = qseg->selector;
    vmx_seg->base = qseg->base;
    vmx_seg->limit = qseg->limit;

    if (!qseg->selector && !x86_is_real(cpu) && !is_tr) {
        /* the TR register is usable after processor reset despite
         * having a null selector */
        vmx_seg->ar = 1 << 16;
        return;
    }
    vmx_seg->ar = (qseg->flags >> DESC_TYPE_SHIFT) & 0xf;
    vmx_seg->ar |= ((qseg->flags >> DESC_G_SHIFT) & 1) << 15;
    vmx_seg->ar |= ((qseg->flags >> DESC_B_SHIFT) & 1) << 14;
    vmx_seg->ar |= ((qseg->flags >> DESC_L_SHIFT) & 1) << 13;
    vmx_seg->ar |= ((qseg->flags >> DESC_AVL_SHIFT) & 1) << 12;
    vmx_seg->ar |= ((qseg->flags >> DESC_P_SHIFT) & 1) << 7;
    vmx_seg->ar |= ((qseg->flags >> DESC_DPL_SHIFT) & 3) << 5;
    vmx_seg->ar |= ((qseg->flags >> DESC_S_SHIFT) & 1) << 4;
}

void hvf_get_segment(SegmentCache *qseg, struct vmx_segment *vmx_seg)
{
    qseg->limit = vmx_seg->limit;
    qseg->base = vmx_seg->base;
    qseg->selector = vmx_seg->sel;
    qseg->flags = ((vmx_seg->ar & 0xf) << DESC_TYPE_SHIFT) |
                  (((vmx_seg->ar >> 4) & 1) << DESC_S_SHIFT) |
                  (((vmx_seg->ar >> 5) & 3) << DESC_DPL_SHIFT) |
                  (((vmx_seg->ar >> 7) & 1) << DESC_P_SHIFT) |
                  (((vmx_seg->ar >> 12) & 1) << DESC_AVL_SHIFT) |
                  (((vmx_seg->ar >> 13) & 1) << DESC_L_SHIFT) |
                  (((vmx_seg->ar >> 14) & 1) << DESC_B_SHIFT) |
                  (((vmx_seg->ar >> 15) & 1) << DESC_G_SHIFT);
}

void hvf_put_xsave(CPUState *cpu_state)
{

    struct X86XSaveArea *xsave;

    xsave = X86_CPU(cpu_state)->env.xsave_buf;

    x86_cpu_xsave_all_areas(X86_CPU(cpu_state), xsave);

    if (hv_vcpu_write_fpstate(cpu_state->hvf_fd, (void*)xsave, 4096)) {
        abort();
    }
}

void hvf_put_segments(CPUState *cpu_state)
{
    CPUX86State *env = &X86_CPU(cpu_state)->env;
    struct vmx_segment seg;
    
    wvmcs(cpu_state->hvf_fd, VMCS_GUEST_IDTR_LIMIT, env->idt.limit);
    wvmcs(cpu_state->hvf_fd, VMCS_GUEST_IDTR_BASE, env->idt.base);

    wvmcs(cpu_state->hvf_fd, VMCS_GUEST_GDTR_LIMIT, env->gdt.limit);
    wvmcs(cpu_state->hvf_fd, VMCS_GUEST_GDTR_BASE, env->gdt.base);

    /* wvmcs(cpu_state->hvf_fd, VMCS_GUEST_CR2, env->cr[2]); */
    wvmcs(cpu_state->hvf_fd, VMCS_GUEST_CR3, env->cr[3]);
    vmx_update_tpr(cpu_state);
    wvmcs(cpu_state->hvf_fd, VMCS_GUEST_IA32_EFER, env->efer);

    macvm_set_cr4(cpu_state->hvf_fd, env->cr[4]);
    macvm_set_cr0(cpu_state->hvf_fd, env->cr[0]);

    hvf_set_segment(cpu_state, &seg, &env->segs[R_CS], false);
    vmx_write_segment_descriptor(cpu_state, &seg, R_CS);
    
    hvf_set_segment(cpu_state, &seg, &env->segs[R_DS], false);
    vmx_write_segment_descriptor(cpu_state, &seg, R_DS);

    hvf_set_segment(cpu_state, &seg, &env->segs[R_ES], false);
    vmx_write_segment_descriptor(cpu_state, &seg, R_ES);

    hvf_set_segment(cpu_state, &seg, &env->segs[R_SS], false);
    vmx_write_segment_descriptor(cpu_state, &seg, R_SS);

    hvf_set_segment(cpu_state, &seg, &env->segs[R_FS], false);
    vmx_write_segment_descriptor(cpu_state, &seg, R_FS);

    hvf_set_segment(cpu_state, &seg, &env->segs[R_GS], false);
    vmx_write_segment_descriptor(cpu_state, &seg, R_GS);

    hvf_set_segment(cpu_state, &seg, &env->tr, true);
    vmx_write_segment_descriptor(cpu_state, &seg, R_TR);

    hvf_set_segment(cpu_state, &seg, &env->ldt, false);
    vmx_write_segment_descriptor(cpu_state, &seg, R_LDTR);
    
    hv_vcpu_flush(cpu_state->hvf_fd);
}
    
void hvf_put_msrs(CPUState *cpu_state)
{
    CPUX86State *env = &X86_CPU(cpu_state)->env;

    hv_vcpu_write_msr(cpu_state->hvf_fd, MSR_IA32_SYSENTER_CS,
                      env->sysenter_cs);
    hv_vcpu_write_msr(cpu_state->hvf_fd, MSR_IA32_SYSENTER_ESP,
                      env->sysenter_esp);
    hv_vcpu_write_msr(cpu_state->hvf_fd, MSR_IA32_SYSENTER_EIP,
                      env->sysenter_eip);

    hv_vcpu_write_msr(cpu_state->hvf_fd, MSR_STAR, env->star);

#ifdef TARGET_X86_64
    hv_vcpu_write_msr(cpu_state->hvf_fd, MSR_CSTAR, env->cstar);
    hv_vcpu_write_msr(cpu_state->hvf_fd, MSR_KERNELGSBASE, env->kernelgsbase);
    hv_vcpu_write_msr(cpu_state->hvf_fd, MSR_FMASK, env->fmask);
    hv_vcpu_write_msr(cpu_state->hvf_fd, MSR_LSTAR, env->lstar);
#endif

    hv_vcpu_write_msr(cpu_state->hvf_fd, MSR_GSBASE, env->segs[R_GS].base);
    hv_vcpu_write_msr(cpu_state->hvf_fd, MSR_FSBASE, env->segs[R_FS].base);
}


void hvf_get_xsave(CPUState *cpu_state)
{
    struct X86XSaveArea *xsave;

    xsave = X86_CPU(cpu_state)->env.xsave_buf;

    if (hv_vcpu_read_fpstate(cpu_state->hvf_fd, (void*)xsave, 4096)) {
        abort();
    }

    x86_cpu_xrstor_all_areas(X86_CPU(cpu_state), xsave);
}

void hvf_get_segments(CPUState *cpu_state)
{
    CPUX86State *env = &X86_CPU(cpu_state)->env;

    struct vmx_segment seg;

    env->interrupt_injected = -1;

    vmx_read_segment_descriptor(cpu_state, &seg, R_CS);
    hvf_get_segment(&env->segs[R_CS], &seg);
    
    vmx_read_segment_descriptor(cpu_state, &seg, R_DS);
    hvf_get_segment(&env->segs[R_DS], &seg);

    vmx_read_segment_descriptor(cpu_state, &seg, R_ES);
    hvf_get_segment(&env->segs[R_ES], &seg);

    vmx_read_segment_descriptor(cpu_state, &seg, R_FS);
    hvf_get_segment(&env->segs[R_FS], &seg);

    vmx_read_segment_descriptor(cpu_state, &seg, R_GS);
    hvf_get_segment(&env->segs[R_GS], &seg);

    vmx_read_segment_descriptor(cpu_state, &seg, R_SS);
    hvf_get_segment(&env->segs[R_SS], &seg);

    vmx_read_segment_descriptor(cpu_state, &seg, R_TR);
    hvf_get_segment(&env->tr, &seg);

    vmx_read_segment_descriptor(cpu_state, &seg, R_LDTR);
    hvf_get_segment(&env->ldt, &seg);

    env->idt.limit = rvmcs(cpu_state->hvf_fd, VMCS_GUEST_IDTR_LIMIT);
    env->idt.base = rvmcs(cpu_state->hvf_fd, VMCS_GUEST_IDTR_BASE);
    env->gdt.limit = rvmcs(cpu_state->hvf_fd, VMCS_GUEST_GDTR_LIMIT);
    env->gdt.base = rvmcs(cpu_state->hvf_fd, VMCS_GUEST_GDTR_BASE);

    env->cr[0] = rvmcs(cpu_state->hvf_fd, VMCS_GUEST_CR0);
    env->cr[2] = 0;
    env->cr[3] = rvmcs(cpu_state->hvf_fd, VMCS_GUEST_CR3);
    env->cr[4] = rvmcs(cpu_state->hvf_fd, VMCS_GUEST_CR4);
    
    env->efer = rvmcs(cpu_state->hvf_fd, VMCS_GUEST_IA32_EFER);
}

void hvf_get_msrs(CPUState *cpu_state)
{
    CPUX86State *env = &X86_CPU(cpu_state)->env;
    uint64_t tmp;
    
    hv_vcpu_read_msr(cpu_state->hvf_fd, MSR_IA32_SYSENTER_CS, &tmp);
    env->sysenter_cs = tmp;
    
    hv_vcpu_read_msr(cpu_state->hvf_fd, MSR_IA32_SYSENTER_ESP, &tmp);
    env->sysenter_esp = tmp;

    hv_vcpu_read_msr(cpu_state->hvf_fd, MSR_IA32_SYSENTER_EIP, &tmp);
    env->sysenter_eip = tmp;

    hv_vcpu_read_msr(cpu_state->hvf_fd, MSR_STAR, &env->star);

#ifdef TARGET_X86_64
    hv_vcpu_read_msr(cpu_state->hvf_fd, MSR_CSTAR, &env->cstar);
    hv_vcpu_read_msr(cpu_state->hvf_fd, MSR_KERNELGSBASE, &env->kernelgsbase);
    hv_vcpu_read_msr(cpu_state->hvf_fd, MSR_FMASK, &env->fmask);
    hv_vcpu_read_msr(cpu_state->hvf_fd, MSR_LSTAR, &env->lstar);
#endif

    hv_vcpu_read_msr(cpu_state->hvf_fd, MSR_IA32_APICBASE, &tmp);
    
    env->tsc = rdtscp() + rvmcs(cpu_state->hvf_fd, VMCS_TSC_OFFSET);
}

int hvf_put_registers(CPUState *cpu_state)
{
    X86CPU *x86cpu = X86_CPU(cpu_state);
    CPUX86State *env = &x86cpu->env;

    wreg(cpu_state->hvf_fd, HV_X86_RAX, env->regs[R_EAX]);
    wreg(cpu_state->hvf_fd, HV_X86_RBX, env->regs[R_EBX]);
    wreg(cpu_state->hvf_fd, HV_X86_RCX, env->regs[R_ECX]);
    wreg(cpu_state->hvf_fd, HV_X86_RDX, env->regs[R_EDX]);
    wreg(cpu_state->hvf_fd, HV_X86_RBP, env->regs[R_EBP]);
    wreg(cpu_state->hvf_fd, HV_X86_RSP, env->regs[R_ESP]);
    wreg(cpu_state->hvf_fd, HV_X86_RSI, env->regs[R_ESI]);
    wreg(cpu_state->hvf_fd, HV_X86_RDI, env->regs[R_EDI]);
    wreg(cpu_state->hvf_fd, HV_X86_R8, env->regs[8]);
    wreg(cpu_state->hvf_fd, HV_X86_R9, env->regs[9]);
    wreg(cpu_state->hvf_fd, HV_X86_R10, env->regs[10]);
    wreg(cpu_state->hvf_fd, HV_X86_R11, env->regs[11]);
    wreg(cpu_state->hvf_fd, HV_X86_R12, env->regs[12]);
    wreg(cpu_state->hvf_fd, HV_X86_R13, env->regs[13]);
    wreg(cpu_state->hvf_fd, HV_X86_R14, env->regs[14]);
    wreg(cpu_state->hvf_fd, HV_X86_R15, env->regs[15]);
    wreg(cpu_state->hvf_fd, HV_X86_RFLAGS, env->eflags);
    wreg(cpu_state->hvf_fd, HV_X86_RIP, env->eip);
   
    wreg(cpu_state->hvf_fd, HV_X86_XCR0, env->xcr0);
    
    hvf_put_xsave(cpu_state);
    
    hvf_put_segments(cpu_state);
    
    hvf_put_msrs(cpu_state);
    
    wreg(cpu_state->hvf_fd, HV_X86_DR0, env->dr[0]);
    wreg(cpu_state->hvf_fd, HV_X86_DR1, env->dr[1]);
    wreg(cpu_state->hvf_fd, HV_X86_DR2, env->dr[2]);
    wreg(cpu_state->hvf_fd, HV_X86_DR3, env->dr[3]);
    wreg(cpu_state->hvf_fd, HV_X86_DR4, env->dr[4]);
    wreg(cpu_state->hvf_fd, HV_X86_DR5, env->dr[5]);
    wreg(cpu_state->hvf_fd, HV_X86_DR6, env->dr[6]);
    wreg(cpu_state->hvf_fd, HV_X86_DR7, env->dr[7]);
    
    return 0;
}

int hvf_get_registers(CPUState *cpu_state)
{
    X86CPU *x86cpu = X86_CPU(cpu_state);
    CPUX86State *env = &x86cpu->env;

    env->regs[R_EAX] = rreg(cpu_state->hvf_fd, HV_X86_RAX);
    env->regs[R_EBX] = rreg(cpu_state->hvf_fd, HV_X86_RBX);
    env->regs[R_ECX] = rreg(cpu_state->hvf_fd, HV_X86_RCX);
    env->regs[R_EDX] = rreg(cpu_state->hvf_fd, HV_X86_RDX);
    env->regs[R_EBP] = rreg(cpu_state->hvf_fd, HV_X86_RBP);
    env->regs[R_ESP] = rreg(cpu_state->hvf_fd, HV_X86_RSP);
    env->regs[R_ESI] = rreg(cpu_state->hvf_fd, HV_X86_RSI);
    env->regs[R_EDI] = rreg(cpu_state->hvf_fd, HV_X86_RDI);
    env->regs[8] = rreg(cpu_state->hvf_fd, HV_X86_R8);
    env->regs[9] = rreg(cpu_state->hvf_fd, HV_X86_R9);
    env->regs[10] = rreg(cpu_state->hvf_fd, HV_X86_R10);
    env->regs[11] = rreg(cpu_state->hvf_fd, HV_X86_R11);
    env->regs[12] = rreg(cpu_state->hvf_fd, HV_X86_R12);
    env->regs[13] = rreg(cpu_state->hvf_fd, HV_X86_R13);
    env->regs[14] = rreg(cpu_state->hvf_fd, HV_X86_R14);
    env->regs[15] = rreg(cpu_state->hvf_fd, HV_X86_R15);
    
    env->eflags = rreg(cpu_state->hvf_fd, HV_X86_RFLAGS);
    env->eip = rreg(cpu_state->hvf_fd, HV_X86_RIP);
   
    hvf_get_xsave(cpu_state);
    env->xcr0 = rreg(cpu_state->hvf_fd, HV_X86_XCR0);
    
    hvf_get_segments(cpu_state);
    hvf_get_msrs(cpu_state);
    
    env->dr[0] = rreg(cpu_state->hvf_fd, HV_X86_DR0);
    env->dr[1] = rreg(cpu_state->hvf_fd, HV_X86_DR1);
    env->dr[2] = rreg(cpu_state->hvf_fd, HV_X86_DR2);
    env->dr[3] = rreg(cpu_state->hvf_fd, HV_X86_DR3);
    env->dr[4] = rreg(cpu_state->hvf_fd, HV_X86_DR4);
    env->dr[5] = rreg(cpu_state->hvf_fd, HV_X86_DR5);
    env->dr[6] = rreg(cpu_state->hvf_fd, HV_X86_DR6);
    env->dr[7] = rreg(cpu_state->hvf_fd, HV_X86_DR7);
    
    x86_update_hflags(env);
    return 0;
}

static void vmx_set_int_window_exiting(CPUState *cpu)
{
     uint64_t val;
     val = rvmcs(cpu->hvf_fd, VMCS_PRI_PROC_BASED_CTLS);
     wvmcs(cpu->hvf_fd, VMCS_PRI_PROC_BASED_CTLS, val |
             VMCS_PRI_PROC_BASED_CTLS_INT_WINDOW_EXITING);
}

void vmx_clear_int_window_exiting(CPUState *cpu)
{
     uint64_t val;
     val = rvmcs(cpu->hvf_fd, VMCS_PRI_PROC_BASED_CTLS);
     wvmcs(cpu->hvf_fd, VMCS_PRI_PROC_BASED_CTLS, val &
             ~VMCS_PRI_PROC_BASED_CTLS_INT_WINDOW_EXITING);
}

bool hvf_inject_interrupts(CPUState *cpu_state)
{
    X86CPU *x86cpu = X86_CPU(cpu_state);
    CPUX86State *env = &x86cpu->env;

    uint8_t vector;
    uint64_t intr_type;
    bool have_event = true;
    if (env->interrupt_injected != -1) {
        vector = env->interrupt_injected;
        if (env->ins_len) {
            intr_type = VMCS_INTR_T_SWINTR;
        } else {
            intr_type = VMCS_INTR_T_HWINTR;
        }
    } else if (env->exception_nr != -1) {
        vector = env->exception_nr;
        if (vector == EXCP03_INT3 || vector == EXCP04_INTO) {
            intr_type = VMCS_INTR_T_SWEXCEPTION;
        } else {
            intr_type = VMCS_INTR_T_HWEXCEPTION;
        }
    } else if (env->nmi_injected) {
        vector = EXCP02_NMI;
        intr_type = VMCS_INTR_T_NMI;
    } else {
        have_event = false;
    }

    uint64_t info = 0;
    if (have_event) {
        info = vector | intr_type | VMCS_INTR_VALID;
        uint64_t reason = rvmcs(cpu_state->hvf_fd, VMCS_EXIT_REASON);
        if (env->nmi_injected && reason != EXIT_REASON_TASK_SWITCH) {
            vmx_clear_nmi_blocking(cpu_state);
        }

        if (!(env->hflags2 & HF2_NMI_MASK) || intr_type != VMCS_INTR_T_NMI) {
            info &= ~(1 << 12); /* clear undefined bit */
            if (intr_type == VMCS_INTR_T_SWINTR ||
                intr_type == VMCS_INTR_T_SWEXCEPTION) {
                wvmcs(cpu_state->hvf_fd, VMCS_ENTRY_INST_LENGTH, env->ins_len);
            }
            
            if (env->has_error_code) {
                wvmcs(cpu_state->hvf_fd, VMCS_ENTRY_EXCEPTION_ERROR,
                      env->error_code);
                /* Indicate that VMCS_ENTRY_EXCEPTION_ERROR is valid */
                info |= VMCS_INTR_DEL_ERRCODE;
            }
            /*printf("reinject  %lx err %d\n", info, err);*/
            wvmcs(cpu_state->hvf_fd, VMCS_ENTRY_INTR_INFO, info);
        };
    }

    if (cpu_state->interrupt_request & CPU_INTERRUPT_NMI) {
        if (!(env->hflags2 & HF2_NMI_MASK) && !(info & VMCS_INTR_VALID)) {
            cpu_state->interrupt_request &= ~CPU_INTERRUPT_NMI;
            info = VMCS_INTR_VALID | VMCS_INTR_T_NMI | EXCP02_NMI;
            wvmcs(cpu_state->hvf_fd, VMCS_ENTRY_INTR_INFO, info);
        } else {
            vmx_set_nmi_window_exiting(cpu_state);
        }
    }

    if (!(env->hflags & HF_INHIBIT_IRQ_MASK) &&
        (cpu_state->interrupt_request & CPU_INTERRUPT_HARD) &&
        (EFLAGS(env) & IF_MASK) && !(info & VMCS_INTR_VALID)) {
        int line = cpu_get_pic_interrupt(&x86cpu->env);
        cpu_state->interrupt_request &= ~CPU_INTERRUPT_HARD;
        if (line >= 0) {
            wvmcs(cpu_state->hvf_fd, VMCS_ENTRY_INTR_INFO, line |
                  VMCS_INTR_VALID | VMCS_INTR_T_HWINTR);
        }
    }
    if (cpu_state->interrupt_request & CPU_INTERRUPT_HARD) {
        vmx_set_int_window_exiting(cpu_state);
    }
    return (cpu_state->interrupt_request
            & (CPU_INTERRUPT_INIT | CPU_INTERRUPT_TPR));
}

int hvf_process_events(CPUState *cpu_state)
{
    X86CPU *cpu = X86_CPU(cpu_state);
    CPUX86State *env = &cpu->env;

    EFLAGS(env) = rreg(cpu_state->hvf_fd, HV_X86_RFLAGS);

    if (cpu_state->interrupt_request & CPU_INTERRUPT_INIT) {
        hvf_cpu_synchronize_state(cpu_state);
        do_cpu_init(cpu);
    }

    if (cpu_state->interrupt_request & CPU_INTERRUPT_POLL) {
        cpu_state->interrupt_request &= ~CPU_INTERRUPT_POLL;
        apic_poll_irq(cpu->apic_state);
    }
    if (((cpu_state->interrupt_request & CPU_INTERRUPT_HARD) &&
        (EFLAGS(env) & IF_MASK)) ||
        (cpu_state->interrupt_request & CPU_INTERRUPT_NMI)) {
        cpu_state->halted = 0;
    }
    if (cpu_state->interrupt_request & CPU_INTERRUPT_SIPI) {
        hvf_cpu_synchronize_state(cpu_state);
        do_cpu_sipi(cpu);
    }
    if (cpu_state->interrupt_request & CPU_INTERRUPT_TPR) {
        cpu_state->interrupt_request &= ~CPU_INTERRUPT_TPR;
        hvf_cpu_synchronize_state(cpu_state);
        apic_handle_tpr_access_report(cpu->apic_state, env->eip,
                                      env->tpr_access_type);
    }
    return cpu_state->halted;
}

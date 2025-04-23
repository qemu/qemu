/*
 * Copyright (c) 2003-2008 Fabrice Bellard
 * Copyright (C) 2016 Veertu Inc,
 * Copyright (C) 2017 Google Inc,
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
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

#include "x86hvf.h"
#include "vmx.h"
#include "vmcs.h"
#include "cpu.h"
#include "x86_descr.h"
#include "emulate/x86_decode.h"
#include "system/hw_accel.h"

#include "hw/i386/apic_internal.h"

#include <Hypervisor/hv.h>
#include <Hypervisor/hv_vmx.h>

void hvf_set_segment(CPUState *cs, struct vmx_segment *vmx_seg,
                     SegmentCache *qseg, bool is_tr)
{
    vmx_seg->sel = qseg->selector;
    vmx_seg->base = qseg->base;
    vmx_seg->limit = qseg->limit;

    if (!qseg->selector && !x86_is_real(cs) && !is_tr) {
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

void hvf_put_xsave(CPUState *cs)
{
    void *xsave = X86_CPU(cs)->env.xsave_buf;
    uint32_t xsave_len = X86_CPU(cs)->env.xsave_buf_len;

    x86_cpu_xsave_all_areas(X86_CPU(cs), xsave, xsave_len);

    if (hv_vcpu_write_fpstate(cs->accel->fd, xsave, xsave_len)) {
        abort();
    }
}

static void hvf_put_segments(CPUState *cs)
{
    CPUX86State *env = &X86_CPU(cs)->env;
    struct vmx_segment seg;
    
    wvmcs(cs->accel->fd, VMCS_GUEST_IDTR_LIMIT, env->idt.limit);
    wvmcs(cs->accel->fd, VMCS_GUEST_IDTR_BASE, env->idt.base);

    wvmcs(cs->accel->fd, VMCS_GUEST_GDTR_LIMIT, env->gdt.limit);
    wvmcs(cs->accel->fd, VMCS_GUEST_GDTR_BASE, env->gdt.base);

    /* wvmcs(cs->accel->fd, VMCS_GUEST_CR2, env->cr[2]); */
    wvmcs(cs->accel->fd, VMCS_GUEST_CR3, env->cr[3]);
    vmx_update_tpr(cs);
    wvmcs(cs->accel->fd, VMCS_GUEST_IA32_EFER, env->efer);

    macvm_set_cr4(cs->accel->fd, env->cr[4]);
    macvm_set_cr0(cs->accel->fd, env->cr[0]);

    hvf_set_segment(cs, &seg, &env->segs[R_CS], false);
    vmx_write_segment_descriptor(cs, &seg, R_CS);
    
    hvf_set_segment(cs, &seg, &env->segs[R_DS], false);
    vmx_write_segment_descriptor(cs, &seg, R_DS);

    hvf_set_segment(cs, &seg, &env->segs[R_ES], false);
    vmx_write_segment_descriptor(cs, &seg, R_ES);

    hvf_set_segment(cs, &seg, &env->segs[R_SS], false);
    vmx_write_segment_descriptor(cs, &seg, R_SS);

    hvf_set_segment(cs, &seg, &env->segs[R_FS], false);
    vmx_write_segment_descriptor(cs, &seg, R_FS);

    hvf_set_segment(cs, &seg, &env->segs[R_GS], false);
    vmx_write_segment_descriptor(cs, &seg, R_GS);

    hvf_set_segment(cs, &seg, &env->tr, true);
    vmx_write_segment_descriptor(cs, &seg, R_TR);

    hvf_set_segment(cs, &seg, &env->ldt, false);
    vmx_write_segment_descriptor(cs, &seg, R_LDTR);
}
    
void hvf_put_msrs(CPUState *cs)
{
    CPUX86State *env = &X86_CPU(cs)->env;

    hv_vcpu_write_msr(cs->accel->fd, MSR_IA32_SYSENTER_CS,
                      env->sysenter_cs);
    hv_vcpu_write_msr(cs->accel->fd, MSR_IA32_SYSENTER_ESP,
                      env->sysenter_esp);
    hv_vcpu_write_msr(cs->accel->fd, MSR_IA32_SYSENTER_EIP,
                      env->sysenter_eip);

    hv_vcpu_write_msr(cs->accel->fd, MSR_STAR, env->star);

#ifdef TARGET_X86_64
    hv_vcpu_write_msr(cs->accel->fd, MSR_CSTAR, env->cstar);
    hv_vcpu_write_msr(cs->accel->fd, MSR_KERNELGSBASE, env->kernelgsbase);
    hv_vcpu_write_msr(cs->accel->fd, MSR_FMASK, env->fmask);
    hv_vcpu_write_msr(cs->accel->fd, MSR_LSTAR, env->lstar);
#endif

    hv_vcpu_write_msr(cs->accel->fd, MSR_GSBASE, env->segs[R_GS].base);
    hv_vcpu_write_msr(cs->accel->fd, MSR_FSBASE, env->segs[R_FS].base);
}


void hvf_get_xsave(CPUState *cs)
{
    void *xsave = X86_CPU(cs)->env.xsave_buf;
    uint32_t xsave_len = X86_CPU(cs)->env.xsave_buf_len;

    if (hv_vcpu_read_fpstate(cs->accel->fd, xsave, xsave_len)) {
        abort();
    }

    x86_cpu_xrstor_all_areas(X86_CPU(cs), xsave, xsave_len);
}

static void hvf_get_segments(CPUState *cs)
{
    CPUX86State *env = &X86_CPU(cs)->env;

    struct vmx_segment seg;

    env->interrupt_injected = -1;

    vmx_read_segment_descriptor(cs, &seg, R_CS);
    hvf_get_segment(&env->segs[R_CS], &seg);
    
    vmx_read_segment_descriptor(cs, &seg, R_DS);
    hvf_get_segment(&env->segs[R_DS], &seg);

    vmx_read_segment_descriptor(cs, &seg, R_ES);
    hvf_get_segment(&env->segs[R_ES], &seg);

    vmx_read_segment_descriptor(cs, &seg, R_FS);
    hvf_get_segment(&env->segs[R_FS], &seg);

    vmx_read_segment_descriptor(cs, &seg, R_GS);
    hvf_get_segment(&env->segs[R_GS], &seg);

    vmx_read_segment_descriptor(cs, &seg, R_SS);
    hvf_get_segment(&env->segs[R_SS], &seg);

    vmx_read_segment_descriptor(cs, &seg, R_TR);
    hvf_get_segment(&env->tr, &seg);

    vmx_read_segment_descriptor(cs, &seg, R_LDTR);
    hvf_get_segment(&env->ldt, &seg);

    env->idt.limit = rvmcs(cs->accel->fd, VMCS_GUEST_IDTR_LIMIT);
    env->idt.base = rvmcs(cs->accel->fd, VMCS_GUEST_IDTR_BASE);
    env->gdt.limit = rvmcs(cs->accel->fd, VMCS_GUEST_GDTR_LIMIT);
    env->gdt.base = rvmcs(cs->accel->fd, VMCS_GUEST_GDTR_BASE);

    env->cr[0] = rvmcs(cs->accel->fd, VMCS_GUEST_CR0);
    env->cr[2] = 0;
    env->cr[3] = rvmcs(cs->accel->fd, VMCS_GUEST_CR3);
    env->cr[4] = rvmcs(cs->accel->fd, VMCS_GUEST_CR4);
    
    env->efer = rvmcs(cs->accel->fd, VMCS_GUEST_IA32_EFER);
}

void hvf_get_msrs(CPUState *cs)
{
    CPUX86State *env = &X86_CPU(cs)->env;
    uint64_t tmp;
    
    hv_vcpu_read_msr(cs->accel->fd, MSR_IA32_SYSENTER_CS, &tmp);
    env->sysenter_cs = tmp;
    
    hv_vcpu_read_msr(cs->accel->fd, MSR_IA32_SYSENTER_ESP, &tmp);
    env->sysenter_esp = tmp;

    hv_vcpu_read_msr(cs->accel->fd, MSR_IA32_SYSENTER_EIP, &tmp);
    env->sysenter_eip = tmp;

    hv_vcpu_read_msr(cs->accel->fd, MSR_STAR, &env->star);

#ifdef TARGET_X86_64
    hv_vcpu_read_msr(cs->accel->fd, MSR_CSTAR, &env->cstar);
    hv_vcpu_read_msr(cs->accel->fd, MSR_KERNELGSBASE, &env->kernelgsbase);
    hv_vcpu_read_msr(cs->accel->fd, MSR_FMASK, &env->fmask);
    hv_vcpu_read_msr(cs->accel->fd, MSR_LSTAR, &env->lstar);
#endif

    hv_vcpu_read_msr(cs->accel->fd, MSR_IA32_APICBASE, &tmp);
    
    env->tsc = rdtscp() + rvmcs(cs->accel->fd, VMCS_TSC_OFFSET);
}

int hvf_put_registers(CPUState *cs)
{
    X86CPU *x86cpu = X86_CPU(cs);
    CPUX86State *env = &x86cpu->env;

    wreg(cs->accel->fd, HV_X86_RAX, env->regs[R_EAX]);
    wreg(cs->accel->fd, HV_X86_RBX, env->regs[R_EBX]);
    wreg(cs->accel->fd, HV_X86_RCX, env->regs[R_ECX]);
    wreg(cs->accel->fd, HV_X86_RDX, env->regs[R_EDX]);
    wreg(cs->accel->fd, HV_X86_RBP, env->regs[R_EBP]);
    wreg(cs->accel->fd, HV_X86_RSP, env->regs[R_ESP]);
    wreg(cs->accel->fd, HV_X86_RSI, env->regs[R_ESI]);
    wreg(cs->accel->fd, HV_X86_RDI, env->regs[R_EDI]);
    wreg(cs->accel->fd, HV_X86_R8, env->regs[8]);
    wreg(cs->accel->fd, HV_X86_R9, env->regs[9]);
    wreg(cs->accel->fd, HV_X86_R10, env->regs[10]);
    wreg(cs->accel->fd, HV_X86_R11, env->regs[11]);
    wreg(cs->accel->fd, HV_X86_R12, env->regs[12]);
    wreg(cs->accel->fd, HV_X86_R13, env->regs[13]);
    wreg(cs->accel->fd, HV_X86_R14, env->regs[14]);
    wreg(cs->accel->fd, HV_X86_R15, env->regs[15]);
    wreg(cs->accel->fd, HV_X86_RFLAGS, env->eflags);
    wreg(cs->accel->fd, HV_X86_RIP, env->eip);
   
    wreg(cs->accel->fd, HV_X86_XCR0, env->xcr0);
    
    hvf_put_xsave(cs);
    
    hvf_put_segments(cs);
    
    hvf_put_msrs(cs);
    
    wreg(cs->accel->fd, HV_X86_DR0, env->dr[0]);
    wreg(cs->accel->fd, HV_X86_DR1, env->dr[1]);
    wreg(cs->accel->fd, HV_X86_DR2, env->dr[2]);
    wreg(cs->accel->fd, HV_X86_DR3, env->dr[3]);
    wreg(cs->accel->fd, HV_X86_DR4, env->dr[4]);
    wreg(cs->accel->fd, HV_X86_DR5, env->dr[5]);
    wreg(cs->accel->fd, HV_X86_DR6, env->dr[6]);
    wreg(cs->accel->fd, HV_X86_DR7, env->dr[7]);
    
    return 0;
}

int hvf_get_registers(CPUState *cs)
{
    X86CPU *x86cpu = X86_CPU(cs);
    CPUX86State *env = &x86cpu->env;

    env->regs[R_EAX] = rreg(cs->accel->fd, HV_X86_RAX);
    env->regs[R_EBX] = rreg(cs->accel->fd, HV_X86_RBX);
    env->regs[R_ECX] = rreg(cs->accel->fd, HV_X86_RCX);
    env->regs[R_EDX] = rreg(cs->accel->fd, HV_X86_RDX);
    env->regs[R_EBP] = rreg(cs->accel->fd, HV_X86_RBP);
    env->regs[R_ESP] = rreg(cs->accel->fd, HV_X86_RSP);
    env->regs[R_ESI] = rreg(cs->accel->fd, HV_X86_RSI);
    env->regs[R_EDI] = rreg(cs->accel->fd, HV_X86_RDI);
    env->regs[8] = rreg(cs->accel->fd, HV_X86_R8);
    env->regs[9] = rreg(cs->accel->fd, HV_X86_R9);
    env->regs[10] = rreg(cs->accel->fd, HV_X86_R10);
    env->regs[11] = rreg(cs->accel->fd, HV_X86_R11);
    env->regs[12] = rreg(cs->accel->fd, HV_X86_R12);
    env->regs[13] = rreg(cs->accel->fd, HV_X86_R13);
    env->regs[14] = rreg(cs->accel->fd, HV_X86_R14);
    env->regs[15] = rreg(cs->accel->fd, HV_X86_R15);
    
    env->eflags = rreg(cs->accel->fd, HV_X86_RFLAGS);
    env->eip = rreg(cs->accel->fd, HV_X86_RIP);
   
    hvf_get_xsave(cs);
    env->xcr0 = rreg(cs->accel->fd, HV_X86_XCR0);
    
    hvf_get_segments(cs);
    hvf_get_msrs(cs);
    
    env->dr[0] = rreg(cs->accel->fd, HV_X86_DR0);
    env->dr[1] = rreg(cs->accel->fd, HV_X86_DR1);
    env->dr[2] = rreg(cs->accel->fd, HV_X86_DR2);
    env->dr[3] = rreg(cs->accel->fd, HV_X86_DR3);
    env->dr[4] = rreg(cs->accel->fd, HV_X86_DR4);
    env->dr[5] = rreg(cs->accel->fd, HV_X86_DR5);
    env->dr[6] = rreg(cs->accel->fd, HV_X86_DR6);
    env->dr[7] = rreg(cs->accel->fd, HV_X86_DR7);
    
    x86_update_hflags(env);
    return 0;
}

static void vmx_set_int_window_exiting(CPUState *cs)
{
     uint64_t val;
     val = rvmcs(cs->accel->fd, VMCS_PRI_PROC_BASED_CTLS);
     wvmcs(cs->accel->fd, VMCS_PRI_PROC_BASED_CTLS, val |
             VMCS_PRI_PROC_BASED_CTLS_INT_WINDOW_EXITING);
}

void vmx_clear_int_window_exiting(CPUState *cs)
{
     uint64_t val;
     val = rvmcs(cs->accel->fd, VMCS_PRI_PROC_BASED_CTLS);
     wvmcs(cs->accel->fd, VMCS_PRI_PROC_BASED_CTLS, val &
             ~VMCS_PRI_PROC_BASED_CTLS_INT_WINDOW_EXITING);
}

bool hvf_inject_interrupts(CPUState *cs)
{
    X86CPU *x86cpu = X86_CPU(cs);
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
        uint64_t reason = rvmcs(cs->accel->fd, VMCS_EXIT_REASON);
        if (env->nmi_injected && reason != EXIT_REASON_TASK_SWITCH) {
            vmx_clear_nmi_blocking(cs);
        }

        if (!(env->hflags2 & HF2_NMI_MASK) || intr_type != VMCS_INTR_T_NMI) {
            info &= ~(1 << 12); /* clear undefined bit */
            if (intr_type == VMCS_INTR_T_SWINTR ||
                intr_type == VMCS_INTR_T_SWEXCEPTION) {
                wvmcs(cs->accel->fd, VMCS_ENTRY_INST_LENGTH, env->ins_len);
            }
            
            if (env->has_error_code) {
                wvmcs(cs->accel->fd, VMCS_ENTRY_EXCEPTION_ERROR,
                      env->error_code);
                /* Indicate that VMCS_ENTRY_EXCEPTION_ERROR is valid */
                info |= VMCS_INTR_DEL_ERRCODE;
            }
            /*printf("reinject  %lx err %d\n", info, err);*/
            wvmcs(cs->accel->fd, VMCS_ENTRY_INTR_INFO, info);
        };
    }

    if (cs->interrupt_request & CPU_INTERRUPT_NMI) {
        if (!(env->hflags2 & HF2_NMI_MASK) && !(info & VMCS_INTR_VALID)) {
            cs->interrupt_request &= ~CPU_INTERRUPT_NMI;
            info = VMCS_INTR_VALID | VMCS_INTR_T_NMI | EXCP02_NMI;
            wvmcs(cs->accel->fd, VMCS_ENTRY_INTR_INFO, info);
        } else {
            vmx_set_nmi_window_exiting(cs);
        }
    }

    if (!(env->hflags & HF_INHIBIT_IRQ_MASK) &&
        (cs->interrupt_request & CPU_INTERRUPT_HARD) &&
        (env->eflags & IF_MASK) && !(info & VMCS_INTR_VALID)) {
        int line = cpu_get_pic_interrupt(env);
        cs->interrupt_request &= ~CPU_INTERRUPT_HARD;
        if (line >= 0) {
            wvmcs(cs->accel->fd, VMCS_ENTRY_INTR_INFO, line |
                  VMCS_INTR_VALID | VMCS_INTR_T_HWINTR);
        }
    }
    if (cs->interrupt_request & CPU_INTERRUPT_HARD) {
        vmx_set_int_window_exiting(cs);
    }
    return (cs->interrupt_request
            & (CPU_INTERRUPT_INIT | CPU_INTERRUPT_TPR));
}

int hvf_process_events(CPUState *cs)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;

    if (!cs->accel->dirty) {
        /* light weight sync for CPU_INTERRUPT_HARD and IF_MASK */
        env->eflags = rreg(cs->accel->fd, HV_X86_RFLAGS);
    }

    if (cs->interrupt_request & CPU_INTERRUPT_INIT) {
        cpu_synchronize_state(cs);
        do_cpu_init(cpu);
    }

    if (cs->interrupt_request & CPU_INTERRUPT_POLL) {
        cs->interrupt_request &= ~CPU_INTERRUPT_POLL;
        apic_poll_irq(cpu->apic_state);
    }
    if (((cs->interrupt_request & CPU_INTERRUPT_HARD) &&
        (env->eflags & IF_MASK)) ||
        (cs->interrupt_request & CPU_INTERRUPT_NMI)) {
        cs->halted = 0;
    }
    if (cs->interrupt_request & CPU_INTERRUPT_SIPI) {
        cpu_synchronize_state(cs);
        do_cpu_sipi(cpu);
    }
    if (cs->interrupt_request & CPU_INTERRUPT_TPR) {
        cs->interrupt_request &= ~CPU_INTERRUPT_TPR;
        cpu_synchronize_state(cs);
        apic_handle_tpr_access_report(cpu->apic_state, env->eip,
                                      env->tpr_access_type);
    }
    return cs->halted;
}

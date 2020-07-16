/*
 * Copyright (C) 2016 Veertu Inc,
 * Copyright (C) 2017 Google Inc,
 * Based on Veertu vddh/vmm/vmx.h
 *
 * Interfaces to Hypervisor.framework to read/write X86 registers and VMCS.
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
 *
 * This file contain code under public domain from the hvdos project:
 * https://github.com/mist64/hvdos
 */

#ifndef VMX_H
#define VMX_H

#include <Hypervisor/hv.h>
#include <Hypervisor/hv_vmx.h>
#include "vmcs.h"
#include "cpu.h"
#include "x86.h"

#include "exec/address-spaces.h"

static inline uint64_t rreg(hv_vcpuid_t vcpu, hv_x86_reg_t reg)
{
    uint64_t v;

    if (hv_vcpu_read_register(vcpu, reg, &v)) {
        abort();
    }

    return v;
}

/* write GPR */
static inline void wreg(hv_vcpuid_t vcpu, hv_x86_reg_t reg, uint64_t v)
{
    if (hv_vcpu_write_register(vcpu, reg, v)) {
        abort();
    }
}

/* read VMCS field */
static inline uint64_t rvmcs(hv_vcpuid_t vcpu, uint32_t field)
{
    uint64_t v;

    hv_vmx_vcpu_read_vmcs(vcpu, field, &v);

    return v;
}

/* write VMCS field */
static inline void wvmcs(hv_vcpuid_t vcpu, uint32_t field, uint64_t v)
{
    hv_vmx_vcpu_write_vmcs(vcpu, field, v);
}

/* desired control word constrained by hardware/hypervisor capabilities */
static inline uint64_t cap2ctrl(uint64_t cap, uint64_t ctrl)
{
    return (ctrl | (cap & 0xffffffff)) & (cap >> 32);
}

#define VM_ENTRY_GUEST_LMA (1LL << 9)

#define AR_TYPE_ACCESSES_MASK 1
#define AR_TYPE_READABLE_MASK (1 << 1)
#define AR_TYPE_WRITEABLE_MASK (1 << 2)
#define AR_TYPE_CODE_MASK (1 << 3)
#define AR_TYPE_MASK 0x0f
#define AR_TYPE_BUSY_64_TSS 11
#define AR_TYPE_BUSY_32_TSS 11
#define AR_TYPE_BUSY_16_TSS 3
#define AR_TYPE_LDT 2

static void enter_long_mode(hv_vcpuid_t vcpu, uint64_t cr0, uint64_t efer)
{
    uint64_t entry_ctls;

    efer |= MSR_EFER_LMA;
    wvmcs(vcpu, VMCS_GUEST_IA32_EFER, efer);
    entry_ctls = rvmcs(vcpu, VMCS_ENTRY_CTLS);
    wvmcs(vcpu, VMCS_ENTRY_CTLS, rvmcs(vcpu, VMCS_ENTRY_CTLS) |
          VM_ENTRY_GUEST_LMA);

    uint64_t guest_tr_ar = rvmcs(vcpu, VMCS_GUEST_TR_ACCESS_RIGHTS);
    if ((efer & MSR_EFER_LME) &&
        (guest_tr_ar & AR_TYPE_MASK) != AR_TYPE_BUSY_64_TSS) {
        wvmcs(vcpu, VMCS_GUEST_TR_ACCESS_RIGHTS,
              (guest_tr_ar & ~AR_TYPE_MASK) | AR_TYPE_BUSY_64_TSS);
    }
}

static void exit_long_mode(hv_vcpuid_t vcpu, uint64_t cr0, uint64_t efer)
{
    uint64_t entry_ctls;

    entry_ctls = rvmcs(vcpu, VMCS_ENTRY_CTLS);
    wvmcs(vcpu, VMCS_ENTRY_CTLS, entry_ctls & ~VM_ENTRY_GUEST_LMA);

    efer &= ~MSR_EFER_LMA;
    wvmcs(vcpu, VMCS_GUEST_IA32_EFER, efer);
}

static inline void macvm_set_cr0(hv_vcpuid_t vcpu, uint64_t cr0)
{
    int i;
    uint64_t pdpte[4] = {0, 0, 0, 0};
    uint64_t efer = rvmcs(vcpu, VMCS_GUEST_IA32_EFER);
    uint64_t old_cr0 = rvmcs(vcpu, VMCS_GUEST_CR0);
    uint64_t changed_cr0 = old_cr0 ^ cr0;
    uint64_t mask = CR0_PG | CR0_CD | CR0_NW | CR0_NE | CR0_ET;
    uint64_t entry_ctls;

    if ((cr0 & CR0_PG) && (rvmcs(vcpu, VMCS_GUEST_CR4) & CR4_PAE) &&
        !(efer & MSR_EFER_LME)) {
        address_space_read(&address_space_memory,
                           rvmcs(vcpu, VMCS_GUEST_CR3) & ~0x1f,
                           MEMTXATTRS_UNSPECIFIED, pdpte, 32);
        /* Only set PDPTE when appropriate. */
        for (i = 0; i < 4; i++) {
            wvmcs(vcpu, VMCS_GUEST_PDPTE0 + i * 2, pdpte[i]);
        }
    }

    wvmcs(vcpu, VMCS_CR0_MASK, mask);
    wvmcs(vcpu, VMCS_CR0_SHADOW, cr0);

    if (efer & MSR_EFER_LME) {
        if (changed_cr0 & CR0_PG) {
            if (cr0 & CR0_PG) {
                enter_long_mode(vcpu, cr0, efer);
            } else {
                exit_long_mode(vcpu, cr0, efer);
            }
        }
    } else {
        entry_ctls = rvmcs(vcpu, VMCS_ENTRY_CTLS);
        wvmcs(vcpu, VMCS_ENTRY_CTLS, entry_ctls & ~VM_ENTRY_GUEST_LMA);
    }

    /* Filter new CR0 after we are finished examining it above. */
    cr0 = (cr0 & ~(mask & ~CR0_PG));
    wvmcs(vcpu, VMCS_GUEST_CR0, cr0 | CR0_NE | CR0_ET);

    hv_vcpu_invalidate_tlb(vcpu);
    hv_vcpu_flush(vcpu);
}

static inline void macvm_set_cr4(hv_vcpuid_t vcpu, uint64_t cr4)
{
    uint64_t guest_cr4 = cr4 | CR4_VMXE;

    wvmcs(vcpu, VMCS_GUEST_CR4, guest_cr4);
    wvmcs(vcpu, VMCS_CR4_SHADOW, cr4);
    wvmcs(vcpu, VMCS_CR4_MASK, CR4_VMXE);

    hv_vcpu_invalidate_tlb(vcpu);
    hv_vcpu_flush(vcpu);
}

static inline void macvm_set_rip(CPUState *cpu, uint64_t rip)
{
    X86CPU *x86_cpu = X86_CPU(cpu);
    CPUX86State *env = &x86_cpu->env;
    uint64_t val;

    /* BUG, should take considering overlap.. */
    wreg(cpu->hvf_fd, HV_X86_RIP, rip);
    env->eip = rip;

    /* after moving forward in rip, we need to clean INTERRUPTABILITY */
   val = rvmcs(cpu->hvf_fd, VMCS_GUEST_INTERRUPTIBILITY);
   if (val & (VMCS_INTERRUPTIBILITY_STI_BLOCKING |
               VMCS_INTERRUPTIBILITY_MOVSS_BLOCKING)) {
        env->hflags &= ~HF_INHIBIT_IRQ_MASK;
        wvmcs(cpu->hvf_fd, VMCS_GUEST_INTERRUPTIBILITY,
               val & ~(VMCS_INTERRUPTIBILITY_STI_BLOCKING |
               VMCS_INTERRUPTIBILITY_MOVSS_BLOCKING));
   }
}

static inline void vmx_clear_nmi_blocking(CPUState *cpu)
{
    X86CPU *x86_cpu = X86_CPU(cpu);
    CPUX86State *env = &x86_cpu->env;

    env->hflags2 &= ~HF2_NMI_MASK;
    uint32_t gi = (uint32_t) rvmcs(cpu->hvf_fd, VMCS_GUEST_INTERRUPTIBILITY);
    gi &= ~VMCS_INTERRUPTIBILITY_NMI_BLOCKING;
    wvmcs(cpu->hvf_fd, VMCS_GUEST_INTERRUPTIBILITY, gi);
}

static inline void vmx_set_nmi_blocking(CPUState *cpu)
{
    X86CPU *x86_cpu = X86_CPU(cpu);
    CPUX86State *env = &x86_cpu->env;

    env->hflags2 |= HF2_NMI_MASK;
    uint32_t gi = (uint32_t)rvmcs(cpu->hvf_fd, VMCS_GUEST_INTERRUPTIBILITY);
    gi |= VMCS_INTERRUPTIBILITY_NMI_BLOCKING;
    wvmcs(cpu->hvf_fd, VMCS_GUEST_INTERRUPTIBILITY, gi);
}

static inline void vmx_set_nmi_window_exiting(CPUState *cpu)
{
    uint64_t val;
    val = rvmcs(cpu->hvf_fd, VMCS_PRI_PROC_BASED_CTLS);
    wvmcs(cpu->hvf_fd, VMCS_PRI_PROC_BASED_CTLS, val |
          VMCS_PRI_PROC_BASED_CTLS_NMI_WINDOW_EXITING);

}

static inline void vmx_clear_nmi_window_exiting(CPUState *cpu)
{

    uint64_t val;
    val = rvmcs(cpu->hvf_fd, VMCS_PRI_PROC_BASED_CTLS);
    wvmcs(cpu->hvf_fd, VMCS_PRI_PROC_BASED_CTLS, val &
          ~VMCS_PRI_PROC_BASED_CTLS_NMI_WINDOW_EXITING);
}

#endif

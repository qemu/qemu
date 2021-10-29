/*
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

#include "cpu.h"
#include "qemu-common.h"
#include "x86_decode.h"
#include "x86_emu.h"
#include "vmcs.h"
#include "vmx.h"
#include "x86_mmu.h"
#include "x86_descr.h"

/* static uint32_t x86_segment_access_rights(struct x86_segment_descriptor *var)
{
   uint32_t ar;

   if (!var->p) {
       ar = 1 << 16;
       return ar;
   }

   ar = var->type & 15;
   ar |= (var->s & 1) << 4;
   ar |= (var->dpl & 3) << 5;
   ar |= (var->p & 1) << 7;
   ar |= (var->avl & 1) << 12;
   ar |= (var->l & 1) << 13;
   ar |= (var->db & 1) << 14;
   ar |= (var->g & 1) << 15;
   return ar;
}*/

bool x86_read_segment_descriptor(struct CPUState *cpu,
                                 struct x86_segment_descriptor *desc,
                                 x68_segment_selector sel)
{
    target_ulong base;
    uint32_t limit;

    memset(desc, 0, sizeof(*desc));

    /* valid gdt descriptors start from index 1 */
    if (!sel.index && GDT_SEL == sel.ti) {
        return false;
    }

    if (GDT_SEL == sel.ti) {
        base  = rvmcs(cpu->hvf->fd, VMCS_GUEST_GDTR_BASE);
        limit = rvmcs(cpu->hvf->fd, VMCS_GUEST_GDTR_LIMIT);
    } else {
        base  = rvmcs(cpu->hvf->fd, VMCS_GUEST_LDTR_BASE);
        limit = rvmcs(cpu->hvf->fd, VMCS_GUEST_LDTR_LIMIT);
    }

    if (sel.index * 8 >= limit) {
        return false;
    }

    vmx_read_mem(cpu, desc, base + sel.index * 8, sizeof(*desc));
    return true;
}

bool x86_write_segment_descriptor(struct CPUState *cpu,
                                  struct x86_segment_descriptor *desc,
                                  x68_segment_selector sel)
{
    target_ulong base;
    uint32_t limit;
    
    if (GDT_SEL == sel.ti) {
        base  = rvmcs(cpu->hvf->fd, VMCS_GUEST_GDTR_BASE);
        limit = rvmcs(cpu->hvf->fd, VMCS_GUEST_GDTR_LIMIT);
    } else {
        base  = rvmcs(cpu->hvf->fd, VMCS_GUEST_LDTR_BASE);
        limit = rvmcs(cpu->hvf->fd, VMCS_GUEST_LDTR_LIMIT);
    }
    
    if (sel.index * 8 >= limit) {
        printf("%s: gdt limit\n", __func__);
        return false;
    }
    vmx_write_mem(cpu, base + sel.index * 8, desc, sizeof(*desc));
    return true;
}

bool x86_read_call_gate(struct CPUState *cpu, struct x86_call_gate *idt_desc,
                        int gate)
{
    target_ulong base  = rvmcs(cpu->hvf->fd, VMCS_GUEST_IDTR_BASE);
    uint32_t limit = rvmcs(cpu->hvf->fd, VMCS_GUEST_IDTR_LIMIT);

    memset(idt_desc, 0, sizeof(*idt_desc));
    if (gate * 8 >= limit) {
        printf("%s: idt limit\n", __func__);
        return false;
    }

    vmx_read_mem(cpu, idt_desc, base + gate * 8, sizeof(*idt_desc));
    return true;
}

bool x86_is_protected(struct CPUState *cpu)
{
    uint64_t cr0 = rvmcs(cpu->hvf->fd, VMCS_GUEST_CR0);
    return cr0 & CR0_PE_MASK;
}

bool x86_is_real(struct CPUState *cpu)
{
    return !x86_is_protected(cpu);
}

bool x86_is_v8086(struct CPUState *cpu)
{
    X86CPU *x86_cpu = X86_CPU(cpu);
    CPUX86State *env = &x86_cpu->env;
    return x86_is_protected(cpu) && (env->eflags & VM_MASK);
}

bool x86_is_long_mode(struct CPUState *cpu)
{
    return rvmcs(cpu->hvf->fd, VMCS_GUEST_IA32_EFER) & MSR_EFER_LMA;
}

bool x86_is_long64_mode(struct CPUState *cpu)
{
    struct vmx_segment desc;
    vmx_read_segment_descriptor(cpu, &desc, R_CS);

    return x86_is_long_mode(cpu) && ((desc.ar >> 13) & 1);
}

bool x86_is_paging_mode(struct CPUState *cpu)
{
    uint64_t cr0 = rvmcs(cpu->hvf->fd, VMCS_GUEST_CR0);
    return cr0 & CR0_PG_MASK;
}

bool x86_is_pae_enabled(struct CPUState *cpu)
{
    uint64_t cr4 = rvmcs(cpu->hvf->fd, VMCS_GUEST_CR4);
    return cr4 & CR4_PAE_MASK;
}

target_ulong linear_addr(struct CPUState *cpu, target_ulong addr, X86Seg seg)
{
    return vmx_read_segment_base(cpu, seg) + addr;
}

target_ulong linear_addr_size(struct CPUState *cpu, target_ulong addr, int size,
                              X86Seg seg)
{
    switch (size) {
    case 2:
        addr = (uint16_t)addr;
        break;
    case 4:
        addr = (uint32_t)addr;
        break;
    default:
        break;
    }
    return linear_addr(cpu, addr, seg);
}

target_ulong linear_rip(struct CPUState *cpu, target_ulong rip)
{
    return linear_addr(cpu, rip, R_CS);
}

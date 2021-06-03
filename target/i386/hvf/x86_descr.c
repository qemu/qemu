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

#include "qemu-common.h"
#include "vmx.h"
#include "x86_descr.h"

#define VMX_SEGMENT_FIELD(seg)                        \
    [R_##seg] = {                                     \
        .selector = VMCS_GUEST_##seg##_SELECTOR,      \
        .base = VMCS_GUEST_##seg##_BASE,              \
        .limit = VMCS_GUEST_##seg##_LIMIT,            \
        .ar_bytes = VMCS_GUEST_##seg##_ACCESS_RIGHTS, \
}

static const struct vmx_segment_field {
    int selector;
    int base;
    int limit;
    int ar_bytes;
} vmx_segment_fields[] = {
    VMX_SEGMENT_FIELD(ES),
    VMX_SEGMENT_FIELD(CS),
    VMX_SEGMENT_FIELD(SS),
    VMX_SEGMENT_FIELD(DS),
    VMX_SEGMENT_FIELD(FS),
    VMX_SEGMENT_FIELD(GS),
    VMX_SEGMENT_FIELD(LDTR),
    VMX_SEGMENT_FIELD(TR),
};

uint32_t vmx_read_segment_limit(CPUState *cpu, X86Seg seg)
{
    return (uint32_t)rvmcs(cpu->hvf->fd, vmx_segment_fields[seg].limit);
}

uint32_t vmx_read_segment_ar(CPUState *cpu, X86Seg seg)
{
    return (uint32_t)rvmcs(cpu->hvf->fd, vmx_segment_fields[seg].ar_bytes);
}

uint64_t vmx_read_segment_base(CPUState *cpu, X86Seg seg)
{
    return rvmcs(cpu->hvf->fd, vmx_segment_fields[seg].base);
}

x68_segment_selector vmx_read_segment_selector(CPUState *cpu, X86Seg seg)
{
    x68_segment_selector sel;
    sel.sel = rvmcs(cpu->hvf->fd, vmx_segment_fields[seg].selector);
    return sel;
}

void vmx_write_segment_selector(struct CPUState *cpu, x68_segment_selector selector, X86Seg seg)
{
    wvmcs(cpu->hvf->fd, vmx_segment_fields[seg].selector, selector.sel);
}

void vmx_read_segment_descriptor(struct CPUState *cpu, struct vmx_segment *desc, X86Seg seg)
{
    desc->sel = rvmcs(cpu->hvf->fd, vmx_segment_fields[seg].selector);
    desc->base = rvmcs(cpu->hvf->fd, vmx_segment_fields[seg].base);
    desc->limit = rvmcs(cpu->hvf->fd, vmx_segment_fields[seg].limit);
    desc->ar = rvmcs(cpu->hvf->fd, vmx_segment_fields[seg].ar_bytes);
}

void vmx_write_segment_descriptor(CPUState *cpu, struct vmx_segment *desc, X86Seg seg)
{
    const struct vmx_segment_field *sf = &vmx_segment_fields[seg];

    wvmcs(cpu->hvf->fd, sf->base, desc->base);
    wvmcs(cpu->hvf->fd, sf->limit, desc->limit);
    wvmcs(cpu->hvf->fd, sf->selector, desc->sel);
    wvmcs(cpu->hvf->fd, sf->ar_bytes, desc->ar);
}

void x86_segment_descriptor_to_vmx(struct CPUState *cpu, x68_segment_selector selector, struct x86_segment_descriptor *desc, struct vmx_segment *vmx_desc)
{
    vmx_desc->sel = selector.sel;
    vmx_desc->base = x86_segment_base(desc);
    vmx_desc->limit = x86_segment_limit(desc);

    vmx_desc->ar = (selector.sel ? 0 : 1) << 16 |
                    desc->g << 15 |
                    desc->db << 14 |
                    desc->l << 13 |
                    desc->avl << 12 |
                    desc->p << 7 |
                    desc->dpl << 5 |
                    desc->s << 4 |
                    desc->type;
}

void vmx_segment_to_x86_descriptor(struct CPUState *cpu, struct vmx_segment *vmx_desc, struct x86_segment_descriptor *desc)
{
    x86_set_segment_limit(desc, vmx_desc->limit);
    x86_set_segment_base(desc, vmx_desc->base);
    
    desc->type = vmx_desc->ar & 15;
    desc->s = (vmx_desc->ar >> 4) & 1;
    desc->dpl = (vmx_desc->ar >> 5) & 3;
    desc->p = (vmx_desc->ar >> 7) & 1;
    desc->avl = (vmx_desc->ar >> 12) & 1;
    desc->l = (vmx_desc->ar >> 13) & 1;
    desc->db = (vmx_desc->ar >> 14) & 1;
    desc->g = (vmx_desc->ar >> 15) & 1;
}


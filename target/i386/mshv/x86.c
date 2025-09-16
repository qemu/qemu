/*
 * QEMU MSHV support
 *
 * Copyright Microsoft, Corp. 2025
 *
 * Authors: Magnus Kulke <magnuskulke@microsoft.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "cpu.h"
#include "emulate/x86_decode.h"
#include "emulate/x86_emu.h"
#include "qemu/typedefs.h"
#include "qemu/error-report.h"
#include "system/mshv.h"

/* RW or Exec segment */
static const uint8_t RWRX_SEGMENT_TYPE        = 0x2;
static const uint8_t CODE_SEGMENT_TYPE        = 0x8;
static const uint8_t EXPAND_DOWN_SEGMENT_TYPE = 0x4;

typedef enum CpuMode {
    REAL_MODE,
    PROTECTED_MODE,
    LONG_MODE,
} CpuMode;

static CpuMode cpu_mode(CPUState *cpu)
{
    enum CpuMode m = REAL_MODE;

    if (x86_is_protected(cpu)) {
        m = PROTECTED_MODE;

        if (x86_is_long_mode(cpu)) {
            m = LONG_MODE;
        }
    }

    return m;
}

static bool segment_type_ro(const SegmentCache *seg)
{
    uint32_t type_ = (seg->flags >> DESC_TYPE_SHIFT) & 15;
    return (type_ & (~RWRX_SEGMENT_TYPE)) == 0;
}

static bool segment_type_code(const SegmentCache *seg)
{
    uint32_t type_ = (seg->flags >> DESC_TYPE_SHIFT) & 15;
    return (type_ & CODE_SEGMENT_TYPE) != 0;
}

static bool segment_expands_down(const SegmentCache *seg)
{
    uint32_t type_ = (seg->flags >> DESC_TYPE_SHIFT) & 15;

    if (segment_type_code(seg)) {
        return false;
    }

    return (type_ & EXPAND_DOWN_SEGMENT_TYPE) != 0;
}

static uint32_t segment_limit(const SegmentCache *seg)
{
    uint32_t limit = seg->limit;
    uint32_t granularity = (seg->flags & DESC_G_MASK) != 0;

    if (granularity != 0) {
        limit = (limit << 12) | 0xFFF;
    }

    return limit;
}

static uint8_t segment_db(const SegmentCache *seg)
{
    return (seg->flags >> DESC_B_SHIFT) & 1;
}

static uint32_t segment_max_limit(const SegmentCache *seg)
{
    if (segment_db(seg) != 0) {
        return 0xFFFFFFFF;
    }
    return 0xFFFF;
}

static int linearize(CPUState *cpu,
                     target_ulong logical_addr, target_ulong *linear_addr,
                     X86Seg seg_idx)
{
    enum CpuMode mode;
    X86CPU *x86_cpu = X86_CPU(cpu);
    CPUX86State *env = &x86_cpu->env;
    SegmentCache *seg = &env->segs[seg_idx];
    target_ulong base = seg->base;
    target_ulong logical_addr_32b;
    uint32_t limit;
    /* TODO: the emulator will not pass us "write" indicator yet */
    bool write = false;

    mode = cpu_mode(cpu);

    switch (mode) {
    case LONG_MODE:
        if (__builtin_add_overflow(logical_addr, base, linear_addr)) {
            error_report("Address overflow");
            return -1;
        }
        break;
    case PROTECTED_MODE:
    case REAL_MODE:
        if (segment_type_ro(seg) && write) {
            error_report("Cannot write to read-only segment");
            return -1;
        }

        logical_addr_32b = logical_addr & 0xFFFFFFFF;
        limit = segment_limit(seg);

        if (segment_expands_down(seg)) {
            if (logical_addr_32b >= limit) {
                error_report("Address exceeds limit (expands down)");
                return -1;
            }

            limit = segment_max_limit(seg);
        }

        if (logical_addr_32b > limit) {
            error_report("Address exceeds limit %u", limit);
            return -1;
        }
        *linear_addr = logical_addr_32b + base;
        break;
    default:
        error_report("Unknown cpu mode: %d", mode);
        return -1;
    }

    return 0;
}

bool x86_read_segment_descriptor(CPUState *cpu,
                                 struct x86_segment_descriptor *desc,
                                 x86_segment_selector sel)
{
    target_ulong base;
    uint32_t limit;
    X86CPU *x86_cpu = X86_CPU(cpu);
    CPUX86State *env = &x86_cpu->env;
    target_ulong gva;

    memset(desc, 0, sizeof(*desc));

    /* valid gdt descriptors start from index 1 */
    if (!sel.index && GDT_SEL == sel.ti) {
        return false;
    }

    if (GDT_SEL == sel.ti) {
        base = env->gdt.base;
        limit = env->gdt.limit;
    } else {
        base = env->ldt.base;
        limit = env->ldt.limit;
    }

    if (sel.index * 8 >= limit) {
        return false;
    }

    gva = base + sel.index * 8;
    emul_ops->read_mem(cpu, desc, gva, sizeof(*desc));

    return true;
}

bool x86_read_call_gate(CPUState *cpu, struct x86_call_gate *idt_desc,
                        int gate)
{
    target_ulong base;
    uint32_t limit;
    X86CPU *x86_cpu = X86_CPU(cpu);
    CPUX86State *env = &x86_cpu->env;
    target_ulong gva;

    base = env->idt.base;
    limit = env->idt.limit;

    memset(idt_desc, 0, sizeof(*idt_desc));
    if (gate * 8 >= limit) {
        perror("call gate exceeds idt limit");
        return false;
    }

    gva = base + gate * 8;
    emul_ops->read_mem(cpu, idt_desc, gva, sizeof(*idt_desc));

    return true;
}

bool x86_is_protected(CPUState *cpu)
{
    X86CPU *x86_cpu = X86_CPU(cpu);
    CPUX86State *env = &x86_cpu->env;
    uint64_t cr0 = env->cr[0];

    return cr0 & CR0_PE_MASK;
}

bool x86_is_real(CPUState *cpu)
{
    return !x86_is_protected(cpu);
}

bool x86_is_v8086(CPUState *cpu)
{
    X86CPU *x86_cpu = X86_CPU(cpu);
    CPUX86State *env = &x86_cpu->env;
    return x86_is_protected(cpu) && (env->eflags & VM_MASK);
}

bool x86_is_long_mode(CPUState *cpu)
{
    X86CPU *x86_cpu = X86_CPU(cpu);
    CPUX86State *env = &x86_cpu->env;
    uint64_t efer = env->efer;
    uint64_t lme_lma = (MSR_EFER_LME | MSR_EFER_LMA);

    return ((efer & lme_lma) == lme_lma);
}

bool x86_is_long64_mode(CPUState *cpu)
{
    error_report("unimplemented: is_long64_mode()");
    abort();
}

bool x86_is_paging_mode(CPUState *cpu)
{
    X86CPU *x86_cpu = X86_CPU(cpu);
    CPUX86State *env = &x86_cpu->env;
    uint64_t cr0 = env->cr[0];

    return cr0 & CR0_PG_MASK;
}

bool x86_is_pae_enabled(CPUState *cpu)
{
    X86CPU *x86_cpu = X86_CPU(cpu);
    CPUX86State *env = &x86_cpu->env;
    uint64_t cr4 = env->cr[4];

    return cr4 & CR4_PAE_MASK;
}

target_ulong linear_addr(CPUState *cpu, target_ulong addr, X86Seg seg)
{
    int ret;
    target_ulong linear_addr;

    ret = linearize(cpu, addr, &linear_addr, seg);
    if (ret < 0) {
        error_report("failed to linearize address");
        abort();
    }

    return linear_addr;
}

target_ulong linear_addr_size(CPUState *cpu, target_ulong addr, int size,
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

target_ulong linear_rip(CPUState *cpu, target_ulong rip)
{
    return linear_addr(cpu, rip, R_CS);
}

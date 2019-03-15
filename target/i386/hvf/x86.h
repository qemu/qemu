/*
 * Copyright (C) 2016 Veertu Inc,
 * Copyright (C) 2017 Veertu Inc,
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

#ifndef HVF_X86_H
#define HVF_X86_H

typedef struct x86_register {
    union {
        struct {
            uint64_t rrx;               /* full 64 bit */
        };
        struct {
            uint32_t erx;               /* low 32 bit part */
            uint32_t hi32_unused1;
        };
        struct {
            uint16_t rx;                /* low 16 bit part */
            uint16_t hi16_unused1;
            uint32_t hi32_unused2;
        };
        struct {
            uint8_t lx;                 /* low 8 bit part */
            uint8_t hx;                 /* high 8 bit */
            uint16_t hi16_unused2;
            uint32_t hi32_unused3;
        };
    };
} __attribute__ ((__packed__)) x86_register;

typedef enum x86_rflags {
    RFLAGS_CF       = (1L << 0),
    RFLAGS_PF       = (1L << 2),
    RFLAGS_AF       = (1L << 4),
    RFLAGS_ZF       = (1L << 6),
    RFLAGS_SF       = (1L << 7),
    RFLAGS_TF       = (1L << 8),
    RFLAGS_IF       = (1L << 9),
    RFLAGS_DF       = (1L << 10),
    RFLAGS_OF       = (1L << 11),
    RFLAGS_IOPL     = (3L << 12),
    RFLAGS_NT       = (1L << 14),
    RFLAGS_RF       = (1L << 16),
    RFLAGS_VM       = (1L << 17),
    RFLAGS_AC       = (1L << 18),
    RFLAGS_VIF      = (1L << 19),
    RFLAGS_VIP      = (1L << 20),
    RFLAGS_ID       = (1L << 21),
} x86_rflags;

/* rflags register */
typedef struct x86_reg_flags {
    union {
        struct {
            uint64_t rflags;
        };
        struct {
            uint32_t eflags;
            uint32_t hi32_unused1;
        };
        struct {
            uint32_t cf:1;
            uint32_t unused1:1;
            uint32_t pf:1;
            uint32_t unused2:1;
            uint32_t af:1;
            uint32_t unused3:1;
            uint32_t zf:1;
            uint32_t sf:1;
            uint32_t tf:1;
            uint32_t ief:1;
            uint32_t df:1;
            uint32_t of:1;
            uint32_t iopl:2;
            uint32_t nt:1;
            uint32_t unused4:1;
            uint32_t rf:1;
            uint32_t vm:1;
            uint32_t ac:1;
            uint32_t vif:1;
            uint32_t vip:1;
            uint32_t id:1;
            uint32_t unused5:10;
            uint32_t hi32_unused2;
        };
    };
} __attribute__ ((__packed__)) x86_reg_flags;

typedef enum x86_reg_cr0 {
    CR0_PE =            (1L << 0),
    CR0_MP =            (1L << 1),
    CR0_EM =            (1L << 2),
    CR0_TS =            (1L << 3),
    CR0_ET =            (1L << 4),
    CR0_NE =            (1L << 5),
    CR0_WP =            (1L << 16),
    CR0_AM =            (1L << 18),
    CR0_NW =            (1L << 29),
    CR0_CD =            (1L << 30),
    CR0_PG =            (1L << 31),
} x86_reg_cr0;

typedef enum x86_reg_cr4 {
    CR4_VME =            (1L << 0),
    CR4_PVI =            (1L << 1),
    CR4_TSD =            (1L << 2),
    CR4_DE  =            (1L << 3),
    CR4_PSE =            (1L << 4),
    CR4_PAE =            (1L << 5),
    CR4_MSE =            (1L << 6),
    CR4_PGE =            (1L << 7),
    CR4_PCE =            (1L << 8),
    CR4_OSFXSR =         (1L << 9),
    CR4_OSXMMEXCPT =     (1L << 10),
    CR4_VMXE =           (1L << 13),
    CR4_SMXE =           (1L << 14),
    CR4_FSGSBASE =       (1L << 16),
    CR4_PCIDE =          (1L << 17),
    CR4_OSXSAVE =        (1L << 18),
    CR4_SMEP =           (1L << 20),
} x86_reg_cr4;

/* 16 bit Task State Segment */
typedef struct x86_tss_segment16 {
    uint16_t link;
    uint16_t sp0;
    uint16_t ss0;
    uint32_t sp1;
    uint16_t ss1;
    uint32_t sp2;
    uint16_t ss2;
    uint16_t ip;
    uint16_t flags;
    uint16_t ax;
    uint16_t cx;
    uint16_t dx;
    uint16_t bx;
    uint16_t sp;
    uint16_t bp;
    uint16_t si;
    uint16_t di;
    uint16_t es;
    uint16_t cs;
    uint16_t ss;
    uint16_t ds;
    uint16_t ldtr;
} __attribute__((packed)) x86_tss_segment16;

/* 32 bit Task State Segment */
typedef struct x86_tss_segment32 {
    uint32_t prev_tss;
    uint32_t esp0;
    uint32_t ss0;
    uint32_t esp1;
    uint32_t ss1;
    uint32_t esp2;
    uint32_t ss2;
    uint32_t cr3;
    uint32_t eip;
    uint32_t eflags;
    uint32_t eax;
    uint32_t ecx;
    uint32_t edx;
    uint32_t ebx;
    uint32_t esp;
    uint32_t ebp;
    uint32_t esi;
    uint32_t edi;
    uint32_t es;
    uint32_t cs;
    uint32_t ss;
    uint32_t ds;
    uint32_t fs;
    uint32_t gs;
    uint32_t ldt;
    uint16_t trap;
    uint16_t iomap_base;
} __attribute__ ((__packed__)) x86_tss_segment32;

/* 64 bit Task State Segment */
typedef struct x86_tss_segment64 {
    uint32_t unused;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t unused1;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t unused2;
    uint16_t unused3;
    uint16_t iomap_base;
} __attribute__ ((__packed__)) x86_tss_segment64;

/* segment descriptors */
typedef struct x86_segment_descriptor {
    uint64_t    limit0:16;
    uint64_t    base0:16;
    uint64_t    base1:8;
    uint64_t    type:4;
    uint64_t    s:1;
    uint64_t    dpl:2;
    uint64_t    p:1;
    uint64_t    limit1:4;
    uint64_t    avl:1;
    uint64_t    l:1;
    uint64_t    db:1;
    uint64_t    g:1;
    uint64_t    base2:8;
} __attribute__ ((__packed__)) x86_segment_descriptor;

static inline uint32_t x86_segment_base(x86_segment_descriptor *desc)
{
    return (uint32_t)((desc->base2 << 24) | (desc->base1 << 16) | desc->base0);
}

static inline void x86_set_segment_base(x86_segment_descriptor *desc,
                                        uint32_t base)
{
    desc->base2 = base >> 24;
    desc->base1 = (base >> 16) & 0xff;
    desc->base0 = base & 0xffff;
}

static inline uint32_t x86_segment_limit(x86_segment_descriptor *desc)
{
    uint32_t limit = (uint32_t)((desc->limit1 << 16) | desc->limit0);
    if (desc->g) {
        return (limit << 12) | 0xfff;
    }
    return limit;
}

static inline void x86_set_segment_limit(x86_segment_descriptor *desc,
                                         uint32_t limit)
{
    desc->limit0 = limit & 0xffff;
    desc->limit1 = limit >> 16;
}

typedef struct x86_call_gate {
    uint64_t offset0:16;
    uint64_t selector:16;
    uint64_t param_count:4;
    uint64_t reserved:3;
    uint64_t type:4;
    uint64_t dpl:1;
    uint64_t p:1;
    uint64_t offset1:16;
} __attribute__ ((__packed__)) x86_call_gate;

static inline uint32_t x86_call_gate_offset(x86_call_gate *gate)
{
    return (uint32_t)((gate->offset1 << 16) | gate->offset0);
}

#define LDT_SEL     0
#define GDT_SEL     1

typedef struct x68_segment_selector {
    union {
        uint16_t sel;
        struct {
            uint16_t rpl:3;
            uint16_t ti:1;
            uint16_t index:12;
        };
    };
} __attribute__ ((__packed__)) x68_segment_selector;

typedef struct lazy_flags {
    target_ulong result;
    target_ulong auxbits;
} lazy_flags;

/* Definition of hvf_x86_state is here */
struct HVFX86EmulatorState {
    int interruptable;
    uint64_t fetch_rip;
    uint64_t rip;
    struct x86_register regs[16];
    struct x86_reg_flags   rflags;
    struct lazy_flags   lflags;
    uint8_t mmio_buf[4096];
};

/* useful register access  macros */
#define RIP(cpu)    (cpu->hvf_emul->rip)
#define EIP(cpu)    ((uint32_t)cpu->hvf_emul->rip)
#define RFLAGS(cpu) (cpu->hvf_emul->rflags.rflags)
#define EFLAGS(cpu) (cpu->hvf_emul->rflags.eflags)

#define RRX(cpu, reg) (cpu->hvf_emul->regs[reg].rrx)
#define RAX(cpu)        RRX(cpu, R_EAX)
#define RCX(cpu)        RRX(cpu, R_ECX)
#define RDX(cpu)        RRX(cpu, R_EDX)
#define RBX(cpu)        RRX(cpu, R_EBX)
#define RSP(cpu)        RRX(cpu, R_ESP)
#define RBP(cpu)        RRX(cpu, R_EBP)
#define RSI(cpu)        RRX(cpu, R_ESI)
#define RDI(cpu)        RRX(cpu, R_EDI)
#define R8(cpu)         RRX(cpu, R_R8)
#define R9(cpu)         RRX(cpu, R_R9)
#define R10(cpu)        RRX(cpu, R_R10)
#define R11(cpu)        RRX(cpu, R_R11)
#define R12(cpu)        RRX(cpu, R_R12)
#define R13(cpu)        RRX(cpu, R_R13)
#define R14(cpu)        RRX(cpu, R_R14)
#define R15(cpu)        RRX(cpu, R_R15)

#define ERX(cpu, reg)   (cpu->hvf_emul->regs[reg].erx)
#define EAX(cpu)        ERX(cpu, R_EAX)
#define ECX(cpu)        ERX(cpu, R_ECX)
#define EDX(cpu)        ERX(cpu, R_EDX)
#define EBX(cpu)        ERX(cpu, R_EBX)
#define ESP(cpu)        ERX(cpu, R_ESP)
#define EBP(cpu)        ERX(cpu, R_EBP)
#define ESI(cpu)        ERX(cpu, R_ESI)
#define EDI(cpu)        ERX(cpu, R_EDI)

#define RX(cpu, reg)   (cpu->hvf_emul->regs[reg].rx)
#define AX(cpu)        RX(cpu, R_EAX)
#define CX(cpu)        RX(cpu, R_ECX)
#define DX(cpu)        RX(cpu, R_EDX)
#define BP(cpu)        RX(cpu, R_EBP)
#define SP(cpu)        RX(cpu, R_ESP)
#define BX(cpu)        RX(cpu, R_EBX)
#define SI(cpu)        RX(cpu, R_ESI)
#define DI(cpu)        RX(cpu, R_EDI)

#define RL(cpu, reg)   (cpu->hvf_emul->regs[reg].lx)
#define AL(cpu)        RL(cpu, R_EAX)
#define CL(cpu)        RL(cpu, R_ECX)
#define DL(cpu)        RL(cpu, R_EDX)
#define BL(cpu)        RL(cpu, R_EBX)

#define RH(cpu, reg)   (cpu->hvf_emul->regs[reg].hx)
#define AH(cpu)        RH(cpu, R_EAX)
#define CH(cpu)        RH(cpu, R_ECX)
#define DH(cpu)        RH(cpu, R_EDX)
#define BH(cpu)        RH(cpu, R_EBX)

/* deal with GDT/LDT descriptors in memory */
bool x86_read_segment_descriptor(struct CPUState *cpu,
                                 struct x86_segment_descriptor *desc,
                                 x68_segment_selector sel);
bool x86_write_segment_descriptor(struct CPUState *cpu,
                                  struct x86_segment_descriptor *desc,
                                  x68_segment_selector sel);

bool x86_read_call_gate(struct CPUState *cpu, struct x86_call_gate *idt_desc,
                        int gate);

/* helpers */
bool x86_is_protected(struct CPUState *cpu);
bool x86_is_real(struct CPUState *cpu);
bool x86_is_v8086(struct CPUState *cpu);
bool x86_is_long_mode(struct CPUState *cpu);
bool x86_is_long64_mode(struct CPUState *cpu);
bool x86_is_paging_mode(struct CPUState *cpu);
bool x86_is_pae_enabled(struct CPUState *cpu);

enum X86Seg;
target_ulong linear_addr(struct CPUState *cpu, target_ulong addr, enum X86Seg seg);
target_ulong linear_addr_size(struct CPUState *cpu, target_ulong addr, int size,
                              enum X86Seg seg);
target_ulong linear_rip(struct CPUState *cpu, target_ulong rip);

static inline uint64_t rdtscp(void)
{
    uint64_t tsc;
    __asm__ __volatile__("rdtscp; "         /* serializing read of tsc */
                         "shl $32,%%rdx; "  /* shift higher 32 bits stored in rdx up */
                         "or %%rdx,%%rax"   /* and or onto rax */
                         : "=a"(tsc)        /* output to tsc variable */
                         :
                         : "%rcx", "%rdx"); /* rcx and rdx are clobbered */

    return tsc;
}

#endif

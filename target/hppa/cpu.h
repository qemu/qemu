/*
 * PA-RISC emulation cpu definitions for qemu.
 *
 * Copyright (c) 2016 Richard Henderson <rth@twiddle.net>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HPPA_CPU_H
#define HPPA_CPU_H

#include "cpu-qom.h"
#include "exec/cpu-defs.h"
#include "qemu/cpu-float.h"
#include "qemu/interval-tree.h"

/* PA-RISC 1.x processors have a strong memory model.  */
/* ??? While we do not yet implement PA-RISC 2.0, those processors have
   a weak memory model, but with TLB bits that force ordering on a per-page
   basis.  It's probably easier to fall back to a strong memory model.  */
#define TCG_GUEST_DEFAULT_MO        TCG_MO_ALL

#define MMU_ABS_W_IDX     6
#define MMU_ABS_IDX       7
#define MMU_KERNEL_IDX    8
#define MMU_KERNEL_P_IDX  9
#define MMU_PL1_IDX       10
#define MMU_PL1_P_IDX     11
#define MMU_PL2_IDX       12
#define MMU_PL2_P_IDX     13
#define MMU_USER_IDX      14
#define MMU_USER_P_IDX    15

#define MMU_IDX_MMU_DISABLED(MIDX)  ((MIDX) < MMU_KERNEL_IDX)
#define MMU_IDX_TO_PRIV(MIDX)       (((MIDX) - MMU_KERNEL_IDX) / 2)
#define MMU_IDX_TO_P(MIDX)          (((MIDX) - MMU_KERNEL_IDX) & 1)
#define PRIV_P_TO_MMU_IDX(PRIV, P)  ((PRIV) * 2 + !!(P) + MMU_KERNEL_IDX)

#define TARGET_INSN_START_EXTRA_WORDS 2

/* No need to flush MMU_ABS*_IDX  */
#define HPPA_MMU_FLUSH_MASK                             \
        (1 << MMU_KERNEL_IDX | 1 << MMU_KERNEL_P_IDX |  \
         1 << MMU_PL1_IDX    | 1 << MMU_PL1_P_IDX    |  \
         1 << MMU_PL2_IDX    | 1 << MMU_PL2_P_IDX    |  \
         1 << MMU_USER_IDX   | 1 << MMU_USER_P_IDX)

/* Indices to flush for access_id changes. */
#define HPPA_MMU_FLUSH_P_MASK \
        (1 << MMU_KERNEL_P_IDX | 1 << MMU_PL1_P_IDX  |  \
         1 << MMU_PL2_P_IDX    | 1 << MMU_USER_P_IDX)

/* Hardware exceptions, interrupts, faults, and traps.  */
#define EXCP_HPMC                1  /* high priority machine check */
#define EXCP_POWER_FAIL          2
#define EXCP_RC                  3  /* recovery counter */
#define EXCP_EXT_INTERRUPT       4  /* external interrupt */
#define EXCP_LPMC                5  /* low priority machine check */
#define EXCP_ITLB_MISS           6  /* itlb miss / instruction page fault */
#define EXCP_IMP                 7  /* instruction memory protection trap */
#define EXCP_ILL                 8  /* illegal instruction trap */
#define EXCP_BREAK               9  /* break instruction */
#define EXCP_PRIV_OPR            10 /* privileged operation trap */
#define EXCP_PRIV_REG            11 /* privileged register trap */
#define EXCP_OVERFLOW            12 /* signed overflow trap */
#define EXCP_COND                13 /* trap-on-condition */
#define EXCP_ASSIST              14 /* assist exception trap */
#define EXCP_DTLB_MISS           15 /* dtlb miss / data page fault */
#define EXCP_NA_ITLB_MISS        16 /* non-access itlb miss */
#define EXCP_NA_DTLB_MISS        17 /* non-access dtlb miss */
#define EXCP_DMP                 18 /* data memory protection trap */
#define EXCP_DMB                 19 /* data memory break trap */
#define EXCP_TLB_DIRTY           20 /* tlb dirty bit trap */
#define EXCP_PAGE_REF            21 /* page reference trap */
#define EXCP_ASSIST_EMU          22 /* assist emulation trap */
#define EXCP_HPT                 23 /* high-privilege transfer trap */
#define EXCP_LPT                 24 /* low-privilege transfer trap */
#define EXCP_TB                  25 /* taken branch trap */
#define EXCP_DMAR                26 /* data memory access rights trap */
#define EXCP_DMPI                27 /* data memory protection id trap */
#define EXCP_UNALIGN             28 /* unaligned data reference trap */
#define EXCP_PER_INTERRUPT       29 /* performance monitor interrupt */

/* Exceptions for linux-user emulation.  */
#define EXCP_SYSCALL             30
#define EXCP_SYSCALL_LWS         31

/* Emulated hardware TOC button */
#define EXCP_TOC                 32 /* TOC = Transfer of control (NMI) */

#define CPU_INTERRUPT_NMI       CPU_INTERRUPT_TGT_EXT_3         /* TOC */

/* Taken from Linux kernel: arch/parisc/include/asm/psw.h */
#define PSW_I            0x00000001
#define PSW_D            0x00000002
#define PSW_P            0x00000004
#define PSW_Q            0x00000008
#define PSW_R            0x00000010
#define PSW_F            0x00000020
#define PSW_G            0x00000040 /* PA1.x only */
#define PSW_O            0x00000080 /* PA2.0 only */
#define PSW_CB           0x0000ff00
#define PSW_M            0x00010000
#define PSW_V            0x00020000
#define PSW_C            0x00040000
#define PSW_B            0x00080000
#define PSW_X            0x00100000
#define PSW_N            0x00200000
#define PSW_L            0x00400000
#define PSW_H            0x00800000
#define PSW_T            0x01000000
#define PSW_S            0x02000000
#define PSW_E            0x04000000
#define PSW_W            0x08000000 /* PA2.0 only */
#define PSW_Z            0x40000000 /* PA1.x only */
#define PSW_Y            0x80000000 /* PA1.x only */

#define PSW_SM (PSW_W | PSW_E | PSW_O | PSW_G | PSW_F \
               | PSW_R | PSW_Q | PSW_P | PSW_D | PSW_I)

/* ssm/rsm instructions number PSW_W and PSW_E differently */
#define PSW_SM_I         PSW_I      /* Enable External Interrupts */
#define PSW_SM_D         PSW_D
#define PSW_SM_P         PSW_P
#define PSW_SM_Q         PSW_Q      /* Enable Interrupt State Collection */
#define PSW_SM_R         PSW_R      /* Enable Recover Counter Trap */
#define PSW_SM_E         0x100
#define PSW_SM_W         0x200      /* PA2.0 only : Enable Wide Mode */

#define CR_RC            0
#define CR_PSW_DEFAULT   6          /* see SeaBIOS PDC_PSW firmware call */
#define  PDC_PSW_WIDE_BIT 2
#define CR_PID1          8
#define CR_PID2          9
#define CR_PID3          12
#define CR_PID4          13
#define CR_SCRCCR        10
#define CR_SAR           11
#define CR_IVA           14
#define CR_EIEM          15
#define CR_IT            16
#define CR_IIASQ         17
#define CR_IIAOQ         18
#define CR_IIR           19
#define CR_ISR           20
#define CR_IOR           21
#define CR_IPSW          22
#define CR_EIRR          23

typedef struct HPPATLBEntry {
    union {
        IntervalTreeNode itree;
        struct HPPATLBEntry *unused_next;
    };

    target_ulong pa;

    unsigned entry_valid : 1;

    unsigned u : 1;
    unsigned t : 1;
    unsigned d : 1;
    unsigned b : 1;
    unsigned ar_type : 3;
    unsigned ar_pl1 : 2;
    unsigned ar_pl2 : 2;
    unsigned access_id : 16;
} HPPATLBEntry;

typedef struct CPUArchState {
    target_ulong iaoq_f;     /* front */
    target_ulong iaoq_b;     /* back, aka next instruction */

    target_ulong gr[32];
    uint64_t fr[32];
    uint64_t sr[8];          /* stored shifted into place for gva */

    target_ulong psw;        /* All psw bits except the following:  */
    target_ulong psw_n;      /* boolean */
    target_long psw_v;       /* in most significant bit */

    /* Splitting the carry-borrow field into the MSB and "the rest", allows
     * for "the rest" to be deleted when it is unused, but the MSB is in use.
     * In addition, it's easier to compute carry-in for bit B+1 than it is to
     * compute carry-out for bit B (3 vs 4 insns for addition, assuming the
     * host has the appropriate add-with-carry insn to compute the msb).
     * Therefore the carry bits are stored as: cb_msb : cb & 0x11111110.
     */
    target_ulong psw_cb;     /* in least significant bit of next nibble */
    target_ulong psw_cb_msb; /* boolean */

    uint64_t iasq_f;
    uint64_t iasq_b;

    uint32_t fr0_shadow;     /* flags, c, ca/cq, rm, d, enables */
    float_status fp_status;

    target_ulong cr[32];     /* control registers */
    target_ulong cr_back[2]; /* back of cr17/cr18 */
    target_ulong shadow[7];  /* shadow registers */

    /*
     * During unwind of a memory insn, the base register of the address.
     * This is used to construct CR_IOR for pa2.0.
     */
    uint32_t unwind_breg;

    /*
     * ??? The number of entries isn't specified by the architecture.
     * BTLBs are not supported in 64-bit machines.
     */
#define PA10_BTLB_FIXED         16
#define PA10_BTLB_VARIABLE      0
#define HPPA_TLB_ENTRIES        256

    /* Index for round-robin tlb eviction. */
    uint32_t tlb_last;

    /*
     * For pa1.x, the partial initialized, still invalid tlb entry
     * which has had ITLBA performed, but not yet ITLBP.
     */
    HPPATLBEntry *tlb_partial;

    /* Linked list of all invalid (unused) tlb entries. */
    HPPATLBEntry *tlb_unused;

    /* Root of the search tree for all valid tlb entries. */
    IntervalTreeRoot tlb_root;

    HPPATLBEntry tlb[HPPA_TLB_ENTRIES];
} CPUHPPAState;

/**
 * HPPACPU:
 * @env: #CPUHPPAState
 *
 * An HPPA CPU.
 */
struct ArchCPU {
    CPUState parent_obj;

    CPUHPPAState env;
    QEMUTimer *alarm_timer;
};

/**
 * HPPACPUClass:
 * @parent_realize: The parent class' realize handler.
 * @parent_reset: The parent class' reset handler.
 *
 * An HPPA CPU model.
 */
struct HPPACPUClass {
    CPUClass parent_class;

    DeviceRealize parent_realize;
    DeviceReset parent_reset;
};

#include "exec/cpu-all.h"

static inline bool hppa_is_pa20(CPUHPPAState *env)
{
    return object_dynamic_cast(OBJECT(env_cpu(env)), TYPE_HPPA64_CPU) != NULL;
}

static inline int HPPA_BTLB_ENTRIES(CPUHPPAState *env)
{
    return hppa_is_pa20(env) ? 0 : PA10_BTLB_FIXED + PA10_BTLB_VARIABLE;
}

void hppa_translate_init(void);

#define CPU_RESOLVING_TYPE TYPE_HPPA_CPU

static inline target_ulong hppa_form_gva_psw(target_ulong psw, uint64_t spc,
                                             target_ulong off)
{
#ifdef CONFIG_USER_ONLY
    return off;
#else
    off &= psw & PSW_W ? MAKE_64BIT_MASK(0, 62) : MAKE_64BIT_MASK(0, 32);
    return spc | off;
#endif
}

static inline target_ulong hppa_form_gva(CPUHPPAState *env, uint64_t spc,
                                         target_ulong off)
{
    return hppa_form_gva_psw(env->psw, spc, off);
}

hwaddr hppa_abs_to_phys_pa2_w0(vaddr addr);
hwaddr hppa_abs_to_phys_pa2_w1(vaddr addr);

/*
 * Since PSW_{I,CB} will never need to be in tb->flags, reuse them.
 * TB_FLAG_SR_SAME indicates that SR4 through SR7 all contain the
 * same value.
 */
#define TB_FLAG_SR_SAME     PSW_I
#define TB_FLAG_PRIV_SHIFT  8
#define TB_FLAG_UNALIGN     0x400

static inline void cpu_get_tb_cpu_state(CPUHPPAState *env, vaddr *pc,
                                        uint64_t *cs_base, uint32_t *pflags)
{
    uint32_t flags = env->psw_n * PSW_N;

    /* TB lookup assumes that PC contains the complete virtual address.
       If we leave space+offset separate, we'll get ITLB misses to an
       incomplete virtual address.  This also means that we must separate
       out current cpu privilege from the low bits of IAOQ_F.  */
#ifdef CONFIG_USER_ONLY
    *pc = env->iaoq_f & -4;
    *cs_base = env->iaoq_b & -4;
    flags |= TB_FLAG_UNALIGN * !env_cpu(env)->prctl_unalign_sigbus;
#else
    /* ??? E, T, H, L, B bits need to be here, when implemented.  */
    flags |= env->psw & (PSW_W | PSW_C | PSW_D | PSW_P);
    flags |= (env->iaoq_f & 3) << TB_FLAG_PRIV_SHIFT;

    *pc = hppa_form_gva_psw(env->psw, (env->psw & PSW_C ? env->iasq_f : 0),
                            env->iaoq_f & -4);
    *cs_base = env->iasq_f;

    /* Insert a difference between IAOQ_B and IAOQ_F within the otherwise zero
       low 32-bits of CS_BASE.  This will succeed for all direct branches,
       which is the primary case we care about -- using goto_tb within a page.
       Failure is indicated by a zero difference.  */
    if (env->iasq_f == env->iasq_b) {
        target_long diff = env->iaoq_b - env->iaoq_f;
        if (diff == (int32_t)diff) {
            *cs_base |= (uint32_t)diff;
        }
    }
    if ((env->sr[4] == env->sr[5])
        & (env->sr[4] == env->sr[6])
        & (env->sr[4] == env->sr[7])) {
        flags |= TB_FLAG_SR_SAME;
    }
#endif

    *pflags = flags;
}

target_ulong cpu_hppa_get_psw(CPUHPPAState *env);
void cpu_hppa_put_psw(CPUHPPAState *env, target_ulong);
void cpu_hppa_loaded_fr0(CPUHPPAState *env);

#ifdef CONFIG_USER_ONLY
static inline void cpu_hppa_change_prot_id(CPUHPPAState *env) { }
#else
void cpu_hppa_change_prot_id(CPUHPPAState *env);
#endif

int hppa_cpu_gdb_read_register(CPUState *cpu, GByteArray *buf, int reg);
int hppa_cpu_gdb_write_register(CPUState *cpu, uint8_t *buf, int reg);
void hppa_cpu_dump_state(CPUState *cs, FILE *f, int);
#ifndef CONFIG_USER_ONLY
void hppa_ptlbe(CPUHPPAState *env);
hwaddr hppa_cpu_get_phys_page_debug(CPUState *cs, vaddr addr);
void hppa_set_ior_and_isr(CPUHPPAState *env, vaddr addr, bool mmu_disabled);
bool hppa_cpu_tlb_fill(CPUState *cs, vaddr address, int size,
                       MMUAccessType access_type, int mmu_idx,
                       bool probe, uintptr_t retaddr);
void hppa_cpu_do_interrupt(CPUState *cpu);
bool hppa_cpu_exec_interrupt(CPUState *cpu, int int_req);
int hppa_get_physical_address(CPUHPPAState *env, vaddr addr, int mmu_idx,
                              int type, hwaddr *pphys, int *pprot,
                              HPPATLBEntry **tlb_entry);
void hppa_cpu_do_transaction_failed(CPUState *cs, hwaddr physaddr,
                                     vaddr addr, unsigned size,
                                     MMUAccessType access_type,
                                     int mmu_idx, MemTxAttrs attrs,
                                     MemTxResult response, uintptr_t retaddr);
extern const MemoryRegionOps hppa_io_eir_ops;
extern const VMStateDescription vmstate_hppa_cpu;
void hppa_cpu_alarm_timer(void *);
int hppa_artype_for_page(CPUHPPAState *env, target_ulong vaddr);
#endif
G_NORETURN void hppa_dynamic_excp(CPUHPPAState *env, int excp, uintptr_t ra);

#define CPU_RESOLVING_TYPE TYPE_HPPA_CPU

#endif /* HPPA_CPU_H */

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

/* PA-RISC 1.x processors have a strong memory model.  */
/* ??? While we do not yet implement PA-RISC 2.0, those processors have
   a weak memory model, but with TLB bits that force ordering on a per-page
   basis.  It's probably easier to fall back to a strong memory model.  */
#define TCG_GUEST_DEFAULT_MO        TCG_MO_ALL

#define MMU_KERNEL_IDX   0
#define MMU_USER_IDX     3
#define MMU_PHYS_IDX     4
#define TARGET_INSN_START_EXTRA_WORDS 1

/* Hardware exceptions, interupts, faults, and traps.  */
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
#ifdef TARGET_HPPA64
#define PSW_W            0x08000000 /* PA2.0 only */
#else
#define PSW_W            0
#endif
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
#ifdef TARGET_HPPA64
#define PSW_SM_E         0x100
#define PSW_SM_W         0x200      /* PA2.0 only : Enable Wide Mode */
#else
#define PSW_SM_E         0
#define PSW_SM_W         0
#endif

#define CR_RC            0
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

#if TARGET_REGISTER_BITS == 32
typedef uint32_t target_ureg;
typedef int32_t  target_sreg;
#define TREG_FMT_lx   "%08"PRIx32
#define TREG_FMT_ld   "%"PRId32
#else
typedef uint64_t target_ureg;
typedef int64_t  target_sreg;
#define TREG_FMT_lx   "%016"PRIx64
#define TREG_FMT_ld   "%"PRId64
#endif

typedef struct {
    uint64_t va_b;
    uint64_t va_e;
    target_ureg pa;
    unsigned u : 1;
    unsigned t : 1;
    unsigned d : 1;
    unsigned b : 1;
    unsigned page_size : 4;
    unsigned ar_type : 3;
    unsigned ar_pl1 : 2;
    unsigned ar_pl2 : 2;
    unsigned entry_valid : 1;
    unsigned access_id : 16;
} hppa_tlb_entry;

typedef struct CPUArchState {
    target_ureg gr[32];
    uint64_t fr[32];
    uint64_t sr[8];          /* stored shifted into place for gva */

    target_ureg psw;         /* All psw bits except the following:  */
    target_ureg psw_n;       /* boolean */
    target_sreg psw_v;       /* in most significant bit */

    /* Splitting the carry-borrow field into the MSB and "the rest", allows
     * for "the rest" to be deleted when it is unused, but the MSB is in use.
     * In addition, it's easier to compute carry-in for bit B+1 than it is to
     * compute carry-out for bit B (3 vs 4 insns for addition, assuming the
     * host has the appropriate add-with-carry insn to compute the msb).
     * Therefore the carry bits are stored as: cb_msb : cb & 0x11111110.
     */
    target_ureg psw_cb;      /* in least significant bit of next nibble */
    target_ureg psw_cb_msb;  /* boolean */

    target_ureg iaoq_f;      /* front */
    target_ureg iaoq_b;      /* back, aka next instruction */
    uint64_t iasq_f;
    uint64_t iasq_b;

    uint32_t fr0_shadow;     /* flags, c, ca/cq, rm, d, enables */
    float_status fp_status;

    target_ureg cr[32];      /* control registers */
    target_ureg cr_back[2];  /* back of cr17/cr18 */
    target_ureg shadow[7];   /* shadow registers */

    /* ??? The number of entries isn't specified by the architecture.  */
#define HPPA_TLB_ENTRIES        256
#define HPPA_BTLB_ENTRIES       0

    /* ??? Implement a unified itlb/dtlb for the moment.  */
    /* ??? We should use a more intelligent data structure.  */
    hppa_tlb_entry tlb[HPPA_TLB_ENTRIES];
    uint32_t tlb_last;
} CPUHPPAState;

/**
 * HPPACPU:
 * @env: #CPUHPPAState
 *
 * An HPPA CPU.
 */
struct ArchCPU {
    /*< private >*/
    CPUState parent_obj;
    /*< public >*/

    CPUNegativeOffsetState neg;
    CPUHPPAState env;
    QEMUTimer *alarm_timer;
};

#include "exec/cpu-all.h"

static inline int cpu_mmu_index(CPUHPPAState *env, bool ifetch)
{
#ifdef CONFIG_USER_ONLY
    return MMU_USER_IDX;
#else
    if (env->psw & (ifetch ? PSW_C : PSW_D)) {
        return env->iaoq_f & 3;
    }
    return MMU_PHYS_IDX;  /* mmu disabled */
#endif
}

void hppa_translate_init(void);

#define CPU_RESOLVING_TYPE TYPE_HPPA_CPU

static inline target_ulong hppa_form_gva_psw(target_ureg psw, uint64_t spc,
                                             target_ureg off)
{
#ifdef CONFIG_USER_ONLY
    return off;
#else
    off &= (psw & PSW_W ? 0x3fffffffffffffffull : 0xffffffffull);
    return spc | off;
#endif
}

static inline target_ulong hppa_form_gva(CPUHPPAState *env, uint64_t spc,
                                         target_ureg off)
{
    return hppa_form_gva_psw(env->psw, spc, off);
}

/*
 * Since PSW_{I,CB} will never need to be in tb->flags, reuse them.
 * TB_FLAG_SR_SAME indicates that SR4 through SR7 all contain the
 * same value.
 */
#define TB_FLAG_SR_SAME     PSW_I
#define TB_FLAG_PRIV_SHIFT  8
#define TB_FLAG_UNALIGN     0x400

static inline void cpu_get_tb_cpu_state(CPUHPPAState *env, target_ulong *pc,
                                        target_ulong *cs_base,
                                        uint32_t *pflags)
{
    uint32_t flags = env->psw_n * PSW_N;

    /* TB lookup assumes that PC contains the complete virtual address.
       If we leave space+offset separate, we'll get ITLB misses to an
       incomplete virtual address.  This also means that we must separate
       out current cpu priviledge from the low bits of IAOQ_F.  */
#ifdef CONFIG_USER_ONLY
    *pc = env->iaoq_f & -4;
    *cs_base = env->iaoq_b & -4;
    flags |= TB_FLAG_UNALIGN * !env_cpu(env)->prctl_unalign_sigbus;
#else
    /* ??? E, T, H, L, B, P bits need to be here, when implemented.  */
    flags |= env->psw & (PSW_W | PSW_C | PSW_D);
    flags |= (env->iaoq_f & 3) << TB_FLAG_PRIV_SHIFT;

    *pc = (env->psw & PSW_C
           ? hppa_form_gva_psw(env->psw, env->iasq_f, env->iaoq_f & -4)
           : env->iaoq_f & -4);
    *cs_base = env->iasq_f;

    /* Insert a difference between IAOQ_B and IAOQ_F within the otherwise zero
       low 32-bits of CS_BASE.  This will succeed for all direct branches,
       which is the primary case we care about -- using goto_tb within a page.
       Failure is indicated by a zero difference.  */
    if (env->iasq_f == env->iasq_b) {
        target_sreg diff = env->iaoq_b - env->iaoq_f;
        if (TARGET_REGISTER_BITS == 32 || diff == (int32_t)diff) {
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

target_ureg cpu_hppa_get_psw(CPUHPPAState *env);
void cpu_hppa_put_psw(CPUHPPAState *env, target_ureg);
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
hwaddr hppa_cpu_get_phys_page_debug(CPUState *cs, vaddr addr);
bool hppa_cpu_tlb_fill(CPUState *cs, vaddr address, int size,
                       MMUAccessType access_type, int mmu_idx,
                       bool probe, uintptr_t retaddr);
void hppa_cpu_do_interrupt(CPUState *cpu);
bool hppa_cpu_exec_interrupt(CPUState *cpu, int int_req);
int hppa_get_physical_address(CPUHPPAState *env, vaddr addr, int mmu_idx,
                              int type, hwaddr *pphys, int *pprot);
extern const MemoryRegionOps hppa_io_eir_ops;
extern const VMStateDescription vmstate_hppa_cpu;
void hppa_cpu_alarm_timer(void *);
int hppa_artype_for_page(CPUHPPAState *env, target_ulong vaddr);
#endif
G_NORETURN void hppa_dynamic_excp(CPUHPPAState *env, int excp, uintptr_t ra);

#endif /* HPPA_CPU_H */

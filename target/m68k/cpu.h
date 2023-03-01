/*
 * m68k virtual CPU header
 *
 *  Copyright (c) 2005-2007 CodeSourcery
 *  Written by Paul Brook
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

#ifndef M68K_CPU_H
#define M68K_CPU_H

#include "exec/cpu-defs.h"
#include "qemu/cpu-float.h"
#include "cpu-qom.h"

#define OS_BYTE     0
#define OS_WORD     1
#define OS_LONG     2
#define OS_SINGLE   3
#define OS_DOUBLE   4
#define OS_EXTENDED 5
#define OS_PACKED   6
#define OS_UNSIZED  7

#define EXCP_ACCESS         2   /* Access (MMU) error.  */
#define EXCP_ADDRESS        3   /* Address error.  */
#define EXCP_ILLEGAL        4   /* Illegal instruction.  */
#define EXCP_DIV0           5   /* Divide by zero */
#define EXCP_CHK            6   /* CHK, CHK2 Instructions */
#define EXCP_TRAPCC         7   /* FTRAPcc, TRAPcc, TRAPV Instructions */
#define EXCP_PRIVILEGE      8   /* Privilege violation.  */
#define EXCP_TRACE          9
#define EXCP_LINEA          10  /* Unimplemented line-A (MAC) opcode.  */
#define EXCP_LINEF          11  /* Unimplemented line-F (FPU) opcode.  */
#define EXCP_DEBUGNBP       12  /* Non-breakpoint debug interrupt.  */
#define EXCP_DEBEGBP        13  /* Breakpoint debug interrupt.  */
#define EXCP_FORMAT         14  /* RTE format error.  */
#define EXCP_UNINITIALIZED  15
#define EXCP_SPURIOUS       24  /* Spurious interrupt */
#define EXCP_INT_LEVEL_1    25  /* Level 1 Interrupt autovector */
#define EXCP_INT_LEVEL_7    31  /* Level 7 Interrupt autovector */
#define EXCP_TRAP0          32   /* User trap #0.  */
#define EXCP_TRAP15         47   /* User trap #15.  */
#define EXCP_FP_BSUN        48 /* Branch Set on Unordered */
#define EXCP_FP_INEX        49 /* Inexact result */
#define EXCP_FP_DZ          50 /* Divide by Zero */
#define EXCP_FP_UNFL        51 /* Underflow */
#define EXCP_FP_OPERR       52 /* Operand Error */
#define EXCP_FP_OVFL        53 /* Overflow */
#define EXCP_FP_SNAN        54 /* Signaling Not-A-Number */
#define EXCP_FP_UNIMP       55 /* Unimplemented Data type */
#define EXCP_MMU_CONF       56  /* MMU Configuration Error */
#define EXCP_MMU_ILLEGAL    57  /* MMU Illegal Operation Error */
#define EXCP_MMU_ACCESS     58  /* MMU Access Level Violation Error */

#define EXCP_RTE            0x100
#define EXCP_HALT_INSN      0x101

#define M68K_DTTR0   0
#define M68K_DTTR1   1
#define M68K_ITTR0   2
#define M68K_ITTR1   3

#define M68K_MAX_TTR 2
#define TTR(type, index) ttr[((type & ACCESS_CODE) == ACCESS_CODE) * 2 + index]

#define TARGET_INSN_START_EXTRA_WORDS 1

typedef CPU_LDoubleU FPReg;

typedef struct CPUArchState {
    uint32_t dregs[8];
    uint32_t aregs[8];
    uint32_t pc;
    uint32_t sr;

    /*
     * The 68020/30/40 support two supervisor stacks, ISP and MSP.
     * The 68000/10, Coldfire, and CPU32 only have USP/SSP.
     *
     * The current_sp is stored in aregs[7], the other here.
     * The USP, SSP, and if used the additional ISP for 68020/30/40.
     */
    int current_sp;
    uint32_t sp[3];

    /* Condition flags.  */
    uint32_t cc_op;
    uint32_t cc_x; /* always 0/1 */
    uint32_t cc_n; /* in bit 31 (i.e. negative) */
    uint32_t cc_v; /* in bit 31, unused, or computed from cc_n and cc_v */
    uint32_t cc_c; /* either 0/1, unused, or computed from cc_n and cc_v */
    uint32_t cc_z; /* == 0 or unused */

    FPReg fregs[8];
    FPReg fp_result;
    uint32_t fpcr;
    uint32_t fpsr;
    float_status fp_status;

    uint64_t mactmp;
    /*
     * EMAC Hardware deals with 48-bit values composed of one 32-bit and
     * two 8-bit parts.  We store a single 64-bit value and
     * rearrange/extend this when changing modes.
     */
    uint64_t macc[4];
    uint32_t macsr;
    uint32_t mac_mask;

    /* MMU status.  */
    struct {
        /*
         * Holds the "address" value in between raising an exception
         * and creation of the exception stack frame.
         * Used for both Format 7 exceptions (Access, i.e. mmu)
         * and Format 2 exceptions (chk, div0, trapcc, etc).
         */
        uint32_t ar;
        uint32_t ssw;
        /* 68040 */
        uint16_t tcr;
        uint32_t urp;
        uint32_t srp;
        bool fault;
        uint32_t ttr[4];
        uint32_t mmusr;
    } mmu;

    /* Control registers.  */
    uint32_t vbr;
    uint32_t mbar;
    uint32_t rambar0;
    uint32_t cacr;
    uint32_t sfc;
    uint32_t dfc;

    int pending_vector;
    int pending_level;

    /* Fields up to this point are cleared by a CPU reset */
    struct {} end_reset_fields;

    /* Fields from here on are preserved across CPU reset. */
    uint64_t features;
} CPUM68KState;

/*
 * M68kCPU:
 * @env: #CPUM68KState
 *
 * A Motorola 68k CPU.
 */
struct ArchCPU {
    /*< private >*/
    CPUState parent_obj;
    /*< public >*/

    CPUNegativeOffsetState neg;
    CPUM68KState env;
};


#ifndef CONFIG_USER_ONLY
void m68k_cpu_do_interrupt(CPUState *cpu);
bool m68k_cpu_exec_interrupt(CPUState *cpu, int int_req);
hwaddr m68k_cpu_get_phys_page_debug(CPUState *cpu, vaddr addr);
#endif /* !CONFIG_USER_ONLY */
void m68k_cpu_dump_state(CPUState *cpu, FILE *f, int flags);
int m68k_cpu_gdb_read_register(CPUState *cpu, GByteArray *buf, int reg);
int m68k_cpu_gdb_write_register(CPUState *cpu, uint8_t *buf, int reg);

void m68k_tcg_init(void);
void m68k_cpu_init_gdb(M68kCPU *cpu);
uint32_t cpu_m68k_get_ccr(CPUM68KState *env);
void cpu_m68k_set_ccr(CPUM68KState *env, uint32_t);
void cpu_m68k_set_sr(CPUM68KState *env, uint32_t);
void cpu_m68k_restore_fp_status(CPUM68KState *env);
void cpu_m68k_set_fpcr(CPUM68KState *env, uint32_t val);


/*
 * Instead of computing the condition codes after each m68k instruction,
 * QEMU just stores one operand (called CC_SRC), the result
 * (called CC_DEST) and the type of operation (called CC_OP). When the
 * condition codes are needed, the condition codes can be calculated
 * using this information. Condition codes are not generated if they
 * are only needed for conditional branches.
 */
typedef enum {
    /* Translator only -- use env->cc_op.  */
    CC_OP_DYNAMIC,

    /* Each flag bit computed into cc_[xcnvz].  */
    CC_OP_FLAGS,

    /* X in cc_x, C = X, N in cc_n, Z in cc_n, V via cc_n/cc_v.  */
    CC_OP_ADDB, CC_OP_ADDW, CC_OP_ADDL,
    CC_OP_SUBB, CC_OP_SUBW, CC_OP_SUBL,

    /* X in cc_x, {N,Z,C,V} via cc_n/cc_v.  */
    CC_OP_CMPB, CC_OP_CMPW, CC_OP_CMPL,

    /* X in cc_x, C = 0, V = 0, N in cc_n, Z in cc_n.  */
    CC_OP_LOGIC,

    CC_OP_NB
} CCOp;

#define CCF_C 0x01
#define CCF_V 0x02
#define CCF_Z 0x04
#define CCF_N 0x08
#define CCF_X 0x10

#define SR_I_SHIFT 8
#define SR_I  0x0700
#define SR_M  0x1000
#define SR_S  0x2000
#define SR_T_SHIFT 14
#define SR_T  0xc000

#define M68K_SR_TRACE(sr) ((sr & SR_T) >> SR_T_SHIFT)
#define M68K_SR_TRACE_ANY_INS 0x2

#define M68K_SSP    0
#define M68K_USP    1
#define M68K_ISP    2

/* bits for 68040 special status word */
#define M68K_CP_040  0x8000
#define M68K_CU_040  0x4000
#define M68K_CT_040  0x2000
#define M68K_CM_040  0x1000
#define M68K_MA_040  0x0800
#define M68K_ATC_040 0x0400
#define M68K_LK_040  0x0200
#define M68K_RW_040  0x0100
#define M68K_SIZ_040 0x0060
#define M68K_TT_040  0x0018
#define M68K_TM_040  0x0007

#define M68K_TM_040_DATA  0x0001
#define M68K_TM_040_CODE  0x0002
#define M68K_TM_040_SUPER 0x0004

/* bits for 68040 write back status word */
#define M68K_WBV_040   0x80
#define M68K_WBSIZ_040 0x60
#define M68K_WBBYT_040 0x20
#define M68K_WBWRD_040 0x40
#define M68K_WBLNG_040 0x00
#define M68K_WBTT_040  0x18
#define M68K_WBTM_040  0x07

/* bus access size codes */
#define M68K_BA_SIZE_MASK    0x60
#define M68K_BA_SIZE_BYTE    0x20
#define M68K_BA_SIZE_WORD    0x40
#define M68K_BA_SIZE_LONG    0x00
#define M68K_BA_SIZE_LINE    0x60

/* bus access transfer type codes */
#define M68K_BA_TT_MOVE16    0x08

/* bits for 68040 MMU status register (mmusr) */
#define M68K_MMU_B_040   0x0800
#define M68K_MMU_G_040   0x0400
#define M68K_MMU_U1_040  0x0200
#define M68K_MMU_U0_040  0x0100
#define M68K_MMU_S_040   0x0080
#define M68K_MMU_CM_040  0x0060
#define M68K_MMU_M_040   0x0010
#define M68K_MMU_WP_040  0x0004
#define M68K_MMU_T_040   0x0002
#define M68K_MMU_R_040   0x0001

#define M68K_MMU_SR_MASK_040 (M68K_MMU_G_040 | M68K_MMU_U1_040 | \
                              M68K_MMU_U0_040 | M68K_MMU_S_040 | \
                              M68K_MMU_CM_040 | M68K_MMU_M_040 | \
                              M68K_MMU_WP_040)

/* bits for 68040 MMU Translation Control Register */
#define M68K_TCR_ENABLED 0x8000
#define M68K_TCR_PAGE_8K 0x4000

/* bits for 68040 MMU Table Descriptor / Page Descriptor / TTR */
#define M68K_DESC_WRITEPROT 0x00000004
#define M68K_DESC_USED      0x00000008
#define M68K_DESC_MODIFIED  0x00000010
#define M68K_DESC_CACHEMODE 0x00000060
#define M68K_DESC_CM_WRTHRU 0x00000000
#define M68K_DESC_CM_COPYBK 0x00000020
#define M68K_DESC_CM_SERIAL 0x00000040
#define M68K_DESC_CM_NCACHE 0x00000060
#define M68K_DESC_SUPERONLY 0x00000080
#define M68K_DESC_USERATTR  0x00000300
#define M68K_DESC_USERATTR_SHIFT     8
#define M68K_DESC_GLOBAL    0x00000400
#define M68K_DESC_URESERVED 0x00000800

#define M68K_ROOT_POINTER_ENTRIES   128
#define M68K_4K_PAGE_MASK           (~0xff)
#define M68K_POINTER_BASE(entry)    (entry & ~0x1ff)
#define M68K_ROOT_INDEX(addr)       ((address >> 23) & 0x1fc)
#define M68K_POINTER_INDEX(addr)    ((address >> 16) & 0x1fc)
#define M68K_4K_PAGE_BASE(entry)    (next & M68K_4K_PAGE_MASK)
#define M68K_4K_PAGE_INDEX(addr)    ((address >> 10) & 0xfc)
#define M68K_8K_PAGE_MASK           (~0x7f)
#define M68K_8K_PAGE_BASE(entry)    (next & M68K_8K_PAGE_MASK)
#define M68K_8K_PAGE_INDEX(addr)    ((address >> 11) & 0x7c)
#define M68K_UDT_VALID(entry)       (entry & 2)
#define M68K_PDT_VALID(entry)       (entry & 3)
#define M68K_PDT_INDIRECT(entry)    ((entry & 3) == 2)
#define M68K_INDIRECT_POINTER(addr) (addr & ~3)
#define M68K_TTS_POINTER_SHIFT      18
#define M68K_TTS_ROOT_SHIFT         25

/* bits for 68040 MMU Transparent Translation Registers */
#define M68K_TTR_ADDR_BASE 0xff000000
#define M68K_TTR_ADDR_MASK 0x00ff0000
#define M68K_TTR_ADDR_MASK_SHIFT    8
#define M68K_TTR_ENABLED   0x00008000
#define M68K_TTR_SFIELD    0x00006000
#define M68K_TTR_SFIELD_USER   0x0000
#define M68K_TTR_SFIELD_SUPER  0x2000

/* m68k Control Registers */

/* ColdFire */
/* Memory Management Control Registers */
#define M68K_CR_ASID     0x003
#define M68K_CR_ACR0     0x004
#define M68K_CR_ACR1     0x005
#define M68K_CR_ACR2     0x006
#define M68K_CR_ACR3     0x007
#define M68K_CR_MMUBAR   0x008

/* Processor Miscellaneous Registers */
#define M68K_CR_PC       0x80F

/* Local Memory and Module Control Registers */
#define M68K_CR_ROMBAR0  0xC00
#define M68K_CR_ROMBAR1  0xC01
#define M68K_CR_RAMBAR0  0xC04
#define M68K_CR_RAMBAR1  0xC05
#define M68K_CR_MPCR     0xC0C
#define M68K_CR_EDRAMBAR 0xC0D
#define M68K_CR_SECMBAR  0xC0E
#define M68K_CR_MBAR     0xC0F

/* Local Memory Address Permutation Control Registers */
#define M68K_CR_PCR1U0   0xD02
#define M68K_CR_PCR1L0   0xD03
#define M68K_CR_PCR2U0   0xD04
#define M68K_CR_PCR2L0   0xD05
#define M68K_CR_PCR3U0   0xD06
#define M68K_CR_PCR3L0   0xD07
#define M68K_CR_PCR1U1   0xD0A
#define M68K_CR_PCR1L1   0xD0B
#define M68K_CR_PCR2U1   0xD0C
#define M68K_CR_PCR2L1   0xD0D
#define M68K_CR_PCR3U1   0xD0E
#define M68K_CR_PCR3L1   0xD0F

/* MC680x0 */
/* MC680[1234]0/CPU32 */
#define M68K_CR_SFC      0x000
#define M68K_CR_DFC      0x001
#define M68K_CR_USP      0x800
#define M68K_CR_VBR      0x801 /* + Coldfire */

/* MC680[234]0 */
#define M68K_CR_CACR     0x002 /* + Coldfire */
#define M68K_CR_CAAR     0x802 /* MC68020 and MC68030 only */
#define M68K_CR_MSP      0x803
#define M68K_CR_ISP      0x804

/* MC68040/MC68LC040 */
#define M68K_CR_TC       0x003
#define M68K_CR_ITT0     0x004
#define M68K_CR_ITT1     0x005
#define M68K_CR_DTT0     0x006
#define M68K_CR_DTT1     0x007
#define M68K_CR_MMUSR    0x805
#define M68K_CR_URP      0x806
#define M68K_CR_SRP      0x807

/* MC68EC040 */
#define M68K_CR_IACR0    0x004
#define M68K_CR_IACR1    0x005
#define M68K_CR_DACR0    0x006
#define M68K_CR_DACR1    0x007

/* MC68060 */
#define M68K_CR_BUSCR    0x008
#define M68K_CR_PCR      0x808

#define M68K_FPIAR_SHIFT  0
#define M68K_FPIAR        (1 << M68K_FPIAR_SHIFT)
#define M68K_FPSR_SHIFT   1
#define M68K_FPSR         (1 << M68K_FPSR_SHIFT)
#define M68K_FPCR_SHIFT   2
#define M68K_FPCR         (1 << M68K_FPCR_SHIFT)

/* Floating-Point Status Register */

/* Condition Code */
#define FPSR_CC_MASK  0x0f000000
#define FPSR_CC_A     0x01000000 /* Not-A-Number */
#define FPSR_CC_I     0x02000000 /* Infinity */
#define FPSR_CC_Z     0x04000000 /* Zero */
#define FPSR_CC_N     0x08000000 /* Negative */

/* Quotient */

#define FPSR_QT_MASK  0x00ff0000
#define FPSR_QT_SHIFT 16

/* Floating-Point Control Register */
/* Rounding mode */
#define FPCR_RND_MASK   0x0030
#define FPCR_RND_N      0x0000
#define FPCR_RND_Z      0x0010
#define FPCR_RND_M      0x0020
#define FPCR_RND_P      0x0030

/* Rounding precision */
#define FPCR_PREC_MASK  0x00c0
#define FPCR_PREC_X     0x0000
#define FPCR_PREC_S     0x0040
#define FPCR_PREC_D     0x0080
#define FPCR_PREC_U     0x00c0

#define FPCR_EXCP_MASK 0xff00

/* CACR fields are implementation defined, but some bits are common.  */
#define M68K_CACR_EUSP  0x10

#define MACSR_PAV0  0x100
#define MACSR_OMC   0x080
#define MACSR_SU    0x040
#define MACSR_FI    0x020
#define MACSR_RT    0x010
#define MACSR_N     0x008
#define MACSR_Z     0x004
#define MACSR_V     0x002
#define MACSR_EV    0x001

void m68k_set_irq_level(M68kCPU *cpu, int level, uint8_t vector);
void m68k_switch_sp(CPUM68KState *env);

void do_m68k_semihosting(CPUM68KState *env, int nr);

/*
 * The 68000 family is defined in six main CPU classes, the 680[012346]0.
 * Generally each successive CPU adds enhanced data/stack/instructions.
 * However, some features are only common to one, or a few classes.
 * The features covers those subsets of instructons.
 *
 * CPU32/32+ are basically 680010 compatible with some 68020 class instructons,
 * and some additional CPU32 instructions. Mostly Supervisor state differences.
 *
 * The ColdFire core ISA is a RISC-style reduction of the 68000 series cpu.
 * There are 4 ColdFire core ISA revisions: A, A+, B and C.
 * Each feature covers the subset of instructions common to the
 * ISA revisions mentioned.
 */

enum m68k_features {
    /* Base Motorola CPU set (not set for Coldfire CPUs) */
    M68K_FEATURE_M68K,
    /* Motorola CPU feature sets */
    M68K_FEATURE_M68010,
    M68K_FEATURE_M68020,
    M68K_FEATURE_M68030,
    M68K_FEATURE_M68040,
    M68K_FEATURE_M68060,
    /* Base Coldfire set Rev A. */
    M68K_FEATURE_CF_ISA_A,
    /* (ISA B or C). */
    M68K_FEATURE_CF_ISA_B,
    /* BIT/BITREV, FF1, STRLDSR (ISA A+ or C). */
    M68K_FEATURE_CF_ISA_APLUSC,
    /* BRA with Long branch. (680[2346]0, ISA A+ or B). */
    M68K_FEATURE_BRAL,
    M68K_FEATURE_CF_FPU,
    M68K_FEATURE_CF_MAC,
    M68K_FEATURE_CF_EMAC,
    /* Revision B EMAC (dual accumulate). */
    M68K_FEATURE_CF_EMAC_B,
    /* User Stack Pointer. (680[012346]0, ISA A+, B or C). */
    M68K_FEATURE_USP,
    /* Master Stack Pointer. (680[234]0) */
    M68K_FEATURE_MSP,
    /* 68020+ full extension word. */
    M68K_FEATURE_EXT_FULL,
    /* word sized address index registers. */
    M68K_FEATURE_WORD_INDEX,
    /* scaled address index registers. */
    M68K_FEATURE_SCALED_INDEX,
    /* 32 bit mul/div. (680[2346]0, and CPU32) */
    M68K_FEATURE_LONG_MULDIV,
    /* 64 bit mul/div. (680[2346]0, and CPU32) */
    M68K_FEATURE_QUAD_MULDIV,
    /* Bcc with Long branches. (680[2346]0, and CPU32) */
    M68K_FEATURE_BCCL,
    /* BFxxx Bit field insns. (680[2346]0) */
    M68K_FEATURE_BITFIELD,
    /* fpu insn. (680[46]0) */
    M68K_FEATURE_FPU,
    /* CAS/CAS2[WL] insns. (680[2346]0) */
    M68K_FEATURE_CAS,
    /* BKPT insn. (680[12346]0, and CPU32) */
    M68K_FEATURE_BKPT,
    /* RTD insn. (680[12346]0, and CPU32) */
    M68K_FEATURE_RTD,
    /* CHK2 insn. (680[2346]0, and CPU32) */
    M68K_FEATURE_CHK2,
    /* MOVEP insn. (680[01234]0, and CPU32) */
    M68K_FEATURE_MOVEP,
    /* MOVEC insn. (from 68010) */
    M68K_FEATURE_MOVEC,
    /* Unaligned data accesses (680[2346]0) */
    M68K_FEATURE_UNALIGNED_DATA,
    /* TRAPcc insn. (680[2346]0, and CPU32) */
    M68K_FEATURE_TRAPCC,
    /* MOVE from SR privileged (from 68010) */
    M68K_FEATURE_MOVEFROMSR_PRIV,
};

static inline bool m68k_feature(CPUM68KState *env, int feature)
{
    return (env->features & BIT_ULL(feature)) != 0;
}

void m68k_cpu_list(void);

void register_m68k_insns (CPUM68KState *env);

enum {
    /* 1 bit to define user level / supervisor access */
    ACCESS_SUPER = 0x01,
    /* 1 bit to indicate direction */
    ACCESS_STORE = 0x02,
    /* 1 bit to indicate debug access */
    ACCESS_DEBUG = 0x04,
    /* PTEST instruction */
    ACCESS_PTEST = 0x08,
    /* Type of instruction that generated the access */
    ACCESS_CODE  = 0x10, /* Code fetch access                */
    ACCESS_DATA  = 0x20, /* Data load/store access        */
};

#define M68K_CPU_TYPE_SUFFIX "-" TYPE_M68K_CPU
#define M68K_CPU_TYPE_NAME(model) model M68K_CPU_TYPE_SUFFIX
#define CPU_RESOLVING_TYPE TYPE_M68K_CPU

#define cpu_list m68k_cpu_list

/* MMU modes definitions */
#define MMU_KERNEL_IDX 0
#define MMU_USER_IDX 1
static inline int cpu_mmu_index (CPUM68KState *env, bool ifetch)
{
    return (env->sr & SR_S) == 0 ? 1 : 0;
}

bool m68k_cpu_tlb_fill(CPUState *cs, vaddr address, int size,
                       MMUAccessType access_type, int mmu_idx,
                       bool probe, uintptr_t retaddr);
#ifndef CONFIG_USER_ONLY
void m68k_cpu_transaction_failed(CPUState *cs, hwaddr physaddr, vaddr addr,
                                 unsigned size, MMUAccessType access_type,
                                 int mmu_idx, MemTxAttrs attrs,
                                 MemTxResult response, uintptr_t retaddr);
#endif

#include "exec/cpu-all.h"

/* TB flags */
#define TB_FLAGS_MACSR          0x0f
#define TB_FLAGS_MSR_S_BIT      13
#define TB_FLAGS_MSR_S          (1 << TB_FLAGS_MSR_S_BIT)
#define TB_FLAGS_SFC_S_BIT      14
#define TB_FLAGS_SFC_S          (1 << TB_FLAGS_SFC_S_BIT)
#define TB_FLAGS_DFC_S_BIT      15
#define TB_FLAGS_DFC_S          (1 << TB_FLAGS_DFC_S_BIT)
#define TB_FLAGS_TRACE          16
#define TB_FLAGS_TRACE_BIT      (1 << TB_FLAGS_TRACE)

static inline void cpu_get_tb_cpu_state(CPUM68KState *env, target_ulong *pc,
                                        target_ulong *cs_base, uint32_t *flags)
{
    *pc = env->pc;
    *cs_base = 0;
    *flags = (env->macsr >> 4) & TB_FLAGS_MACSR;
    if (env->sr & SR_S) {
        *flags |= TB_FLAGS_MSR_S;
        *flags |= (env->sfc << (TB_FLAGS_SFC_S_BIT - 2)) & TB_FLAGS_SFC_S;
        *flags |= (env->dfc << (TB_FLAGS_DFC_S_BIT - 2)) & TB_FLAGS_DFC_S;
    }
    if (M68K_SR_TRACE(env->sr) == M68K_SR_TRACE_ANY_INS) {
        *flags |= TB_FLAGS_TRACE;
    }
}

void dump_mmu(CPUM68KState *env);

#endif

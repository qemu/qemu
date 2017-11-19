/*
 * CSKY virtual CPU header
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef CSKY_CPU_H
#define CSKY_CPU_H

#define ALIGNED_ONLY

#define CPUArchState struct CPUCSKYState

/* target long bits */
#define TARGET_LONG_BITS    32

#define TARGET_PAGE_BITS    12

#define TARGET_PHYS_ADDR_SPACE_BITS 32
#define TARGET_VIRT_ADDR_SPACE_BITS 32

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "exec/cpu-defs.h"
#include "cpu-qom.h"

#include "fpu/softfloat.h"

/* CSKY Exception definition */
#define EXCP_NONE                   -1
#define EXCP_CSKY_RESET             0
#define EXCP_CSKY_ALIGN             1
#define EXCP_CSKY_DATA_ABORT        2
#define EXCP_CSKY_DIV               3
#define EXCP_CSKY_UDEF              4
#define EXCP_CSKY_PRIVILEGE         5
#define EXCP_CSKY_TRACE             6
#define EXCP_CSKY_BKPT              7
#define EXCP_CSKY_URESTORE          8
#define EXCP_CSKY_IDLY4             9
#define EXCP_CSKY_IRQ               10
#define EXCP_CSKY_FIQ               11
#define EXCP_CSKY_HAI               12
#define EXCP_CSKY_FP                13
#define EXCP_CSKY_TLB_UNMATCH       14
#define EXCP_CSKY_TLB_MODIFY        15
#define EXCP_CSKY_TRAP0             16
#define EXCP_CSKY_TRAP1             17
#define EXCP_CSKY_TRAP2             18
#define EXCP_CSKY_TRAP3             19
#define EXCP_CSKY_TLB_READ_INVALID  20
#define EXCP_CSKY_TLB_WRITE_INVALID 21
#define EXCP_CSKY_FLOAT             30
#define EXCP_CSKY_CPU_END           31

#define CPU_INTERRUPT_FIQ   CPU_INTERRUPT_TGT_EXT_1

#define NB_MMU_MODES 2

#define TB_TRACE_NUM 4096
struct csky_trace_info {
    int tb_pc;
};

/* MMU Control Registers */
struct CSKYMMU {
   uint32_t mir;        /* CR0 */
   uint32_t mrr;        /* CR1 */
   uint32_t mel0;       /* CR2 */
   uint32_t mel1;       /* CR3 */
   uint32_t meh;        /* CR4 */
   uint32_t mcr;        /* CR5 */
   uint32_t mpr;        /* CR6 */
   uint32_t mwr;        /* CR7 */
   uint32_t mcir;       /* CR8 */
   uint32_t cr9;        /* CR9 */
   uint32_t cr10;       /* CR10 */
   uint32_t cr11;       /* CR11 */
   uint32_t cr12;       /* CR12 */
   uint32_t cr13;       /* CR13 */
   uint32_t cr14;       /* CR14 */
   uint32_t cr15;       /* CR15 */
   uint32_t cr16;       /* CR16 */
   uint32_t mpar;       /* CR29 */
   uint32_t msa0;       /* CR30 */
   uint32_t msa1;       /* CR31 */
};

/* CSKY CPUCSKYState definition */
typedef struct CPUCSKYState {
    uint32_t regs[32];
    /* target pc */
    uint32_t pc;
    /* C register PSR[0] */
    uint32_t psr_c;
    /* S register PSR[31] */
    uint32_t psr_s;
    /* T register PSR[30] */
    uint32_t psr_t;
    /* bm register PSR[10] */
    uint32_t psr_bm;
    /* TM register PSR[15:14] */
    uint32_t psr_tm;
    /* dsp control status register */
    uint32_t dcsr_v;
    /* dsp hi, lo, high_guard, lo_guard register */
    uint32_t hi;
    uint32_t lo;
    uint32_t hi_guard;
    uint32_t lo_guard;
    /* Banked Registers */
    uint32_t banked_regs[16];
    /* Idly4 counter */
    uint32_t idly4_counter;
    /* which instructions sequences should be translation */
    uint32_t sce_condexec_bits;
    /* sce sequence may be interrupted */
    uint32_t sce_condexec_bits_bk;
    /* interface for intc */
    struct {
        uint32_t avec_b;
        uint32_t fint_b;
        uint32_t int_b;
        uint32_t vec_b;
        uint32_t iabr;
        uint32_t isr;
        uint32_t iptr;
        uint32_t issr;
    } intc_signals;

    /* system control coprocessor (cp0) */
    struct {
        uint32_t psr;    /* CR0 */
        uint32_t vbr;    /* CR1 */
        uint32_t epsr;   /* CR2 */
        uint32_t fpsr;   /* CR3 */
        uint32_t epc;    /* CR4 */
        uint32_t fpc;    /* CR5 */
        uint32_t ss0;    /* CR6 */
        uint32_t ss1;    /* CR7 */
        uint32_t ss2;    /* CR8 */
        uint32_t ss3;    /* CR9 */
        uint32_t ss4;    /* CR10 */
        uint32_t gcr;    /* CR11 */
        uint32_t gsr;    /* CR12 */
        uint32_t cpidr[4];  /* CSKYV2 have four physic CR13 register */
        uint32_t cpidr_counter;
        uint32_t dcsr;    /* CR14 */
        uint32_t cpwr;    /* CR15 */
        uint32_t dummy;   /* no CR16 */
        uint32_t cfr;     /* CR17 */
        uint32_t ccr;     /* CR18 */
        uint32_t capr;    /* CR19 */
        uint32_t pacr[8]; /* CR20 */
        uint32_t prsr;    /* CR21 */
    } cp0;

    /* stack point, sp used now is put in regs[14].
     * if cpu not has the feature ABIV2_TEE, only use nt_Xsp. */
    struct {
        uint32_t nt_usp;  /* Non-secured user sp */
        uint32_t nt_ssp;  /* CR<6,3>, Non-secured supervisor sp */
        uint32_t nt_asp;  /* AF = 1, Non-secured sp */
        uint32_t t_usp;   /* CR<7,3>, Secured user sp */
        uint32_t t_ssp;   /* Secured supervisor sp */
        uint32_t t_asp;   /* AF = 1, Secured sp */
    } stackpoint;

    /* registers for tee */
    struct {
        uint32_t t_psr;   /* CR<0,0>, T_PSR */
        uint32_t nt_psr;  /* CR<0,0>, CR<0,3>, NT_PSR */
        uint32_t t_vbr;   /* CR<1,0>, T_VBR */
        uint32_t nt_vbr;  /* CR<1,0>, CR<1,3>, NT_VBR */
        uint32_t t_epsr;  /* CR<2,0>, T_EPSR */
        uint32_t nt_epsr; /* CR<2,0>, CR<2,3>, NT_EPSR */
        uint32_t t_epc;   /* CR<4,0>, T_EPC */
        uint32_t nt_epc;  /* CR<4,0>, CR<4,3>, NT_EPC */
        uint32_t t_dcr;   /* CR<8,3>, T_DCR */
        uint32_t t_pcr;   /* CR<9,3>, T_PCR */
        uint32_t t_ebr;   /* CR<1,1>, T_EBR */
        uint32_t nt_ebr;  /* CR<1,1>, CR<10,3>, NT_EBR */
    } tee;

    /* FPU registers */
    struct {
        float32 fr[32];     /* FPU general registers */
        uint32_t fpcid;     /* Provide the information about FPC. */
        uint32_t fcr;       /* Control register of FPC */
        uint32_t fsr;       /* Status register of FPC */
        uint32_t fir;       /* Instruction register of FPC */
        uint32_t fesr;      /* Status register for exception process */
        uint32_t feinst1;   /* The exceptional instruction */
        uint32_t feinst2;   /* The exceptional instruction */
        float_status fp_status;
        float_status standard_fp_status;
    } cp1;

    /* VFP coprocessor state.  */
    struct {
        union VDSP {
            float64  fpu[2];
            uint64_t udspl[2];
            uint32_t udspi[4];
            int32_t  dspi[4];
            uint16_t udsps[8];
            int16_t  dsps[8];
            uint8_t  udspc[16];
            int8_t   dspc[16];
        } reg[16];
        uint32_t fid;
        uint32_t fcr;
        uint32_t fesr;
        /* fp_status is the "normal" fp status. standard_fp_status retains
         * values corresponding to the ARM "Standard FPSCR Value", ie
         * default-NaN, flush-to-zero, round-to-nearest and is used by
         * any operations (generally Neon) which the architecture defines
         * as controlled by the standard FPSCR value rather than the FPSCR.
         *
         * To avoid having to transfer exception bits around, we simply
         * say that the FPSCR cumulative exception flags are the logical
         * OR of the flags in the two fp statuses. This relies on the
         * only thing which needs to read the exception flags being
         * an explicit FPSCR read.
         */
        float_status fp_status;
        float_status standard_fp_status;
    } vfp;

    struct CSKYMMU mmu;     /* mmu control registers used now. */
    struct CSKYMMU nt_mmu;  /* Non-Trust mmu control registers. */
    struct CSKYMMU t_mmu;   /* Non-Trust mmu control registers. */

#if !defined(CONFIG_USER_ONLY)
    struct CPUCSKYTLBContext *tlb_context;
#endif

    uint32_t tls_value;
    CPU_COMMON

    /* These fields after the common ones so they are preserved on reset.  */

    /* Internal CPU feature flags.  */
    uint64_t features;

    /* pctrace */
    uint32_t pctraces_max_num;

    /* binstart */
    uint32_t binstart;

    uint32_t cpuid;

    void *nvic;

    uint32_t mmu_default;

    uint32_t tb_trace;
    uint32_t jcount_start;
    uint32_t jcount_end;

    struct csky_boot_info *boot_info;
    struct csky_trace_info *trace_info;
    uint32_t trace_index;
} CPUCSKYState;

/**
 * CSKYCPU:
 * @env: #CPUCSKYState
 *
 * A CSKY CPU.
 */
struct CSKYCPU {
    /*< private >*/
    CPUState parent_obj;
    /*< public >*/

    CPUCSKYState env;
};

static inline CSKYCPU *csky_env_get_cpu(CPUCSKYState *env)
{
    return container_of(env, CSKYCPU, env);
}

#define ENV_GET_CPU(e) CPU(csky_env_get_cpu(e))

#define ENV_OFFSET offsetof(CSKYCPU, env)

/* functions statement */
CSKYCPU *cpu_csky_init(const char *cpu_model);
void csky_translate_init(void);
int csky_cpu_handle_mmu_fault(CPUState *cs, vaddr address, int rw,
                              int mmu_idx);
int cpu_csky_signal_handler(int host_signum, void *pinfo, void *puc);
void csky_cpu_list(FILE *f, fprintf_function cpu_fprintf);

void csky_cpu_do_unaligned_access(CPUState *cs, vaddr vaddr,
                                  MMUAccessType access_type,
                                  int mmu_idx, uintptr_t retaddr);
int csky_cpu_gdb_read_register(CPUState *cs, uint8_t *mem_buf, int n);
int csky_cpu_gdb_write_register(CPUState *cs, uint8_t *mem_buf, int n);
void csky_cpu_do_interrupt(CPUState *cs);
hwaddr csky_cpu_get_phys_page_debug(CPUState *env, vaddr addr);
bool csky_cpu_exec_interrupt(CPUState *cs, int interrupt_request);
void csky_nommu_init(CPUCSKYState *env);
void csky_cpu_dump_state(CPUState *cs, FILE *f, fprintf_function cpu_fprintf,
                         int flags);

#define cpu_init(cpu_model) CPU(cpu_csky_init(cpu_model))
#define cpu_signal_handler  cpu_csky_signal_handler
#define cpu_list    csky_cpu_list

/* FIXME MMU modes definitions */
#define MMU_USER_IDX  0
#define CSKY_USERMODE 0

#include "exec/cpu-all.h"

/* bit usage in tb flags field. */
#define CSKY_TBFLAG_SCE_CONDEXEC_SHIFT  0
#define CSKY_TBFLAG_SCE_CONDEXEC_MASK   (0x1F << CSKY_TBFLAG_SCE_CONDEXEC_SHIFT)
#define CSKY_TBFLAG_PSR_S_SHIFT         5
#define CSKY_TBFLAG_PSR_S_MASK          (0x1 << CSKY_TBFLAG_PSR_S_SHIFT)
#define CSKY_TBFLAG_CPID_SHIFT          6
#define CSKY_TBFLAG_CPID_MASK           (0xF << CSKY_TBFLAG_CPID_SHIFT)
#define CSKY_TBFLAG_ASID_SHIFT          10
#define CSKY_TBFLAG_ASID_MASK           (0xFF << CSKY_TBFLAG_ASID_SHIFT)
#define CSKY_TBFLAG_PSR_BM_SHIFT        18
#define CSKY_TBFLAG_PSR_BM_MASK         (0x1 << CSKY_TBFLAG_PSR_BM_SHIFT)
#define CSKY_TBFLAG_PSR_TM_SHIFT        19
#define CSKY_TBFLAG_PSR_TM_MASK         (0x3 << CSKY_TBFLAG_PSR_TM_SHIFT)
#define CSKY_TBFLAG_PSR_T_SHIFT         21
#define CSKY_TBFLAG_PSR_T_MASK          (0x1 << CSKY_TBFLAG_PSR_T_SHIFT)
#define CSKY_TBFLAG_IDLY4_SHIFT         22
#define CSKY_TBFLAG_IDLY4_MASK          (0x7 << CSKY_TBFLAG_IDLY4_SHIFT)
/* TB flags[25:31] are unused */


#define CSKY_TBFLAG_SCE_CONDEXEC(flag)  \
    (((flag) & CSKY_TBFLAG_SCE_CONDEXEC_MASK) >> CSKY_TBFLAG_SCE_CONDEXEC_SHIFT)
#define CSKY_TBFLAG_PSR_S(flag) \
    (((flag) & CSKY_TBFLAG_PSR_S_MASK) >> CSKY_TBFLAG_PSR_S_SHIFT)
#define CSKY_TBFLAG_PSR_BM(flag) \
    (((flag) & CSKY_TBFLAG_PSR_BM_MASK) >> CSKY_TBFLAG_PSR_BM_SHIFT)
#define CSKY_TBFLAG_CPID(flag)    \
    (((flag) & CSKY_TBFLAG_CPID_MASK) >> CSKY_TBFLAG_CPID_SHIFT)
#define CSKY_TBFLAG_PSR_TM(flag)    \
    (((flag) & CSKY_TBFLAG_PSR_TM_MASK) >> CSKY_TBFLAG_PSR_TM_SHIFT)
#define CSKY_TBFLAG_PSR_T(flag)    \
    (((flag) & CSKY_TBFLAG_PSR_T_MASK) >> CSKY_TBFLAG_PSR_T_SHIFT)

/* CPU id */
#define CSKY_CPUID_CK510      0x00000000
#define CSKY_CPUID_CK520      0x00000000
#define CSKY_CPUID_CK610      0x1000f002

#define CSKY_CPUID_CK801      0x04880003
#define CSKY_CPUID_CK802      0x04880003
#define CSKY_CPUID_CK803      0x04800003
#define CSKY_CPUID_CK803S     0x04900003
#define CSKY_CPUID_CK807      0x048c0203  /* default mmu */
#define CSKY_CPUID_CK810      0x04840203  /* default mmu */

/* cpu features flags */
#define CPU_ABIV1               (1 << 0)
#define CPU_ABIV2               (1 << 1)
#define CPU_510                 (1 << 2)
#define CPU_520                 (1 << 3)
#define CPU_610                 (1 << 4)
#define CPU_801                 (1 << 6)
#define CPU_802                 (1 << 7)
#define CPU_803                 (1 << 8)
#define CPU_803S                (1 << 9)
#define CPU_807                 (1 << 10)
#define CPU_810                 (1 << 11)
#define CSKY_MMU                (1 << 16)
#define CSKY_MGU                (1 << 17)
#define ABIV1_DSP               (1 << 18)
#define ABIV1_FPU               (1 << 19)
#define ABIV2_TEE               (1 << 20)
#define ABIV2_DSP               (1 << 21)
#define ABIV2_FPU               (1 << 22)
#define ABIV2_FPU_803S          (1 << 23)
#define ABIV2_EDSP              (1 << 24)
#define ABIV2_803S_R1           (1 << 25)
#define ABIV2_JAVA              (1 << 26)
#define ABIV2_VDSP64            (1 << 27)
#define ABIV2_VDSP128           (1 << 28)
#define ABIV2_ELRW              (1 << 29)
#define UNALIGNED_ACCESS        (1 << 30)

#define ABIV2_FLOAT_S           (ABIV2_FPU_803S | ABIV2_FPU)
#define ABIV2_FLOAT_D           (ABIV2_FPU)
#define ABIV2_FLOAT_ALL         (ABIV2_FPU)

/* get bit from psr */
#define PSR_CPID_MASK   0x0f000000
#define PSR_CPID(psr)   (((psr) & PSR_CPID_MASK) >> 24)
#define PSR_IE_MASK     0x00000040
#define PSR_IE(psr)     (((psr) & PSR_IE_MASK) >> 6)
#define PSR_EE_MASK     0x00000100
#define PSR_EE(psr)     (((psr) & PSR_EE_MASK) >> 8)
#define PSR_FE_MASK     0x00000010
#define PSR_FE(psr)     (((psr) & PSR_FE_MASK) >> 4)
#define PSR_S_MASK      0x80000000
#define PSR_S(psr)      (((psr) & PSR_S_MASK) >> 31)
#define PSR_BM_MASK     0x00000400
#define PSR_BM(psr)     (((psr) & PSR_BM_MASK) >> 10)
#define PSR_C_MASK      0x00000001
#define PSR_C(psr)      (((psr) & PSR_C_MASK) >> 0)
#define PSR_TM_MASK     0x0000c000
#define PSR_TM(psr)     (((psr) & PSR_TM_MASK) >> 14)
#define PSR_TP_MASK     0x00002000
#define PSR_TP(psr)     (((psr) & PSR_TP_MASK) >> 13)
#define PSR_VEC_MASK    0x00ff0000
#define PSR_VEC(psr)    (((psr) & PSR_VEC_MASK) >> 16)
/* get bit from psr when has tee */
#define PSR_T_MASK      0x40000000
#define PSR_T(psr)      (((psr) & PSR_T_MASK) >> 30)
#define PSR_SP_MASK     0x20000000
#define PSR_SP(psr)     (((psr) & PSR_SP_MASK) >> 29)
#define PSR_HS_MASK     0x10000000
#define PSR_HS(psr)     (((psr) & PSR_HS_MASK) >> 28)
#define PSR_SC_MASK     0x08000000
#define PSR_SC(psr)     (((psr) & PSR_SC_MASK) >> 27)
#define PSR_SD_MASK     0x04000000
#define PSR_SD(psr)     (((psr) & PSR_SD_MASK) >> 26)
#define PSR_ST_MASK     0x02000000
#define PSR_ST(psr)     (((psr) & PSR_ST_MASK) >> 25)

/* MMU MCIR bit MASK */
#define CSKY_MCIR_TLBP_SHIFT        31
#define CSKY_MCIR_TLBP_MASK         (1 << CSKY_MCIR_TLBP_SHIFT)
#define CSKY_MCIR_TLBR_SHIFT        30
#define CSKY_MCIR_TLBR_MASK         (1 << CSKY_MCIR_TLBR_SHIFT)
#define CSKY_MCIR_TLBWI_SHIFT       29
#define CSKY_MCIR_TLBWI_MASK        (1 << CSKY_MCIR_TLBWI_SHIFT)
#define CSKY_MCIR_TLBWR_SHIFT       28
#define CSKY_MCIR_TLBWR_MASK        (1 << CSKY_MCIR_TLBWR_SHIFT)
#define CSKY_MCIR_TLBINV_SHIFT      27
#define CSKY_MCIR_TLBINV_MASK       (1 << CSKY_MCIR_TLBINV_SHIFT)
#define CSKY_MCIR_TLBINV_ALL_SHIFT  26
#define CSKY_MCIR_TLBINV_ALL_MASK   (1 << CSKY_MCIR_TLBINV_ALL_SHIFT)
#define CSKY_MCIR_TTLBINV_ALL_SHIFT 24
#define CSKY_MCIR_TTLBINV_ALL_MASK  (1 << CSKY_MCIR_TTLBINV_ALL_SHIFT)

static inline int cpu_mmu_index(CPUCSKYState *env, bool ifetch)
{
    return PSR_S(env->cp0.psr);
}

static inline void cpu_get_tb_cpu_state(CPUCSKYState *env, target_ulong *pc,
        target_ulong *cs_base, uint32_t *flags)
{
    *pc = env->pc;
    *cs_base = 0;
#if defined(TARGET_CSKYV2)
    *flags = (env->psr_s << CSKY_TBFLAG_PSR_S_SHIFT)
        | (env->psr_bm << CSKY_TBFLAG_PSR_BM_SHIFT)
        | (env->sce_condexec_bits << CSKY_TBFLAG_SCE_CONDEXEC_SHIFT)
        | (env->mmu.meh & 0xff) << CSKY_TBFLAG_ASID_SHIFT
        | (env->psr_tm << CSKY_TBFLAG_PSR_TM_SHIFT)
        | (env->psr_t << CSKY_TBFLAG_PSR_T_SHIFT)
        | (env->idly4_counter << CSKY_TBFLAG_IDLY4_SHIFT);
#else
    *flags = (PSR_CPID(env->cp0.psr) << CSKY_TBFLAG_CPID_SHIFT)
        | (env->psr_s << CSKY_TBFLAG_PSR_S_SHIFT)
        | (env->mmu.meh & 0xff) << CSKY_TBFLAG_ASID_SHIFT
        | (env->psr_tm << CSKY_TBFLAG_PSR_TM_SHIFT)
        | (env->idly4_counter << CSKY_TBFLAG_IDLY4_SHIFT);
#endif
}

#endif /* CSKY_CPU_H */

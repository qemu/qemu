/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QEMU LoongArch CPU
 *
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 */

#ifndef LOONGARCH_CPU_H
#define LOONGARCH_CPU_H

#include "qemu/int128.h"
#include "exec/cpu-defs.h"
#include "fpu/softfloat-types.h"
#include "hw/registerfields.h"
#include "qemu/timer.h"
#ifndef CONFIG_USER_ONLY
#include "exec/memory.h"
#endif
#include "cpu-csr.h"

#define IOCSRF_TEMP             0
#define IOCSRF_NODECNT          1
#define IOCSRF_MSI              2
#define IOCSRF_EXTIOI           3
#define IOCSRF_CSRIPI           4
#define IOCSRF_FREQCSR          5
#define IOCSRF_FREQSCALE        6
#define IOCSRF_DVFSV1           7
#define IOCSRF_GMOD             9
#define IOCSRF_VM               11

#define VERSION_REG             0x0
#define FEATURE_REG             0x8
#define VENDOR_REG              0x10
#define CPUNAME_REG             0x20
#define MISC_FUNC_REG           0x420
#define IOCSRM_EXTIOI_EN        48

#define IOCSR_MEM_SIZE          0x428

#define TCG_GUEST_DEFAULT_MO (0)

#define FCSR0_M1    0x1f         /* FCSR1 mask, Enables */
#define FCSR0_M2    0x1f1f0000   /* FCSR2 mask, Cause and Flags */
#define FCSR0_M3    0x300        /* FCSR3 mask, Round Mode */
#define FCSR0_RM    8            /* Round Mode bit num on fcsr0 */

FIELD(FCSR0, ENABLES, 0, 5)
FIELD(FCSR0, RM, 8, 2)
FIELD(FCSR0, FLAGS, 16, 5)
FIELD(FCSR0, CAUSE, 24, 5)

#define GET_FP_CAUSE(REG)      FIELD_EX32(REG, FCSR0, CAUSE)
#define SET_FP_CAUSE(REG, V) \
    do { \
        (REG) = FIELD_DP32(REG, FCSR0, CAUSE, V); \
    } while (0)
#define UPDATE_FP_CAUSE(REG, V) \
    do { \
        (REG) |= FIELD_DP32(0, FCSR0, CAUSE, V); \
    } while (0)

#define GET_FP_ENABLES(REG)    FIELD_EX32(REG, FCSR0, ENABLES)
#define SET_FP_ENABLES(REG, V) \
    do { \
        (REG) = FIELD_DP32(REG, FCSR0, ENABLES, V); \
    } while (0)

#define GET_FP_FLAGS(REG)      FIELD_EX32(REG, FCSR0, FLAGS)
#define SET_FP_FLAGS(REG, V) \
    do { \
        (REG) = FIELD_DP32(REG, FCSR0, FLAGS, V); \
    } while (0)

#define UPDATE_FP_FLAGS(REG, V) \
    do { \
        (REG) |= FIELD_DP32(0, FCSR0, FLAGS, V); \
    } while (0)

#define FP_INEXACT        1
#define FP_UNDERFLOW      2
#define FP_OVERFLOW       4
#define FP_DIV0           8
#define FP_INVALID        16

#define EXCODE(code, subcode) ( ((subcode) << 6) | (code) )
#define EXCODE_MCODE(code)    ( (code) & 0x3f )
#define EXCODE_SUBCODE(code)  ( (code) >> 6 )

#define  EXCCODE_EXTERNAL_INT        64   /* plus external interrupt number */
#define  EXCCODE_INT                 EXCODE(0, 0)
#define  EXCCODE_PIL                 EXCODE(1, 0)
#define  EXCCODE_PIS                 EXCODE(2, 0)
#define  EXCCODE_PIF                 EXCODE(3, 0)
#define  EXCCODE_PME                 EXCODE(4, 0)
#define  EXCCODE_PNR                 EXCODE(5, 0)
#define  EXCCODE_PNX                 EXCODE(6, 0)
#define  EXCCODE_PPI                 EXCODE(7, 0)
#define  EXCCODE_ADEF                EXCODE(8, 0) /* Different exception subcode */
#define  EXCCODE_ADEM                EXCODE(8, 1)
#define  EXCCODE_ALE                 EXCODE(9, 0)
#define  EXCCODE_BCE                 EXCODE(10, 0)
#define  EXCCODE_SYS                 EXCODE(11, 0)
#define  EXCCODE_BRK                 EXCODE(12, 0)
#define  EXCCODE_INE                 EXCODE(13, 0)
#define  EXCCODE_IPE                 EXCODE(14, 0)
#define  EXCCODE_FPD                 EXCODE(15, 0)
#define  EXCCODE_SXD                 EXCODE(16, 0)
#define  EXCCODE_ASXD                EXCODE(17, 0)
#define  EXCCODE_FPE                 EXCODE(18, 0) /* Different exception subcode */
#define  EXCCODE_VFPE                EXCODE(18, 1)
#define  EXCCODE_WPEF                EXCODE(19, 0) /* Different exception subcode */
#define  EXCCODE_WPEM                EXCODE(19, 1)
#define  EXCCODE_BTD                 EXCODE(20, 0)
#define  EXCCODE_BTE                 EXCODE(21, 0)
#define  EXCCODE_DBP                 EXCODE(26, 0) /* Reserved subcode used for debug */

/* cpucfg[0] bits */
FIELD(CPUCFG0, PRID, 0, 32)

/* cpucfg[1] bits */
FIELD(CPUCFG1, ARCH, 0, 2)
FIELD(CPUCFG1, PGMMU, 2, 1)
FIELD(CPUCFG1, IOCSR, 3, 1)
FIELD(CPUCFG1, PALEN, 4, 8)
FIELD(CPUCFG1, VALEN, 12, 8)
FIELD(CPUCFG1, UAL, 20, 1)
FIELD(CPUCFG1, RI, 21, 1)
FIELD(CPUCFG1, EP, 22, 1)
FIELD(CPUCFG1, RPLV, 23, 1)
FIELD(CPUCFG1, HP, 24, 1)
FIELD(CPUCFG1, IOCSR_BRD, 25, 1)
FIELD(CPUCFG1, MSG_INT, 26, 1)

/* cpucfg[2] bits */
FIELD(CPUCFG2, FP, 0, 1)
FIELD(CPUCFG2, FP_SP, 1, 1)
FIELD(CPUCFG2, FP_DP, 2, 1)
FIELD(CPUCFG2, FP_VER, 3, 3)
FIELD(CPUCFG2, LSX, 6, 1)
FIELD(CPUCFG2, LASX, 7, 1)
FIELD(CPUCFG2, COMPLEX, 8, 1)
FIELD(CPUCFG2, CRYPTO, 9, 1)
FIELD(CPUCFG2, LVZ, 10, 1)
FIELD(CPUCFG2, LVZ_VER, 11, 3)
FIELD(CPUCFG2, LLFTP, 14, 1)
FIELD(CPUCFG2, LLFTP_VER, 15, 3)
FIELD(CPUCFG2, LBT_X86, 18, 1)
FIELD(CPUCFG2, LBT_ARM, 19, 1)
FIELD(CPUCFG2, LBT_MIPS, 20, 1)
FIELD(CPUCFG2, LSPW, 21, 1)
FIELD(CPUCFG2, LAM, 22, 1)

/* cpucfg[3] bits */
FIELD(CPUCFG3, CCDMA, 0, 1)
FIELD(CPUCFG3, SFB, 1, 1)
FIELD(CPUCFG3, UCACC, 2, 1)
FIELD(CPUCFG3, LLEXC, 3, 1)
FIELD(CPUCFG3, SCDLY, 4, 1)
FIELD(CPUCFG3, LLDBAR, 5, 1)
FIELD(CPUCFG3, ITLBHMC, 6, 1)
FIELD(CPUCFG3, ICHMC, 7, 1)
FIELD(CPUCFG3, SPW_LVL, 8, 3)
FIELD(CPUCFG3, SPW_HP_HF, 11, 1)
FIELD(CPUCFG3, RVA, 12, 1)
FIELD(CPUCFG3, RVAMAX, 13, 4)

/* cpucfg[4] bits */
FIELD(CPUCFG4, CC_FREQ, 0, 32)

/* cpucfg[5] bits */
FIELD(CPUCFG5, CC_MUL, 0, 16)
FIELD(CPUCFG5, CC_DIV, 16, 16)

/* cpucfg[6] bits */
FIELD(CPUCFG6, PMP, 0, 1)
FIELD(CPUCFG6, PMVER, 1, 3)
FIELD(CPUCFG6, PMNUM, 4, 4)
FIELD(CPUCFG6, PMBITS, 8, 6)
FIELD(CPUCFG6, UPM, 14, 1)

/* cpucfg[16] bits */
FIELD(CPUCFG16, L1_IUPRE, 0, 1)
FIELD(CPUCFG16, L1_IUUNIFY, 1, 1)
FIELD(CPUCFG16, L1_DPRE, 2, 1)
FIELD(CPUCFG16, L2_IUPRE, 3, 1)
FIELD(CPUCFG16, L2_IUUNIFY, 4, 1)
FIELD(CPUCFG16, L2_IUPRIV, 5, 1)
FIELD(CPUCFG16, L2_IUINCL, 6, 1)
FIELD(CPUCFG16, L2_DPRE, 7, 1)
FIELD(CPUCFG16, L2_DPRIV, 8, 1)
FIELD(CPUCFG16, L2_DINCL, 9, 1)
FIELD(CPUCFG16, L3_IUPRE, 10, 1)
FIELD(CPUCFG16, L3_IUUNIFY, 11, 1)
FIELD(CPUCFG16, L3_IUPRIV, 12, 1)
FIELD(CPUCFG16, L3_IUINCL, 13, 1)
FIELD(CPUCFG16, L3_DPRE, 14, 1)
FIELD(CPUCFG16, L3_DPRIV, 15, 1)
FIELD(CPUCFG16, L3_DINCL, 16, 1)

/* cpucfg[17] bits */
FIELD(CPUCFG17, L1IU_WAYS, 0, 16)
FIELD(CPUCFG17, L1IU_SETS, 16, 8)
FIELD(CPUCFG17, L1IU_SIZE, 24, 7)

/* cpucfg[18] bits */
FIELD(CPUCFG18, L1D_WAYS, 0, 16)
FIELD(CPUCFG18, L1D_SETS, 16, 8)
FIELD(CPUCFG18, L1D_SIZE, 24, 7)

/* cpucfg[19] bits */
FIELD(CPUCFG19, L2IU_WAYS, 0, 16)
FIELD(CPUCFG19, L2IU_SETS, 16, 8)
FIELD(CPUCFG19, L2IU_SIZE, 24, 7)

/* cpucfg[20] bits */
FIELD(CPUCFG20, L3IU_WAYS, 0, 16)
FIELD(CPUCFG20, L3IU_SETS, 16, 8)
FIELD(CPUCFG20, L3IU_SIZE, 24, 7)

/*CSR_CRMD */
FIELD(CSR_CRMD, PLV, 0, 2)
FIELD(CSR_CRMD, IE, 2, 1)
FIELD(CSR_CRMD, DA, 3, 1)
FIELD(CSR_CRMD, PG, 4, 1)
FIELD(CSR_CRMD, DATF, 5, 2)
FIELD(CSR_CRMD, DATM, 7, 2)
FIELD(CSR_CRMD, WE, 9, 1)

extern const char * const regnames[32];
extern const char * const fregnames[32];

#define N_IRQS      13
#define IRQ_TIMER   11
#define IRQ_IPI     12

#define LOONGARCH_STLB         2048 /* 2048 STLB */
#define LOONGARCH_MTLB         64   /* 64 MTLB */
#define LOONGARCH_TLB_MAX      (LOONGARCH_STLB + LOONGARCH_MTLB)

/*
 * define the ASID PS E VPPN field of TLB
 */
FIELD(TLB_MISC, E, 0, 1)
FIELD(TLB_MISC, ASID, 1, 10)
FIELD(TLB_MISC, VPPN, 13, 35)
FIELD(TLB_MISC, PS, 48, 6)

#define LSX_LEN   (128)
typedef union VReg {
    int8_t   B[LSX_LEN / 8];
    int16_t  H[LSX_LEN / 16];
    int32_t  W[LSX_LEN / 32];
    int64_t  D[LSX_LEN / 64];
    uint8_t  UB[LSX_LEN / 8];
    uint16_t UH[LSX_LEN / 16];
    uint32_t UW[LSX_LEN / 32];
    uint64_t UD[LSX_LEN / 64];
    Int128   Q[LSX_LEN / 128];
}VReg;

typedef union fpr_t fpr_t;
union fpr_t {
    VReg  vreg;
};

struct LoongArchTLB {
    uint64_t tlb_misc;
    /* Fields corresponding to CSR_TLBELO0/1 */
    uint64_t tlb_entry0;
    uint64_t tlb_entry1;
};
typedef struct LoongArchTLB LoongArchTLB;

typedef struct CPUArchState {
    uint64_t gpr[32];
    uint64_t pc;

    fpr_t fpr[32];
    float_status fp_status;
    bool cf[8];

    uint32_t fcsr0;
    uint32_t fcsr0_mask;

    uint32_t cpucfg[21];

    uint64_t lladdr; /* LL virtual address compared against SC */
    uint64_t llval;

    /* LoongArch CSRs */
    uint64_t CSR_CRMD;
    uint64_t CSR_PRMD;
    uint64_t CSR_EUEN;
    uint64_t CSR_MISC;
    uint64_t CSR_ECFG;
    uint64_t CSR_ESTAT;
    uint64_t CSR_ERA;
    uint64_t CSR_BADV;
    uint64_t CSR_BADI;
    uint64_t CSR_EENTRY;
    uint64_t CSR_TLBIDX;
    uint64_t CSR_TLBEHI;
    uint64_t CSR_TLBELO0;
    uint64_t CSR_TLBELO1;
    uint64_t CSR_ASID;
    uint64_t CSR_PGDL;
    uint64_t CSR_PGDH;
    uint64_t CSR_PGD;
    uint64_t CSR_PWCL;
    uint64_t CSR_PWCH;
    uint64_t CSR_STLBPS;
    uint64_t CSR_RVACFG;
    uint64_t CSR_PRCFG1;
    uint64_t CSR_PRCFG2;
    uint64_t CSR_PRCFG3;
    uint64_t CSR_SAVE[16];
    uint64_t CSR_TID;
    uint64_t CSR_TCFG;
    uint64_t CSR_TVAL;
    uint64_t CSR_CNTC;
    uint64_t CSR_TICLR;
    uint64_t CSR_LLBCTL;
    uint64_t CSR_IMPCTL1;
    uint64_t CSR_IMPCTL2;
    uint64_t CSR_TLBRENTRY;
    uint64_t CSR_TLBRBADV;
    uint64_t CSR_TLBRERA;
    uint64_t CSR_TLBRSAVE;
    uint64_t CSR_TLBRELO0;
    uint64_t CSR_TLBRELO1;
    uint64_t CSR_TLBREHI;
    uint64_t CSR_TLBRPRMD;
    uint64_t CSR_MERRCTL;
    uint64_t CSR_MERRINFO1;
    uint64_t CSR_MERRINFO2;
    uint64_t CSR_MERRENTRY;
    uint64_t CSR_MERRERA;
    uint64_t CSR_MERRSAVE;
    uint64_t CSR_CTAG;
    uint64_t CSR_DMW[4];
    uint64_t CSR_DBG;
    uint64_t CSR_DERA;
    uint64_t CSR_DSAVE;

#ifndef CONFIG_USER_ONLY
    LoongArchTLB  tlb[LOONGARCH_TLB_MAX];

    AddressSpace address_space_iocsr;
    MemoryRegion system_iocsr;
    MemoryRegion iocsr_mem;
    bool load_elf;
    uint64_t elf_address;
#endif
} CPULoongArchState;

/**
 * LoongArchCPU:
 * @env: #CPULoongArchState
 *
 * A LoongArch CPU.
 */
struct ArchCPU {
    /*< private >*/
    CPUState parent_obj;
    /*< public >*/

    CPUNegativeOffsetState neg;
    CPULoongArchState env;
    QEMUTimer timer;

    /* 'compatible' string for this CPU for Linux device trees */
    const char *dtb_compatible;
};

#define TYPE_LOONGARCH_CPU "loongarch-cpu"

OBJECT_DECLARE_CPU_TYPE(LoongArchCPU, LoongArchCPUClass,
                        LOONGARCH_CPU)

/**
 * LoongArchCPUClass:
 * @parent_realize: The parent class' realize handler.
 * @parent_phases: The parent class' reset phase handlers.
 *
 * A LoongArch CPU model.
 */
struct LoongArchCPUClass {
    /*< private >*/
    CPUClass parent_class;
    /*< public >*/

    DeviceRealize parent_realize;
    ResettablePhases parent_phases;
};

/*
 * LoongArch CPUs has 4 privilege levels.
 * 0 for kernel mode, 3 for user mode.
 * Define an extra index for DA(direct addressing) mode.
 */
#define MMU_PLV_KERNEL   0
#define MMU_PLV_USER     3
#define MMU_IDX_KERNEL   MMU_PLV_KERNEL
#define MMU_IDX_USER     MMU_PLV_USER
#define MMU_IDX_DA       4

static inline int cpu_mmu_index(CPULoongArchState *env, bool ifetch)
{
#ifdef CONFIG_USER_ONLY
    return MMU_IDX_USER;
#else
    if (FIELD_EX64(env->CSR_CRMD, CSR_CRMD, PG)) {
        return FIELD_EX64(env->CSR_CRMD, CSR_CRMD, PLV);
    }
    return MMU_IDX_DA;
#endif
}

/*
 * LoongArch CPUs hardware flags.
 */
#define HW_FLAGS_PLV_MASK   R_CSR_CRMD_PLV_MASK  /* 0x03 */
#define HW_FLAGS_CRMD_PG    R_CSR_CRMD_PG_MASK   /* 0x10 */
#define HW_FLAGS_EUEN_FPE   0x04
#define HW_FLAGS_EUEN_SXE   0x08

static inline void cpu_get_tb_cpu_state(CPULoongArchState *env,
                                        target_ulong *pc,
                                        target_ulong *cs_base,
                                        uint32_t *flags)
{
    *pc = env->pc;
    *cs_base = 0;
    *flags = env->CSR_CRMD & (R_CSR_CRMD_PLV_MASK | R_CSR_CRMD_PG_MASK);
    *flags |= FIELD_EX64(env->CSR_EUEN, CSR_EUEN, FPE) * HW_FLAGS_EUEN_FPE;
    *flags |= FIELD_EX64(env->CSR_EUEN, CSR_EUEN, SXE) * HW_FLAGS_EUEN_SXE;
}

void loongarch_cpu_list(void);

#define cpu_list loongarch_cpu_list

#include "exec/cpu-all.h"

#define LOONGARCH_CPU_TYPE_SUFFIX "-" TYPE_LOONGARCH_CPU
#define LOONGARCH_CPU_TYPE_NAME(model) model LOONGARCH_CPU_TYPE_SUFFIX
#define CPU_RESOLVING_TYPE TYPE_LOONGARCH_CPU

#endif /* LOONGARCH_CPU_H */

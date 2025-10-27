/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QEMU LoongArch CPU
 *
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 */

#ifndef LOONGARCH_CPU_H
#define LOONGARCH_CPU_H

#include "qemu/int128.h"
#include "exec/cpu-common.h"
#include "exec/cpu-defs.h"
#include "exec/cpu-interrupt.h"
#include "fpu/softfloat-types.h"
#include "hw/registerfields.h"
#include "qemu/timer.h"
#ifndef CONFIG_USER_ONLY
#include "system/memory.h"
#endif
#include "cpu-csr.h"
#include "cpu-qom.h"

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
FIELD(CPUCFG1, CRC, 25, 1)
FIELD(CPUCFG1, MSG_INT, 26, 1)

/* cpucfg[1].arch */
#define CPUCFG1_ARCH_LA32R       0
#define CPUCFG1_ARCH_LA32        1
#define CPUCFG1_ARCH_LA64        2

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
FIELD(CPUCFG2, LBT_ALL, 18, 3)
FIELD(CPUCFG2, LSPW, 21, 1)
FIELD(CPUCFG2, LAM, 22, 1)
FIELD(CPUCFG2, HPTW, 24, 1)

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

#define N_IRQS      15
#define IRQ_TIMER   11
#define IRQ_IPI     12
#define INT_DMSI    14

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

/*Msg interrupt registers */
#define N_MSGIS                4
FIELD(CSR_MSGIS, IS, 0, 63)
FIELD(CSR_MSGIR, INTNUM, 0, 8)
FIELD(CSR_MSGIR, ACTIVE, 31, 1)
FIELD(CSR_MSGIE, PT, 0, 8)

#define LSX_LEN    (128)
#define LASX_LEN   (256)

typedef union VReg {
    int8_t   B[LASX_LEN / 8];
    int16_t  H[LASX_LEN / 16];
    int32_t  W[LASX_LEN / 32];
    int64_t  D[LASX_LEN / 64];
    uint8_t  UB[LASX_LEN / 8];
    uint16_t UH[LASX_LEN / 16];
    uint32_t UW[LASX_LEN / 32];
    uint64_t UD[LASX_LEN / 64];
    Int128   Q[LASX_LEN / 128];
} VReg;

typedef union fpr_t fpr_t;
union fpr_t {
    VReg  vreg;
};

#ifdef CONFIG_TCG
struct LoongArchTLB {
    uint64_t tlb_misc;
    /* Fields corresponding to CSR_TLBELO0/1 */
    uint64_t tlb_entry0;
    uint64_t tlb_entry1;
};
typedef struct LoongArchTLB LoongArchTLB;
#endif

enum loongarch_features {
    LOONGARCH_FEATURE_LSX,
    LOONGARCH_FEATURE_LASX,
    LOONGARCH_FEATURE_LBT, /* loongson binary translation extension */
    LOONGARCH_FEATURE_PMU,
    LOONGARCH_FEATURE_PV_IPI,
    LOONGARCH_FEATURE_STEALTIME,
    LOONGARCH_FEATURE_PTW,
};

typedef struct  LoongArchBT {
    /* scratch registers */
    uint64_t scr0;
    uint64_t scr1;
    uint64_t scr2;
    uint64_t scr3;
    /* loongarch eflags */
    uint32_t eflags;
    uint32_t ftop;
} lbt_t;

typedef struct CPUArchState {
    uint64_t gpr[32];
    uint64_t pc;

    fpr_t fpr[32];
    bool cf[8];
    uint32_t fcsr0;
    lbt_t  lbt;

    uint32_t cpucfg[21];
    uint32_t pv_features;

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
    uint64_t CSR_CPUID;
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
    /* Msg interrupt registers */
    uint64_t CSR_MSGIS[N_MSGIS];
    uint64_t CSR_MSGIR;
    uint64_t CSR_MSGIE;
    struct {
        uint64_t guest_addr;
    } stealtime;

#ifdef CONFIG_TCG
    float_status fp_status;
    uint32_t fcsr0_mask;
    uint64_t lladdr; /* LL virtual address compared against SC */
    uint64_t llval;
#endif
#ifndef CONFIG_USER_ONLY
#ifdef CONFIG_TCG
    LoongArchTLB  tlb[LOONGARCH_TLB_MAX];
#endif

    AddressSpace *address_space_iocsr;
    uint32_t mp_state;
#endif
} CPULoongArchState;

typedef struct LoongArchCPUTopo {
    int32_t socket_id;  /* socket-id of this VCPU */
    int32_t core_id;    /* core-id of this VCPU */
    int32_t thread_id;  /* thread-id of this VCPU */
} LoongArchCPUTopo;

/**
 * LoongArchCPU:
 * @env: #CPULoongArchState
 *
 * A LoongArch CPU.
 */
struct ArchCPU {
    CPUState parent_obj;

    CPULoongArchState env;
    QEMUTimer timer;
    uint32_t  phy_id;
    OnOffAuto lbt;
    OnOffAuto pmu;
    OnOffAuto ptw;
    OnOffAuto lsx;
    OnOffAuto lasx;
    OnOffAuto msgint;
    OnOffAuto kvm_pv_ipi;
    OnOffAuto kvm_steal_time;
    int32_t socket_id;  /* socket-id of this CPU */
    int32_t core_id;    /* core-id of this CPU */
    int32_t thread_id;  /* thread-id of this CPU */
    int32_t node_id;    /* NUMA node of this CPU */

    /* 'compatible' string for this CPU for Linux device trees */
    const char *dtb_compatible;
    /* used by KVM_REG_LOONGARCH_COUNTER ioctl to access guest time counters */
    uint64_t kvm_state_counter;
    VMChangeStateEntry *vmsentry;
};

/**
 * LoongArchCPUClass:
 * @parent_realize: The parent class' realize handler.
 * @parent_phases: The parent class' reset phase handlers.
 *
 * A LoongArch CPU model.
 */
struct LoongArchCPUClass {
    CPUClass parent_class;

    DeviceRealize parent_realize;
    DeviceUnrealize parent_unrealize;
    ResettablePhases parent_phases;
};

/*
 * LoongArch CPUs has 4 privilege levels.
 * 0 for kernel mode, 3 for user mode.
 * Define an extra index for DA(direct addressing) mode.
 */
#define MMU_PLV_KERNEL   0
#define MMU_PLV_USER     3
#define MMU_KERNEL_IDX   MMU_PLV_KERNEL
#define MMU_USER_IDX     MMU_PLV_USER
#define MMU_DA_IDX       4

static inline bool is_la64(CPULoongArchState *env)
{
    return FIELD_EX32(env->cpucfg[1], CPUCFG1, ARCH) == CPUCFG1_ARCH_LA64;
}

static inline bool is_va32(CPULoongArchState *env)
{
    /* VA32 if !LA64 or VA32L[1-3] */
    bool va32 = !is_la64(env);
    uint64_t plv = FIELD_EX64(env->CSR_CRMD, CSR_CRMD, PLV);
    if (plv >= 1 && (FIELD_EX64(env->CSR_MISC, CSR_MISC, VA32) & (1 << plv))) {
        va32 = true;
    }
    return va32;
}

static inline void set_pc(CPULoongArchState *env, uint64_t value)
{
    if (is_va32(env)) {
        env->pc = (uint32_t)value;
    } else {
        env->pc = value;
    }
}

/*
 * LoongArch CPUs hardware flags.
 */
#define HW_FLAGS_PLV_MASK   R_CSR_CRMD_PLV_MASK  /* 0x03 */
#define HW_FLAGS_EUEN_FPE   0x04
#define HW_FLAGS_EUEN_SXE   0x08
#define HW_FLAGS_CRMD_PG    R_CSR_CRMD_PG_MASK   /* 0x10 */
#define HW_FLAGS_VA32       0x20
#define HW_FLAGS_EUEN_ASXE  0x40

#define CPU_RESOLVING_TYPE TYPE_LOONGARCH_CPU

#endif /* LOONGARCH_CPU_H */

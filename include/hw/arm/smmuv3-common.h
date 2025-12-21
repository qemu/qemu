/*
 * ARM SMMUv3 support - Common API
 *
 * Copyright (C) 2014-2016 Broadcom Corporation
 * Copyright (c) 2017 Red Hat, Inc.
 * Written by Prem Mallappa, Eric Auger
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_ARM_SMMUV3_COMMON_H
#define HW_ARM_SMMUV3_COMMON_H

#include "hw/core/registerfields.h"

/* Configuration Data */

/* STE Level 1 Descriptor */
typedef struct STEDesc {
    uint32_t word[2];
} STEDesc;

/* CD Level 1 Descriptor */
typedef struct CDDesc {
    uint32_t word[2];
} CDDesc;

/* Stream Table Entry(STE) */
typedef struct STE {
    uint32_t word[16];
} STE;

/* Context Descriptor(CD) */
typedef struct CD {
    uint32_t word[16];
} CD;

/* STE fields */

REG32(STE_0, 0)
    FIELD(STE_0, VALID, 0, 1)
    FIELD(STE_0, CONFIG, 1, 3)
    FIELD(STE_0, S1FMT, 4, 2)
    FIELD(STE_0, CTXPTR_LO, 6, 26)
REG32(STE_1, 4)
    FIELD(STE_1, CTXPTR_HI, 0, 24)
    FIELD(STE_1, S1CDMAX, 27, 5)
REG32(STE_2, 8)
    FIELD(STE_2, S1STALLD, 27, 1)
    FIELD(STE_2, EATS, 28, 2)
    FIELD(STE_2, STRW, 30, 2)
REG32(STE_4, 16)
    FIELD(STE_4, S2VMID, 0, 16)
REG32(STE_5, 20)
    FIELD(STE_5, S2T0SZ, 0, 6)
    FIELD(STE_5, S2SL0, 6, 2)
    FIELD(STE_5, S2TG, 14, 2)
    FIELD(STE_5, S2PS, 16, 3)
    FIELD(STE_5, S2AA64, 19, 1)
    FIELD(STE_5, S2ENDI, 20, 1)
    FIELD(STE_5, S2AFFD, 21, 1)
    FIELD(STE_5, S2HD, 23, 1)
    FIELD(STE_5, S2HA, 24, 1)
    FIELD(STE_5, S2S, 25, 1)
    FIELD(STE_5, S2R, 26, 1)
REG32(STE_6, 24)
    FIELD(STE_6, S2TTB_LO, 4, 28)
REG32(STE_7, 28)
    FIELD(STE_7, S2TTB_HI, 0, 20)

/* Get STE fields */
#define STE_VALID(x)      FIELD_EX32((x)->word[0], STE_0, VALID)
#define STE_CONFIG(x)     FIELD_EX32((x)->word[0], STE_0, CONFIG)
#define STE_S1FMT(x)      FIELD_EX32((x)->word[0], STE_0, S1FMT)
#define STE_CTXPTR(x)                                               \
    (((uint64_t)FIELD_EX32((x)->word[0], STE_0, CTXPTR_LO) << 6) |  \
     ((uint64_t)FIELD_EX32((x)->word[1], STE_1, CTXPTR_HI) << 32))
#define STE_S1CDMAX(x)    FIELD_EX32((x)->word[1], STE_1, S1CDMAX)
#define STE_S1STALLD(x)   FIELD_EX32((x)->word[2], STE_2, S1STALLD)
#define STE_EATS(x)       FIELD_EX32((x)->word[2], STE_2, EATS)
#define STE_STRW(x)       FIELD_EX32((x)->word[2], STE_2, STRW)
#define STE_S2VMID(x)     FIELD_EX32((x)->word[4], STE_4, S2VMID)
#define STE_S2T0SZ(x)     FIELD_EX32((x)->word[5], STE_5, S2T0SZ)
#define STE_S2SL0(x)      FIELD_EX32((x)->word[5], STE_5, S2SL0)
#define STE_S2TG(x)       FIELD_EX32((x)->word[5], STE_5, S2TG)
#define STE_S2PS(x)       FIELD_EX32((x)->word[5], STE_5, S2PS)
#define STE_S2AA64(x)     FIELD_EX32((x)->word[5], STE_5, S2AA64)
#define STE_S2ENDI(x)     FIELD_EX32((x)->word[5], STE_5, S2ENDI)
#define STE_S2AFFD(x)     FIELD_EX32((x)->word[5], STE_5, S2AFFD)
#define STE_S2HD(x)       FIELD_EX32((x)->word[5], STE_5, S2HD)
#define STE_S2HA(x)       FIELD_EX32((x)->word[5], STE_5, S2HA)
#define STE_S2S(x)        FIELD_EX32((x)->word[5], STE_5, S2S)
#define STE_S2R(x)        FIELD_EX32((x)->word[5], STE_5, S2R)
#define STE_S2TTB(x)                                                \
    (((uint64_t)FIELD_EX32((x)->word[6], STE_6, S2TTB_LO) << 4) |   \
     ((uint64_t)FIELD_EX32((x)->word[7], STE_7, S2TTB_HI) << 32))

#define STE_CFG_S1_ENABLED(config) (config & 0x1)
#define STE_CFG_S2_ENABLED(config) (config & 0x2)
#define STE_CFG_ABORT(config)      (!(config & 0x4))
#define STE_CFG_BYPASS(config)     (config == 0x4)

/* Update STE fields */
#define STE_SET_VALID(ste, v)                                                 \
    ((ste)->word[0] = FIELD_DP32((ste)->word[0], STE_0, VALID, (v)))
#define STE_SET_CONFIG(ste, v)                                                \
    ((ste)->word[0] = FIELD_DP32((ste)->word[0], STE_0, CONFIG, (v)))

#define STE_SET_CTXPTR(ste, v) do {                                           \
    (ste)->word[0] = FIELD_DP32((ste)->word[0], STE_0, CTXPTR_LO, (v) >> 6);  \
    (ste)->word[1] = FIELD_DP32((ste)->word[1], STE_1, CTXPTR_HI, (v) >> 32); \
} while (0)
#define STE_SET_S2T0SZ(ste, v)                                                \
    ((ste)->word[5] = FIELD_DP32((ste)->word[5], STE_5, S2T0SZ, (v)))
#define STE_SET_S2SL0(ste, v)                                                 \
    ((ste)->word[5] = FIELD_DP32((ste)->word[5], STE_5, S2SL0, (v)))
#define STE_SET_S2TG(ste, v)                                                  \
    ((ste)->word[5] = FIELD_DP32((ste)->word[5], STE_5, S2TG, (v)))
#define STE_SET_S2PS(ste, v)                                                  \
    ((ste)->word[5] = FIELD_DP32((ste)->word[5], STE_5, S2PS, (v)))
#define STE_SET_S2AA64(ste, v)                                                \
    ((ste)->word[5] = FIELD_DP32((ste)->word[5], STE_5, S2AA64, (v)))
#define STE_SET_S2ENDI(ste, v)                                                \
    ((ste)->word[5] = FIELD_DP32((ste)->word[5], STE_5, S2ENDI, (v)))
#define STE_SET_S2AFFD(ste, v)                                                \
    ((ste)->word[5] = FIELD_DP32((ste)->word[5], STE_5, S2AFFD, (v)))
#define STE_SET_S2S(ste, v)                                                   \
    ((ste)->word[5] = FIELD_DP32((ste)->word[5], STE_5, S2S, (v)))
#define STE_SET_S2TTB(ste, v) do {                                            \
    (ste)->word[6] = FIELD_DP32((ste)->word[6], STE_6, S2TTB_LO, (v) >> 4);   \
    (ste)->word[7] = FIELD_DP32((ste)->word[7], STE_7, S2TTB_HI, (v) >> 32);  \
} while (0)

/* CD fields */

REG32(CD_0, 0)
    FIELD(CD_0, TSZ0, 0, 6)
    FIELD(CD_0, TG0, 6, 2)
    FIELD(CD_0, EPD0, 14, 1)
    FIELD(CD_0, ENDI, 15, 1)
    FIELD(CD_0, TSZ1, 16, 6)
    FIELD(CD_0, TG1, 22, 2)
    FIELD(CD_0, EPD1, 30, 1)
    FIELD(CD_0, VALID, 31, 1)
REG32(CD_1, 4)
    FIELD(CD_1, IPS, 0, 3)
    FIELD(CD_1, AFFD, 3, 1)
    FIELD(CD_1, TBI, 6, 2)
    FIELD(CD_1, AARCH64, 9, 1)
    FIELD(CD_1, HD, 10, 1)
    FIELD(CD_1, HA, 11, 1)
    FIELD(CD_1, S, 12, 1)
    FIELD(CD_1, R, 13, 1)
    FIELD(CD_1, A, 14, 1)
    FIELD(CD_1, ASID, 16, 16)
REG32(CD_2, 8)
    FIELD(CD_2, NSCFG0, 0, 1)
    FIELD(CD_2, HAD0, 1, 1)
    FIELD(CD_2, TTB0_LO, 4, 28)
REG32(CD_3, 12)
    FIELD(CD_3, TTB0_HI, 0, 20)
REG32(CD_4, 16)
    FIELD(CD_4, NSCFG1, 0, 1)
    FIELD(CD_4, HAD1, 1, 1)
    FIELD(CD_4, TTB1_LO, 4, 28)
REG32(CD_5, 20)
    FIELD(CD_5, TTB1_HI, 0, 20)

/* Get CD fields */
#define CD_TSZ(x, sel)   ((sel) ?          \
    FIELD_EX32((x)->word[0], CD_0, TSZ1) : \
    FIELD_EX32((x)->word[0], CD_0, TSZ0))
#define CD_TG(x, sel)    ((sel) ?          \
    FIELD_EX32((x)->word[0], CD_0, TG1) :  \
    FIELD_EX32((x)->word[0], CD_0, TG0))
#define CD_EPD(x, sel)   ((sel) ?          \
    FIELD_EX32((x)->word[0], CD_0, EPD1) : \
    FIELD_EX32((x)->word[0], CD_0, EPD0))
#define CD_ENDI(x)       FIELD_EX32((x)->word[0], CD_0, ENDI)
#define CD_VALID(x)      FIELD_EX32((x)->word[0], CD_0, VALID)
#define CD_IPS(x)        FIELD_EX32((x)->word[1], CD_1, IPS)
#define CD_AFFD(x)       FIELD_EX32((x)->word[1], CD_1, AFFD)
#define CD_TBI(x)        FIELD_EX32((x)->word[1], CD_1, TBI)
#define CD_AARCH64(x)    FIELD_EX32((x)->word[1], CD_1, AARCH64)
#define CD_HD(x)         FIELD_EX32((x)->word[1], CD_1, HD)
#define CD_HA(x)         FIELD_EX32((x)->word[1], CD_1, HA)
#define CD_S(x)          FIELD_EX32((x)->word[1], CD_1, S)
#define CD_R(x)          FIELD_EX32((x)->word[1], CD_1, R)
#define CD_A(x)          FIELD_EX32((x)->word[1], CD_1, A)
#define CD_ASID(x)       FIELD_EX32((x)->word[1], CD_1, ASID)
#define CD_NSCFG(x, sel) ((sel) ?                                         \
    FIELD_EX32((x)->word[4], CD_4, NSCFG1) :                              \
    FIELD_EX32((x)->word[2], CD_2, NSCFG0))
#define CD_HAD(x, sel)   ((sel) ?                                         \
    FIELD_EX32((x)->word[4], CD_4, HAD1) :                                \
    FIELD_EX32((x)->word[2], CD_2, HAD0))
#define CD_TTB(x, sel)                                                    \
    ((sel) ? (((uint64_t)FIELD_EX32((x)->word[5], CD_5, TTB1_HI) << 32) | \
              ((uint64_t)FIELD_EX32((x)->word[4], CD_4, TTB1_LO) << 4)) : \
             (((uint64_t)FIELD_EX32((x)->word[3], CD_3, TTB0_HI) << 32) | \
              ((uint64_t)FIELD_EX32((x)->word[2], CD_2, TTB0_LO) << 4)))

/* Update CD fields */
#define CD_SET_VALID(cd, v)                                                   \
    ((cd)->word[0] = FIELD_DP32((cd)->word[0], CD_0, VALID, (v)))
#define CD_SET_ASID(cd, v)                                                    \
    ((cd)->word[1] = FIELD_DP32((cd)->word[1], CD_1, ASID, (v)))
#define CD_SET_TTB(cd, sel, v) do {                                           \
    if (sel) {                                                                \
        (cd)->word[4] = FIELD_DP32((cd)->word[4], CD_4, TTB1_LO, (v) >> 4);   \
        (cd)->word[5] = FIELD_DP32((cd)->word[5], CD_5, TTB1_HI, (v) >> 32);  \
    } else {                                                                  \
        (cd)->word[2] = FIELD_DP32((cd)->word[2], CD_2, TTB0_LO, (v) >> 4);   \
        (cd)->word[3] = FIELD_DP32((cd)->word[3], CD_3, TTB0_HI, (v) >> 32);  \
    }                                                                         \
} while (0)

#define CD_SET_TSZ(cd, sel, v)                                                \
    ((cd)->word[0] = (sel) ? FIELD_DP32((cd)->word[0], CD_0, TSZ1, (v)) :     \
                             FIELD_DP32((cd)->word[0], CD_0, TSZ0, (v)))
#define CD_SET_TG(cd, sel, v)                                                 \
    ((cd)->word[0] = (sel) ? FIELD_DP32((cd)->word[0], CD_0, TG1, (v)) :      \
                             FIELD_DP32((cd)->word[0], CD_0, TG0, (v)))
#define CD_SET_EPD(cd, sel, v)                                                \
    ((cd)->word[0] = (sel) ? FIELD_DP32((cd)->word[0], CD_0, EPD1, (v)) :     \
                             FIELD_DP32((cd)->word[0], CD_0, EPD0, (v)))
#define CD_SET_ENDI(cd, v)                                                    \
    ((cd)->word[0] = FIELD_DP32((cd)->word[0], CD_0, ENDI, (v)))
#define CD_SET_IPS(cd, v)                                                     \
    ((cd)->word[1] = FIELD_DP32((cd)->word[1], CD_1, IPS, (v)))
#define CD_SET_AFFD(cd, v)                                                    \
    ((cd)->word[1] = FIELD_DP32((cd)->word[1], CD_1, AFFD, (v)))
#define CD_SET_TBI(cd, v)                                                     \
    ((cd)->word[1] = FIELD_DP32((cd)->word[1], CD_1, TBI, (v)))
#define CD_SET_HD(cd, v)                                                      \
    ((cd)->word[1] = FIELD_DP32((cd)->word[1], CD_1, HD, (v)))
#define CD_SET_HA(cd, v)                                                      \
    ((cd)->word[1] = FIELD_DP32((cd)->word[1], CD_1, HA, (v)))
#define CD_SET_S(cd, v)                                                       \
    ((cd)->word[1] = FIELD_DP32((cd)->word[1], CD_1, S, (v)))
#define CD_SET_R(cd, v)                                                       \
    ((cd)->word[1] = FIELD_DP32((cd)->word[1], CD_1, R, (v)))
#define CD_SET_A(cd, v)                                                       \
    ((cd)->word[1] = FIELD_DP32((cd)->word[1], CD_1, A, (v)))
#define CD_SET_AARCH64(cd, v)                                                 \
    ((cd)->word[1] = FIELD_DP32((cd)->word[1], CD_1, AARCH64, (v)))
#define CD_SET_NSCFG(cd, sel, v)                                              \
    ((sel) ? ((cd)->word[4] = FIELD_DP32((cd)->word[4], CD_4, NSCFG1, (v))) : \
             ((cd)->word[2] = FIELD_DP32((cd)->word[2], CD_2, NSCFG0, (v))))

/* MMIO Registers */

REG32(IDR0,                0x0)
    FIELD(IDR0, S2P,         0 , 1)
    FIELD(IDR0, S1P,         1 , 1)
    FIELD(IDR0, TTF,         2 , 2)
    FIELD(IDR0, COHACC,      4 , 1)
    FIELD(IDR0, BTM,         5 , 1)
    FIELD(IDR0, HTTU,        6 , 2)
    FIELD(IDR0, DORMHINT,    8 , 1)
    FIELD(IDR0, HYP,         9 , 1)
    FIELD(IDR0, ATS,         10, 1)
    FIELD(IDR0, NS1ATS,      11, 1)
    FIELD(IDR0, ASID16,      12, 1)
    FIELD(IDR0, MSI,         13, 1)
    FIELD(IDR0, SEV,         14, 1)
    FIELD(IDR0, ATOS,        15, 1)
    FIELD(IDR0, PRI,         16, 1)
    FIELD(IDR0, VMW,         17, 1)
    FIELD(IDR0, VMID16,      18, 1)
    FIELD(IDR0, CD2L,        19, 1)
    FIELD(IDR0, VATOS,       20, 1)
    FIELD(IDR0, TTENDIAN,    21, 2)
    FIELD(IDR0, ATSRECERR,   23, 1)
    FIELD(IDR0, STALL_MODEL, 24, 2)
    FIELD(IDR0, TERM_MODEL,  26, 1)
    FIELD(IDR0, STLEVEL,     27, 2)
    FIELD(IDR0, RME_IMPL,    30, 1)

REG32(IDR1,                0x4)
    FIELD(IDR1, SIDSIZE,      0 , 6)
    FIELD(IDR1, SSIDSIZE,     6 , 5)
    FIELD(IDR1, PRIQS,        11, 5)
    FIELD(IDR1, EVENTQS,      16, 5)
    FIELD(IDR1, CMDQS,        21, 5)
    FIELD(IDR1, ATTR_PERMS_OVR, 26, 1)
    FIELD(IDR1, ATTR_TYPES_OVR, 27, 1)
    FIELD(IDR1, REL,          28, 1)
    FIELD(IDR1, QUEUES_PRESET, 29, 1)
    FIELD(IDR1, TABLES_PRESET, 30, 1)
    FIELD(IDR1, ECMDQ,        31, 1)

#define SMMU_IDR1_SIDSIZE 16
#define SMMU_CMDQS   19
#define SMMU_EVENTQS 19

REG32(IDR2,                0x8)
     FIELD(IDR2, BA_VATOS, 0, 10)

REG32(IDR3,                0xc)
     FIELD(IDR3, HAD,         2, 1);
     FIELD(IDR3, PBHA,        3, 1);
     FIELD(IDR3, XNX,         4, 1);
     FIELD(IDR3, PPS,         5, 1);
     FIELD(IDR3, MPAM,        7, 1);
     FIELD(IDR3, FWB,         8, 1);
     FIELD(IDR3, STT,         9, 1);
     FIELD(IDR3, RIL,        10, 1);
     FIELD(IDR3, BBML,       11, 2);
     FIELD(IDR3, E0PD,       13, 1);
     FIELD(IDR3, PTWNNC,     14, 1);
     FIELD(IDR3, DPT,        15, 1);

REG32(IDR4,                0x10)

REG32(IDR5,                0x14)
     FIELD(IDR5, OAS,         0, 3);
     FIELD(IDR5, GRAN4K,      4, 1);
     FIELD(IDR5, GRAN16K,     5, 1);
     FIELD(IDR5, GRAN64K,     6, 1);
     FIELD(IDR5, VAX,        10, 2);
     FIELD(IDR5, STALL_MAX,  16, 16);

#define SMMU_IDR5_OAS 4

REG32(IIDR,                0x18)
REG32(AIDR,                0x1c)
REG32(CR0,                 0x20)
    FIELD(CR0, SMMU_ENABLE,   0, 1)
    FIELD(CR0, EVENTQEN,      2, 1)
    FIELD(CR0, CMDQEN,        3, 1)

#define SMMU_CR0_RESERVED 0xFFFFFC20

REG32(CR0ACK,              0x24)
REG32(CR1,                 0x28)
REG32(CR2,                 0x2c)
REG32(STATUSR,             0x40)
REG32(GBPA,                0x44)
    FIELD(GBPA, ABORT,        20, 1)
    FIELD(GBPA, UPDATE,       31, 1)

/* Use incoming. */
#define SMMU_GBPA_RESET_VAL 0x1000

REG32(IRQ_CTRL,            0x50)
    FIELD(IRQ_CTRL, GERROR_IRQEN,        0, 1)
    FIELD(IRQ_CTRL, PRI_IRQEN,           1, 1)
    FIELD(IRQ_CTRL, EVENTQ_IRQEN,        2, 1)

REG32(IRQ_CTRL_ACK,        0x54)
REG32(GERROR,              0x60)
    FIELD(GERROR, CMDQ_ERR,           0, 1)
    FIELD(GERROR, EVENTQ_ABT_ERR,     2, 1)
    FIELD(GERROR, PRIQ_ABT_ERR,       3, 1)
    FIELD(GERROR, MSI_CMDQ_ABT_ERR,   4, 1)
    FIELD(GERROR, MSI_EVENTQ_ABT_ERR, 5, 1)
    FIELD(GERROR, MSI_PRIQ_ABT_ERR,   6, 1)
    FIELD(GERROR, MSI_GERROR_ABT_ERR, 7, 1)
    FIELD(GERROR, MSI_SFM_ERR,        8, 1)

REG32(GERRORN,             0x64)

#define A_GERROR_IRQ_CFG0  0x68 /* 64b */
REG32(GERROR_IRQ_CFG1, 0x70)
REG32(GERROR_IRQ_CFG2, 0x74)

#define A_STRTAB_BASE      0x80 /* 64b */

#define SMMU_BASE_ADDR_MASK 0xfffffffffffc0

REG32(STRTAB_BASE_CFG,     0x88)
    FIELD(STRTAB_BASE_CFG, FMT,      16, 2)
    FIELD(STRTAB_BASE_CFG, SPLIT,    6 , 5)
    FIELD(STRTAB_BASE_CFG, LOG2SIZE, 0 , 6)

#define A_CMDQ_BASE        0x90 /* 64b */
REG32(CMDQ_PROD,           0x98)
REG32(CMDQ_CONS,           0x9c)
    FIELD(CMDQ_CONS, ERR, 24, 7)

#define A_EVENTQ_BASE      0xa0 /* 64b */
REG32(EVENTQ_PROD,         0xa8)
REG32(EVENTQ_CONS,         0xac)

#define A_EVENTQ_IRQ_CFG0  0xb0 /* 64b */
REG32(EVENTQ_IRQ_CFG1,     0xb8)
REG32(EVENTQ_IRQ_CFG2,     0xbc)

#define A_IDREGS           0xfd0

/* Commands */

typedef enum SMMUCommandType {
    SMMU_CMD_NONE            = 0x00,
    SMMU_CMD_PREFETCH_CONFIG       ,
    SMMU_CMD_PREFETCH_ADDR,
    SMMU_CMD_CFGI_STE,
    SMMU_CMD_CFGI_STE_RANGE,
    SMMU_CMD_CFGI_CD,
    SMMU_CMD_CFGI_CD_ALL,
    SMMU_CMD_CFGI_ALL,
    SMMU_CMD_TLBI_NH_ALL     = 0x10,
    SMMU_CMD_TLBI_NH_ASID,
    SMMU_CMD_TLBI_NH_VA,
    SMMU_CMD_TLBI_NH_VAA,
    SMMU_CMD_TLBI_EL3_ALL    = 0x18,
    SMMU_CMD_TLBI_EL3_VA     = 0x1a,
    SMMU_CMD_TLBI_EL2_ALL    = 0x20,
    SMMU_CMD_TLBI_EL2_ASID,
    SMMU_CMD_TLBI_EL2_VA,
    SMMU_CMD_TLBI_EL2_VAA,
    SMMU_CMD_TLBI_S12_VMALL  = 0x28,
    SMMU_CMD_TLBI_S2_IPA     = 0x2a,
    SMMU_CMD_TLBI_NSNH_ALL   = 0x30,
    SMMU_CMD_ATC_INV         = 0x40,
    SMMU_CMD_PRI_RESP,
    SMMU_CMD_RESUME          = 0x44,
    SMMU_CMD_STALL_TERM,
    SMMU_CMD_SYNC,
} SMMUCommandType;

#endif /* HW_ARM_SMMUV3_COMMON_H */

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

#define STE_VALID(x)   extract32((x)->word[0], 0, 1)

#define STE_CONFIG(x)  extract32((x)->word[0], 1, 3)
#define STE_CFG_S1_ENABLED(config) (config & 0x1)
#define STE_CFG_S2_ENABLED(config) (config & 0x2)
#define STE_CFG_ABORT(config)      (!(config & 0x4))
#define STE_CFG_BYPASS(config)     (config == 0x4)

#define STE_S1FMT(x)       extract32((x)->word[0], 4 , 2)
#define STE_S1CDMAX(x)     extract32((x)->word[1], 27, 5)
#define STE_S1STALLD(x)    extract32((x)->word[2], 27, 1)
#define STE_EATS(x)        extract32((x)->word[2], 28, 2)
#define STE_STRW(x)        extract32((x)->word[2], 30, 2)
#define STE_S2VMID(x)      extract32((x)->word[4], 0 , 16)
#define STE_S2T0SZ(x)      extract32((x)->word[5], 0 , 6)
#define STE_S2SL0(x)       extract32((x)->word[5], 6 , 2)
#define STE_S2TG(x)        extract32((x)->word[5], 14, 2)
#define STE_S2PS(x)        extract32((x)->word[5], 16, 3)
#define STE_S2AA64(x)      extract32((x)->word[5], 19, 1)
#define STE_S2ENDI(x)      extract32((x)->word[5], 20, 1)
#define STE_S2AFFD(x)      extract32((x)->word[5], 21, 1)
#define STE_S2HD(x)        extract32((x)->word[5], 23, 1)
#define STE_S2HA(x)        extract32((x)->word[5], 24, 1)
#define STE_S2S(x)         extract32((x)->word[5], 25, 1)
#define STE_S2R(x)         extract32((x)->word[5], 26, 1)

#define STE_CTXPTR(x)                                   \
    ((extract64((x)->word[1], 0, 16) << 32) |           \
     ((x)->word[0] & 0xffffffc0))

#define STE_S2TTB(x)                                    \
    ((extract64((x)->word[7], 0, 16) << 32) |           \
     ((x)->word[6] & 0xfffffff0))

/* CD fields */

#define CD_VALID(x)   extract32((x)->word[0], 31, 1)
#define CD_ASID(x)    extract32((x)->word[1], 16, 16)
#define CD_TTB(x, sel)                                          \
    ((extract64((x)->word[(sel) * 2 + 3], 0, 19) << 32) |       \
     ((x)->word[(sel) * 2 + 2] & ~0xfULL))

#define CD_HAD(x, sel)   extract32((x)->word[(sel) * 2 + 2], 1, 1)

#define CD_TSZ(x, sel)   extract32((x)->word[0], (16 * (sel)) + 0, 6)
#define CD_TG(x, sel)    extract32((x)->word[0], (16 * (sel)) + 6, 2)
#define CD_EPD(x, sel)   extract32((x)->word[0], (16 * (sel)) + 14, 1)
#define CD_ENDI(x)       extract32((x)->word[0], 15, 1)
#define CD_IPS(x)        extract32((x)->word[1], 0 , 3)
#define CD_AFFD(x)       extract32((x)->word[1], 3 , 1)
#define CD_TBI(x)        extract32((x)->word[1], 6 , 2)
#define CD_HD(x)         extract32((x)->word[1], 10 , 1)
#define CD_HA(x)         extract32((x)->word[1], 11 , 1)
#define CD_S(x)          extract32((x)->word[1], 12, 1)
#define CD_R(x)          extract32((x)->word[1], 13, 1)
#define CD_A(x)          extract32((x)->word[1], 14, 1)
#define CD_AARCH64(x)    extract32((x)->word[1], 9 , 1)

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

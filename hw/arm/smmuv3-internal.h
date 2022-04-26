/*
 * ARM SMMUv3 support - Internal API
 *
 * Copyright (C) 2014-2016 Broadcom Corporation
 * Copyright (c) 2017 Red Hat, Inc.
 * Written by Prem Mallappa, Eric Auger
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HW_ARM_SMMUV3_INTERNAL_H
#define HW_ARM_SMMUV3_INTERNAL_H

#include "hw/arm/smmu-common.h"

typedef enum SMMUTranslationStatus {
    SMMU_TRANS_DISABLE,
    SMMU_TRANS_ABORT,
    SMMU_TRANS_BYPASS,
    SMMU_TRANS_ERROR,
    SMMU_TRANS_SUCCESS,
} SMMUTranslationStatus;

/* MMIO Registers */

REG32(IDR0,                0x0)
    FIELD(IDR0, S1P,         1 , 1)
    FIELD(IDR0, TTF,         2 , 2)
    FIELD(IDR0, COHACC,      4 , 1)
    FIELD(IDR0, ASID16,      12, 1)
    FIELD(IDR0, TTENDIAN,    21, 2)
    FIELD(IDR0, STALL_MODEL, 24, 2)
    FIELD(IDR0, TERM_MODEL,  26, 1)
    FIELD(IDR0, STLEVEL,     27, 2)

REG32(IDR1,                0x4)
    FIELD(IDR1, SIDSIZE,      0 , 6)
    FIELD(IDR1, EVENTQS,      16, 5)
    FIELD(IDR1, CMDQS,        21, 5)

#define SMMU_IDR1_SIDSIZE 16
#define SMMU_CMDQS   19
#define SMMU_EVENTQS 19

REG32(IDR2,                0x8)
REG32(IDR3,                0xc)
     FIELD(IDR3, HAD,         2, 1);
     FIELD(IDR3, RIL,        10, 1);
     FIELD(IDR3, BBML,       11, 2);
REG32(IDR4,                0x10)
REG32(IDR5,                0x14)
     FIELD(IDR5, OAS,         0, 3);
     FIELD(IDR5, GRAN4K,      4, 1);
     FIELD(IDR5, GRAN16K,     5, 1);
     FIELD(IDR5, GRAN64K,     6, 1);

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

static inline int smmu_enabled(SMMUv3State *s)
{
    return FIELD_EX32(s->cr[0], CR0, SMMU_ENABLE);
}

/* Command Queue Entry */
typedef struct Cmd {
    uint32_t word[4];
} Cmd;

/* Event Queue Entry */
typedef struct Evt  {
    uint32_t word[8];
} Evt;

static inline uint32_t smmuv3_idreg(int regoffset)
{
    /*
     * Return the value of the Primecell/Corelink ID registers at the
     * specified offset from the first ID register.
     * These value indicate an ARM implementation of MMU600 p1
     */
    static const uint8_t smmuv3_ids[] = {
        0x04, 0, 0, 0, 0x84, 0xB4, 0xF0, 0x10, 0x0D, 0xF0, 0x05, 0xB1
    };
    return smmuv3_ids[regoffset / 4];
}

static inline bool smmuv3_eventq_irq_enabled(SMMUv3State *s)
{
    return FIELD_EX32(s->irq_ctrl, IRQ_CTRL, EVENTQ_IRQEN);
}

static inline bool smmuv3_gerror_irq_enabled(SMMUv3State *s)
{
    return FIELD_EX32(s->irq_ctrl, IRQ_CTRL, GERROR_IRQEN);
}

/* Queue Handling */

#define Q_BASE(q)          ((q)->base & SMMU_BASE_ADDR_MASK)
#define WRAP_MASK(q)       (1 << (q)->log2size)
#define INDEX_MASK(q)      (((1 << (q)->log2size)) - 1)
#define WRAP_INDEX_MASK(q) ((1 << ((q)->log2size + 1)) - 1)

#define Q_CONS(q) ((q)->cons & INDEX_MASK(q))
#define Q_PROD(q) ((q)->prod & INDEX_MASK(q))

#define Q_CONS_ENTRY(q)  (Q_BASE(q) + (q)->entry_size * Q_CONS(q))
#define Q_PROD_ENTRY(q)  (Q_BASE(q) + (q)->entry_size * Q_PROD(q))

#define Q_CONS_WRAP(q) (((q)->cons & WRAP_MASK(q)) >> (q)->log2size)
#define Q_PROD_WRAP(q) (((q)->prod & WRAP_MASK(q)) >> (q)->log2size)

static inline bool smmuv3_q_full(SMMUQueue *q)
{
    return ((q->cons ^ q->prod) & WRAP_INDEX_MASK(q)) == WRAP_MASK(q);
}

static inline bool smmuv3_q_empty(SMMUQueue *q)
{
    return (q->cons & WRAP_INDEX_MASK(q)) == (q->prod & WRAP_INDEX_MASK(q));
}

static inline void queue_prod_incr(SMMUQueue *q)
{
    q->prod = (q->prod + 1) & WRAP_INDEX_MASK(q);
}

static inline void queue_cons_incr(SMMUQueue *q)
{
    /*
     * We have to use deposit for the CONS registers to preserve
     * the ERR field in the high bits.
     */
    q->cons = deposit32(q->cons, 0, q->log2size + 1, q->cons + 1);
}

static inline bool smmuv3_cmdq_enabled(SMMUv3State *s)
{
    return FIELD_EX32(s->cr[0], CR0, CMDQEN);
}

static inline bool smmuv3_eventq_enabled(SMMUv3State *s)
{
    return FIELD_EX32(s->cr[0], CR0, EVENTQEN);
}

static inline void smmu_write_cmdq_err(SMMUv3State *s, uint32_t err_type)
{
    s->cmdq.cons = FIELD_DP32(s->cmdq.cons, CMDQ_CONS, ERR, err_type);
}

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

static const char *cmd_stringify[] = {
    [SMMU_CMD_PREFETCH_CONFIG] = "SMMU_CMD_PREFETCH_CONFIG",
    [SMMU_CMD_PREFETCH_ADDR]   = "SMMU_CMD_PREFETCH_ADDR",
    [SMMU_CMD_CFGI_STE]        = "SMMU_CMD_CFGI_STE",
    [SMMU_CMD_CFGI_STE_RANGE]  = "SMMU_CMD_CFGI_STE_RANGE",
    [SMMU_CMD_CFGI_CD]         = "SMMU_CMD_CFGI_CD",
    [SMMU_CMD_CFGI_CD_ALL]     = "SMMU_CMD_CFGI_CD_ALL",
    [SMMU_CMD_CFGI_ALL]        = "SMMU_CMD_CFGI_ALL",
    [SMMU_CMD_TLBI_NH_ALL]     = "SMMU_CMD_TLBI_NH_ALL",
    [SMMU_CMD_TLBI_NH_ASID]    = "SMMU_CMD_TLBI_NH_ASID",
    [SMMU_CMD_TLBI_NH_VA]      = "SMMU_CMD_TLBI_NH_VA",
    [SMMU_CMD_TLBI_NH_VAA]     = "SMMU_CMD_TLBI_NH_VAA",
    [SMMU_CMD_TLBI_EL3_ALL]    = "SMMU_CMD_TLBI_EL3_ALL",
    [SMMU_CMD_TLBI_EL3_VA]     = "SMMU_CMD_TLBI_EL3_VA",
    [SMMU_CMD_TLBI_EL2_ALL]    = "SMMU_CMD_TLBI_EL2_ALL",
    [SMMU_CMD_TLBI_EL2_ASID]   = "SMMU_CMD_TLBI_EL2_ASID",
    [SMMU_CMD_TLBI_EL2_VA]     = "SMMU_CMD_TLBI_EL2_VA",
    [SMMU_CMD_TLBI_EL2_VAA]    = "SMMU_CMD_TLBI_EL2_VAA",
    [SMMU_CMD_TLBI_S12_VMALL]  = "SMMU_CMD_TLBI_S12_VMALL",
    [SMMU_CMD_TLBI_S2_IPA]     = "SMMU_CMD_TLBI_S2_IPA",
    [SMMU_CMD_TLBI_NSNH_ALL]   = "SMMU_CMD_TLBI_NSNH_ALL",
    [SMMU_CMD_ATC_INV]         = "SMMU_CMD_ATC_INV",
    [SMMU_CMD_PRI_RESP]        = "SMMU_CMD_PRI_RESP",
    [SMMU_CMD_RESUME]          = "SMMU_CMD_RESUME",
    [SMMU_CMD_STALL_TERM]      = "SMMU_CMD_STALL_TERM",
    [SMMU_CMD_SYNC]            = "SMMU_CMD_SYNC",
};

static inline const char *smmu_cmd_string(SMMUCommandType type)
{
    if (type > SMMU_CMD_NONE && type < ARRAY_SIZE(cmd_stringify)) {
        return cmd_stringify[type] ? cmd_stringify[type] : "UNKNOWN";
    } else {
        return "INVALID";
    }
}

/* CMDQ fields */

typedef enum {
    SMMU_CERROR_NONE = 0,
    SMMU_CERROR_ILL,
    SMMU_CERROR_ABT,
    SMMU_CERROR_ATC_INV_SYNC,
} SMMUCmdError;

enum { /* Command completion notification */
    CMD_SYNC_SIG_NONE,
    CMD_SYNC_SIG_IRQ,
    CMD_SYNC_SIG_SEV,
};

#define CMD_TYPE(x)         extract32((x)->word[0], 0 , 8)
#define CMD_NUM(x)          extract32((x)->word[0], 12 , 5)
#define CMD_SCALE(x)        extract32((x)->word[0], 20 , 5)
#define CMD_SSEC(x)         extract32((x)->word[0], 10, 1)
#define CMD_SSV(x)          extract32((x)->word[0], 11, 1)
#define CMD_RESUME_AC(x)    extract32((x)->word[0], 12, 1)
#define CMD_RESUME_AB(x)    extract32((x)->word[0], 13, 1)
#define CMD_SYNC_CS(x)      extract32((x)->word[0], 12, 2)
#define CMD_SSID(x)         extract32((x)->word[0], 12, 20)
#define CMD_SID(x)          ((x)->word[1])
#define CMD_VMID(x)         extract32((x)->word[1], 0 , 16)
#define CMD_ASID(x)         extract32((x)->word[1], 16, 16)
#define CMD_RESUME_STAG(x)  extract32((x)->word[2], 0 , 16)
#define CMD_RESP(x)         extract32((x)->word[2], 11, 2)
#define CMD_LEAF(x)         extract32((x)->word[2], 0 , 1)
#define CMD_TTL(x)          extract32((x)->word[2], 8 , 2)
#define CMD_TG(x)           extract32((x)->word[2], 10, 2)
#define CMD_STE_RANGE(x)    extract32((x)->word[2], 0 , 5)
#define CMD_ADDR(x) ({                                        \
            uint64_t high = (uint64_t)(x)->word[3];           \
            uint64_t low = extract32((x)->word[2], 12, 20);    \
            uint64_t addr = high << 32 | (low << 12);         \
            addr;                                             \
        })

#define SMMU_FEATURE_2LVL_STE (1 << 0)

/* Events */

typedef enum SMMUEventType {
    SMMU_EVT_NONE               = 0x00,
    SMMU_EVT_F_UUT                    ,
    SMMU_EVT_C_BAD_STREAMID           ,
    SMMU_EVT_F_STE_FETCH              ,
    SMMU_EVT_C_BAD_STE                ,
    SMMU_EVT_F_BAD_ATS_TREQ           ,
    SMMU_EVT_F_STREAM_DISABLED        ,
    SMMU_EVT_F_TRANS_FORBIDDEN        ,
    SMMU_EVT_C_BAD_SUBSTREAMID        ,
    SMMU_EVT_F_CD_FETCH               ,
    SMMU_EVT_C_BAD_CD                 ,
    SMMU_EVT_F_WALK_EABT              ,
    SMMU_EVT_F_TRANSLATION      = 0x10,
    SMMU_EVT_F_ADDR_SIZE              ,
    SMMU_EVT_F_ACCESS                 ,
    SMMU_EVT_F_PERMISSION             ,
    SMMU_EVT_F_TLB_CONFLICT     = 0x20,
    SMMU_EVT_F_CFG_CONFLICT           ,
    SMMU_EVT_E_PAGE_REQ         = 0x24,
} SMMUEventType;

static const char *event_stringify[] = {
    [SMMU_EVT_NONE]                     = "no recorded event",
    [SMMU_EVT_F_UUT]                    = "SMMU_EVT_F_UUT",
    [SMMU_EVT_C_BAD_STREAMID]           = "SMMU_EVT_C_BAD_STREAMID",
    [SMMU_EVT_F_STE_FETCH]              = "SMMU_EVT_F_STE_FETCH",
    [SMMU_EVT_C_BAD_STE]                = "SMMU_EVT_C_BAD_STE",
    [SMMU_EVT_F_BAD_ATS_TREQ]           = "SMMU_EVT_F_BAD_ATS_TREQ",
    [SMMU_EVT_F_STREAM_DISABLED]        = "SMMU_EVT_F_STREAM_DISABLED",
    [SMMU_EVT_F_TRANS_FORBIDDEN]        = "SMMU_EVT_F_TRANS_FORBIDDEN",
    [SMMU_EVT_C_BAD_SUBSTREAMID]        = "SMMU_EVT_C_BAD_SUBSTREAMID",
    [SMMU_EVT_F_CD_FETCH]               = "SMMU_EVT_F_CD_FETCH",
    [SMMU_EVT_C_BAD_CD]                 = "SMMU_EVT_C_BAD_CD",
    [SMMU_EVT_F_WALK_EABT]              = "SMMU_EVT_F_WALK_EABT",
    [SMMU_EVT_F_TRANSLATION]            = "SMMU_EVT_F_TRANSLATION",
    [SMMU_EVT_F_ADDR_SIZE]              = "SMMU_EVT_F_ADDR_SIZE",
    [SMMU_EVT_F_ACCESS]                 = "SMMU_EVT_F_ACCESS",
    [SMMU_EVT_F_PERMISSION]             = "SMMU_EVT_F_PERMISSION",
    [SMMU_EVT_F_TLB_CONFLICT]           = "SMMU_EVT_F_TLB_CONFLICT",
    [SMMU_EVT_F_CFG_CONFLICT]           = "SMMU_EVT_F_CFG_CONFLICT",
    [SMMU_EVT_E_PAGE_REQ]               = "SMMU_EVT_E_PAGE_REQ",
};

static inline const char *smmu_event_string(SMMUEventType type)
{
    if (type < ARRAY_SIZE(event_stringify)) {
        return event_stringify[type] ? event_stringify[type] : "UNKNOWN";
    } else {
        return "INVALID";
    }
}

/*  Encode an event record */
typedef struct SMMUEventInfo {
    SMMUEventType type;
    uint32_t sid;
    bool recorded;
    bool inval_ste_allowed;
    union {
        struct {
            uint32_t ssid;
            bool ssv;
            dma_addr_t addr;
            bool rnw;
            bool pnu;
            bool ind;
       } f_uut;
       struct SSIDInfo {
            uint32_t ssid;
            bool ssv;
       } c_bad_streamid;
       struct SSIDAddrInfo {
            uint32_t ssid;
            bool ssv;
            dma_addr_t addr;
       } f_ste_fetch;
       struct SSIDInfo c_bad_ste;
       struct {
            dma_addr_t addr;
            bool rnw;
       } f_transl_forbidden;
       struct {
            uint32_t ssid;
       } c_bad_substream;
       struct SSIDAddrInfo f_cd_fetch;
       struct SSIDInfo c_bad_cd;
       struct FullInfo {
            bool stall;
            uint16_t stag;
            uint32_t ssid;
            bool ssv;
            bool s2;
            dma_addr_t addr;
            bool rnw;
            bool pnu;
            bool ind;
            uint8_t class;
            dma_addr_t addr2;
       } f_walk_eabt;
       struct FullInfo f_translation;
       struct FullInfo f_addr_size;
       struct FullInfo f_access;
       struct FullInfo f_permission;
       struct SSIDInfo f_cfg_conflict;
       /**
        * not supported yet:
        * F_BAD_ATS_TREQ
        * F_BAD_ATS_TREQ
        * F_TLB_CONFLICT
        * E_PAGE_REQUEST
        * IMPDEF_EVENTn
        */
    } u;
} SMMUEventInfo;

/* EVTQ fields */

#define EVT_Q_OVERFLOW        (1 << 31)

#define EVT_SET_TYPE(x, v)  ((x)->word[0] = deposit32((x)->word[0], 0 , 8 , v))
#define EVT_SET_SSV(x, v)   ((x)->word[0] = deposit32((x)->word[0], 11, 1 , v))
#define EVT_SET_SSID(x, v)  ((x)->word[0] = deposit32((x)->word[0], 12, 20, v))
#define EVT_SET_SID(x, v)   ((x)->word[1] = v)
#define EVT_SET_STAG(x, v)  ((x)->word[2] = deposit32((x)->word[2], 0 , 16, v))
#define EVT_SET_STALL(x, v) ((x)->word[2] = deposit32((x)->word[2], 31, 1 , v))
#define EVT_SET_PNU(x, v)   ((x)->word[3] = deposit32((x)->word[3], 1 , 1 , v))
#define EVT_SET_IND(x, v)   ((x)->word[3] = deposit32((x)->word[3], 2 , 1 , v))
#define EVT_SET_RNW(x, v)   ((x)->word[3] = deposit32((x)->word[3], 3 , 1 , v))
#define EVT_SET_S2(x, v)    ((x)->word[3] = deposit32((x)->word[3], 7 , 1 , v))
#define EVT_SET_CLASS(x, v) ((x)->word[3] = deposit32((x)->word[3], 8 , 2 , v))
#define EVT_SET_ADDR(x, addr)                             \
    do {                                                  \
            (x)->word[5] = (uint32_t)(addr >> 32);        \
            (x)->word[4] = (uint32_t)(addr & 0xffffffff); \
    } while (0)
#define EVT_SET_ADDR2(x, addr)                            \
    do {                                                  \
            (x)->word[7] = (uint32_t)(addr >> 32);        \
            (x)->word[6] = (uint32_t)(addr & 0xffffffff); \
    } while (0)

void smmuv3_record_event(SMMUv3State *s, SMMUEventInfo *event);

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
#define STE_S2HD(x)        extract32((x)->word[5], 24, 1)
#define STE_S2HA(x)        extract32((x)->word[5], 25, 1)
#define STE_S2S(x)         extract32((x)->word[5], 26, 1)
#define STE_CTXPTR(x)                                           \
    ({                                                          \
        unsigned long addr;                                     \
        addr = (uint64_t)extract32((x)->word[1], 0, 16) << 32;  \
        addr |= (uint64_t)((x)->word[0] & 0xffffffc0);          \
        addr;                                                   \
    })

#define STE_S2TTB(x)                                            \
    ({                                                          \
        unsigned long addr;                                     \
        addr = (uint64_t)extract32((x)->word[7], 0, 16) << 32;  \
        addr |= (uint64_t)((x)->word[6] & 0xfffffff0);          \
        addr;                                                   \
    })

static inline int oas2bits(int oas_field)
{
    switch (oas_field) {
    case 0:
        return 32;
    case 1:
        return 36;
    case 2:
        return 40;
    case 3:
        return 42;
    case 4:
        return 44;
    case 5:
        return 48;
    }
    return -1;
}

static inline int pa_range(STE *ste)
{
    int oas_field = MIN(STE_S2PS(ste), SMMU_IDR5_OAS);

    if (!STE_S2AA64(ste)) {
        return 40;
    }

    return oas2bits(oas_field);
}

#define MAX_PA(ste) ((1 << pa_range(ste)) - 1)

/* CD fields */

#define CD_VALID(x)   extract32((x)->word[0], 31, 1)
#define CD_ASID(x)    extract32((x)->word[1], 16, 16)
#define CD_TTB(x, sel)                                      \
    ({                                                      \
        uint64_t hi, lo;                                    \
        hi = extract32((x)->word[(sel) * 2 + 3], 0, 19);    \
        hi <<= 32;                                          \
        lo = (x)->word[(sel) * 2 + 2] & ~0xfULL;            \
        hi | lo;                                            \
    })
#define CD_HAD(x, sel)   extract32((x)->word[(sel) * 2 + 2], 1, 1)

#define CD_TSZ(x, sel)   extract32((x)->word[0], (16 * (sel)) + 0, 6)
#define CD_TG(x, sel)    extract32((x)->word[0], (16 * (sel)) + 6, 2)
#define CD_EPD(x, sel)   extract32((x)->word[0], (16 * (sel)) + 14, 1)
#define CD_ENDI(x)       extract32((x)->word[0], 15, 1)
#define CD_IPS(x)        extract32((x)->word[1], 0 , 3)
#define CD_TBI(x)        extract32((x)->word[1], 6 , 2)
#define CD_HD(x)         extract32((x)->word[1], 10 , 1)
#define CD_HA(x)         extract32((x)->word[1], 11 , 1)
#define CD_S(x)          extract32((x)->word[1], 12, 1)
#define CD_R(x)          extract32((x)->word[1], 13, 1)
#define CD_A(x)          extract32((x)->word[1], 14, 1)
#define CD_AARCH64(x)    extract32((x)->word[1], 9 , 1)

/**
 * tg2granule - Decodes the CD translation granule size field according
 * to the ttbr in use
 * @bits: TG0/1 fields
 * @ttbr: ttbr index in use
 */
static inline int tg2granule(int bits, int ttbr)
{
    switch (bits) {
    case 0:
        return ttbr ? 0  : 12;
    case 1:
        return ttbr ? 14 : 16;
    case 2:
        return ttbr ? 12 : 14;
    case 3:
        return ttbr ? 16 :  0;
    default:
        return 0;
    }
}

static inline uint64_t l1std_l2ptr(STEDesc *desc)
{
    uint64_t hi, lo;

    hi = desc->word[1];
    lo = desc->word[0] & ~0x1fULL;
    return hi << 32 | lo;
}

#define L1STD_SPAN(stm) (extract32((stm)->word[0], 0, 5))

#endif

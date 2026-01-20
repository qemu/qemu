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

#include "hw/core/registerfields.h"
#include "hw/arm/smmu-common.h"
#include "hw/arm/smmuv3-common.h"

typedef enum SMMUTranslationStatus {
    SMMU_TRANS_DISABLE,
    SMMU_TRANS_ABORT,
    SMMU_TRANS_BYPASS,
    SMMU_TRANS_ERROR,
    SMMU_TRANS_SUCCESS,
} SMMUTranslationStatus;

typedef enum SMMUTranslationClass {
    SMMU_CLASS_CD,
    SMMU_CLASS_TT,
    SMMU_CLASS_IN,
} SMMUTranslationClass;

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
#define CMD_ADDR(x)                             \
    (((uint64_t)((x)->word[3]) << 32) |         \
     ((extract64((x)->word[2], 12, 20)) << 12))

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

    g_assert_not_reached();
}

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

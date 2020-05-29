/*
 * QEMU PowerPC XIVE internal structure definitions
 *
 *
 * The XIVE structures are accessed by the HW and their format is
 * architected to be big-endian. Some macros are provided to ease
 * access to the different fields.
 *
 *
 * Copyright (c) 2016-2018, IBM Corporation.
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 */

#ifndef PPC_XIVE_REGS_H
#define PPC_XIVE_REGS_H

#include "qemu/bswap.h"
#include "qemu/host-utils.h"

/*
 * Interrupt source number encoding on PowerBUS
 */
/*
 * Trigger data definition
 *
 * The trigger definition is used for triggers both for HW source
 * interrupts (PHB, PSI), as well as for rerouting interrupts between
 * Interrupt Controller.
 *
 * HW source controllers set bit0 of word0 to ‘0’ as they provide EAS
 * information (EAS block + EAS index) in the 8 byte data and not END
 * information, which is use for rerouting interrupts.
 *
 * bit1 of word0 to ‘1’ signals that the state bit check has been
 * performed.
 */
#define XIVE_TRIGGER_END        PPC_BIT(0)
#define XIVE_TRIGGER_PQ         PPC_BIT(1)

/*
 * QEMU macros to manipulate the trigger payload in native endian
 */
#define XIVE_EAS_BLOCK(n)       (((n) >> 28) & 0xf)
#define XIVE_EAS_INDEX(n)       ((n) & 0x0fffffff)
#define XIVE_EAS(blk, idx)      ((uint32_t)(blk) << 28 | (idx))

#define TM_SHIFT                16

/* TM register offsets */
#define TM_QW0_USER             0x000 /* All rings */
#define TM_QW1_OS               0x010 /* Ring 0..2 */
#define TM_QW2_HV_POOL          0x020 /* Ring 0..1 */
#define TM_QW3_HV_PHYS          0x030 /* Ring 0..1 */

/* Byte offsets inside a QW             QW0 QW1 QW2 QW3 */
#define TM_NSR                  0x0  /*  +   +   -   +  */
#define TM_CPPR                 0x1  /*  -   +   -   +  */
#define TM_IPB                  0x2  /*  -   +   +   +  */
#define TM_LSMFB                0x3  /*  -   +   +   +  */
#define TM_ACK_CNT              0x4  /*  -   +   -   -  */
#define TM_INC                  0x5  /*  -   +   -   +  */
#define TM_AGE                  0x6  /*  -   +   -   +  */
#define TM_PIPR                 0x7  /*  -   +   -   +  */

#define TM_WORD0                0x0
#define TM_WORD1                0x4

/*
 * QW word 2 contains the valid bit at the top and other fields
 * depending on the QW.
 */
#define   TM_WORD2              0x8
#define   TM_QW0W2_VU           PPC_BIT32(0)
#define   TM_QW0W2_LOGIC_SERV   PPC_BITMASK32(1, 31) /* XX 2,31 ? */
#define   TM_QW1W2_VO           PPC_BIT32(0)
#define   TM_QW1W2_OS_CAM       PPC_BITMASK32(8, 31)
#define   TM_QW2W2_VP           PPC_BIT32(0)
#define   TM_QW2W2_POOL_CAM     PPC_BITMASK32(8, 31)
#define   TM_QW3W2_VT           PPC_BIT32(0)
#define   TM_QW3W2_LP           PPC_BIT32(6)
#define   TM_QW3W2_LE           PPC_BIT32(7)
#define   TM_QW3W2_T            PPC_BIT32(31)

/*
 * In addition to normal loads to "peek" and writes (only when invalid)
 * using 4 and 8 bytes accesses, the above registers support these
 * "special" byte operations:
 *
 *   - Byte load from QW0[NSR] - User level NSR (EBB)
 *   - Byte store to QW0[NSR] - User level NSR (EBB)
 *   - Byte load/store to QW1[CPPR] and QW3[CPPR] - CPPR access
 *   - Byte load from QW3[TM_WORD2] - Read VT||00000||LP||LE on thrd 0
 *                                    otherwise VT||0000000
 *   - Byte store to QW3[TM_WORD2] - Set VT bit (and LP/LE if present)
 *
 * Then we have all these "special" CI ops at these offset that trigger
 * all sorts of side effects:
 */
#define TM_SPC_ACK_EBB          0x800   /* Load8 ack EBB to reg*/
#define TM_SPC_ACK_OS_REG       0x810   /* Load16 ack OS irq to reg */
#define TM_SPC_PUSH_USR_CTX     0x808   /* Store32 Push/Validate user context */
#define TM_SPC_PULL_USR_CTX     0x808   /* Load32 Pull/Invalidate user
                                         * context */
#define TM_SPC_SET_OS_PENDING   0x812   /* Store8 Set OS irq pending bit */
#define TM_SPC_PULL_OS_CTX      0x818   /* Load32/Load64 Pull/Invalidate OS
                                         * context to reg */
#define TM_SPC_PULL_POOL_CTX    0x828   /* Load32/Load64 Pull/Invalidate Pool
                                         * context to reg*/
#define TM_SPC_ACK_HV_REG       0x830   /* Load16 ack HV irq to reg */
#define TM_SPC_PULL_USR_CTX_OL  0xc08   /* Store8 Pull/Inval usr ctx to odd
                                         * line */
#define TM_SPC_ACK_OS_EL        0xc10   /* Store8 ack OS irq to even line */
#define TM_SPC_ACK_HV_POOL_EL   0xc20   /* Store8 ack HV evt pool to even
                                         * line */
#define TM_SPC_ACK_HV_EL        0xc30   /* Store8 ack HV irq to even line */
/* XXX more... */

/* NSR fields for the various QW ack types */
#define TM_QW0_NSR_EB           PPC_BIT8(0)
#define TM_QW1_NSR_EO           PPC_BIT8(0)
#define TM_QW3_NSR_HE           PPC_BITMASK8(0, 1)
#define  TM_QW3_NSR_HE_NONE     0
#define  TM_QW3_NSR_HE_POOL     1
#define  TM_QW3_NSR_HE_PHYS     2
#define  TM_QW3_NSR_HE_LSI      3
#define TM_QW3_NSR_I            PPC_BIT8(2)
#define TM_QW3_NSR_GRP_LVL      PPC_BIT8(3, 7)

/*
 * EAS (Event Assignment Structure)
 *
 * One per interrupt source. Targets an interrupt to a given Event
 * Notification Descriptor (END) and provides the corresponding
 * logical interrupt number (END data)
 */
typedef struct XiveEAS {
        /*
         * Use a single 64-bit definition to make it easier to perform
         * atomic updates
         */
        uint64_t        w;
#define EAS_VALID       PPC_BIT(0)
#define EAS_END_BLOCK   PPC_BITMASK(4, 7)        /* Destination END block# */
#define EAS_END_INDEX   PPC_BITMASK(8, 31)       /* Destination END index */
#define EAS_MASKED      PPC_BIT(32)              /* Masked */
#define EAS_END_DATA    PPC_BITMASK(33, 63)      /* Data written to the END */
} XiveEAS;

#define xive_eas_is_valid(eas)   (be64_to_cpu((eas)->w) & EAS_VALID)
#define xive_eas_is_masked(eas)  (be64_to_cpu((eas)->w) & EAS_MASKED)

void xive_eas_pic_print_info(XiveEAS *eas, uint32_t lisn, Monitor *mon);

static inline uint64_t xive_get_field64(uint64_t mask, uint64_t word)
{
    return (be64_to_cpu(word) & mask) >> ctz64(mask);
}

static inline uint64_t xive_set_field64(uint64_t mask, uint64_t word,
                                        uint64_t value)
{
    uint64_t tmp =
        (be64_to_cpu(word) & ~mask) | ((value << ctz64(mask)) & mask);
    return cpu_to_be64(tmp);
}

static inline uint32_t xive_get_field32(uint32_t mask, uint32_t word)
{
    return (be32_to_cpu(word) & mask) >> ctz32(mask);
}

static inline uint32_t xive_set_field32(uint32_t mask, uint32_t word,
                                        uint32_t value)
{
    uint32_t tmp =
        (be32_to_cpu(word) & ~mask) | ((value << ctz32(mask)) & mask);
    return cpu_to_be32(tmp);
}

/* Event Notification Descriptor (END) */
typedef struct XiveEND {
        uint32_t        w0;
#define END_W0_VALID             PPC_BIT32(0) /* "v" bit */
#define END_W0_ENQUEUE           PPC_BIT32(1) /* "q" bit */
#define END_W0_UCOND_NOTIFY      PPC_BIT32(2) /* "n" bit */
#define END_W0_BACKLOG           PPC_BIT32(3) /* "b" bit */
#define END_W0_PRECL_ESC_CTL     PPC_BIT32(4) /* "p" bit */
#define END_W0_ESCALATE_CTL      PPC_BIT32(5) /* "e" bit */
#define END_W0_UNCOND_ESCALATE   PPC_BIT32(6) /* "u" bit - DD2.0 */
#define END_W0_SILENT_ESCALATE   PPC_BIT32(7) /* "s" bit - DD2.0 */
#define END_W0_QSIZE             PPC_BITMASK32(12, 15)
#define END_W0_SW0               PPC_BIT32(16)
#define END_W0_FIRMWARE          END_W0_SW0 /* Owned by FW */
#define END_QSIZE_4K             0
#define END_QSIZE_64K            4
#define END_W0_HWDEP             PPC_BITMASK32(24, 31)
        uint32_t        w1;
#define END_W1_ESn               PPC_BITMASK32(0, 1)
#define END_W1_ESn_P             PPC_BIT32(0)
#define END_W1_ESn_Q             PPC_BIT32(1)
#define END_W1_ESe               PPC_BITMASK32(2, 3)
#define END_W1_ESe_P             PPC_BIT32(2)
#define END_W1_ESe_Q             PPC_BIT32(3)
#define END_W1_GENERATION        PPC_BIT32(9)
#define END_W1_PAGE_OFF          PPC_BITMASK32(10, 31)
        uint32_t        w2;
#define END_W2_MIGRATION_REG     PPC_BITMASK32(0, 3)
#define END_W2_OP_DESC_HI        PPC_BITMASK32(4, 31)
        uint32_t        w3;
#define END_W3_OP_DESC_LO        PPC_BITMASK32(0, 31)
        uint32_t        w4;
#define END_W4_ESC_END_BLOCK     PPC_BITMASK32(4, 7)
#define END_W4_ESC_END_INDEX     PPC_BITMASK32(8, 31)
        uint32_t        w5;
#define END_W5_ESC_END_DATA      PPC_BITMASK32(1, 31)
        uint32_t        w6;
#define END_W6_FORMAT_BIT        PPC_BIT32(8)
#define END_W6_NVT_BLOCK         PPC_BITMASK32(9, 12)
#define END_W6_NVT_INDEX         PPC_BITMASK32(13, 31)
        uint32_t        w7;
#define END_W7_F0_IGNORE         PPC_BIT32(0)
#define END_W7_F0_BLK_GROUPING   PPC_BIT32(1)
#define END_W7_F0_PRIORITY       PPC_BITMASK32(8, 15)
#define END_W7_F1_WAKEZ          PPC_BIT32(0)
#define END_W7_F1_LOG_SERVER_ID  PPC_BITMASK32(1, 31)
} XiveEND;

#define xive_end_is_valid(end)    (be32_to_cpu((end)->w0) & END_W0_VALID)
#define xive_end_is_enqueue(end)  (be32_to_cpu((end)->w0) & END_W0_ENQUEUE)
#define xive_end_is_notify(end)   (be32_to_cpu((end)->w0) & END_W0_UCOND_NOTIFY)
#define xive_end_is_backlog(end)  (be32_to_cpu((end)->w0) & END_W0_BACKLOG)
#define xive_end_is_escalate(end) (be32_to_cpu((end)->w0) & END_W0_ESCALATE_CTL)
#define xive_end_is_uncond_escalation(end)              \
    (be32_to_cpu((end)->w0) & END_W0_UNCOND_ESCALATE)
#define xive_end_is_silent_escalation(end)              \
    (be32_to_cpu((end)->w0) & END_W0_SILENT_ESCALATE)

static inline uint64_t xive_end_qaddr(XiveEND *end)
{
    return ((uint64_t) be32_to_cpu(end->w2) & 0x0fffffff) << 32 |
        be32_to_cpu(end->w3);
}

void xive_end_pic_print_info(XiveEND *end, uint32_t end_idx, Monitor *mon);
void xive_end_queue_pic_print_info(XiveEND *end, uint32_t width, Monitor *mon);
void xive_end_eas_pic_print_info(XiveEND *end, uint32_t end_idx, Monitor *mon);

/* Notification Virtual Target (NVT) */
typedef struct XiveNVT {
        uint32_t        w0;
#define NVT_W0_VALID             PPC_BIT32(0)
        uint32_t        w1;
#define NVT_W1_EQ_BLOCK          PPC_BITMASK32(0, 3)
#define NVT_W1_EQ_INDEX          PPC_BITMASK32(4, 31)
        uint32_t        w2;
        uint32_t        w3;
        uint32_t        w4;
#define NVT_W4_IPB               PPC_BITMASK32(16, 23)
        uint32_t        w5;
        uint32_t        w6;
        uint32_t        w7;
        uint32_t        w8;
#define NVT_W8_GRP_VALID         PPC_BIT32(0)
        uint32_t        w9;
        uint32_t        wa;
        uint32_t        wb;
        uint32_t        wc;
        uint32_t        wd;
        uint32_t        we;
        uint32_t        wf;
} XiveNVT;

#define xive_nvt_is_valid(nvt)    (be32_to_cpu((nvt)->w0) & NVT_W0_VALID)

/*
 * The VP number space in a block is defined by the END_W6_NVT_INDEX
 * field of the XIVE END
 */
#define XIVE_NVT_SHIFT                19
#define XIVE_NVT_COUNT                (1 << XIVE_NVT_SHIFT)

static inline uint32_t xive_nvt_cam_line(uint8_t nvt_blk, uint32_t nvt_idx)
{
    return (nvt_blk << XIVE_NVT_SHIFT) | nvt_idx;
}

static inline uint32_t xive_nvt_idx(uint32_t cam_line)
{
    return cam_line & ((1 << XIVE_NVT_SHIFT) - 1);
}

static inline uint32_t xive_nvt_blk(uint32_t cam_line)
{
    return (cam_line >> XIVE_NVT_SHIFT) & 0xf;
}

#endif /* PPC_XIVE_REGS_H */

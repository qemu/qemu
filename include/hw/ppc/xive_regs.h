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

/*
 * Interrupt source number encoding on PowerBUS
 */
#define XIVE_SRCNO_BLOCK(srcno) (((srcno) >> 28) & 0xf)
#define XIVE_SRCNO_INDEX(srcno) ((srcno) & 0x0fffffff)
#define XIVE_SRCNO(blk, idx)    ((uint32_t)(blk) << 28 | (idx))

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

#endif /* PPC_XIVE_REGS_H */

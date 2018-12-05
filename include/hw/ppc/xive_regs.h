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

#endif /* PPC_XIVE_REGS_H */

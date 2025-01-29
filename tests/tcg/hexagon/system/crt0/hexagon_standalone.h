/*
 *  Copyright(c) 2024-2025 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdint.h>
#include <stdio.h>

#ifndef _TLB_H
#define _TLB_H

typedef enum {
    SHIFT_4K = 0,
    SHIFT_16K,
    SHIFT_64K,
    SHIFT_256K,
    SHIFT_1M,
    SHIFT_4M,
    SHIFT_16M,
    SHIFT_64M,
    SHIFT_256M,
    SHIFT_1G,
} PageShift;

typedef enum {
    PAGE_4K   = 1 << SHIFT_4K,
    PAGE_16K  = 1 << SHIFT_16K,
    PAGE_64K  = 1 << SHIFT_64K,
    PAGE_256K = 1 << SHIFT_256K,
    PAGE_1M   = 1 << SHIFT_1M,
    PAGE_4M   = 1 << SHIFT_4M,
    PAGE_16M  = 1 << SHIFT_16M,
    PAGE_64M  = 1 << SHIFT_64M,
    PAGE_256M = 1 << SHIFT_256M,
    PAGE_1G   = 1 << SHIFT_1G,
} PageSize;


/*
 * TLB entry format:
 *
 * TLBHI:
 *    63 | 62 | 61 | 60:59 | 58 -- 52 | 51 -------- 32 |
 *    V  | G  | EP   PPNex | ASID     | Virtual Page # |
 *    -------------------------------------------
 *
 *    V            - Valid bit.
 *    G            - Global bit.  If set ASID is ignored and the page
 *                   is globally  accessible.
 *    EP           - Extra Physical Bit
 *    PPNex        - Extended Physical Page. (V73 and beyond)
 *    ASID         - Address Space Identifier.
 *    Virtual Page - Virtual Page number.  It has a minimum 4K alignment.
 *                   This means the input value is right shifted 12 bits
 *                   and that is what is placed into this field.
 *
 * TLBLO:
 *    31 | 30 | 29 | 28 | 27 -- 24 | 23 --------- 1  | 0 |
 *    X  | W  | R  | U  | C        | Physical Page # | S |
 *    ----------------------------------------------------
 *
 *    X              - Execute Enabled
 *    W              - Write Enabled
 *    R              - Read Enabled
 *    U              - User mode accessible
 *    C              - Cacheablilty attributes: L1/L2 Cacheable Writeback/thru
 *    Physical Page  - Physical Page #
 *
 */

typedef union {
  struct {
    uint64_t S:1;
    uint64_t PPN:23;
    uint64_t CacheAttr:4;
    uint64_t XWRU:4;
    uint64_t VirtualPage:20;
    uint64_t ASID:7;
#if __HEXAGON_ARCH__ < 73
    uint64_t A0:1;
    uint64_t A1:1;
#else
    uint64_t PPN_EX:2;
#endif
    uint64_t EP:1;
    uint64_t VG:2;
  };
  uint64_t raw;
} TLBEntry;


#define TLB_NOT_FOUND 0x80000000

int add_translation_extended(int index, void *va, uint64_t pa,
                              unsigned int page_size, unsigned int xwru,
                              unsigned int cccc, unsigned int asid,
                              unsigned int aa, unsigned int vg);
void add_translation_fixed(int index, void *va, void *pa, int cccc,
                            int permissions);
void add_translation(void *va, void *pa, int cccc);

#endif /* _TLB_H */

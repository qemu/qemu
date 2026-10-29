/*
 * PNV QTest common definitions
 *
 * Copyright (c) 2026, IBM Corporation.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef TEST_PNV_QTEST_COMMON_H
#define TEST_PNV_QTEST_COMMON_H

#define PPC_BIT(bit)            (0x8000000000000000ULL >> (bit))
#define PPC_BIT32(bit)          (0x80000000 >> (bit))
#define PPC_BIT8(bit)           (0x80 >> (bit))
#define PPC_BITMASK(bs, be)     ((PPC_BIT(bs) - PPC_BIT(be)) | PPC_BIT(bs))
#define PPC_BITMASK32(bs, be)   ((PPC_BIT32(bs) - PPC_BIT32(be)) | \
                                 PPC_BIT32(bs))

#define MASK_TO_LSH(m)          (__builtin_ffsll(m) - 1)
#define GETFIELD(m, v)          (((v) & (m)) >> MASK_TO_LSH(m))
#define SETFIELD(m, v, val) \
        (((v) & ~(m)) | ((((typeof(v))(val)) << MASK_TO_LSH(m)) & (m)))

#endif

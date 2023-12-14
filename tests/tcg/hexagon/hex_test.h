/*
 *  Copyright(c) 2022-2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */


#ifndef HEX_TEST_H
#define HEX_TEST_H

static inline void __check32(int line, uint32_t val, uint32_t expect)
{
    if (val != expect) {
        printf("ERROR at line %d: 0x%08x != 0x%08x\n", line, val, expect);
        err++;
    }
}

#define check32(RES, EXP) __check32(__LINE__, RES, EXP)

static inline void __check64(int line, uint64_t val, uint64_t expect)
{
    if (val != expect) {
        printf("ERROR at line %d: 0x%016llx != 0x%016llx\n", line, val, expect);
        err++;
    }
}

#define check64(RES, EXP) __check64(__LINE__, RES, EXP)

static inline void __chk_error(const char *filename, int line, int ret)
{
    if (ret < 0) {
        printf("ERROR %s:%d - %d\n", filename, line, ret);
        err++;
    }
}

#define chk_error(ret) __chk_error(__FILE__, __LINE__, (ret))

static inline void __checkp(int line, void *p, void *expect)
{
    if (p != expect) {
        printf("ERROR at line %d: 0x%p != 0x%p\n", line, p, expect);
        err++;
    }
}

#define checkp(RES, EXP) __checkp(__LINE__, RES, EXP)

static inline void __check32_ne(int line, uint32_t val, uint32_t expect)
{
    if (val == expect) {
        printf("ERROR at line %d: 0x%08x == 0x%08x\n", line, val, expect);
        err++;
    }
}

#define check32_ne(RES, EXP) __check32_ne(__LINE__, RES, EXP)

static inline void __check64_ne(int line, uint64_t val, uint64_t expect)
{
    if (val == expect) {
        printf("ERROR at line %d: 0x%016llx == 0x%016llx\n", line, val, expect);
        err++;
    }
}

#define check64_ne(RES, EXP) __check64_ne(__LINE__, RES, EXP)

/* Define the bits in Hexagon USR register */
#define USR_OVF_BIT          0        /* Sticky saturation overflow */
#define USR_FPINVF_BIT       1        /* IEEE FP invalid sticky flag */
#define USR_FPDBZF_BIT       2        /* IEEE FP divide-by-zero sticky flag */
#define USR_FPOVFF_BIT       3        /* IEEE FP overflow sticky flag */
#define USR_FPUNFF_BIT       4        /* IEEE FP underflow sticky flag */
#define USR_FPINPF_BIT       5        /* IEEE FP inexact sticky flag */

/* Corresponding values in USR */
#define USR_CLEAR            0
#define USR_OVF              (1 << USR_OVF_BIT)
#define USR_FPINVF           (1 << USR_FPINVF_BIT)
#define USR_FPDBZF           (1 << USR_FPDBZF_BIT)
#define USR_FPOVFF           (1 << USR_FPOVFF_BIT)
#define USR_FPUNFF           (1 << USR_FPUNFF_BIT)
#define USR_FPINPF           (1 << USR_FPINPF_BIT)

/* Clear bits 0-5 in USR */
#define CLEAR_USRBITS \
    "r2 = usr\n\t" \
    "r2 = and(r2, #0xffffffc0)\n\t" \
    "usr = r2\n\t"

/* Clear bits 1-5 in USR */
#define CLEAR_FPSTATUS \
    "r2 = usr\n\t" \
    "r2 = and(r2, #0xffffffc1)\n\t" \
    "usr = r2\n\t"

/* Some useful floating point values */
const uint32_t SF_INF =              0x7f800000;
const uint32_t SF_QNaN =             0x7fc00000;
const uint32_t SF_QNaN_special =     0x7f800001;
const uint32_t SF_SNaN =             0x7fb00000;
const uint32_t SF_QNaN_neg =         0xffc00000;
const uint32_t SF_SNaN_neg =         0xffb00000;
const uint32_t SF_HEX_NaN =          0xffffffff;
const uint32_t SF_zero =             0x00000000;
const uint32_t SF_zero_neg =         0x80000000;
const uint32_t SF_one =              0x3f800000;
const uint32_t SF_one_recip =        0x3f7f0001;         /* 0.9960...  */
const uint32_t SF_one_invsqrta =     0x3f7f0000;         /* 0.99609375 */
const uint32_t SF_two =              0x40000000;
const uint32_t SF_four =             0x40800000;
const uint32_t SF_small_neg =        0xab98fba8;
const uint32_t SF_large_pos =        0x5afa572e;
const uint32_t SF_any =              0x3f800000;
const uint32_t SF_denorm =           0x00000001;
const uint32_t SF_random =           0x346001d6;

const uint64_t DF_QNaN =             0x7ff8000000000000ULL;
const uint64_t DF_SNaN =             0x7ff7000000000000ULL;
const uint64_t DF_QNaN_neg =         0xfff8000000000000ULL;
const uint64_t DF_SNaN_neg =         0xfff7000000000000ULL;
const uint64_t DF_HEX_NaN =          0xffffffffffffffffULL;
const uint64_t DF_zero =             0x0000000000000000ULL;
const uint64_t DF_zero_neg =         0x8000000000000000ULL;
const uint64_t DF_any =              0x3f80000000000000ULL;
const uint64_t DF_one =              0x3ff0000000000000ULL;
const uint64_t DF_one_hh =           0x3ff001ff80000000ULL;     /* 1.00048... */
const uint64_t DF_small_neg =        0xbd731f7500000000ULL;
const uint64_t DF_large_pos =        0x7f80000000000001ULL;

#endif

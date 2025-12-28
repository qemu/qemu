/*
 *  x86 condition code helpers for AF/CF/OF
 *
 *  Copyright (c) 2008 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#define DATA_BITS (1 << (3 + SHIFT))

#if DATA_BITS == 8
#define SUFFIX b
#define DATA_TYPE uint8_t
#define WIDER_TYPE uint32_t
#elif DATA_BITS == 16
#define SUFFIX w
#define DATA_TYPE uint16_t
#define WIDER_TYPE uint32_t
#elif DATA_BITS == 32
#define SUFFIX l
#define DATA_TYPE uint32_t
#if HOST_LONG_BITS >= 64
#define WIDER_TYPE uint64_t
#endif
#elif DATA_BITS == 64
#define SUFFIX q
#define DATA_TYPE uint64_t
#else
#error unhandled operand size
#endif

#define SIGN_MASK (((DATA_TYPE)1) << (DATA_BITS - 1))

/* dynamic flags computation */

static uint32_t glue(compute_aco_cout, SUFFIX)(DATA_TYPE carries)
{
    uint32_t af_cf, of;

    /*
     * AF, CF, OF computed from carry out vector.  To compute AF and CF, rotate it
     * left by one so cout(DATA_BITS - 1) is in bit 0 and cout(3) in bit 4.
     *
     * To compute OF, place the highest two carry bits into OF and the bit
     * immediately to the right of it; then, adding CC_O / 2 XORs them.
     */
    af_cf = ((carries << 1) | (carries >> (DATA_BITS - 1))) & (CC_A | CC_C);
    of = (lshift(carries, 12 - DATA_BITS) + CC_O / 2) & CC_O;
    return af_cf + of;
}

static uint32_t glue(compute_aco_add, SUFFIX)(DATA_TYPE dst, DATA_TYPE src1)
{
    DATA_TYPE src2 = dst - src1;
    DATA_TYPE carries = ADD_COUT_VEC(src1, src2, dst);
    return glue(compute_aco_cout, SUFFIX)(carries);
}

static int glue(compute_c_add, SUFFIX)(DATA_TYPE dst, DATA_TYPE src1)
{
    return dst < src1;
}

static uint32_t glue(compute_aco_adc, SUFFIX)(DATA_TYPE dst, DATA_TYPE src1,
                                         DATA_TYPE src3)
{
    DATA_TYPE src2 = dst - src1 - src3;
    DATA_TYPE carries = ADD_COUT_VEC(src1, src2, dst);
    return glue(compute_aco_cout, SUFFIX)(carries);
}

static int glue(compute_c_adc, SUFFIX)(DATA_TYPE dst, DATA_TYPE src1,
                                       DATA_TYPE src3)
{
#ifdef WIDER_TYPE
    WIDER_TYPE src13 = (WIDER_TYPE) src1 + (WIDER_TYPE) src3;

    return dst < src13;
#else
    return src3 ? dst <= src1 : dst < src1;
#endif
}

static uint32_t glue(compute_aco_sub, SUFFIX)(DATA_TYPE dst, DATA_TYPE src2)
{
    DATA_TYPE src1 = dst + src2;
    DATA_TYPE carries = SUB_COUT_VEC(src1, src2, dst);
    return glue(compute_aco_cout, SUFFIX)(carries);
}

static int glue(compute_c_sub, SUFFIX)(DATA_TYPE dst, DATA_TYPE src2)
{
    DATA_TYPE src1 = dst + src2;

    return src1 < src2;
}

static uint32_t glue(compute_aco_sbb, SUFFIX)(DATA_TYPE dst, DATA_TYPE src2,
                                         DATA_TYPE src3)
{
    DATA_TYPE src1 = dst + src2 + src3;
    DATA_TYPE carries = SUB_COUT_VEC(src1, src2, dst);
    return glue(compute_aco_cout, SUFFIX)(carries);
}

static int glue(compute_c_sbb, SUFFIX)(DATA_TYPE dst, DATA_TYPE src2,
                                       DATA_TYPE src3)
{
#ifdef WIDER_TYPE
    WIDER_TYPE src23 = (WIDER_TYPE) src2 + (WIDER_TYPE) src3;
    DATA_TYPE src1 = dst + src23;

    return src1 < src23;
#else
    DATA_TYPE src1 = dst + src2 + src3;

    return (src3 ? src1 <= src2 : src1 < src2);
#endif
}

static uint32_t glue(compute_aco_inc, SUFFIX)(DATA_TYPE dst, DATA_TYPE src1)
{
    uint32_t cf, af, of;

    cf = src1;
    af = (dst ^ (dst - 1)) & CC_A; /* bits 0..3 are all clear */
    of = (dst == SIGN_MASK) * CC_O;
    return cf + af + of;
}

static uint32_t glue(compute_aco_dec, SUFFIX)(DATA_TYPE dst, DATA_TYPE src1)
{
    uint32_t cf, af, of;

    cf = src1;
    af = (dst ^ (dst + 1)) & CC_A; /* bits 0..3 are all set */
    of = (dst == SIGN_MASK - 1) * CC_O;
    return cf + af + of;
}

static uint32_t glue(compute_aco_shl, SUFFIX)(DATA_TYPE dst, DATA_TYPE src1)
{
    uint32_t cf, af, of;

    cf = (src1 >> (DATA_BITS - 1)) & CC_C;
    af = 0; /* undefined */
    /* of is defined iff shift count == 1 */
    of = lshift(src1 ^ dst, 12 - DATA_BITS) & CC_O;
    return cf + af + of;
}

static int glue(compute_c_shl, SUFFIX)(DATA_TYPE dst, DATA_TYPE src1)
{
    return (src1 >> (DATA_BITS - 1)) & CC_C;
}

static uint32_t glue(compute_aco_sar, SUFFIX)(DATA_TYPE dst, DATA_TYPE src1)
{
    uint32_t cf, af, of;

    cf = src1 & 1;
    af = 0; /* undefined */
    /* of is defined iff shift count == 1 */
    of = lshift(src1 ^ dst, 12 - DATA_BITS) & CC_O;
    return cf + af + of;
}

static uint32_t glue(compute_aco_bmilg, SUFFIX)(DATA_TYPE dst, DATA_TYPE src1)
{
    uint32_t cf, af, of;

    cf = (src1 == 0);
    af = 0; /* undefined */
    of = 0;
    return cf + af + of;
}

static int glue(compute_c_bmilg, SUFFIX)(DATA_TYPE dst, DATA_TYPE src1)
{
    return src1 == 0;
}

static int glue(compute_aco_blsi, SUFFIX)(DATA_TYPE dst, DATA_TYPE src1)
{
    uint32_t cf, af, of;

    cf = (src1 != 0);
    af = 0; /* undefined */
    of = 0;
    return cf + af + of;
}

static int glue(compute_c_blsi, SUFFIX)(DATA_TYPE dst, DATA_TYPE src1)
{
    return src1 != 0;
}

#undef DATA_BITS
#undef SIGN_MASK
#undef DATA_TYPE
#undef SUFFIX
#undef WIDER_TYPE

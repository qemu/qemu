/*
 *  x86 condition code helpers
 *
 *  Copyright (c) 2008 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
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
#elif DATA_BITS == 16
#define SUFFIX w
#define DATA_TYPE uint16_t
#elif DATA_BITS == 32
#define SUFFIX l
#define DATA_TYPE uint32_t
#elif DATA_BITS == 64
#define SUFFIX q
#define DATA_TYPE uint64_t
#else
#error unhandled operand size
#endif

#define SIGN_MASK (((DATA_TYPE)1) << (DATA_BITS - 1))

/* dynamic flags computation */

static int glue(compute_all_add, SUFFIX)(DATA_TYPE dst, DATA_TYPE src1)
{
    int cf, pf, af, zf, sf, of;
    DATA_TYPE src2 = dst - src1;

    cf = dst < src1;
    pf = parity_table[(uint8_t)dst];
    af = (dst ^ src1 ^ src2) & CC_A;
    zf = (dst == 0) * CC_Z;
    sf = lshift(dst, 8 - DATA_BITS) & CC_S;
    of = lshift((src1 ^ src2 ^ -1) & (src1 ^ dst), 12 - DATA_BITS) & CC_O;
    return cf | pf | af | zf | sf | of;
}

static int glue(compute_c_add, SUFFIX)(DATA_TYPE dst, DATA_TYPE src1)
{
    return dst < src1;
}

static int glue(compute_all_adc, SUFFIX)(DATA_TYPE dst, DATA_TYPE src1,
                                         DATA_TYPE src3)
{
    int cf, pf, af, zf, sf, of;
    DATA_TYPE src2 = dst - src1 - src3;

    cf = (src3 ? dst <= src1 : dst < src1);
    pf = parity_table[(uint8_t)dst];
    af = (dst ^ src1 ^ src2) & 0x10;
    zf = (dst == 0) << 6;
    sf = lshift(dst, 8 - DATA_BITS) & 0x80;
    of = lshift((src1 ^ src2 ^ -1) & (src1 ^ dst), 12 - DATA_BITS) & CC_O;
    return cf | pf | af | zf | sf | of;
}

static int glue(compute_c_adc, SUFFIX)(DATA_TYPE dst, DATA_TYPE src1,
                                       DATA_TYPE src3)
{
    return src3 ? dst <= src1 : dst < src1;
}

static int glue(compute_all_sub, SUFFIX)(DATA_TYPE dst, DATA_TYPE src2)
{
    int cf, pf, af, zf, sf, of;
    DATA_TYPE src1 = dst + src2;

    cf = src1 < src2;
    pf = parity_table[(uint8_t)dst];
    af = (dst ^ src1 ^ src2) & CC_A;
    zf = (dst == 0) * CC_Z;
    sf = lshift(dst, 8 - DATA_BITS) & CC_S;
    of = lshift((src1 ^ src2) & (src1 ^ dst), 12 - DATA_BITS) & CC_O;
    return cf | pf | af | zf | sf | of;
}

static int glue(compute_c_sub, SUFFIX)(DATA_TYPE dst, DATA_TYPE src2)
{
    DATA_TYPE src1 = dst + src2;

    return src1 < src2;
}

static int glue(compute_all_sbb, SUFFIX)(DATA_TYPE dst, DATA_TYPE src2,
                                         DATA_TYPE src3)
{
    int cf, pf, af, zf, sf, of;
    DATA_TYPE src1 = dst + src2 + src3;

    cf = (src3 ? src1 <= src2 : src1 < src2);
    pf = parity_table[(uint8_t)dst];
    af = (dst ^ src1 ^ src2) & 0x10;
    zf = (dst == 0) << 6;
    sf = lshift(dst, 8 - DATA_BITS) & 0x80;
    of = lshift((src1 ^ src2) & (src1 ^ dst), 12 - DATA_BITS) & CC_O;
    return cf | pf | af | zf | sf | of;
}

static int glue(compute_c_sbb, SUFFIX)(DATA_TYPE dst, DATA_TYPE src2,
                                       DATA_TYPE src3)
{
    DATA_TYPE src1 = dst + src2 + src3;

    return (src3 ? src1 <= src2 : src1 < src2);
}

static int glue(compute_all_logic, SUFFIX)(DATA_TYPE dst, DATA_TYPE src1)
{
    int cf, pf, af, zf, sf, of;

    cf = 0;
    pf = parity_table[(uint8_t)dst];
    af = 0;
    zf = (dst == 0) * CC_Z;
    sf = lshift(dst, 8 - DATA_BITS) & CC_S;
    of = 0;
    return cf | pf | af | zf | sf | of;
}

static int glue(compute_all_inc, SUFFIX)(DATA_TYPE dst, DATA_TYPE src1)
{
    int cf, pf, af, zf, sf, of;
    DATA_TYPE src2;

    cf = src1;
    src1 = dst - 1;
    src2 = 1;
    pf = parity_table[(uint8_t)dst];
    af = (dst ^ src1 ^ src2) & CC_A;
    zf = (dst == 0) * CC_Z;
    sf = lshift(dst, 8 - DATA_BITS) & CC_S;
    of = (dst == SIGN_MASK) * CC_O;
    return cf | pf | af | zf | sf | of;
}

static int glue(compute_all_dec, SUFFIX)(DATA_TYPE dst, DATA_TYPE src1)
{
    int cf, pf, af, zf, sf, of;
    DATA_TYPE src2;

    cf = src1;
    src1 = dst + 1;
    src2 = 1;
    pf = parity_table[(uint8_t)dst];
    af = (dst ^ src1 ^ src2) & CC_A;
    zf = (dst == 0) * CC_Z;
    sf = lshift(dst, 8 - DATA_BITS) & CC_S;
    of = (dst == SIGN_MASK - 1) * CC_O;
    return cf | pf | af | zf | sf | of;
}

static int glue(compute_all_shl, SUFFIX)(DATA_TYPE dst, DATA_TYPE src1)
{
    int cf, pf, af, zf, sf, of;

    cf = (src1 >> (DATA_BITS - 1)) & CC_C;
    pf = parity_table[(uint8_t)dst];
    af = 0; /* undefined */
    zf = (dst == 0) * CC_Z;
    sf = lshift(dst, 8 - DATA_BITS) & CC_S;
    /* of is defined iff shift count == 1 */
    of = lshift(src1 ^ dst, 12 - DATA_BITS) & CC_O;
    return cf | pf | af | zf | sf | of;
}

static int glue(compute_c_shl, SUFFIX)(DATA_TYPE dst, DATA_TYPE src1)
{
    return (src1 >> (DATA_BITS - 1)) & CC_C;
}

static int glue(compute_all_sar, SUFFIX)(DATA_TYPE dst, DATA_TYPE src1)
{
    int cf, pf, af, zf, sf, of;

    cf = src1 & 1;
    pf = parity_table[(uint8_t)dst];
    af = 0; /* undefined */
    zf = (dst == 0) * CC_Z;
    sf = lshift(dst, 8 - DATA_BITS) & CC_S;
    /* of is defined iff shift count == 1 */
    of = lshift(src1 ^ dst, 12 - DATA_BITS) & CC_O;
    return cf | pf | af | zf | sf | of;
}

/* NOTE: we compute the flags like the P4. On olders CPUs, only OF and
   CF are modified and it is slower to do that.  Note as well that we
   don't truncate SRC1 for computing carry to DATA_TYPE.  */
static int glue(compute_all_mul, SUFFIX)(DATA_TYPE dst, target_long src1)
{
    int cf, pf, af, zf, sf, of;

    cf = (src1 != 0);
    pf = parity_table[(uint8_t)dst];
    af = 0; /* undefined */
    zf = (dst == 0) * CC_Z;
    sf = lshift(dst, 8 - DATA_BITS) & CC_S;
    of = cf * CC_O;
    return cf | pf | af | zf | sf | of;
}

static int glue(compute_all_bmilg, SUFFIX)(DATA_TYPE dst, DATA_TYPE src1)
{
    int cf, pf, af, zf, sf, of;

    cf = (src1 == 0);
    pf = 0; /* undefined */
    af = 0; /* undefined */
    zf = (dst == 0) * CC_Z;
    sf = lshift(dst, 8 - DATA_BITS) & CC_S;
    of = 0;
    return cf | pf | af | zf | sf | of;
}

static int glue(compute_c_bmilg, SUFFIX)(DATA_TYPE dst, DATA_TYPE src1)
{
    return src1 == 0;
}

#undef DATA_BITS
#undef SIGN_MASK
#undef DATA_TYPE
#undef DATA_MASK
#undef SUFFIX

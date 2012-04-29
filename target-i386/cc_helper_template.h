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
#define SIGN_MASK (((target_ulong)1) << (DATA_BITS - 1))

#if DATA_BITS == 8
#define SUFFIX b
#define DATA_TYPE uint8_t
#define DATA_MASK 0xff
#elif DATA_BITS == 16
#define SUFFIX w
#define DATA_TYPE uint16_t
#define DATA_MASK 0xffff
#elif DATA_BITS == 32
#define SUFFIX l
#define DATA_TYPE uint32_t
#define DATA_MASK 0xffffffff
#elif DATA_BITS == 64
#define SUFFIX q
#define DATA_TYPE uint64_t
#define DATA_MASK 0xffffffffffffffffULL
#else
#error unhandled operand size
#endif

/* dynamic flags computation */

static int glue(compute_all_add, SUFFIX)(CPUX86State *env)
{
    int cf, pf, af, zf, sf, of;
    target_long src1, src2;

    src1 = CC_SRC;
    src2 = CC_DST - CC_SRC;
    cf = (DATA_TYPE)CC_DST < (DATA_TYPE)src1;
    pf = parity_table[(uint8_t)CC_DST];
    af = (CC_DST ^ src1 ^ src2) & 0x10;
    zf = ((DATA_TYPE)CC_DST == 0) << 6;
    sf = lshift(CC_DST, 8 - DATA_BITS) & 0x80;
    of = lshift((src1 ^ src2 ^ -1) & (src1 ^ CC_DST), 12 - DATA_BITS) & CC_O;
    return cf | pf | af | zf | sf | of;
}

static int glue(compute_c_add, SUFFIX)(CPUX86State *env)
{
    int cf;
    target_long src1;

    src1 = CC_SRC;
    cf = (DATA_TYPE)CC_DST < (DATA_TYPE)src1;
    return cf;
}

static int glue(compute_all_adc, SUFFIX)(CPUX86State *env)
{
    int cf, pf, af, zf, sf, of;
    target_long src1, src2;

    src1 = CC_SRC;
    src2 = CC_DST - CC_SRC - 1;
    cf = (DATA_TYPE)CC_DST <= (DATA_TYPE)src1;
    pf = parity_table[(uint8_t)CC_DST];
    af = (CC_DST ^ src1 ^ src2) & 0x10;
    zf = ((DATA_TYPE)CC_DST == 0) << 6;
    sf = lshift(CC_DST, 8 - DATA_BITS) & 0x80;
    of = lshift((src1 ^ src2 ^ -1) & (src1 ^ CC_DST), 12 - DATA_BITS) & CC_O;
    return cf | pf | af | zf | sf | of;
}

static int glue(compute_c_adc, SUFFIX)(CPUX86State *env)
{
    int cf;
    target_long src1;

    src1 = CC_SRC;
    cf = (DATA_TYPE)CC_DST <= (DATA_TYPE)src1;
    return cf;
}

static int glue(compute_all_sub, SUFFIX)(CPUX86State *env)
{
    int cf, pf, af, zf, sf, of;
    target_long src1, src2;

    src1 = CC_DST + CC_SRC;
    src2 = CC_SRC;
    cf = (DATA_TYPE)src1 < (DATA_TYPE)src2;
    pf = parity_table[(uint8_t)CC_DST];
    af = (CC_DST ^ src1 ^ src2) & 0x10;
    zf = ((DATA_TYPE)CC_DST == 0) << 6;
    sf = lshift(CC_DST, 8 - DATA_BITS) & 0x80;
    of = lshift((src1 ^ src2) & (src1 ^ CC_DST), 12 - DATA_BITS) & CC_O;
    return cf | pf | af | zf | sf | of;
}

static int glue(compute_c_sub, SUFFIX)(CPUX86State *env)
{
    int cf;
    target_long src1, src2;

    src1 = CC_DST + CC_SRC;
    src2 = CC_SRC;
    cf = (DATA_TYPE)src1 < (DATA_TYPE)src2;
    return cf;
}

static int glue(compute_all_sbb, SUFFIX)(CPUX86State *env)
{
    int cf, pf, af, zf, sf, of;
    target_long src1, src2;

    src1 = CC_DST + CC_SRC + 1;
    src2 = CC_SRC;
    cf = (DATA_TYPE)src1 <= (DATA_TYPE)src2;
    pf = parity_table[(uint8_t)CC_DST];
    af = (CC_DST ^ src1 ^ src2) & 0x10;
    zf = ((DATA_TYPE)CC_DST == 0) << 6;
    sf = lshift(CC_DST, 8 - DATA_BITS) & 0x80;
    of = lshift((src1 ^ src2) & (src1 ^ CC_DST), 12 - DATA_BITS) & CC_O;
    return cf | pf | af | zf | sf | of;
}

static int glue(compute_c_sbb, SUFFIX)(CPUX86State *env)
{
    int cf;
    target_long src1, src2;

    src1 = CC_DST + CC_SRC + 1;
    src2 = CC_SRC;
    cf = (DATA_TYPE)src1 <= (DATA_TYPE)src2;
    return cf;
}

static int glue(compute_all_logic, SUFFIX)(CPUX86State *env)
{
    int cf, pf, af, zf, sf, of;

    cf = 0;
    pf = parity_table[(uint8_t)CC_DST];
    af = 0;
    zf = ((DATA_TYPE)CC_DST == 0) << 6;
    sf = lshift(CC_DST, 8 - DATA_BITS) & 0x80;
    of = 0;
    return cf | pf | af | zf | sf | of;
}

static int glue(compute_c_logic, SUFFIX)(void)
{
    return 0;
}

static int glue(compute_all_inc, SUFFIX)(CPUX86State *env)
{
    int cf, pf, af, zf, sf, of;
    target_long src1, src2;

    src1 = CC_DST - 1;
    src2 = 1;
    cf = CC_SRC;
    pf = parity_table[(uint8_t)CC_DST];
    af = (CC_DST ^ src1 ^ src2) & 0x10;
    zf = ((DATA_TYPE)CC_DST == 0) << 6;
    sf = lshift(CC_DST, 8 - DATA_BITS) & 0x80;
    of = ((CC_DST & DATA_MASK) == SIGN_MASK) << 11;
    return cf | pf | af | zf | sf | of;
}

#if DATA_BITS == 32
static int glue(compute_c_inc, SUFFIX)(CPUX86State *env)
{
    return CC_SRC;
}
#endif

static int glue(compute_all_dec, SUFFIX)(CPUX86State *env)
{
    int cf, pf, af, zf, sf, of;
    target_long src1, src2;

    src1 = CC_DST + 1;
    src2 = 1;
    cf = CC_SRC;
    pf = parity_table[(uint8_t)CC_DST];
    af = (CC_DST ^ src1 ^ src2) & 0x10;
    zf = ((DATA_TYPE)CC_DST == 0) << 6;
    sf = lshift(CC_DST, 8 - DATA_BITS) & 0x80;
    of = ((CC_DST & DATA_MASK) == ((target_ulong)SIGN_MASK - 1)) << 11;
    return cf | pf | af | zf | sf | of;
}

static int glue(compute_all_shl, SUFFIX)(CPUX86State *env)
{
    int cf, pf, af, zf, sf, of;

    cf = (CC_SRC >> (DATA_BITS - 1)) & CC_C;
    pf = parity_table[(uint8_t)CC_DST];
    af = 0; /* undefined */
    zf = ((DATA_TYPE)CC_DST == 0) << 6;
    sf = lshift(CC_DST, 8 - DATA_BITS) & 0x80;
    /* of is defined if shift count == 1 */
    of = lshift(CC_SRC ^ CC_DST, 12 - DATA_BITS) & CC_O;
    return cf | pf | af | zf | sf | of;
}

static int glue(compute_c_shl, SUFFIX)(CPUX86State *env)
{
    return (CC_SRC >> (DATA_BITS - 1)) & CC_C;
}

#if DATA_BITS == 32
static int glue(compute_c_sar, SUFFIX)(CPUX86State *env)
{
    return CC_SRC & 1;
}
#endif

static int glue(compute_all_sar, SUFFIX)(CPUX86State *env)
{
    int cf, pf, af, zf, sf, of;

    cf = CC_SRC & 1;
    pf = parity_table[(uint8_t)CC_DST];
    af = 0; /* undefined */
    zf = ((DATA_TYPE)CC_DST == 0) << 6;
    sf = lshift(CC_DST, 8 - DATA_BITS) & 0x80;
    /* of is defined if shift count == 1 */
    of = lshift(CC_SRC ^ CC_DST, 12 - DATA_BITS) & CC_O;
    return cf | pf | af | zf | sf | of;
}

#if DATA_BITS == 32
static int glue(compute_c_mul, SUFFIX)(CPUX86State *env)
{
    int cf;

    cf = (CC_SRC != 0);
    return cf;
}
#endif

/* NOTE: we compute the flags like the P4. On olders CPUs, only OF and
   CF are modified and it is slower to do that. */
static int glue(compute_all_mul, SUFFIX)(CPUX86State *env)
{
    int cf, pf, af, zf, sf, of;

    cf = (CC_SRC != 0);
    pf = parity_table[(uint8_t)CC_DST];
    af = 0; /* undefined */
    zf = ((DATA_TYPE)CC_DST == 0) << 6;
    sf = lshift(CC_DST, 8 - DATA_BITS) & 0x80;
    of = cf << 11;
    return cf | pf | af | zf | sf | of;
}

#undef DATA_BITS
#undef SIGN_MASK
#undef DATA_TYPE
#undef DATA_MASK
#undef SUFFIX

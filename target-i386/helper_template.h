/*
 *  i386 helpers
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
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#define DATA_BITS (1 << (3 + SHIFT))
#define SHIFT_MASK (DATA_BITS - 1)
#define SIGN_MASK (((target_ulong)1) << (DATA_BITS - 1))
#if DATA_BITS <= 32
#define SHIFT1_MASK 0x1f
#else
#define SHIFT1_MASK 0x3f
#endif

#if DATA_BITS == 8
#define SUFFIX b
#define DATA_TYPE uint8_t
#define DATA_STYPE int8_t
#define DATA_MASK 0xff
#elif DATA_BITS == 16
#define SUFFIX w
#define DATA_TYPE uint16_t
#define DATA_STYPE int16_t
#define DATA_MASK 0xffff
#elif DATA_BITS == 32
#define SUFFIX l
#define DATA_TYPE uint32_t
#define DATA_STYPE int32_t
#define DATA_MASK 0xffffffff
#elif DATA_BITS == 64
#define SUFFIX q
#define DATA_TYPE uint64_t
#define DATA_STYPE int64_t
#define DATA_MASK 0xffffffffffffffffULL
#else
#error unhandled operand size
#endif

/* dynamic flags computation */

static int glue(compute_all_add, SUFFIX)(void)
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

static int glue(compute_c_add, SUFFIX)(void)
{
    int cf;
    target_long src1;
    src1 = CC_SRC;
    cf = (DATA_TYPE)CC_DST < (DATA_TYPE)src1;
    return cf;
}

static int glue(compute_all_adc, SUFFIX)(void)
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

static int glue(compute_c_adc, SUFFIX)(void)
{
    int cf;
    target_long src1;
    src1 = CC_SRC;
    cf = (DATA_TYPE)CC_DST <= (DATA_TYPE)src1;
    return cf;
}

static int glue(compute_all_sub, SUFFIX)(void)
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

static int glue(compute_c_sub, SUFFIX)(void)
{
    int cf;
    target_long src1, src2;
    src1 = CC_DST + CC_SRC;
    src2 = CC_SRC;
    cf = (DATA_TYPE)src1 < (DATA_TYPE)src2;
    return cf;
}

static int glue(compute_all_sbb, SUFFIX)(void)
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

static int glue(compute_c_sbb, SUFFIX)(void)
{
    int cf;
    target_long src1, src2;
    src1 = CC_DST + CC_SRC + 1;
    src2 = CC_SRC;
    cf = (DATA_TYPE)src1 <= (DATA_TYPE)src2;
    return cf;
}

static int glue(compute_all_logic, SUFFIX)(void)
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

static int glue(compute_all_inc, SUFFIX)(void)
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
static int glue(compute_c_inc, SUFFIX)(void)
{
    return CC_SRC;
}
#endif

static int glue(compute_all_dec, SUFFIX)(void)
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

static int glue(compute_all_shl, SUFFIX)(void)
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

static int glue(compute_c_shl, SUFFIX)(void)
{
    return (CC_SRC >> (DATA_BITS - 1)) & CC_C;
}

#if DATA_BITS == 32
static int glue(compute_c_sar, SUFFIX)(void)
{
    return CC_SRC & 1;
}
#endif

static int glue(compute_all_sar, SUFFIX)(void)
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
static int glue(compute_c_mul, SUFFIX)(void)
{
    int cf;
    cf = (CC_SRC != 0);
    return cf;
}
#endif

/* NOTE: we compute the flags like the P4. On olders CPUs, only OF and
   CF are modified and it is slower to do that. */
static int glue(compute_all_mul, SUFFIX)(void)
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

/* shifts */

target_ulong glue(helper_rcl, SUFFIX)(target_ulong t0, target_ulong t1)
{
    int count, eflags;
    target_ulong src;
    target_long res;

    count = t1 & SHIFT1_MASK;
#if DATA_BITS == 16
    count = rclw_table[count];
#elif DATA_BITS == 8
    count = rclb_table[count];
#endif
    if (count) {
        eflags = helper_cc_compute_all(CC_OP);
        t0 &= DATA_MASK;
        src = t0;
        res = (t0 << count) | ((target_ulong)(eflags & CC_C) << (count - 1));
        if (count > 1)
            res |= t0 >> (DATA_BITS + 1 - count);
        t0 = res;
        env->cc_tmp = (eflags & ~(CC_C | CC_O)) |
            (lshift(src ^ t0, 11 - (DATA_BITS - 1)) & CC_O) |
            ((src >> (DATA_BITS - count)) & CC_C);
    } else {
        env->cc_tmp = -1;
    }
    return t0;
}

target_ulong glue(helper_rcr, SUFFIX)(target_ulong t0, target_ulong t1)
{
    int count, eflags;
    target_ulong src;
    target_long res;

    count = t1 & SHIFT1_MASK;
#if DATA_BITS == 16
    count = rclw_table[count];
#elif DATA_BITS == 8
    count = rclb_table[count];
#endif
    if (count) {
        eflags = helper_cc_compute_all(CC_OP);
        t0 &= DATA_MASK;
        src = t0;
        res = (t0 >> count) | ((target_ulong)(eflags & CC_C) << (DATA_BITS - count));
        if (count > 1)
            res |= t0 << (DATA_BITS + 1 - count);
        t0 = res;
        env->cc_tmp = (eflags & ~(CC_C | CC_O)) |
            (lshift(src ^ t0, 11 - (DATA_BITS - 1)) & CC_O) |
            ((src >> (count - 1)) & CC_C);
    } else {
        env->cc_tmp = -1;
    }
    return t0;
}

#undef DATA_BITS
#undef SHIFT_MASK
#undef SHIFT1_MASK
#undef SIGN_MASK
#undef DATA_TYPE
#undef DATA_STYPE
#undef DATA_MASK
#undef SUFFIX

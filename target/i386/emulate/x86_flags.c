/////////////////////////////////////////////////////////////////////////
//
//  Copyright (C) 2001-2012  The Bochs Project
//  Copyright (C) 2017 Google Inc.
//
//  This library is free software; you can redistribute it and/or
//  modify it under the terms of the GNU Lesser General Public
//  License as published by the Free Software Foundation; either
//  version 2.1 of the License, or (at your option) any later version.
//
//  This library is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
//  Lesser General Public License for more details.
//
//  You should have received a copy of the GNU Lesser General Public
//  License along with this library; if not, see
//  <https://www.gnu.org/licenses/>.
/////////////////////////////////////////////////////////////////////////
/*
 * flags functions
 */

#include "qemu/osdep.h"

#include "panic.h"
#include "cpu.h"
#include "x86_flags.h"
#include "x86.h"


/*
 * The algorithms here are similar to those in Bochs.  After an ALU
 * operation, CC_DST can be used to compute ZF, SF and PF, whereas
 * CC_SRC is used to compute AF, CF and OF.  In reality, SF and PF are the
 * XOR of the value computed from CC_DST and the value found in bits 7 and 2
 * of CC_SRC; this way the same logic can be used to compute the flags
 * both before and after an ALU operation.
 *
 * Compared to the TCG CC_OP codes, this avoids conditionals when converting
 * to and from the RFLAGS representation.
 */

#define LF_SIGN_BIT    (TARGET_LONG_BITS - 1)

#define LF_BIT_PD      (2)          /* lazy Parity Delta, same bit as PF */
#define LF_BIT_AF      (3)          /* lazy Adjust flag */
#define LF_BIT_SD      (7)          /* lazy Sign Flag Delta, same bit as SF */
#define LF_BIT_CF      (TARGET_LONG_BITS - 1) /* lazy Carry Flag */
#define LF_BIT_PO      (TARGET_LONG_BITS - 2) /* lazy Partial Overflow = CF ^ OF */

#define LF_MASK_PD     ((target_ulong)0x01 << LF_BIT_PD)
#define LF_MASK_AF     ((target_ulong)0x01 << LF_BIT_AF)
#define LF_MASK_SD     ((target_ulong)0x01 << LF_BIT_SD)
#define LF_MASK_CF     ((target_ulong)0x01 << LF_BIT_CF)
#define LF_MASK_PO     ((target_ulong)0x01 << LF_BIT_PO)

/* ******************* */
/* OSZAPC */
/* ******************* */

/* use carries to fill in AF, PO and CF, while ensuring PD and SD are clear.
 * for full-word operations just clear PD and SD; for smaller operand
 * sizes only keep AF in the low byte and shift the carries left to
 * place PO and CF in the top two bits.
 */
#define SET_FLAGS_OSZAPC_SIZE(size, lf_carries, lf_result) { \
    env->cc_dst = (target_ulong)(int##size##_t)(lf_result); \
    target_ulong temp = (lf_carries); \
    if ((size) == TARGET_LONG_BITS) { \
        temp = temp & ~(LF_MASK_PD | LF_MASK_SD); \
    } else { \
        temp = (temp & LF_MASK_AF) | (temp << (TARGET_LONG_BITS - (size))); \
    } \
    env->cc_src = temp; \
}

/* carries, result */
#define SET_FLAGS_OSZAPC_8(carries, result) \
    SET_FLAGS_OSZAPC_SIZE(8, carries, result)
#define SET_FLAGS_OSZAPC_16(carries, result) \
    SET_FLAGS_OSZAPC_SIZE(16, carries, result)
#define SET_FLAGS_OSZAPC_32(carries, result) \
    SET_FLAGS_OSZAPC_SIZE(32, carries, result)

/* ******************* */
/* OSZAP */
/* ******************* */
/* same as setting OSZAPC, but preserve CF and flip PO if the old value of CF
 * did not match the high bit of lf_carries. */
#define SET_FLAGS_OSZAP_SIZE(size, lf_carries, lf_result) { \
    env->cc_dst = (target_ulong)(int##size##_t)(lf_result); \
    target_ulong temp = (lf_carries); \
    if ((size) == TARGET_LONG_BITS) { \
        temp = (temp & ~(LF_MASK_PD | LF_MASK_SD)); \
    } else { \
        temp = (temp & LF_MASK_AF) | (temp << (TARGET_LONG_BITS - (size))); \
    } \
    target_ulong cf_changed = ((target_long)(env->cc_src ^ temp)) < 0; \
    env->cc_src = temp ^ (cf_changed * (LF_MASK_PO | LF_MASK_CF)); \
}

/* carries, result */
#define SET_FLAGS_OSZAP_8(carries, result) \
    SET_FLAGS_OSZAP_SIZE(8, carries, result)
#define SET_FLAGS_OSZAP_16(carries, result) \
    SET_FLAGS_OSZAP_SIZE(16, carries, result)
#define SET_FLAGS_OSZAP_32(carries, result) \
    SET_FLAGS_OSZAP_SIZE(32, carries, result)

void SET_FLAGS_OxxxxC(CPUX86State *env, bool new_of, bool new_cf)
{
    env->cc_src &= ~(LF_MASK_PO | LF_MASK_CF);
    env->cc_src |= (-(target_ulong)new_cf << LF_BIT_PO);
    env->cc_src ^= ((target_ulong)new_of << LF_BIT_PO);
}

void SET_FLAGS_OSZAPC_SUB32(CPUX86State *env, uint32_t v1, uint32_t v2,
                            uint32_t diff)
{
    SET_FLAGS_OSZAPC_32(SUB_COUT_VEC(v1, v2, diff), diff);
}

void SET_FLAGS_OSZAPC_SUB16(CPUX86State *env, uint16_t v1, uint16_t v2,
                            uint16_t diff)
{
    SET_FLAGS_OSZAPC_16(SUB_COUT_VEC(v1, v2, diff), diff);
}

void SET_FLAGS_OSZAPC_SUB8(CPUX86State *env, uint8_t v1, uint8_t v2,
                            uint8_t diff)
{
    SET_FLAGS_OSZAPC_8(SUB_COUT_VEC(v1, v2, diff), diff);
}

void SET_FLAGS_OSZAPC_ADD32(CPUX86State *env, uint32_t v1, uint32_t v2,
                            uint32_t diff)
{
    SET_FLAGS_OSZAPC_32(ADD_COUT_VEC(v1, v2, diff), diff);
}

void SET_FLAGS_OSZAPC_ADD16(CPUX86State *env, uint16_t v1, uint16_t v2,
                            uint16_t diff)
{
    SET_FLAGS_OSZAPC_16(ADD_COUT_VEC(v1, v2, diff), diff);
}

void SET_FLAGS_OSZAPC_ADD8(CPUX86State *env, uint8_t v1, uint8_t v2,
                            uint8_t diff)
{
    SET_FLAGS_OSZAPC_8(ADD_COUT_VEC(v1, v2, diff), diff);
}

void SET_FLAGS_OSZAP_SUB32(CPUX86State *env, uint32_t v1, uint32_t v2,
                            uint32_t diff)
{
    SET_FLAGS_OSZAP_32(SUB_COUT_VEC(v1, v2, diff), diff);
}

void SET_FLAGS_OSZAP_SUB16(CPUX86State *env, uint16_t v1, uint16_t v2,
                            uint16_t diff)
{
    SET_FLAGS_OSZAP_16(SUB_COUT_VEC(v1, v2, diff), diff);
}

void SET_FLAGS_OSZAP_SUB8(CPUX86State *env, uint8_t v1, uint8_t v2,
                            uint8_t diff)
{
    SET_FLAGS_OSZAP_8(SUB_COUT_VEC(v1, v2, diff), diff);
}

void SET_FLAGS_OSZAP_ADD32(CPUX86State *env, uint32_t v1, uint32_t v2,
                            uint32_t diff)
{
    SET_FLAGS_OSZAP_32(ADD_COUT_VEC(v1, v2, diff), diff);
}

void SET_FLAGS_OSZAP_ADD16(CPUX86State *env, uint16_t v1, uint16_t v2,
                            uint16_t diff)
{
    SET_FLAGS_OSZAP_16(ADD_COUT_VEC(v1, v2, diff), diff);
}

void SET_FLAGS_OSZAP_ADD8(CPUX86State *env, uint8_t v1, uint8_t v2,
                            uint8_t diff)
{
    SET_FLAGS_OSZAP_8(ADD_COUT_VEC(v1, v2, diff), diff);
}


void SET_FLAGS_OSZAPC_LOGIC32(CPUX86State *env, uint32_t v1, uint32_t v2,
                              uint32_t diff)
{
    SET_FLAGS_OSZAPC_32(0, diff);
}

void SET_FLAGS_OSZAPC_LOGIC16(CPUX86State *env, uint16_t v1, uint16_t v2,
                              uint16_t diff)
{
    SET_FLAGS_OSZAPC_16(0, diff);
}

void SET_FLAGS_OSZAPC_LOGIC8(CPUX86State *env, uint8_t v1, uint8_t v2,
                             uint8_t diff)
{
    SET_FLAGS_OSZAPC_8(0, diff);
}

static inline uint32_t get_PF(CPUX86State *env)
{
    return ((parity8(env->cc_dst) - 1) ^ env->cc_src) & CC_P;
}

static inline uint32_t get_OF(CPUX86State *env)
{
    return ((env->cc_src >> (LF_BIT_CF - 11)) + CC_O / 2) & CC_O;
}

bool get_CF(CPUX86State *env)
{
    return ((target_long)env->cc_src) < 0;
}

void set_CF(CPUX86State *env, bool val)
{
    /* If CF changes, flip PO and CF */
    target_ulong temp = -(target_ulong)val;
    target_ulong cf_changed = ((target_long)(env->cc_src ^ temp)) < 0;
    env->cc_src ^= cf_changed * (LF_MASK_PO | LF_MASK_CF);
}

static inline uint32_t get_ZF(CPUX86State *env)
{
    return env->cc_dst ? 0 : CC_Z;
}

static inline uint32_t get_SF(CPUX86State *env)
{
    return ((env->cc_dst >> (LF_SIGN_BIT - LF_BIT_SD)) ^
            env->cc_src) & CC_S;
}

void lflags_to_rflags(CPUX86State *env)
{
    env->eflags &= ~(CC_C|CC_P|CC_A|CC_Z|CC_S|CC_O);
    /* rotate left by one to move carry-out bits into CF and AF */
    env->eflags |= (
        (env->cc_src << 1) |
        (env->cc_src >> (TARGET_LONG_BITS - 1))) & (CC_C | CC_A);
    env->eflags |= get_SF(env);
    env->eflags |= get_PF(env);
    env->eflags |= get_ZF(env);
    env->eflags |= get_OF(env);
}

void rflags_to_lflags(CPUX86State *env)
{
    target_ulong cf_af, cf_xor_of;

    /* Leave the low byte zero so that parity is always even...  */
    env->cc_dst = !(env->eflags & CC_Z) << 8;

    /* ... and therefore cc_src always uses opposite polarity.  */
    env->cc_src = CC_P;
    env->cc_src ^= env->eflags & (CC_S | CC_P);

    /* rotate right by one to move CF and AF into the carry-out positions */
    cf_af = env->eflags & (CC_C | CC_A);
    env->cc_src |= ((cf_af >> 1) | (cf_af << (TARGET_LONG_BITS - 1)));

    cf_xor_of = ((env->eflags & (CC_C | CC_O)) + (CC_O - CC_C)) & CC_O;
    env->cc_src |= -cf_xor_of & LF_MASK_PO;
}

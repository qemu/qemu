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
//  License along with this library; if not, write to the Free Software
//  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA B 02110-1301 USA
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
 * operation, RESULT can be used to compute ZF, SF and PF, whereas
 * AUXBITS is used to compute AF, CF and OF.  In reality, SF and PF are the
 * XOR of the value computed from RESULT and the value found in bits 7 and 2
 * of AUXBITS; this way the same logic can be used to compute the flags
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
    env->lflags.result = (target_ulong)(int##size##_t)(lf_result); \
    target_ulong temp = (lf_carries); \
    if ((size) == TARGET_LONG_BITS) { \
        temp = temp & ~(LF_MASK_PD | LF_MASK_SD); \
    } else { \
        temp = (temp & LF_MASK_AF) | (temp << (TARGET_LONG_BITS - (size))); \
    } \
    env->lflags.auxbits = temp; \
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
    env->lflags.result = (target_ulong)(int##size##_t)(lf_result); \
    target_ulong temp = (lf_carries); \
    if ((size) == TARGET_LONG_BITS) { \
        temp = (temp & ~(LF_MASK_PD | LF_MASK_SD)); \
    } else { \
        temp = (temp & LF_MASK_AF) | (temp << (TARGET_LONG_BITS - (size))); \
    } \
    target_ulong cf_changed = ((target_long)(env->lflags.auxbits ^ temp)) < 0; \
    env->lflags.auxbits = temp ^ (cf_changed * (LF_MASK_PO | LF_MASK_CF)); \
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
    env->lflags.auxbits &= ~(LF_MASK_PO | LF_MASK_CF);
    env->lflags.auxbits |= (-(target_ulong)new_cf << LF_BIT_PO);
    env->lflags.auxbits ^= ((target_ulong)new_of << LF_BIT_PO);
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
    uint8_t temp = env->lflags.result;
    return ((parity8(temp) - 1) ^ env->lflags.auxbits) & CC_P;
}

static inline uint32_t get_OF(CPUX86State *env)
{
    return ((env->lflags.auxbits >> (LF_BIT_CF - 11)) + CC_O / 2) & CC_O;
}

bool get_CF(CPUX86State *env)
{
    return ((target_long)env->lflags.auxbits) < 0;
}

void set_CF(CPUX86State *env, bool val)
{
    /* If CF changes, flip PO and CF */
    target_ulong temp = -(target_ulong)val;
    target_ulong cf_changed = ((target_long)(env->lflags.auxbits ^ temp)) < 0;
    env->lflags.auxbits ^= cf_changed * (LF_MASK_PO | LF_MASK_CF);
}

static inline uint32_t get_ZF(CPUX86State *env)
{
    return env->lflags.result ? 0 : CC_Z;
}

static inline uint32_t get_SF(CPUX86State *env)
{
    return ((env->lflags.result >> (LF_SIGN_BIT - LF_BIT_SD)) ^
            env->lflags.auxbits) & CC_S;
}

void lflags_to_rflags(CPUX86State *env)
{
    env->eflags &= ~(CC_C|CC_P|CC_A|CC_Z|CC_S|CC_O);
    /* rotate left by one to move carry-out bits into CF and AF */
    env->eflags |= (
        (env->lflags.auxbits << 1) |
        (env->lflags.auxbits >> (TARGET_LONG_BITS - 1))) & (CC_C | CC_A);
    env->eflags |= get_SF(env);
    env->eflags |= get_PF(env);
    env->eflags |= get_ZF(env);
    env->eflags |= get_OF(env);
}

void rflags_to_lflags(CPUX86State *env)
{
    target_ulong cf_xor_of;

    env->lflags.auxbits = CC_P;
    env->lflags.auxbits ^= env->eflags & (CC_S | CC_P);

    /* rotate right by one to move CF and AF into the carry-out positions */
    env->lflags.auxbits |= (
        (env->eflags >> 1) |
        (env->eflags << (TARGET_LONG_BITS - 1))) & (CC_C | CC_A);

    cf_xor_of = (env->eflags & (CC_C | CC_O)) + (CC_O - CC_C);
    env->lflags.auxbits |= -cf_xor_of & LF_MASK_PO;

    /* Leave the low byte zero so that parity is not affected.  */
    env->lflags.result = !(env->eflags & CC_Z) << 8;
}

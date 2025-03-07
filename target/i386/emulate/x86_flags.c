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


/* this is basically bocsh code */

#define LF_SIGN_BIT     31

#define LF_BIT_SD      (0)          /* lazy Sign Flag Delta            */
#define LF_BIT_AF      (3)          /* lazy Adjust flag                */
#define LF_BIT_PDB     (8)          /* lazy Parity Delta Byte (8 bits) */
#define LF_BIT_CF      (31)         /* lazy Carry Flag                 */
#define LF_BIT_PO      (30)         /* lazy Partial Overflow = CF ^ OF */

#define LF_MASK_SD     (0x01 << LF_BIT_SD)
#define LF_MASK_AF     (0x01 << LF_BIT_AF)
#define LF_MASK_PDB    (0xFF << LF_BIT_PDB)
#define LF_MASK_CF     (0x01 << LF_BIT_CF)
#define LF_MASK_PO     (0x01 << LF_BIT_PO)

/* ******************* */
/* OSZAPC */
/* ******************* */

/* size, carries, result */
#define SET_FLAGS_OSZAPC_SIZE(size, lf_carries, lf_result) { \
    target_ulong temp = ((lf_carries) & (LF_MASK_AF)) | \
    (((lf_carries) >> (size - 2)) << LF_BIT_PO); \
    env->lflags.result = (target_ulong)(int##size##_t)(lf_result); \
    if ((size) == 32) { \
        temp = ((lf_carries) & ~(LF_MASK_PDB | LF_MASK_SD)); \
    } else if ((size) == 16) { \
        temp = ((lf_carries) & (LF_MASK_AF)) | ((lf_carries) << 16); \
    } else if ((size) == 8)  { \
        temp = ((lf_carries) & (LF_MASK_AF)) | ((lf_carries) << 24); \
    } else { \
        VM_PANIC("unimplemented");  \
    } \
    env->lflags.auxbits = (target_ulong)(uint32_t)temp; \
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
/* size, carries, result */
#define SET_FLAGS_OSZAP_SIZE(size, lf_carries, lf_result) { \
    target_ulong temp = ((lf_carries) & (LF_MASK_AF)) | \
    (((lf_carries) >> (size - 2)) << LF_BIT_PO); \
    if ((size) == 32) { \
        temp = ((lf_carries) & ~(LF_MASK_PDB | LF_MASK_SD)); \
    } else if ((size) == 16) { \
        temp = ((lf_carries) & (LF_MASK_AF)) | ((lf_carries) << 16); \
    } else if ((size) == 8) { \
        temp = ((lf_carries) & (LF_MASK_AF)) | ((lf_carries) << 24); \
    } else { \
        VM_PANIC("unimplemented");      \
    } \
    env->lflags.result = (target_ulong)(int##size##_t)(lf_result); \
    target_ulong delta_c = (env->lflags.auxbits ^ temp) & LF_MASK_CF; \
    delta_c ^= (delta_c >> 1); \
    env->lflags.auxbits = (target_ulong)(uint32_t)(temp ^ delta_c); \
}

/* carries, result */
#define SET_FLAGS_OSZAP_8(carries, result) \
    SET_FLAGS_OSZAP_SIZE(8, carries, result)
#define SET_FLAGS_OSZAP_16(carries, result) \
    SET_FLAGS_OSZAP_SIZE(16, carries, result)
#define SET_FLAGS_OSZAP_32(carries, result) \
    SET_FLAGS_OSZAP_SIZE(32, carries, result)

void SET_FLAGS_OxxxxC(CPUX86State *env, uint32_t new_of, uint32_t new_cf)
{
    uint32_t temp_po = new_of ^ new_cf;
    env->lflags.auxbits &= ~(LF_MASK_PO | LF_MASK_CF);
    env->lflags.auxbits |= (temp_po << LF_BIT_PO) | (new_cf << LF_BIT_CF);
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

bool get_PF(CPUX86State *env)
{
    uint32_t temp = (255 & env->lflags.result);
    temp = temp ^ (255 & (env->lflags.auxbits >> LF_BIT_PDB));
    temp = (temp ^ (temp >> 4)) & 0x0F;
    return (0x9669U >> temp) & 1;
}

void set_PF(CPUX86State *env, bool val)
{
    uint32_t temp = (255 & env->lflags.result) ^ (!val);
    env->lflags.auxbits &= ~(LF_MASK_PDB);
    env->lflags.auxbits |= (temp << LF_BIT_PDB);
}

bool get_OF(CPUX86State *env)
{
    return ((env->lflags.auxbits + (1U << LF_BIT_PO)) >> LF_BIT_CF) & 1;
}

bool get_CF(CPUX86State *env)
{
    return (env->lflags.auxbits >> LF_BIT_CF) & 1;
}

void set_OF(CPUX86State *env, bool val)
{
    bool old_cf = get_CF(env);
    SET_FLAGS_OxxxxC(env, val, old_cf);
}

void set_CF(CPUX86State *env, bool val)
{
    bool old_of = get_OF(env);
    SET_FLAGS_OxxxxC(env, old_of, val);
}

bool get_AF(CPUX86State *env)
{
    return (env->lflags.auxbits >> LF_BIT_AF) & 1;
}

void set_AF(CPUX86State *env, bool val)
{
    env->lflags.auxbits &= ~(LF_MASK_AF);
    env->lflags.auxbits |= val << LF_BIT_AF;
}

bool get_ZF(CPUX86State *env)
{
    return !env->lflags.result;
}

void set_ZF(CPUX86State *env, bool val)
{
    if (val) {
        env->lflags.auxbits ^=
         (((env->lflags.result >> LF_SIGN_BIT) & 1) << LF_BIT_SD);
        /* merge the parity bits into the Parity Delta Byte */
        uint32_t temp_pdb = (255 & env->lflags.result);
        env->lflags.auxbits ^= (temp_pdb << LF_BIT_PDB);
        /* now zero the .result value */
        env->lflags.result = 0;
    } else {
        env->lflags.result |= (1 << 8);
    }
}

bool get_SF(CPUX86State *env)
{
    return ((env->lflags.result >> LF_SIGN_BIT) ^
            (env->lflags.auxbits >> LF_BIT_SD)) & 1;
}

void set_SF(CPUX86State *env, bool val)
{
    bool temp_sf = get_SF(env);
    env->lflags.auxbits ^= (temp_sf ^ val) << LF_BIT_SD;
}

void lflags_to_rflags(CPUX86State *env)
{
    env->eflags &= ~(CC_C|CC_P|CC_A|CC_Z|CC_S|CC_O);
    env->eflags |= get_CF(env) ? CC_C : 0;
    env->eflags |= get_PF(env) ? CC_P : 0;
    env->eflags |= get_AF(env) ? CC_A : 0;
    env->eflags |= get_ZF(env) ? CC_Z : 0;
    env->eflags |= get_SF(env) ? CC_S : 0;
    env->eflags |= get_OF(env) ? CC_O : 0;
}

void rflags_to_lflags(CPUX86State *env)
{
    env->lflags.auxbits = env->lflags.result = 0;
    set_OF(env, env->eflags & CC_O);
    set_SF(env, env->eflags & CC_S);
    set_ZF(env, env->eflags & CC_Z);
    set_AF(env, env->eflags & CC_A);
    set_PF(env, env->eflags & CC_P);
    set_CF(env, env->eflags & CC_C);
}

/////////////////////////////////////////////////////////////////////////
//
//  Copyright (C) 2001-2012  The Bochs Project
//  Copyright (C) 2017 Google Inc.
//
//  This library is free software; you can redistribute it and/or
//  modify it under the terms of the GNU Lesser General Public
//  License as published by the Free Software Foundation; either
//  version 2 of the License, or (at your option) any later version.
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
 * x86 eflags functions
 */
#ifndef __X86_FLAGS_H__
#define __X86_FLAGS_H__

#include "x86_gen.h"
#include "cpu.h"

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

#define ADD_COUT_VEC(op1, op2, result) \
   (((op1) & (op2)) | (((op1) | (op2)) & (~(result))))

#define SUB_COUT_VEC(op1, op2, result) \
   (((~(op1)) & (op2)) | (((~(op1)) ^ (op2)) & (result)))

#define GET_ADD_OVERFLOW(op1, op2, result, mask) \
   ((((op1) ^ (result)) & ((op2) ^ (result))) & (mask))

/* ******************* */
/* OSZAPC */
/* ******************* */

/* size, carries, result */
#define SET_FLAGS_OSZAPC_SIZE(size, lf_carries, lf_result) { \
    addr_t temp = ((lf_carries) & (LF_MASK_AF)) | \
    (((lf_carries) >> (size - 2)) << LF_BIT_PO); \
    env->hvf_emul->lflags.result = (addr_t)(int##size##_t)(lf_result); \
    if ((size) == 32) { \
        temp = ((lf_carries) & ~(LF_MASK_PDB | LF_MASK_SD)); \
    } else if ((size) == 16) { \
        temp = ((lf_carries) & (LF_MASK_AF)) | ((lf_carries) << 16); \
    } else if ((size) == 8)  { \
        temp = ((lf_carries) & (LF_MASK_AF)) | ((lf_carries) << 24); \
    } else { \
        VM_PANIC("unimplemented");  \
    } \
    env->hvf_emul->lflags.auxbits = (addr_t)(uint32_t)temp; \
}

/* carries, result */
#define SET_FLAGS_OSZAPC_8(carries, result) \
    SET_FLAGS_OSZAPC_SIZE(8, carries, result)
#define SET_FLAGS_OSZAPC_16(carries, result) \
    SET_FLAGS_OSZAPC_SIZE(16, carries, result)
#define SET_FLAGS_OSZAPC_32(carries, result) \
    SET_FLAGS_OSZAPC_SIZE(32, carries, result)

/* result */
#define SET_FLAGS_OSZAPC_LOGIC_8(result_8) \
    SET_FLAGS_OSZAPC_8(0, (result_8))
#define SET_FLAGS_OSZAPC_LOGIC_16(result_16) \
    SET_FLAGS_OSZAPC_16(0, (result_16))
#define SET_FLAGS_OSZAPC_LOGIC_32(result_32) \
    SET_FLAGS_OSZAPC_32(0, (result_32))
#define SET_FLAGS_OSZAPC_LOGIC_SIZE(size, result) {             \
    if (32 == size) { \
        SET_FLAGS_OSZAPC_LOGIC_32(result); \
    } else if (16 == size) { \
        SET_FLAGS_OSZAPC_LOGIC_16(result); \
    } else if (8 == size) { \
        SET_FLAGS_OSZAPC_LOGIC_8(result); \
    } else { \
        VM_PANIC("unimplemented");                            \
    } \
}

/* op1, op2, result */
#define SET_FLAGS_OSZAPC_ADD_8(op1_8, op2_8, sum_8) \
    SET_FLAGS_OSZAPC_8(ADD_COUT_VEC((op1_8), (op2_8), (sum_8)), (sum_8))
#define SET_FLAGS_OSZAPC_ADD_16(op1_16, op2_16, sum_16) \
    SET_FLAGS_OSZAPC_16(ADD_COUT_VEC((op1_16), (op2_16), (sum_16)), (sum_16))
#define SET_FLAGS_OSZAPC_ADD_32(op1_32, op2_32, sum_32) \
    SET_FLAGS_OSZAPC_32(ADD_COUT_VEC((op1_32), (op2_32), (sum_32)), (sum_32))

/* op1, op2, result */
#define SET_FLAGS_OSZAPC_SUB_8(op1_8, op2_8, diff_8) \
    SET_FLAGS_OSZAPC_8(SUB_COUT_VEC((op1_8), (op2_8), (diff_8)), (diff_8))
#define SET_FLAGS_OSZAPC_SUB_16(op1_16, op2_16, diff_16) \
    SET_FLAGS_OSZAPC_16(SUB_COUT_VEC((op1_16), (op2_16), (diff_16)), (diff_16))
#define SET_FLAGS_OSZAPC_SUB_32(op1_32, op2_32, diff_32) \
    SET_FLAGS_OSZAPC_32(SUB_COUT_VEC((op1_32), (op2_32), (diff_32)), (diff_32))

/* ******************* */
/* OSZAP */
/* ******************* */
/* size, carries, result */
#define SET_FLAGS_OSZAP_SIZE(size, lf_carries, lf_result) { \
    addr_t temp = ((lf_carries) & (LF_MASK_AF)) | \
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
    env->hvf_emul->lflags.result = (addr_t)(int##size##_t)(lf_result); \
    addr_t delta_c = (env->hvf_emul->lflags.auxbits ^ temp) & LF_MASK_CF; \
    delta_c ^= (delta_c >> 1); \
    env->hvf_emul->lflags.auxbits = (addr_t)(uint32_t)(temp ^ delta_c); \
}

/* carries, result */
#define SET_FLAGS_OSZAP_8(carries, result) \
    SET_FLAGS_OSZAP_SIZE(8, carries, result)
#define SET_FLAGS_OSZAP_16(carries, result) \
    SET_FLAGS_OSZAP_SIZE(16, carries, result)
#define SET_FLAGS_OSZAP_32(carries, result) \
    SET_FLAGS_OSZAP_SIZE(32, carries, result)

/* op1, op2, result */
#define SET_FLAGS_OSZAP_ADD_8(op1_8, op2_8, sum_8) \
    SET_FLAGS_OSZAP_8(ADD_COUT_VEC((op1_8), (op2_8), (sum_8)), (sum_8))
#define SET_FLAGS_OSZAP_ADD_16(op1_16, op2_16, sum_16) \
    SET_FLAGS_OSZAP_16(ADD_COUT_VEC((op1_16), (op2_16), (sum_16)), (sum_16))
#define SET_FLAGS_OSZAP_ADD_32(op1_32, op2_32, sum_32) \
    SET_FLAGS_OSZAP_32(ADD_COUT_VEC((op1_32), (op2_32), (sum_32)), (sum_32))

/* op1, op2, result */
#define SET_FLAGS_OSZAP_SUB_8(op1_8, op2_8, diff_8) \
    SET_FLAGS_OSZAP_8(SUB_COUT_VEC((op1_8), (op2_8), (diff_8)), (diff_8))
#define SET_FLAGS_OSZAP_SUB_16(op1_16, op2_16, diff_16) \
    SET_FLAGS_OSZAP_16(SUB_COUT_VEC((op1_16), (op2_16), (diff_16)), (diff_16))
#define SET_FLAGS_OSZAP_SUB_32(op1_32, op2_32, diff_32) \
    SET_FLAGS_OSZAP_32(SUB_COUT_VEC((op1_32), (op2_32), (diff_32)), (diff_32))

/* ******************* */
/* OSZAxC */
/* ******************* */
/* size, carries, result */
#define SET_FLAGS_OSZAxC_LOGIC_SIZE(size, lf_result) { \
    bool saved_PF = getB_PF(); \
    SET_FLAGS_OSZAPC_SIZE(size, (int##size##_t)(0), lf_result); \
    set_PF(saved_PF); \
}

/* result */
#define SET_FLAGS_OSZAxC_LOGIC_32(result_32) \
    SET_FLAGS_OSZAxC_LOGIC_SIZE(32, (result_32))

void lflags_to_rflags(CPUX86State *env);
void rflags_to_lflags(CPUX86State *env);

bool get_PF(CPUX86State *env);
void set_PF(CPUX86State *env, bool val);
bool get_CF(CPUX86State *env);
void set_CF(CPUX86State *env, bool val);
bool get_AF(CPUX86State *env);
void set_AF(CPUX86State *env, bool val);
bool get_ZF(CPUX86State *env);
void set_ZF(CPUX86State *env, bool val);
bool get_SF(CPUX86State *env);
void set_SF(CPUX86State *env, bool val);
bool get_OF(CPUX86State *env);
void set_OF(CPUX86State *env, bool val);
void set_OSZAPC(CPUX86State *env, uint32_t flags32);

void SET_FLAGS_OxxxxC(CPUX86State *env, uint32_t new_of, uint32_t new_cf);

void SET_FLAGS_OSZAPC_SUB32(CPUX86State *env, uint32_t v1, uint32_t v2,
                            uint32_t diff);
void SET_FLAGS_OSZAPC_SUB16(CPUX86State *env, uint16_t v1, uint16_t v2,
                            uint16_t diff);
void SET_FLAGS_OSZAPC_SUB8(CPUX86State *env, uint8_t v1, uint8_t v2,
                           uint8_t diff);

void SET_FLAGS_OSZAPC_ADD32(CPUX86State *env, uint32_t v1, uint32_t v2,
                            uint32_t diff);
void SET_FLAGS_OSZAPC_ADD16(CPUX86State *env, uint16_t v1, uint16_t v2,
                            uint16_t diff);
void SET_FLAGS_OSZAPC_ADD8(CPUX86State *env, uint8_t v1, uint8_t v2,
                           uint8_t diff);

void SET_FLAGS_OSZAP_SUB32(CPUX86State *env, uint32_t v1, uint32_t v2,
                           uint32_t diff);
void SET_FLAGS_OSZAP_SUB16(CPUX86State *env, uint16_t v1, uint16_t v2,
                           uint16_t diff);
void SET_FLAGS_OSZAP_SUB8(CPUX86State *env, uint8_t v1, uint8_t v2,
                          uint8_t diff);

void SET_FLAGS_OSZAP_ADD32(CPUX86State *env, uint32_t v1, uint32_t v2,
                           uint32_t diff);
void SET_FLAGS_OSZAP_ADD16(CPUX86State *env, uint16_t v1, uint16_t v2,
                           uint16_t diff);
void SET_FLAGS_OSZAP_ADD8(CPUX86State *env, uint8_t v1, uint8_t v2,
                          uint8_t diff);

void SET_FLAGS_OSZAPC_LOGIC32(CPUX86State *env, uint32_t diff);
void SET_FLAGS_OSZAPC_LOGIC16(CPUX86State *env, uint16_t diff);
void SET_FLAGS_OSZAPC_LOGIC8(CPUX86State *env, uint8_t diff);

void SET_FLAGS_SHR32(CPUX86State *env, uint32_t v, int count, uint32_t res);
void SET_FLAGS_SHR16(CPUX86State *env, uint16_t v, int count, uint16_t res);
void SET_FLAGS_SHR8(CPUX86State *env, uint8_t v, int count, uint8_t res);

void SET_FLAGS_SAR32(CPUX86State *env, int32_t v, int count, uint32_t res);
void SET_FLAGS_SAR16(CPUX86State *env, int16_t v, int count, uint16_t res);
void SET_FLAGS_SAR8(CPUX86State *env, int8_t v, int count, uint8_t res);

void SET_FLAGS_SHL32(CPUX86State *env, uint32_t v, int count, uint32_t res);
void SET_FLAGS_SHL16(CPUX86State *env, uint16_t v, int count, uint16_t res);
void SET_FLAGS_SHL8(CPUX86State *env, uint8_t v, int count, uint8_t res);

bool _get_OF(CPUX86State *env);
bool _get_CF(CPUX86State *env);
#endif /* __X86_FLAGS_H__ */

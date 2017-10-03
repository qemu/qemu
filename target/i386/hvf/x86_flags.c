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
 * flags functions
 */

#include "qemu/osdep.h"

#include "qemu-common.h"
#include "cpu.h"
#include "x86_flags.h"
#include "x86.h"

void SET_FLAGS_OxxxxC(CPUX86State *env, uint32_t new_of, uint32_t new_cf)
{
    uint32_t temp_po = new_of ^ new_cf;
    env->hvf_emul->lflags.auxbits &= ~(LF_MASK_PO | LF_MASK_CF);
    env->hvf_emul->lflags.auxbits |= (temp_po << LF_BIT_PO) |
                                     (new_cf << LF_BIT_CF);
}

void SET_FLAGS_OSZAPC_SUB32(CPUX86State *env, uint32_t v1, uint32_t v2,
                            uint32_t diff)
{
    SET_FLAGS_OSZAPC_SUB_32(v1, v2, diff);
}

void SET_FLAGS_OSZAPC_SUB16(CPUX86State *env, uint16_t v1, uint16_t v2,
                            uint16_t diff)
{
    SET_FLAGS_OSZAPC_SUB_16(v1, v2, diff);
}

void SET_FLAGS_OSZAPC_SUB8(CPUX86State *env, uint8_t v1, uint8_t v2,
                            uint8_t diff)
{
    SET_FLAGS_OSZAPC_SUB_8(v1, v2, diff);
}

void SET_FLAGS_OSZAPC_ADD32(CPUX86State *env, uint32_t v1, uint32_t v2,
                            uint32_t diff)
{
    SET_FLAGS_OSZAPC_ADD_32(v1, v2, diff);
}

void SET_FLAGS_OSZAPC_ADD16(CPUX86State *env, uint16_t v1, uint16_t v2,
                            uint16_t diff)
{
    SET_FLAGS_OSZAPC_ADD_16(v1, v2, diff);
}

void SET_FLAGS_OSZAPC_ADD8(CPUX86State *env, uint8_t v1, uint8_t v2,
                            uint8_t diff)
{
    SET_FLAGS_OSZAPC_ADD_8(v1, v2, diff);
}

void SET_FLAGS_OSZAP_SUB32(CPUX86State *env, uint32_t v1, uint32_t v2,
                            uint32_t diff)
{
    SET_FLAGS_OSZAP_SUB_32(v1, v2, diff);
}

void SET_FLAGS_OSZAP_SUB16(CPUX86State *env, uint16_t v1, uint16_t v2,
                            uint16_t diff)
{
    SET_FLAGS_OSZAP_SUB_16(v1, v2, diff);
}

void SET_FLAGS_OSZAP_SUB8(CPUX86State *env, uint8_t v1, uint8_t v2,
                            uint8_t diff)
{
    SET_FLAGS_OSZAP_SUB_8(v1, v2, diff);
}

void SET_FLAGS_OSZAP_ADD32(CPUX86State *env, uint32_t v1, uint32_t v2,
                            uint32_t diff)
{
    SET_FLAGS_OSZAP_ADD_32(v1, v2, diff);
}

void SET_FLAGS_OSZAP_ADD16(CPUX86State *env, uint16_t v1, uint16_t v2,
                            uint16_t diff)
{
    SET_FLAGS_OSZAP_ADD_16(v1, v2, diff);
}

void SET_FLAGS_OSZAP_ADD8(CPUX86State *env, uint8_t v1, uint8_t v2,
                            uint8_t diff)
{
    SET_FLAGS_OSZAP_ADD_8(v1, v2, diff);
}


void SET_FLAGS_OSZAPC_LOGIC32(CPUX86State *env, uint32_t diff)
{
    SET_FLAGS_OSZAPC_LOGIC_32(diff);
}

void SET_FLAGS_OSZAPC_LOGIC16(CPUX86State *env, uint16_t diff)
{
    SET_FLAGS_OSZAPC_LOGIC_16(diff);
}

void SET_FLAGS_OSZAPC_LOGIC8(CPUX86State *env, uint8_t diff)
{
    SET_FLAGS_OSZAPC_LOGIC_8(diff);
}

void SET_FLAGS_SHR32(CPUX86State *env, uint32_t v, int count, uint32_t res)
{
    int cf = (v >> (count - 1)) & 0x1;
    int of = (((res << 1) ^ res) >> 31);

    SET_FLAGS_OSZAPC_LOGIC_32(res);
    SET_FLAGS_OxxxxC(env, of, cf);
}

void SET_FLAGS_SHR16(CPUX86State *env, uint16_t v, int count, uint16_t res)
{
    int cf = (v >> (count - 1)) & 0x1;
    int of = (((res << 1) ^ res) >> 15);

    SET_FLAGS_OSZAPC_LOGIC_16(res);
    SET_FLAGS_OxxxxC(env, of, cf);
}

void SET_FLAGS_SHR8(CPUX86State *env, uint8_t v, int count, uint8_t res)
{
    int cf = (v >> (count - 1)) & 0x1;
    int of = (((res << 1) ^ res) >> 7);

    SET_FLAGS_OSZAPC_LOGIC_8(res);
    SET_FLAGS_OxxxxC(env, of, cf);
}

void SET_FLAGS_SAR32(CPUX86State *env, int32_t v, int count, uint32_t res)
{
    int cf = (v >> (count - 1)) & 0x1;

    SET_FLAGS_OSZAPC_LOGIC_32(res);
    SET_FLAGS_OxxxxC(env, 0, cf);
}

void SET_FLAGS_SAR16(CPUX86State *env, int16_t v, int count, uint16_t res)
{
    int cf = (v >> (count - 1)) & 0x1;

    SET_FLAGS_OSZAPC_LOGIC_16(res);
    SET_FLAGS_OxxxxC(env, 0, cf);
}

void SET_FLAGS_SAR8(CPUX86State *env, int8_t v, int count, uint8_t res)
{
    int cf = (v >> (count - 1)) & 0x1;

    SET_FLAGS_OSZAPC_LOGIC_8(res);
    SET_FLAGS_OxxxxC(env, 0, cf);
}


void SET_FLAGS_SHL32(CPUX86State *env, uint32_t v, int count, uint32_t res)
{
    int of, cf;

    cf = (v >> (32 - count)) & 0x1;
    of = cf ^ (res >> 31);

    SET_FLAGS_OSZAPC_LOGIC_32(res);
    SET_FLAGS_OxxxxC(env, of, cf);
}

void SET_FLAGS_SHL16(CPUX86State *env, uint16_t v, int count, uint16_t res)
{
    int of = 0, cf = 0;

    if (count <= 16) {
        cf = (v >> (16 - count)) & 0x1;
        of = cf ^ (res >> 15);
    }

    SET_FLAGS_OSZAPC_LOGIC_16(res);
    SET_FLAGS_OxxxxC(env, of, cf);
}

void SET_FLAGS_SHL8(CPUX86State *env, uint8_t v, int count, uint8_t res)
{
    int of = 0, cf = 0;

    if (count <= 8) {
        cf = (v >> (8 - count)) & 0x1;
        of = cf ^ (res >> 7);
    }

    SET_FLAGS_OSZAPC_LOGIC_8(res);
    SET_FLAGS_OxxxxC(env, of, cf);
}

bool get_PF(CPUX86State *env)
{
    uint32_t temp = (255 & env->hvf_emul->lflags.result);
    temp = temp ^ (255 & (env->hvf_emul->lflags.auxbits >> LF_BIT_PDB));
    temp = (temp ^ (temp >> 4)) & 0x0F;
    return (0x9669U >> temp) & 1;
}

void set_PF(CPUX86State *env, bool val)
{
    uint32_t temp = (255 & env->hvf_emul->lflags.result) ^ (!val);
    env->hvf_emul->lflags.auxbits &= ~(LF_MASK_PDB);
    env->hvf_emul->lflags.auxbits |= (temp << LF_BIT_PDB);
}

bool _get_OF(CPUX86State *env)
{
    return ((env->hvf_emul->lflags.auxbits + (1U << LF_BIT_PO)) >> LF_BIT_CF) & 1;
}

bool get_OF(CPUX86State *env)
{
    return _get_OF(env);
}

bool _get_CF(CPUX86State *env)
{
    return (env->hvf_emul->lflags.auxbits >> LF_BIT_CF) & 1;
}

bool get_CF(CPUX86State *env)
{
    return _get_CF(env);
}

void set_OF(CPUX86State *env, bool val)
{
    SET_FLAGS_OxxxxC(env, val, _get_CF(env));
}

void set_CF(CPUX86State *env, bool val)
{
    SET_FLAGS_OxxxxC(env, _get_OF(env), (val));
}

bool get_AF(CPUX86State *env)
{
    return (env->hvf_emul->lflags.auxbits >> LF_BIT_AF) & 1;
}

void set_AF(CPUX86State *env, bool val)
{
    env->hvf_emul->lflags.auxbits &= ~(LF_MASK_AF);
    env->hvf_emul->lflags.auxbits |= (val) << LF_BIT_AF;
}

bool get_ZF(CPUX86State *env)
{
    return !env->hvf_emul->lflags.result;
}

void set_ZF(CPUX86State *env, bool val)
{
    if (val) {
        env->hvf_emul->lflags.auxbits ^=
         (((env->hvf_emul->lflags.result >> LF_SIGN_BIT) & 1) << LF_BIT_SD);
        /* merge the parity bits into the Parity Delta Byte */
        uint32_t temp_pdb = (255 & env->hvf_emul->lflags.result);
        env->hvf_emul->lflags.auxbits ^= (temp_pdb << LF_BIT_PDB);
        /* now zero the .result value */
        env->hvf_emul->lflags.result = 0;
    } else {
        env->hvf_emul->lflags.result |= (1 << 8);
    }
}

bool get_SF(CPUX86State *env)
{
    return ((env->hvf_emul->lflags.result >> LF_SIGN_BIT) ^
            (env->hvf_emul->lflags.auxbits >> LF_BIT_SD)) & 1;
}

void set_SF(CPUX86State *env, bool val)
{
    bool temp_sf = get_SF(env);
    env->hvf_emul->lflags.auxbits ^= (temp_sf ^ val) << LF_BIT_SD;
}

void lflags_to_rflags(CPUX86State *env)
{
    env->hvf_emul->rflags.cf = get_CF(env);
    env->hvf_emul->rflags.pf = get_PF(env);
    env->hvf_emul->rflags.af = get_AF(env);
    env->hvf_emul->rflags.zf = get_ZF(env);
    env->hvf_emul->rflags.sf = get_SF(env);
    env->hvf_emul->rflags.of = get_OF(env);
}

void rflags_to_lflags(CPUX86State *env)
{
    env->hvf_emul->lflags.auxbits = env->hvf_emul->lflags.result = 0;
    set_OF(env, env->hvf_emul->rflags.of);
    set_SF(env, env->hvf_emul->rflags.sf);
    set_ZF(env, env->hvf_emul->rflags.zf);
    set_AF(env, env->hvf_emul->rflags.af);
    set_PF(env, env->hvf_emul->rflags.pf);
    set_CF(env, env->hvf_emul->rflags.cf);
}

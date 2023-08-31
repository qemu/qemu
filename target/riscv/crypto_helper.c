/*
 * RISC-V Crypto Emulation Helpers for QEMU.
 *
 * Copyright (c) 2021 Ruibo Lu, luruibo2000@163.com
 * Copyright (c) 2021 Zewen Ye, lustrew@foxmail.com
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/exec-all.h"
#include "exec/helper-proto.h"
#include "crypto/aes.h"
#include "crypto/aes-round.h"
#include "crypto/sm4.h"

#define sext32_xlen(x) (target_ulong)(int32_t)(x)

static inline target_ulong aes32_operation(target_ulong shamt,
                                           target_ulong rs1, target_ulong rs2,
                                           bool enc, bool mix)
{
    uint8_t si = rs2 >> shamt;
    uint32_t mixed;
    target_ulong res;

    if (enc) {
        if (mix) {
            mixed = be32_to_cpu(AES_Te0[si]);
        } else {
            mixed = AES_sbox[si];
        }
    } else {
        if (mix) {
            mixed = be32_to_cpu(AES_Td0[si]);
        } else {
            mixed = AES_isbox[si];
        }
    }
    mixed = rol32(mixed, shamt);
    res = rs1 ^ mixed;

    return sext32_xlen(res);
}

target_ulong HELPER(aes32esmi)(target_ulong rs1, target_ulong rs2,
                               target_ulong shamt)
{
    return aes32_operation(shamt, rs1, rs2, true, true);
}

target_ulong HELPER(aes32esi)(target_ulong rs1, target_ulong rs2,
                              target_ulong shamt)
{
    return aes32_operation(shamt, rs1, rs2, true, false);
}

target_ulong HELPER(aes32dsmi)(target_ulong rs1, target_ulong rs2,
                               target_ulong shamt)
{
    return aes32_operation(shamt, rs1, rs2, false, true);
}

target_ulong HELPER(aes32dsi)(target_ulong rs1, target_ulong rs2,
                              target_ulong shamt)
{
    return aes32_operation(shamt, rs1, rs2, false, false);
}

static const AESState aes_zero = { };

target_ulong HELPER(aes64esm)(target_ulong rs1, target_ulong rs2)
{
    AESState t;

    t.d[HOST_BIG_ENDIAN] = rs1;
    t.d[!HOST_BIG_ENDIAN] = rs2;
    aesenc_SB_SR_MC_AK(&t, &t, &aes_zero, false);
    return t.d[HOST_BIG_ENDIAN];
}

target_ulong HELPER(aes64es)(target_ulong rs1, target_ulong rs2)
{
    AESState t;

    t.d[HOST_BIG_ENDIAN] = rs1;
    t.d[!HOST_BIG_ENDIAN] = rs2;
    aesenc_SB_SR_AK(&t, &t, &aes_zero, false);
    return t.d[HOST_BIG_ENDIAN];
}

target_ulong HELPER(aes64ds)(target_ulong rs1, target_ulong rs2)
{
    AESState t;

    t.d[HOST_BIG_ENDIAN] = rs1;
    t.d[!HOST_BIG_ENDIAN] = rs2;
    aesdec_ISB_ISR_AK(&t, &t, &aes_zero, false);
    return t.d[HOST_BIG_ENDIAN];
}

target_ulong HELPER(aes64dsm)(target_ulong rs1, target_ulong rs2)
{
    AESState t, z = { };

    /*
     * This instruction does not include a round key,
     * so supply a zero to our primitive.
     */
    t.d[HOST_BIG_ENDIAN] = rs1;
    t.d[!HOST_BIG_ENDIAN] = rs2;
    aesdec_ISB_ISR_IMC_AK(&t, &t, &z, false);
    return t.d[HOST_BIG_ENDIAN];
}

target_ulong HELPER(aes64ks2)(target_ulong rs1, target_ulong rs2)
{
    uint64_t RS1 = rs1;
    uint64_t RS2 = rs2;
    uint32_t rs1_hi = RS1 >> 32;
    uint32_t rs2_lo = RS2;
    uint32_t rs2_hi = RS2 >> 32;

    uint32_t r_lo = (rs1_hi ^ rs2_lo);
    uint32_t r_hi = (rs1_hi ^ rs2_lo ^ rs2_hi);
    target_ulong result = ((uint64_t)r_hi << 32) | r_lo;

    return result;
}

target_ulong HELPER(aes64ks1i)(target_ulong rs1, target_ulong rnum)
{
    uint64_t RS1 = rs1;
    static const uint8_t round_consts[10] = {
        0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36
    };

    uint8_t enc_rnum = rnum;
    uint32_t temp = (RS1 >> 32) & 0xFFFFFFFF;
    AESState t, rc = {};

    if (enc_rnum != 0xA) {
        temp = ror32(temp, 8); /* Rotate right by 8 */
        rc.w[0] = rc.w[1] = round_consts[enc_rnum];
    }

    t.w[0] = t.w[1] = t.w[2] = t.w[3] = temp;
    aesenc_SB_SR_AK(&t, &t, &rc, false);

    return t.d[0];
}

target_ulong HELPER(aes64im)(target_ulong rs1)
{
    AESState t;

    t.d[HOST_BIG_ENDIAN] = rs1;
    t.d[!HOST_BIG_ENDIAN] = 0;
    aesdec_IMC(&t, &t, false);
    return t.d[HOST_BIG_ENDIAN];
}

target_ulong HELPER(sm4ed)(target_ulong rs1, target_ulong rs2,
                           target_ulong shamt)
{
    uint32_t sb_in = (uint8_t)(rs2 >> shamt);
    uint32_t sb_out = (uint32_t)sm4_sbox[sb_in];

    uint32_t x = sb_out ^ (sb_out << 8) ^ (sb_out << 2) ^ (sb_out << 18) ^
                 ((sb_out & 0x3f) << 26) ^ ((sb_out & 0xC0) << 10);

    uint32_t rotl = rol32(x, shamt);

    return sext32_xlen(rotl ^ (uint32_t)rs1);
}

target_ulong HELPER(sm4ks)(target_ulong rs1, target_ulong rs2,
                           target_ulong shamt)
{
    uint32_t sb_in = (uint8_t)(rs2 >> shamt);
    uint32_t sb_out = sm4_sbox[sb_in];

    uint32_t x = sb_out ^ ((sb_out & 0x07) << 29) ^ ((sb_out & 0xFE) << 7) ^
                 ((sb_out & 0x01) << 23) ^ ((sb_out & 0xF8) << 13);

    uint32_t rotl = rol32(x, shamt);

    return sext32_xlen(rotl ^ (uint32_t)rs1);
}
#undef sext32_xlen

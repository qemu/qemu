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
#include "crypto/sm4.h"

#define AES_XTIME(a) \
    ((a << 1) ^ ((a & 0x80) ? 0x1b : 0))

#define AES_GFMUL(a, b) (( \
    (((b) & 0x1) ? (a) : 0) ^ \
    (((b) & 0x2) ? AES_XTIME(a) : 0) ^ \
    (((b) & 0x4) ? AES_XTIME(AES_XTIME(a)) : 0) ^ \
    (((b) & 0x8) ? AES_XTIME(AES_XTIME(AES_XTIME(a))) : 0)) & 0xFF)

static inline uint32_t aes_mixcolumn_byte(uint8_t x, bool fwd)
{
    uint32_t u;

    if (fwd) {
        u = (AES_GFMUL(x, 3) << 24) | (x << 16) | (x << 8) |
            (AES_GFMUL(x, 2) << 0);
    } else {
        u = (AES_GFMUL(x, 0xb) << 24) | (AES_GFMUL(x, 0xd) << 16) |
            (AES_GFMUL(x, 0x9) << 8) | (AES_GFMUL(x, 0xe) << 0);
    }
    return u;
}

#define sext32_xlen(x) (target_ulong)(int32_t)(x)

static inline target_ulong aes32_operation(target_ulong shamt,
                                           target_ulong rs1, target_ulong rs2,
                                           bool enc, bool mix)
{
    uint8_t si = rs2 >> shamt;
    uint8_t so;
    uint32_t mixed;
    target_ulong res;

    if (enc) {
        so = AES_sbox[si];
        if (mix) {
            mixed = aes_mixcolumn_byte(so, true);
        } else {
            mixed = so;
        }
    } else {
        so = AES_isbox[si];
        if (mix) {
            mixed = aes_mixcolumn_byte(so, false);
        } else {
            mixed = so;
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

#define BY(X, I) ((X >> (8 * I)) & 0xFF)

#define AES_SHIFROWS_LO(RS1, RS2) ( \
    (((RS1 >> 24) & 0xFF) << 56) | (((RS2 >> 48) & 0xFF) << 48) | \
    (((RS2 >> 8) & 0xFF) << 40) | (((RS1 >> 32) & 0xFF) << 32) | \
    (((RS2 >> 56) & 0xFF) << 24) | (((RS2 >> 16) & 0xFF) << 16) | \
    (((RS1 >> 40) & 0xFF) << 8) | (((RS1 >> 0) & 0xFF) << 0))

#define AES_INVSHIFROWS_LO(RS1, RS2) ( \
    (((RS2 >> 24) & 0xFF) << 56) | (((RS2 >> 48) & 0xFF) << 48) | \
    (((RS1 >> 8) & 0xFF) << 40) | (((RS1 >> 32) & 0xFF) << 32) | \
    (((RS1 >> 56) & 0xFF) << 24) | (((RS2 >> 16) & 0xFF) << 16) | \
    (((RS2 >> 40) & 0xFF) << 8) | (((RS1 >> 0) & 0xFF) << 0))

#define AES_MIXBYTE(COL, B0, B1, B2, B3) ( \
    BY(COL, B3) ^ BY(COL, B2) ^ AES_GFMUL(BY(COL, B1), 3) ^ \
    AES_GFMUL(BY(COL, B0), 2))

#define AES_MIXCOLUMN(COL) ( \
    AES_MIXBYTE(COL, 3, 0, 1, 2) << 24 | \
    AES_MIXBYTE(COL, 2, 3, 0, 1) << 16 | \
    AES_MIXBYTE(COL, 1, 2, 3, 0) << 8 | AES_MIXBYTE(COL, 0, 1, 2, 3) << 0)

#define AES_INVMIXBYTE(COL, B0, B1, B2, B3) ( \
    AES_GFMUL(BY(COL, B3), 0x9) ^ AES_GFMUL(BY(COL, B2), 0xd) ^ \
    AES_GFMUL(BY(COL, B1), 0xb) ^ AES_GFMUL(BY(COL, B0), 0xe))

#define AES_INVMIXCOLUMN(COL) ( \
    AES_INVMIXBYTE(COL, 3, 0, 1, 2) << 24 | \
    AES_INVMIXBYTE(COL, 2, 3, 0, 1) << 16 | \
    AES_INVMIXBYTE(COL, 1, 2, 3, 0) << 8 | \
    AES_INVMIXBYTE(COL, 0, 1, 2, 3) << 0)

static inline target_ulong aes64_operation(target_ulong rs1, target_ulong rs2,
                                           bool enc, bool mix)
{
    uint64_t RS1 = rs1;
    uint64_t RS2 = rs2;
    uint64_t result;
    uint64_t temp;
    uint32_t col_0;
    uint32_t col_1;

    if (enc) {
        temp = AES_SHIFROWS_LO(RS1, RS2);
        temp = (((uint64_t)AES_sbox[(temp >> 0) & 0xFF] << 0) |
                ((uint64_t)AES_sbox[(temp >> 8) & 0xFF] << 8) |
                ((uint64_t)AES_sbox[(temp >> 16) & 0xFF] << 16) |
                ((uint64_t)AES_sbox[(temp >> 24) & 0xFF] << 24) |
                ((uint64_t)AES_sbox[(temp >> 32) & 0xFF] << 32) |
                ((uint64_t)AES_sbox[(temp >> 40) & 0xFF] << 40) |
                ((uint64_t)AES_sbox[(temp >> 48) & 0xFF] << 48) |
                ((uint64_t)AES_sbox[(temp >> 56) & 0xFF] << 56));
        if (mix) {
            col_0 = temp & 0xFFFFFFFF;
            col_1 = temp >> 32;

            col_0 = AES_MIXCOLUMN(col_0);
            col_1 = AES_MIXCOLUMN(col_1);

            result = ((uint64_t)col_1 << 32) | col_0;
        } else {
            result = temp;
        }
    } else {
        temp = AES_INVSHIFROWS_LO(RS1, RS2);
        temp = (((uint64_t)AES_isbox[(temp >> 0) & 0xFF] << 0) |
                ((uint64_t)AES_isbox[(temp >> 8) & 0xFF] << 8) |
                ((uint64_t)AES_isbox[(temp >> 16) & 0xFF] << 16) |
                ((uint64_t)AES_isbox[(temp >> 24) & 0xFF] << 24) |
                ((uint64_t)AES_isbox[(temp >> 32) & 0xFF] << 32) |
                ((uint64_t)AES_isbox[(temp >> 40) & 0xFF] << 40) |
                ((uint64_t)AES_isbox[(temp >> 48) & 0xFF] << 48) |
                ((uint64_t)AES_isbox[(temp >> 56) & 0xFF] << 56));
        if (mix) {
            col_0 = temp & 0xFFFFFFFF;
            col_1 = temp >> 32;

            col_0 = AES_INVMIXCOLUMN(col_0);
            col_1 = AES_INVMIXCOLUMN(col_1);

            result = ((uint64_t)col_1 << 32) | col_0;
        } else {
            result = temp;
        }
    }

    return result;
}

target_ulong HELPER(aes64esm)(target_ulong rs1, target_ulong rs2)
{
    return aes64_operation(rs1, rs2, true, true);
}

target_ulong HELPER(aes64es)(target_ulong rs1, target_ulong rs2)
{
    return aes64_operation(rs1, rs2, true, false);
}

target_ulong HELPER(aes64ds)(target_ulong rs1, target_ulong rs2)
{
    return aes64_operation(rs1, rs2, false, false);
}

target_ulong HELPER(aes64dsm)(target_ulong rs1, target_ulong rs2)
{
    return aes64_operation(rs1, rs2, false, true);
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
    uint8_t rcon_ = 0;
    target_ulong result;

    if (enc_rnum != 0xA) {
        temp = ror32(temp, 8); /* Rotate right by 8 */
        rcon_ = round_consts[enc_rnum];
    }

    temp = ((uint32_t)AES_sbox[(temp >> 24) & 0xFF] << 24) |
           ((uint32_t)AES_sbox[(temp >> 16) & 0xFF] << 16) |
           ((uint32_t)AES_sbox[(temp >> 8) & 0xFF] << 8) |
           ((uint32_t)AES_sbox[(temp >> 0) & 0xFF] << 0);

    temp ^= rcon_;

    result = ((uint64_t)temp << 32) | temp;

    return result;
}

target_ulong HELPER(aes64im)(target_ulong rs1)
{
    uint64_t RS1 = rs1;
    uint32_t col_0 = RS1 & 0xFFFFFFFF;
    uint32_t col_1 = RS1 >> 32;
    target_ulong result;

    col_0 = AES_INVMIXCOLUMN(col_0);
    col_1 = AES_INVMIXCOLUMN(col_1);

    result = ((uint64_t)col_1 << 32) | col_0;

    return result;
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

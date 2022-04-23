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
#undef sext32_xlen

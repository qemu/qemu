/*
 * RISC-V Bitmanip Extension Helpers for QEMU.
 *
 * Copyright (c) 2020 Kito Cheng, kito.cheng@sifive.com
 * Copyright (c) 2020 Frank Chang, frank.chang@sifive.com
 * Copyright (c) 2021 Philipp Tomsich, philipp.tomsich@vrull.eu
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
#include "qemu/host-utils.h"
#include "exec/exec-all.h"
#include "exec/helper-proto.h"
#include "tcg/tcg.h"

target_ulong HELPER(clmul)(target_ulong rs1, target_ulong rs2)
{
    target_ulong result = 0;

    for (int i = 0; i < TARGET_LONG_BITS; i++) {
        if ((rs2 >> i) & 1) {
            result ^= (rs1 << i);
        }
    }

    return result;
}

target_ulong HELPER(clmulr)(target_ulong rs1, target_ulong rs2)
{
    target_ulong result = 0;

    for (int i = 0; i < TARGET_LONG_BITS; i++) {
        if ((rs2 >> i) & 1) {
            result ^= (rs1 >> (TARGET_LONG_BITS - i - 1));
        }
    }

    return result;
}

static inline target_ulong do_swap(target_ulong x, uint64_t mask, int shift)
{
    return ((x & mask) << shift) | ((x & ~mask) >> shift);
}

target_ulong HELPER(brev8)(target_ulong rs1)
{
    target_ulong x = rs1;

    x = do_swap(x, 0x5555555555555555ull, 1);
    x = do_swap(x, 0x3333333333333333ull, 2);
    x = do_swap(x, 0x0f0f0f0f0f0f0f0full, 4);
    return x;
}

static const uint64_t shuf_masks[] = {
    dup_const(MO_8, 0x44),
    dup_const(MO_8, 0x30),
    dup_const(MO_16, 0x0f00),
    dup_const(MO_32, 0xff0000)
};

static inline target_ulong do_shuf_stage(target_ulong src, uint64_t maskL,
                                         uint64_t maskR, int shift)
{
    target_ulong x = src & ~(maskL | maskR);

    x |= ((src << shift) & maskL) | ((src >> shift) & maskR);
    return x;
}

target_ulong HELPER(unzip)(target_ulong rs1)
{
    target_ulong x = rs1;

    x = do_shuf_stage(x, shuf_masks[0], shuf_masks[0] >> 1, 1);
    x = do_shuf_stage(x, shuf_masks[1], shuf_masks[1] >> 2, 2);
    x = do_shuf_stage(x, shuf_masks[2], shuf_masks[2] >> 4, 4);
    x = do_shuf_stage(x, shuf_masks[3], shuf_masks[3] >> 8, 8);
    return x;
}

target_ulong HELPER(zip)(target_ulong rs1)
{
    target_ulong x = rs1;

    x = do_shuf_stage(x, shuf_masks[3], shuf_masks[3] >> 8, 8);
    x = do_shuf_stage(x, shuf_masks[2], shuf_masks[2] >> 4, 4);
    x = do_shuf_stage(x, shuf_masks[1], shuf_masks[1] >> 2, 2);
    x = do_shuf_stage(x, shuf_masks[0], shuf_masks[0] >> 1, 1);
    return x;
}

static inline target_ulong do_xperm(target_ulong rs1, target_ulong rs2,
                                    uint32_t sz_log2)
{
    target_ulong r = 0;
    target_ulong sz = 1LL << sz_log2;
    target_ulong mask = (1LL << sz) - 1;
    target_ulong pos;

    for (int i = 0; i < TARGET_LONG_BITS; i += sz) {
        pos = ((rs2 >> i) & mask) << sz_log2;
        if (pos < sizeof(target_ulong) * 8) {
            r |= ((rs1 >> pos) & mask) << i;
        }
    }
    return r;
}

target_ulong HELPER(xperm4)(target_ulong rs1, target_ulong rs2)
{
    return do_xperm(rs1, rs2, 2);
}

target_ulong HELPER(xperm8)(target_ulong rs1, target_ulong rs2)
{
    return do_xperm(rs1, rs2, 3);
}

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

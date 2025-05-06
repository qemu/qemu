/*
 * RISC-V Zcmt Extension Helper for QEMU.
 *
 * Copyright (c) 2021-2022 PLCT Lab
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
#include "exec/helper-proto.h"
#include "accel/tcg/cpu-ldst.h"

target_ulong HELPER(cm_jalt)(CPURISCVState *env, uint32_t index)
{

#if !defined(CONFIG_USER_ONLY)
    RISCVException ret = smstateen_acc_ok(env, 0, SMSTATEEN0_JVT);
    if (ret != RISCV_EXCP_NONE) {
        riscv_raise_exception(env, ret, 0);
    }
#endif

    target_ulong target;
    target_ulong val = env->jvt;
    int xlen = riscv_cpu_xlen(env);
    uint8_t mode = get_field(val, JVT_MODE);
    target_ulong base = val & JVT_BASE;
    target_ulong t0;

    if (mode != 0) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, 0);
    }

    if (xlen == 32) {
        t0 = base + (index << 2);
        target = cpu_ldl_code(env, t0);
    } else {
        t0 = base + (index << 3);
        target = cpu_ldq_code(env, t0);
    }

    return target & ~0x1;
}

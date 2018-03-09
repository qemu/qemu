/*
 * RISC-V GDB Server Stub
 *
 * Copyright (c) 2016-2017 Sagar Karandikar, sagark@eecs.berkeley.edu
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
#include "qemu-common.h"
#include "exec/gdbstub.h"
#include "cpu.h"

int riscv_cpu_gdb_read_register(CPUState *cs, uint8_t *mem_buf, int n)
{
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;

    if (n < 32) {
        return gdb_get_regl(mem_buf, env->gpr[n]);
    } else if (n == 32) {
        return gdb_get_regl(mem_buf, env->pc);
    } else if (n < 65) {
        return gdb_get_reg64(mem_buf, env->fpr[n - 33]);
    } else if (n < 4096 + 65) {
        return gdb_get_regl(mem_buf, csr_read_helper(env, n - 65));
    }
    return 0;
}

int riscv_cpu_gdb_write_register(CPUState *cs, uint8_t *mem_buf, int n)
{
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;

    if (n == 0) {
        /* discard writes to x0 */
        return sizeof(target_ulong);
    } else if (n < 32) {
        env->gpr[n] = ldtul_p(mem_buf);
        return sizeof(target_ulong);
    } else if (n == 32) {
        env->pc = ldtul_p(mem_buf);
        return sizeof(target_ulong);
    } else if (n < 65) {
        env->fpr[n - 33] = ldq_p(mem_buf); /* always 64-bit */
        return sizeof(uint64_t);
    } else if (n < 4096 + 65) {
        csr_write_helper(env, ldtul_p(mem_buf), n - 65);
    }
    return 0;
}

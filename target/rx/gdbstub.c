/*
 * RX gdb server stub
 *
 * Copyright (c) 2019 Yoshinori Sato
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
#include "cpu.h"
#include "exec/gdbstub.h"

int rx_cpu_gdb_read_register(CPUState *cs, GByteArray *mem_buf, int n)
{
    RXCPU *cpu = RX_CPU(cs);
    CPURXState *env = &cpu->env;

    switch (n) {
    case 0 ... 15:
        return gdb_get_regl(mem_buf, env->regs[n]);
    case 16:
        return gdb_get_regl(mem_buf, (env->psw_u) ? env->regs[0] : env->usp);
    case 17:
        return gdb_get_regl(mem_buf, (!env->psw_u) ? env->regs[0] : env->isp);
    case 18:
        return gdb_get_regl(mem_buf, rx_cpu_pack_psw(env));
    case 19:
        return gdb_get_regl(mem_buf, env->pc);
    case 20:
        return gdb_get_regl(mem_buf, env->intb);
    case 21:
        return gdb_get_regl(mem_buf, env->bpsw);
    case 22:
        return gdb_get_regl(mem_buf, env->bpc);
    case 23:
        return gdb_get_regl(mem_buf, env->fintv);
    case 24:
        return gdb_get_regl(mem_buf, env->fpsw);
    case 25:
        return 0;
    }
    return 0;
}

int rx_cpu_gdb_write_register(CPUState *cs, uint8_t *mem_buf, int n)
{
    RXCPU *cpu = RX_CPU(cs);
    CPURXState *env = &cpu->env;
    uint32_t psw;
    switch (n) {
    case 0 ... 15:
        env->regs[n] = ldl_p(mem_buf);
        if (n == 0) {
            if (env->psw_u) {
                env->usp = env->regs[0];
            } else {
                env->isp = env->regs[0];
            }
        }
        break;
    case 16:
        env->usp = ldl_p(mem_buf);
        if (env->psw_u) {
            env->regs[0] = ldl_p(mem_buf);
        }
        break;
    case 17:
        env->isp = ldl_p(mem_buf);
        if (!env->psw_u) {
            env->regs[0] = ldl_p(mem_buf);
        }
        break;
    case 18:
        psw = ldl_p(mem_buf);
        rx_cpu_unpack_psw(env, psw, 1);
        break;
    case 19:
        env->pc = ldl_p(mem_buf);
        break;
    case 20:
        env->intb = ldl_p(mem_buf);
        break;
    case 21:
        env->bpsw = ldl_p(mem_buf);
        break;
    case 22:
        env->bpc = ldl_p(mem_buf);
        break;
    case 23:
        env->fintv = ldl_p(mem_buf);
        break;
    case 24:
        env->fpsw = ldl_p(mem_buf);
        break;
    case 25:
        return 8;
    default:
        return 0;
    }

    return 4;
}

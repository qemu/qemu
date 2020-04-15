/*
 * SuperH gdb server stub
 *
 * Copyright (c) 2003-2005 Fabrice Bellard
 * Copyright (c) 2013 SUSE LINUX Products GmbH
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */
#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/gdbstub.h"

/* Hint: Use "set architecture sh4" in GDB to see fpu registers */
/* FIXME: We should use XML for this.  */

int superh_cpu_gdb_read_register(CPUState *cs, GByteArray *mem_buf, int n)
{
    SuperHCPU *cpu = SUPERH_CPU(cs);
    CPUSH4State *env = &cpu->env;

    switch (n) {
    case 0 ... 7:
        if ((env->sr & (1u << SR_MD)) && (env->sr & (1u << SR_RB))) {
            return gdb_get_regl(mem_buf, env->gregs[n + 16]);
        } else {
            return gdb_get_regl(mem_buf, env->gregs[n]);
        }
    case 8 ... 15:
        return gdb_get_regl(mem_buf, env->gregs[n]);
    case 16:
        return gdb_get_regl(mem_buf, env->pc);
    case 17:
        return gdb_get_regl(mem_buf, env->pr);
    case 18:
        return gdb_get_regl(mem_buf, env->gbr);
    case 19:
        return gdb_get_regl(mem_buf, env->vbr);
    case 20:
        return gdb_get_regl(mem_buf, env->mach);
    case 21:
        return gdb_get_regl(mem_buf, env->macl);
    case 22:
        return gdb_get_regl(mem_buf, cpu_read_sr(env));
    case 23:
        return gdb_get_regl(mem_buf, env->fpul);
    case 24:
        return gdb_get_regl(mem_buf, env->fpscr);
    case 25 ... 40:
        if (env->fpscr & FPSCR_FR) {
            return gdb_get_float32(mem_buf, env->fregs[n - 9]);
        }
        return gdb_get_float32(mem_buf, env->fregs[n - 25]);
    case 41:
        return gdb_get_regl(mem_buf, env->ssr);
    case 42:
        return gdb_get_regl(mem_buf, env->spc);
    case 43 ... 50:
        return gdb_get_regl(mem_buf, env->gregs[n - 43]);
    case 51 ... 58:
        return gdb_get_regl(mem_buf, env->gregs[n - (51 - 16)]);
    }

    return 0;
}

int superh_cpu_gdb_write_register(CPUState *cs, uint8_t *mem_buf, int n)
{
    SuperHCPU *cpu = SUPERH_CPU(cs);
    CPUSH4State *env = &cpu->env;

    switch (n) {
    case 0 ... 7:
        if ((env->sr & (1u << SR_MD)) && (env->sr & (1u << SR_RB))) {
            env->gregs[n + 16] = ldl_p(mem_buf);
        } else {
            env->gregs[n] = ldl_p(mem_buf);
        }
        break;
    case 8 ... 15:
        env->gregs[n] = ldl_p(mem_buf);
        break;
    case 16:
        env->pc = ldl_p(mem_buf);
        break;
    case 17:
        env->pr = ldl_p(mem_buf);
        break;
    case 18:
        env->gbr = ldl_p(mem_buf);
        break;
    case 19:
        env->vbr = ldl_p(mem_buf);
        break;
    case 20:
        env->mach = ldl_p(mem_buf);
        break;
    case 21:
        env->macl = ldl_p(mem_buf);
        break;
    case 22:
        cpu_write_sr(env, ldl_p(mem_buf));
        break;
    case 23:
        env->fpul = ldl_p(mem_buf);
        break;
    case 24:
        env->fpscr = ldl_p(mem_buf);
        break;
    case 25 ... 40:
        if (env->fpscr & FPSCR_FR) {
            env->fregs[n - 9] = ldfl_p(mem_buf);
        } else {
            env->fregs[n - 25] = ldfl_p(mem_buf);
        }
        break;
    case 41:
        env->ssr = ldl_p(mem_buf);
        break;
    case 42:
        env->spc = ldl_p(mem_buf);
        break;
    case 43 ... 50:
        env->gregs[n - 43] = ldl_p(mem_buf);
        break;
    case 51 ... 58:
        env->gregs[n - (51 - 16)] = ldl_p(mem_buf);
        break;
    default:
        return 0;
    }

    return 4;
}

/*
 * Alpha gdb server stub
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
#include "gdbstub/helpers.h"

int alpha_cpu_gdb_read_register(CPUState *cs, GByteArray *mem_buf, int n)
{
    AlphaCPU *cpu = ALPHA_CPU(cs);
    CPUAlphaState *env = &cpu->env;
    uint64_t val;
    CPU_DoubleU d;

    switch (n) {
    case 0 ... 30:
        val = cpu_alpha_load_gr(env, n);
        break;
    case 32 ... 62:
        d.d = env->fir[n - 32];
        val = d.ll;
        break;
    case 63:
        val = cpu_alpha_load_fpcr(env);
        break;
    case 64:
        val = env->pc;
        break;
    case 66:
        val = env->unique;
        break;
    case 31:
    case 65:
        /* 31 really is the zero register; 65 is unassigned in the
           gdb protocol, but is still required to occupy 8 bytes. */
        val = 0;
        break;
    default:
        return 0;
    }
    return gdb_get_regl(mem_buf, val);
}

int alpha_cpu_gdb_write_register(CPUState *cs, uint8_t *mem_buf, int n)
{
    AlphaCPU *cpu = ALPHA_CPU(cs);
    CPUAlphaState *env = &cpu->env;
    target_ulong tmp = ldtul_p(mem_buf);
    CPU_DoubleU d;

    switch (n) {
    case 0 ... 30:
        cpu_alpha_store_gr(env, n, tmp);
        break;
    case 32 ... 62:
        d.ll = tmp;
        env->fir[n - 32] = d.d;
        break;
    case 63:
        cpu_alpha_store_fpcr(env, tmp);
        break;
    case 64:
        env->pc = tmp;
        break;
    case 66:
        env->unique = tmp;
        break;
    case 31:
    case 65:
        /* 31 really is the zero register; 65 is unassigned in the
           gdb protocol, but is still required to occupy 8 bytes. */
        break;
    default:
        return 0;
    }
    return 8;
}

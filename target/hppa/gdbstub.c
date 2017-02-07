/*
 * HPPA gdb server stub
 *
 * Copyright (c) 2016 Richard Henderson <rth@twiddle.net>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
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
#include "qemu-common.h"
#include "cpu.h"
#include "exec/gdbstub.h"

int hppa_cpu_gdb_read_register(CPUState *cs, uint8_t *mem_buf, int n)
{
    HPPACPU *cpu = HPPA_CPU(cs);
    CPUHPPAState *env = &cpu->env;
    target_ulong val;

    switch (n) {
    case 0:
        val = cpu_hppa_get_psw(env);
        break;
    case 1 ... 31:
        val = env->gr[n];
        break;
    case 32:
        val = env->sar;
        break;
    case 33:
        val = env->iaoq_f;
        break;
    case 35:
        val = env->iaoq_b;
        break;
    case 59:
        val = env->cr26;
        break;
    case 60:
        val = env->cr27;
        break;
    case 64 ... 127:
        val = extract64(env->fr[(n - 64) / 2], (n & 1 ? 0 : 32), 32);
        break;
    default:
        if (n < 128) {
            val = 0;
        } else {
            return 0;
        }
        break;
    }
    return gdb_get_regl(mem_buf, val);
}

int hppa_cpu_gdb_write_register(CPUState *cs, uint8_t *mem_buf, int n)
{
    HPPACPU *cpu = HPPA_CPU(cs);
    CPUHPPAState *env = &cpu->env;
    target_ulong val = ldtul_p(mem_buf);

    switch (n) {
    case 0:
        cpu_hppa_put_psw(env, val);
        break;
    case 1 ... 31:
        env->gr[n] = val;
        break;
    case 32:
        env->sar = val;
        break;
    case 33:
        env->iaoq_f = val;
        break;
    case 35:
        env->iaoq_b = val;
        break;
    case 59:
        env->cr26 = val;
        break;
    case 60:
        env->cr27 = val;
        break;
    case 64:
        env->fr[0] = deposit64(env->fr[0], 32, 32, val);
        cpu_hppa_loaded_fr0(env);
        break;
    case 65 ... 127:
        {
            uint64_t *fr = &env->fr[(n - 64) / 2];
            *fr = deposit64(*fr, val, (n & 1 ? 0 : 32), 32);
        }
        break;
    default:
        if (n >= 128) {
            return 0;
        }
        break;
    }
    return sizeof(target_ulong);
}

/*
 * HPPA gdb server stub
 *
 * Copyright (c) 2016 Richard Henderson <rth@twiddle.net>
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

int hppa_cpu_gdb_read_register(CPUState *cs, GByteArray *mem_buf, int n)
{
    HPPACPU *cpu = HPPA_CPU(cs);
    CPUHPPAState *env = &cpu->env;
    target_ureg val;

    switch (n) {
    case 0:
        val = cpu_hppa_get_psw(env);
        break;
    case 1 ... 31:
        val = env->gr[n];
        break;
    case 32:
        val = env->cr[CR_SAR];
        break;
    case 33:
        val = env->iaoq_f;
        break;
    case 34:
        val = env->iasq_f >> 32;
        break;
    case 35:
        val = env->iaoq_b;
        break;
    case 36:
        val = env->iasq_b >> 32;
        break;
    case 37:
        val = env->cr[CR_EIEM];
        break;
    case 38:
        val = env->cr[CR_IIR];
        break;
    case 39:
        val = env->cr[CR_ISR];
        break;
    case 40:
        val = env->cr[CR_IOR];
        break;
    case 41:
        val = env->cr[CR_IPSW];
        break;
    case 43:
        val = env->sr[4] >> 32;
        break;
    case 44:
        val = env->sr[0] >> 32;
        break;
    case 45:
        val = env->sr[1] >> 32;
        break;
    case 46:
        val = env->sr[2] >> 32;
        break;
    case 47:
        val = env->sr[3] >> 32;
        break;
    case 48:
        val = env->sr[5] >> 32;
        break;
    case 49:
        val = env->sr[6] >> 32;
        break;
    case 50:
        val = env->sr[7] >> 32;
        break;
    case 51:
        val = env->cr[CR_RC];
        break;
    case 52:
        val = env->cr[CR_PID1];
        break;
    case 53:
        val = env->cr[CR_PID2];
        break;
    case 54:
        val = env->cr[CR_SCRCCR];
        break;
    case 55:
        val = env->cr[CR_PID3];
        break;
    case 56:
        val = env->cr[CR_PID4];
        break;
    case 57:
        val = env->cr[24];
        break;
    case 58:
        val = env->cr[25];
        break;
    case 59:
        val = env->cr[26];
        break;
    case 60:
        val = env->cr[27];
        break;
    case 61:
        val = env->cr[28];
        break;
    case 62:
        val = env->cr[29];
        break;
    case 63:
        val = env->cr[30];
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

    if (TARGET_REGISTER_BITS == 64) {
        return gdb_get_reg64(mem_buf, val);
    } else {
        return gdb_get_reg32(mem_buf, val);
    }
}

int hppa_cpu_gdb_write_register(CPUState *cs, uint8_t *mem_buf, int n)
{
    HPPACPU *cpu = HPPA_CPU(cs);
    CPUHPPAState *env = &cpu->env;
    target_ureg val;

    if (TARGET_REGISTER_BITS == 64) {
        val = ldq_p(mem_buf);
    } else {
        val = ldl_p(mem_buf);
    }

    switch (n) {
    case 0:
        cpu_hppa_put_psw(env, val);
        break;
    case 1 ... 31:
        env->gr[n] = val;
        break;
    case 32:
        env->cr[CR_SAR] = val;
        break;
    case 33:
        env->iaoq_f = val;
        break;
    case 34:
        env->iasq_f = (uint64_t)val << 32;
        break;
    case 35:
        env->iaoq_b = val;
        break;
    case 36:
        env->iasq_b = (uint64_t)val << 32;
        break;
    case 37:
        env->cr[CR_EIEM] = val;
        break;
    case 38:
        env->cr[CR_IIR] = val;
        break;
    case 39:
        env->cr[CR_ISR] = val;
        break;
    case 40:
        env->cr[CR_IOR] = val;
        break;
    case 41:
        env->cr[CR_IPSW] = val;
        break;
    case 43:
        env->sr[4] = (uint64_t)val << 32;
        break;
    case 44:
        env->sr[0] = (uint64_t)val << 32;
        break;
    case 45:
        env->sr[1] = (uint64_t)val << 32;
        break;
    case 46:
        env->sr[2] = (uint64_t)val << 32;
        break;
    case 47:
        env->sr[3] = (uint64_t)val << 32;
        break;
    case 48:
        env->sr[5] = (uint64_t)val << 32;
        break;
    case 49:
        env->sr[6] = (uint64_t)val << 32;
        break;
    case 50:
        env->sr[7] = (uint64_t)val << 32;
        break;
    case 51:
        env->cr[CR_RC] = val;
        break;
    case 52:
        env->cr[CR_PID1] = val;
        cpu_hppa_change_prot_id(env);
        break;
    case 53:
        env->cr[CR_PID2] = val;
        cpu_hppa_change_prot_id(env);
        break;
    case 54:
        env->cr[CR_SCRCCR] = val;
        break;
    case 55:
        env->cr[CR_PID3] = val;
        cpu_hppa_change_prot_id(env);
        break;
    case 56:
        env->cr[CR_PID4] = val;
        cpu_hppa_change_prot_id(env);
        break;
    case 57:
        env->cr[24] = val;
        break;
    case 58:
        env->cr[25] = val;
        break;
    case 59:
        env->cr[26] = val;
        break;
    case 60:
        env->cr[27] = val;
        break;
    case 61:
        env->cr[28] = val;
        break;
    case 62:
        env->cr[29] = val;
        break;
    case 63:
        env->cr[30] = val;
        break;
    case 64:
        env->fr[0] = deposit64(env->fr[0], 32, 32, val);
        cpu_hppa_loaded_fr0(env);
        break;
    case 65 ... 127:
        {
            uint64_t *fr = &env->fr[(n - 64) / 2];
            *fr = deposit64(*fr, (n & 1 ? 0 : 32), 32, val);
        }
        break;
    default:
        if (n >= 128) {
            return 0;
        }
        break;
    }
    return sizeof(target_ureg);
}

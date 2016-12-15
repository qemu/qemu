/*
 * Helpers for HPPA instructions.
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
#include "cpu.h"
#include "exec/exec-all.h"
#include "exec/helper-proto.h"

void QEMU_NORETURN HELPER(excp)(CPUHPPAState *env, int excp)
{
    HPPACPU *cpu = hppa_env_get_cpu(env);
    CPUState *cs = CPU(cpu);

    cs->exception_index = excp;
    cpu_loop_exit(cs);
}

void HELPER(loaded_fr0)(CPUHPPAState *env)
{
    uint32_t shadow = env->fr[0] >> 32;
    int rm, d;

    env->fr0_shadow = shadow;

    switch (extract32(shadow, 9, 2)) {
    default:
        rm = float_round_nearest_even;
        break;
    case 1:
        rm = float_round_to_zero;
        break;
    case 2:
        rm = float_round_up;
        break;
    case 3:
        rm = float_round_down;
        break;
    }
    set_float_rounding_mode(rm, &env->fp_status);

    d = extract32(shadow, 5, 1);
    set_flush_to_zero(d, &env->fp_status);
    set_flush_inputs_to_zero(d, &env->fp_status);
}

void cpu_hppa_loaded_fr0(CPUHPPAState *env)
{
    helper_loaded_fr0(env);
}

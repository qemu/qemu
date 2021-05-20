/*
 *  i386 breakpoint helpers
 *
 *  Copyright (c) 2003 Fabrice Bellard
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
#include "exec/helper-proto.h"
#include "helper-tcg.h"

void QEMU_NORETURN helper_single_step(CPUX86State *env)
{
#ifndef CONFIG_USER_ONLY
    check_hw_breakpoints(env, true);
    env->dr[6] |= DR6_BS;
#endif
    raise_exception(env, EXCP01_DB);
}

void helper_rechecking_single_step(CPUX86State *env)
{
    if ((env->eflags & TF_MASK) != 0) {
        helper_single_step(env);
    }
}

target_ulong helper_get_dr(CPUX86State *env, int reg)
{
    switch (reg) {
    case 0: case 1: case 2: case 3: case 6: case 7:
        return env->dr[reg];
    case 4:
        if (env->cr[4] & CR4_DE_MASK) {
            break;
        } else {
            return env->dr[6];
        }
    case 5:
        if (env->cr[4] & CR4_DE_MASK) {
            break;
        } else {
            return env->dr[7];
        }
    }
    raise_exception_err_ra(env, EXCP06_ILLOP, 0, GETPC());
}

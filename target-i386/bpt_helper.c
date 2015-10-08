/*
 *  i386 breakpoint helpers
 *
 *  Copyright (c) 2003 Fabrice Bellard
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

#include "cpu.h"
#include "exec/helper-proto.h"


#ifndef CONFIG_USER_ONLY
static void hw_breakpoint_insert(CPUX86State *env, int index)
{
    CPUState *cs = CPU(x86_env_get_cpu(env));
    int type = 0, err = 0;

    switch (hw_breakpoint_type(env->dr[7], index)) {
    case DR7_TYPE_BP_INST:
        if (hw_breakpoint_enabled(env->dr[7], index)) {
            err = cpu_breakpoint_insert(cs, env->dr[index], BP_CPU,
                                        &env->cpu_breakpoint[index]);
        }
        break;
    case DR7_TYPE_DATA_WR:
        type = BP_CPU | BP_MEM_WRITE;
        break;
    case DR7_TYPE_IO_RW:
        /* No support for I/O watchpoints yet */
        break;
    case DR7_TYPE_DATA_RW:
        type = BP_CPU | BP_MEM_ACCESS;
        break;
    }

    if (type != 0) {
        err = cpu_watchpoint_insert(cs, env->dr[index],
                                    hw_breakpoint_len(env->dr[7], index),
                                    type, &env->cpu_watchpoint[index]);
    }

    if (err) {
        env->cpu_breakpoint[index] = NULL;
    }
}

static void hw_breakpoint_remove(CPUX86State *env, int index)
{
    CPUState *cs;

    if (!env->cpu_breakpoint[index]) {
        return;
    }
    cs = CPU(x86_env_get_cpu(env));
    switch (hw_breakpoint_type(env->dr[7], index)) {
    case DR7_TYPE_BP_INST:
        if (hw_breakpoint_enabled(env->dr[7], index)) {
            cpu_breakpoint_remove_by_ref(cs, env->cpu_breakpoint[index]);
        }
        break;
    case DR7_TYPE_DATA_WR:
    case DR7_TYPE_DATA_RW:
        cpu_watchpoint_remove_by_ref(cs, env->cpu_watchpoint[index]);
        break;
    case DR7_TYPE_IO_RW:
        /* No support for I/O watchpoints yet */
        break;
    }
}

void cpu_x86_update_dr7(CPUX86State *env, uint32_t new_dr7)
{
    target_ulong old_dr7 = env->dr[7];
    int i;

    new_dr7 |= DR7_FIXED_1;

    /* If nothing is changing except the global/local enable bits,
       then we can make the change more efficient.  */
    if (((old_dr7 ^ new_dr7) & ~0xff) == 0) {
        /* Fold the global and local enable bits together into the
           global fields, then xor to show which registers have
           changed collective enable state.  */
        int mod = ((old_dr7 | old_dr7 * 2) ^ (new_dr7 | new_dr7 * 2)) & 0xff;

        for (i = 0; i < DR7_MAX_BP; i++) {
            if ((mod & (2 << i * 2)) && !hw_breakpoint_enabled(new_dr7, i)) {
                hw_breakpoint_remove(env, i);
            }
        }
        env->dr[7] = new_dr7;
        for (i = 0; i < DR7_MAX_BP; i++) {
            if (mod & (2 << i * 2) && hw_breakpoint_enabled(new_dr7, i)) {
                hw_breakpoint_insert(env, i);
            }
        }
    } else {
        for (i = 0; i < DR7_MAX_BP; i++) {
            hw_breakpoint_remove(env, i);
        }
        env->dr[7] = new_dr7;
        for (i = 0; i < DR7_MAX_BP; i++) {
            hw_breakpoint_insert(env, i);
        }
    }
}
#endif

static bool check_hw_breakpoints(CPUX86State *env, bool force_dr6_update)
{
    target_ulong dr6;
    int reg;
    bool hit_enabled = false;

    dr6 = env->dr[6] & ~0xf;
    for (reg = 0; reg < DR7_MAX_BP; reg++) {
        bool bp_match = false;
        bool wp_match = false;

        switch (hw_breakpoint_type(env->dr[7], reg)) {
        case DR7_TYPE_BP_INST:
            if (env->dr[reg] == env->eip) {
                bp_match = true;
            }
            break;
        case DR7_TYPE_DATA_WR:
        case DR7_TYPE_DATA_RW:
            if (env->cpu_watchpoint[reg] &&
                env->cpu_watchpoint[reg]->flags & BP_WATCHPOINT_HIT) {
                wp_match = true;
            }
            break;
        case DR7_TYPE_IO_RW:
            break;
        }
        if (bp_match || wp_match) {
            dr6 |= 1 << reg;
            if (hw_breakpoint_enabled(env->dr[7], reg)) {
                hit_enabled = true;
            }
        }
    }

    if (hit_enabled || force_dr6_update) {
        env->dr[6] = dr6;
    }

    return hit_enabled;
}

void breakpoint_handler(CPUState *cs)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;
    CPUBreakpoint *bp;

    if (cs->watchpoint_hit) {
        if (cs->watchpoint_hit->flags & BP_CPU) {
            cs->watchpoint_hit = NULL;
            if (check_hw_breakpoints(env, false)) {
                raise_exception(env, EXCP01_DB);
            } else {
                cpu_resume_from_signal(cs, NULL);
            }
        }
    } else {
        QTAILQ_FOREACH(bp, &cs->breakpoints, entry) {
            if (bp->pc == env->eip) {
                if (bp->flags & BP_CPU) {
                    check_hw_breakpoints(env, true);
                    raise_exception(env, EXCP01_DB);
                }
                break;
            }
        }
    }
}

void helper_single_step(CPUX86State *env)
{
#ifndef CONFIG_USER_ONLY
    check_hw_breakpoints(env, true);
    env->dr[6] |= DR6_BS;
#endif
    raise_exception(env, EXCP01_DB);
}

void helper_movl_drN_T0(CPUX86State *env, int reg, target_ulong t0)
{
#ifndef CONFIG_USER_ONLY
    if (reg < 4) {
        hw_breakpoint_remove(env, reg);
        env->dr[reg] = t0;
        hw_breakpoint_insert(env, reg);
    } else if (reg == 7) {
        cpu_x86_update_dr7(env, t0);
    } else {
        env->dr[reg] = t0;
    }
#endif
}

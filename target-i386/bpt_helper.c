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

#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/helper-proto.h"


#ifndef CONFIG_USER_ONLY
static inline bool hw_local_breakpoint_enabled(unsigned long dr7, int index)
{
    return (dr7 >> (index * 2)) & 1;
}

static inline bool hw_global_breakpoint_enabled(unsigned long dr7, int index)
{
    return (dr7 >> (index * 2)) & 2;

}
static inline bool hw_breakpoint_enabled(unsigned long dr7, int index)
{
    return hw_global_breakpoint_enabled(dr7, index) ||
           hw_local_breakpoint_enabled(dr7, index);
}

static inline int hw_breakpoint_type(unsigned long dr7, int index)
{
    return (dr7 >> (DR7_TYPE_SHIFT + (index * 4))) & 3;
}

static inline int hw_breakpoint_len(unsigned long dr7, int index)
{
    int len = ((dr7 >> (DR7_LEN_SHIFT + (index * 4))) & 3);
    return (len == 2) ? 8 : len + 1;
}

static int hw_breakpoint_insert(CPUX86State *env, int index)
{
    CPUState *cs = CPU(x86_env_get_cpu(env));
    target_ulong dr7 = env->dr[7];
    target_ulong drN = env->dr[index];
    int err = 0;

    switch (hw_breakpoint_type(dr7, index)) {
    case DR7_TYPE_BP_INST:
        if (hw_breakpoint_enabled(dr7, index)) {
            err = cpu_breakpoint_insert(cs, drN, BP_CPU,
                                        &env->cpu_breakpoint[index]);
        }
        break;

    case DR7_TYPE_IO_RW:
        /* Notice when we should enable calls to bpt_io.  */
        return hw_breakpoint_enabled(env->dr[7], index)
               ? HF_IOBPT_MASK : 0;

    case DR7_TYPE_DATA_WR:
        if (hw_breakpoint_enabled(dr7, index)) {
            err = cpu_watchpoint_insert(cs, drN,
                                        hw_breakpoint_len(dr7, index),
                                        BP_CPU | BP_MEM_WRITE,
                                        &env->cpu_watchpoint[index]);
        }
        break;

    case DR7_TYPE_DATA_RW:
        if (hw_breakpoint_enabled(dr7, index)) {
            err = cpu_watchpoint_insert(cs, drN,
                                        hw_breakpoint_len(dr7, index),
                                        BP_CPU | BP_MEM_ACCESS,
                                        &env->cpu_watchpoint[index]);
        }
        break;
    }
    if (err) {
        env->cpu_breakpoint[index] = NULL;
    }
    return 0;
}

static void hw_breakpoint_remove(CPUX86State *env, int index)
{
    CPUState *cs = CPU(x86_env_get_cpu(env));

    switch (hw_breakpoint_type(env->dr[7], index)) {
    case DR7_TYPE_BP_INST:
        if (env->cpu_breakpoint[index]) {
            cpu_breakpoint_remove_by_ref(cs, env->cpu_breakpoint[index]);
            env->cpu_breakpoint[index] = NULL;
        }
        break;

    case DR7_TYPE_DATA_WR:
    case DR7_TYPE_DATA_RW:
        if (env->cpu_breakpoint[index]) {
            cpu_watchpoint_remove_by_ref(cs, env->cpu_watchpoint[index]);
            env->cpu_breakpoint[index] = NULL;
        }
        break;

    case DR7_TYPE_IO_RW:
        /* HF_IOBPT_MASK cleared elsewhere.  */
        break;
    }
}

void cpu_x86_update_dr7(CPUX86State *env, uint32_t new_dr7)
{
    target_ulong old_dr7 = env->dr[7];
    int iobpt = 0;
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
                iobpt |= hw_breakpoint_insert(env, i);
            } else if (hw_breakpoint_type(new_dr7, i) == DR7_TYPE_IO_RW
                       && hw_breakpoint_enabled(new_dr7, i)) {
                iobpt |= HF_IOBPT_MASK;
            }
        }
    } else {
        for (i = 0; i < DR7_MAX_BP; i++) {
            hw_breakpoint_remove(env, i);
        }
        env->dr[7] = new_dr7;
        for (i = 0; i < DR7_MAX_BP; i++) {
            iobpt |= hw_breakpoint_insert(env, i);
        }
    }

    env->hflags = (env->hflags & ~HF_IOBPT_MASK) | iobpt;
}

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
#endif

void helper_single_step(CPUX86State *env)
{
#ifndef CONFIG_USER_ONLY
    check_hw_breakpoints(env, true);
    env->dr[6] |= DR6_BS;
#endif
    raise_exception(env, EXCP01_DB);
}

void helper_set_dr(CPUX86State *env, int reg, target_ulong t0)
{
#ifndef CONFIG_USER_ONLY
    switch (reg) {
    case 0: case 1: case 2: case 3:
        if (hw_breakpoint_enabled(env->dr[7], reg)
            && hw_breakpoint_type(env->dr[7], reg) != DR7_TYPE_IO_RW) {
            hw_breakpoint_remove(env, reg);
            env->dr[reg] = t0;
            hw_breakpoint_insert(env, reg);
        } else {
            env->dr[reg] = t0;
        }
        return;
    case 4:
        if (env->cr[4] & CR4_DE_MASK) {
            break;
        }
        /* fallthru */
    case 6:
        env->dr[6] = t0 | DR6_FIXED_1;
        return;
    case 5:
        if (env->cr[4] & CR4_DE_MASK) {
            break;
        }
        /* fallthru */
    case 7:
        cpu_x86_update_dr7(env, t0);
        return;
    }
    raise_exception_err_ra(env, EXCP06_ILLOP, 0, GETPC());
#endif
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

/* Check if Port I/O is trapped by a breakpoint.  */
void helper_bpt_io(CPUX86State *env, uint32_t port,
                   uint32_t size, target_ulong next_eip)
{
#ifndef CONFIG_USER_ONLY
    target_ulong dr7 = env->dr[7];
    int i, hit = 0;

    for (i = 0; i < DR7_MAX_BP; ++i) {
        if (hw_breakpoint_type(dr7, i) == DR7_TYPE_IO_RW
            && hw_breakpoint_enabled(dr7, i)) {
            int bpt_len = hw_breakpoint_len(dr7, i);
            if (port + size - 1 >= env->dr[i]
                && port <= env->dr[i] + bpt_len - 1) {
                hit |= 1 << i;
            }
        }
    }

    if (hit) {
        env->dr[6] = (env->dr[6] & ~0xf) | hit;
        env->eip = next_eip;
        raise_exception(env, EXCP01_DB);
    }
#endif
}

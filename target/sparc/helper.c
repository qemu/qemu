/*
 *  Misc Sparc helpers
 *
 *  Copyright (c) 2003-2005 Fabrice Bellard
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
#include "exec/exec-all.h"
#include "qemu/host-utils.h"
#include "exec/helper-proto.h"

void cpu_raise_exception_ra(CPUSPARCState *env, int tt, uintptr_t ra)
{
    CPUState *cs = env_cpu(env);

    cs->exception_index = tt;
    cpu_loop_exit_restore(cs, ra);
}

void helper_raise_exception(CPUSPARCState *env, int tt)
{
    CPUState *cs = env_cpu(env);

    cs->exception_index = tt;
    cpu_loop_exit(cs);
}

void helper_debug(CPUSPARCState *env)
{
    CPUState *cs = env_cpu(env);

    cs->exception_index = EXCP_DEBUG;
    cpu_loop_exit(cs);
}

#ifdef TARGET_SPARC64
void helper_tick_set_count(void *opaque, uint64_t count)
{
#if !defined(CONFIG_USER_ONLY)
    cpu_tick_set_count(opaque, count);
#endif
}

uint64_t helper_tick_get_count(CPUSPARCState *env, void *opaque, int mem_idx)
{
#if !defined(CONFIG_USER_ONLY)
    CPUTimer *timer = opaque;

    if (timer->npt && mem_idx < MMU_KERNEL_IDX) {
        cpu_raise_exception_ra(env, TT_PRIV_INSN, GETPC());
    }

    return cpu_tick_get_count(timer);
#else
    /* In user-mode, QEMU_CLOCK_VIRTUAL doesn't exist.
       Just pass through the host cpu clock ticks.  */
    return cpu_get_host_ticks();
#endif
}

void helper_tick_set_limit(void *opaque, uint64_t limit)
{
#if !defined(CONFIG_USER_ONLY)
    cpu_tick_set_limit(opaque, limit);
#endif
}
#endif

static target_ulong do_udiv(CPUSPARCState *env, target_ulong a,
                            target_ulong b, int cc, uintptr_t ra)
{
    int overflow = 0;
    uint64_t x0;
    uint32_t x1;

    x0 = (a & 0xffffffff) | ((int64_t) (env->y) << 32);
    x1 = (b & 0xffffffff);

    if (x1 == 0) {
        cpu_raise_exception_ra(env, TT_DIV_ZERO, ra);
    }

    x0 = x0 / x1;
    if (x0 > UINT32_MAX) {
        x0 = UINT32_MAX;
        overflow = 1;
    }

    if (cc) {
        env->cc_dst = x0;
        env->cc_src2 = overflow;
        env->cc_op = CC_OP_DIV;
    }
    return x0;
}

target_ulong helper_udiv(CPUSPARCState *env, target_ulong a, target_ulong b)
{
    return do_udiv(env, a, b, 0, GETPC());
}

target_ulong helper_udiv_cc(CPUSPARCState *env, target_ulong a, target_ulong b)
{
    return do_udiv(env, a, b, 1, GETPC());
}

static target_ulong do_sdiv(CPUSPARCState *env, target_ulong a,
                            target_ulong b, int cc, uintptr_t ra)
{
    int overflow = 0;
    int64_t x0;
    int32_t x1;

    x0 = (a & 0xffffffff) | ((int64_t) (env->y) << 32);
    x1 = (b & 0xffffffff);

    if (x1 == 0) {
        cpu_raise_exception_ra(env, TT_DIV_ZERO, ra);
    } else if (x1 == -1 && x0 == INT64_MIN) {
        x0 = INT32_MAX;
        overflow = 1;
    } else {
        x0 = x0 / x1;
        if ((int32_t) x0 != x0) {
            x0 = x0 < 0 ? INT32_MIN : INT32_MAX;
            overflow = 1;
        }
    }

    if (cc) {
        env->cc_dst = x0;
        env->cc_src2 = overflow;
        env->cc_op = CC_OP_DIV;
    }
    return x0;
}

target_ulong helper_sdiv(CPUSPARCState *env, target_ulong a, target_ulong b)
{
    return do_sdiv(env, a, b, 0, GETPC());
}

target_ulong helper_sdiv_cc(CPUSPARCState *env, target_ulong a, target_ulong b)
{
    return do_sdiv(env, a, b, 1, GETPC());
}

#ifdef TARGET_SPARC64
int64_t helper_sdivx(CPUSPARCState *env, int64_t a, int64_t b)
{
    if (b == 0) {
        /* Raise divide by zero trap.  */
        cpu_raise_exception_ra(env, TT_DIV_ZERO, GETPC());
    } else if (b == -1) {
        /* Avoid overflow trap with i386 divide insn.  */
        return -a;
    } else {
        return a / b;
    }
}

uint64_t helper_udivx(CPUSPARCState *env, uint64_t a, uint64_t b)
{
    if (b == 0) {
        /* Raise divide by zero trap.  */
        cpu_raise_exception_ra(env, TT_DIV_ZERO, GETPC());
    }
    return a / b;
}
#endif

target_ulong helper_taddcctv(CPUSPARCState *env, target_ulong src1,
                             target_ulong src2)
{
    target_ulong dst;

    /* Tag overflow occurs if either input has bits 0 or 1 set.  */
    if ((src1 | src2) & 3) {
        goto tag_overflow;
    }

    dst = src1 + src2;

    /* Tag overflow occurs if the addition overflows.  */
    if (~(src1 ^ src2) & (src1 ^ dst) & (1u << 31)) {
        goto tag_overflow;
    }

    /* Only modify the CC after any exceptions have been generated.  */
    env->cc_op = CC_OP_TADDTV;
    env->cc_src = src1;
    env->cc_src2 = src2;
    env->cc_dst = dst;
    return dst;

 tag_overflow:
    cpu_raise_exception_ra(env, TT_TOVF, GETPC());
}

target_ulong helper_tsubcctv(CPUSPARCState *env, target_ulong src1,
                             target_ulong src2)
{
    target_ulong dst;

    /* Tag overflow occurs if either input has bits 0 or 1 set.  */
    if ((src1 | src2) & 3) {
        goto tag_overflow;
    }

    dst = src1 - src2;

    /* Tag overflow occurs if the subtraction overflows.  */
    if ((src1 ^ src2) & (src1 ^ dst) & (1u << 31)) {
        goto tag_overflow;
    }

    /* Only modify the CC after any exceptions have been generated.  */
    env->cc_op = CC_OP_TSUBTV;
    env->cc_src = src1;
    env->cc_src2 = src2;
    env->cc_dst = dst;
    return dst;

 tag_overflow:
    cpu_raise_exception_ra(env, TT_TOVF, GETPC());
}

#ifndef TARGET_SPARC64
void helper_power_down(CPUSPARCState *env)
{
    CPUState *cs = env_cpu(env);

    cs->halted = 1;
    cs->exception_index = EXCP_HLT;
    env->pc = env->npc;
    env->npc = env->pc + 4;
    cpu_loop_exit(cs);
}
#endif

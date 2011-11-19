/*
 *  Misc Sparc helpers
 *
 *  Copyright (c) 2003-2005 Fabrice Bellard
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
#include "host-utils.h"
#include "helper.h"
#include "sysemu.h"

void helper_raise_exception(CPUState *env, int tt)
{
    env->exception_index = tt;
    cpu_loop_exit(env);
}

void helper_debug(CPUState *env)
{
    env->exception_index = EXCP_DEBUG;
    cpu_loop_exit(env);
}

#ifdef TARGET_SPARC64
target_ulong helper_popc(target_ulong val)
{
    return ctpop64(val);
}

void helper_tick_set_count(void *opaque, uint64_t count)
{
#if !defined(CONFIG_USER_ONLY)
    cpu_tick_set_count(opaque, count);
#endif
}

uint64_t helper_tick_get_count(void *opaque)
{
#if !defined(CONFIG_USER_ONLY)
    return cpu_tick_get_count(opaque);
#else
    return 0;
#endif
}

void helper_tick_set_limit(void *opaque, uint64_t limit)
{
#if !defined(CONFIG_USER_ONLY)
    cpu_tick_set_limit(opaque, limit);
#endif
}
#endif

static target_ulong helper_udiv_common(CPUState *env, target_ulong a,
                                       target_ulong b, int cc)
{
    int overflow = 0;
    uint64_t x0;
    uint32_t x1;

    x0 = (a & 0xffffffff) | ((int64_t) (env->y) << 32);
    x1 = (b & 0xffffffff);

    if (x1 == 0) {
        helper_raise_exception(env, TT_DIV_ZERO);
    }

    x0 = x0 / x1;
    if (x0 > 0xffffffff) {
        x0 = 0xffffffff;
        overflow = 1;
    }

    if (cc) {
        env->cc_dst = x0;
        env->cc_src2 = overflow;
        env->cc_op = CC_OP_DIV;
    }
    return x0;
}

target_ulong helper_udiv(CPUState *env, target_ulong a, target_ulong b)
{
    return helper_udiv_common(env, a, b, 0);
}

target_ulong helper_udiv_cc(CPUState *env, target_ulong a, target_ulong b)
{
    return helper_udiv_common(env, a, b, 1);
}

static target_ulong helper_sdiv_common(CPUState *env, target_ulong a,
                                       target_ulong b, int cc)
{
    int overflow = 0;
    int64_t x0;
    int32_t x1;

    x0 = (a & 0xffffffff) | ((int64_t) (env->y) << 32);
    x1 = (b & 0xffffffff);

    if (x1 == 0) {
        helper_raise_exception(env, TT_DIV_ZERO);
    }

    x0 = x0 / x1;
    if ((int32_t) x0 != x0) {
        x0 = x0 < 0 ? 0x80000000 : 0x7fffffff;
        overflow = 1;
    }

    if (cc) {
        env->cc_dst = x0;
        env->cc_src2 = overflow;
        env->cc_op = CC_OP_DIV;
    }
    return x0;
}

target_ulong helper_sdiv(CPUState *env, target_ulong a, target_ulong b)
{
    return helper_sdiv_common(env, a, b, 0);
}

target_ulong helper_sdiv_cc(CPUState *env, target_ulong a, target_ulong b)
{
    return helper_sdiv_common(env, a, b, 1);
}

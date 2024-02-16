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
#include "qemu/timer.h"
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

uint64_t helper_udiv(CPUSPARCState *env, target_ulong a, target_ulong b)
{
    uint64_t a64 = (uint32_t)a | ((uint64_t)env->y << 32);
    uint32_t b32 = b;
    uint32_t r;

    if (b32 == 0) {
        cpu_raise_exception_ra(env, TT_DIV_ZERO, GETPC());
    }

    a64 /= b32;
    r = a64;
    if (unlikely(a64 > UINT32_MAX)) {
        return -1; /* r = UINT32_MAX, v = 1 */
    }
    return r;
}

uint64_t helper_sdiv(CPUSPARCState *env, target_ulong a, target_ulong b)
{
    int64_t a64 = (uint32_t)a | ((uint64_t)env->y << 32);
    int32_t b32 = b;
    int32_t r;

    if (b32 == 0) {
        cpu_raise_exception_ra(env, TT_DIV_ZERO, GETPC());
    }

    if (unlikely(a64 == INT64_MIN)) {
        /*
         * Special case INT64_MIN / -1 is required to avoid trap on x86 host.
         * However, with a dividend of INT64_MIN, there is no 32-bit divisor
         * which can yield a 32-bit result:
         *    INT64_MIN / INT32_MIN =  0x1_0000_0000
         *    INT64_MIN / INT32_MAX = -0x1_0000_0002
         * Therefore we know we must overflow and saturate.
         */
        return (uint32_t)(b32 < 0 ? INT32_MAX : INT32_MIN) | (-1ull << 32);
    }

    a64 /= b;
    r = a64;
    if (unlikely(r != a64)) {
        return (uint32_t)(a64 < 0 ? INT32_MIN : INT32_MAX) | (-1ull << 32);
    }
    return (uint32_t)r;
}

target_ulong helper_taddcctv(CPUSPARCState *env, target_ulong src1,
                             target_ulong src2)
{
    target_ulong dst, v;

    /* Tag overflow occurs if either input has bits 0 or 1 set.  */
    if ((src1 | src2) & 3) {
        goto tag_overflow;
    }

    dst = src1 + src2;

    /* Tag overflow occurs if the addition overflows.  */
    v = ~(src1 ^ src2) & (src1 ^ dst);
    if (v & (1u << 31)) {
        goto tag_overflow;
    }

    /* Only modify the CC after any exceptions have been generated.  */
    env->cc_V = v;
    env->cc_N = dst;
    env->icc_Z = dst;
#ifdef TARGET_SPARC64
    env->xcc_Z = dst;
    env->icc_C = dst ^ src1 ^ src2;
    env->xcc_C = dst < src1;
#else
    env->icc_C = dst < src1;
#endif

    return dst;

 tag_overflow:
    cpu_raise_exception_ra(env, TT_TOVF, GETPC());
}

target_ulong helper_tsubcctv(CPUSPARCState *env, target_ulong src1,
                             target_ulong src2)
{
    target_ulong dst, v;

    /* Tag overflow occurs if either input has bits 0 or 1 set.  */
    if ((src1 | src2) & 3) {
        goto tag_overflow;
    }

    dst = src1 - src2;

    /* Tag overflow occurs if the subtraction overflows.  */
    v = (src1 ^ src2) & (src1 ^ dst);
    if (v & (1u << 31)) {
        goto tag_overflow;
    }

    /* Only modify the CC after any exceptions have been generated.  */
    env->cc_V = v;
    env->cc_N = dst;
    env->icc_Z = dst;
#ifdef TARGET_SPARC64
    env->xcc_Z = dst;
    env->icc_C = dst ^ src1 ^ src2;
    env->xcc_C = src1 < src2;
#else
    env->icc_C = src1 < src2;
#endif

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

target_ulong helper_rdasr17(CPUSPARCState *env)
{
    CPUState *cs = env_cpu(env);
    target_ulong val;

    /*
     * TODO: There are many more fields to be filled,
     * some of which are writable.
     */
    val = env->def.nwindows - 1;    /* [4:0]   NWIN   */
    val |= 1 << 8;                  /* [8]      V8    */
    val |= (cs->cpu_index) << 28;   /* [31:28] INDEX  */

    return val;
}
#endif

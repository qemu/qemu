/*
 *  UniCore32 helper routines
 *
 * Copyright (C) 2010-2012 Guan Xuetao
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation, or (at your option) any
 * later version. See the COPYING file in the top-level directory.
 */
#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/helper-proto.h"
#include "exec/cpu_ldst.h"

#define SIGNBIT (uint32_t)0x80000000
#define SIGNBIT64 ((uint64_t)1 << 63)

void HELPER(exception)(CPUUniCore32State *env, uint32_t excp)
{
    CPUState *cs = CPU(uc32_env_get_cpu(env));

    cs->exception_index = excp;
    cpu_loop_exit(cs);
}

static target_ulong asr_read(CPUUniCore32State *env)
{
    int ZF;
    ZF = (env->ZF == 0);
    return env->uncached_asr | (env->NF & 0x80000000) | (ZF << 30) |
        (env->CF << 29) | ((env->VF & 0x80000000) >> 3);
}

target_ulong cpu_asr_read(CPUUniCore32State *env)
{
    return asr_read(env);
}

target_ulong HELPER(asr_read)(CPUUniCore32State *env)
{
    return asr_read(env);
}

static void asr_write(CPUUniCore32State *env, target_ulong val,
                      target_ulong mask)
{
    if (mask & ASR_NZCV) {
        env->ZF = (~val) & ASR_Z;
        env->NF = val;
        env->CF = (val >> 29) & 1;
        env->VF = (val << 3) & 0x80000000;
    }

    if ((env->uncached_asr ^ val) & mask & ASR_M) {
        switch_mode(env, val & ASR_M);
    }
    mask &= ~ASR_NZCV;
    env->uncached_asr = (env->uncached_asr & ~mask) | (val & mask);
}

void cpu_asr_write(CPUUniCore32State *env, target_ulong val, target_ulong mask)
{
    asr_write(env, val, mask);
}

void HELPER(asr_write)(CPUUniCore32State *env, target_ulong val,
                       target_ulong mask)
{
    asr_write(env, val, mask);
}

/* Access to user mode registers from privileged modes.  */
uint32_t HELPER(get_user_reg)(CPUUniCore32State *env, uint32_t regno)
{
    uint32_t val;

    if (regno == 29) {
        val = env->banked_r29[0];
    } else if (regno == 30) {
        val = env->banked_r30[0];
    } else {
        val = env->regs[regno];
    }
    return val;
}

void HELPER(set_user_reg)(CPUUniCore32State *env, uint32_t regno, uint32_t val)
{
    if (regno == 29) {
        env->banked_r29[0] = val;
    } else if (regno == 30) {
        env->banked_r30[0] = val;
    } else {
        env->regs[regno] = val;
    }
}

/* ??? Flag setting arithmetic is awkward because we need to do comparisons.
   The only way to do that in TCG is a conditional branch, which clobbers
   all our temporaries.  For now implement these as helper functions.  */

uint32_t HELPER(add_cc)(CPUUniCore32State *env, uint32_t a, uint32_t b)
{
    uint32_t result;
    result = a + b;
    env->NF = env->ZF = result;
    env->CF = result < a;
    env->VF = (a ^ b ^ -1) & (a ^ result);
    return result;
}

uint32_t HELPER(adc_cc)(CPUUniCore32State *env, uint32_t a, uint32_t b)
{
    uint32_t result;
    if (!env->CF) {
        result = a + b;
        env->CF = result < a;
    } else {
        result = a + b + 1;
        env->CF = result <= a;
    }
    env->VF = (a ^ b ^ -1) & (a ^ result);
    env->NF = env->ZF = result;
    return result;
}

uint32_t HELPER(sub_cc)(CPUUniCore32State *env, uint32_t a, uint32_t b)
{
    uint32_t result;
    result = a - b;
    env->NF = env->ZF = result;
    env->CF = a >= b;
    env->VF = (a ^ b) & (a ^ result);
    return result;
}

uint32_t HELPER(sbc_cc)(CPUUniCore32State *env, uint32_t a, uint32_t b)
{
    uint32_t result;
    if (!env->CF) {
        result = a - b - 1;
        env->CF = a > b;
    } else {
        result = a - b;
        env->CF = a >= b;
    }
    env->VF = (a ^ b) & (a ^ result);
    env->NF = env->ZF = result;
    return result;
}

/* Similarly for variable shift instructions.  */

uint32_t HELPER(shl)(uint32_t x, uint32_t i)
{
    int shift = i & 0xff;
    if (shift >= 32) {
        return 0;
    }
    return x << shift;
}

uint32_t HELPER(shr)(uint32_t x, uint32_t i)
{
    int shift = i & 0xff;
    if (shift >= 32) {
        return 0;
    }
    return (uint32_t)x >> shift;
}

uint32_t HELPER(sar)(uint32_t x, uint32_t i)
{
    int shift = i & 0xff;
    if (shift >= 32) {
        shift = 31;
    }
    return (int32_t)x >> shift;
}

uint32_t HELPER(shl_cc)(CPUUniCore32State *env, uint32_t x, uint32_t i)
{
    int shift = i & 0xff;
    if (shift >= 32) {
        if (shift == 32) {
            env->CF = x & 1;
        } else {
            env->CF = 0;
        }
        return 0;
    } else if (shift != 0) {
        env->CF = (x >> (32 - shift)) & 1;
        return x << shift;
    }
    return x;
}

uint32_t HELPER(shr_cc)(CPUUniCore32State *env, uint32_t x, uint32_t i)
{
    int shift = i & 0xff;
    if (shift >= 32) {
        if (shift == 32) {
            env->CF = (x >> 31) & 1;
        } else {
            env->CF = 0;
        }
        return 0;
    } else if (shift != 0) {
        env->CF = (x >> (shift - 1)) & 1;
        return x >> shift;
    }
    return x;
}

uint32_t HELPER(sar_cc)(CPUUniCore32State *env, uint32_t x, uint32_t i)
{
    int shift = i & 0xff;
    if (shift >= 32) {
        env->CF = (x >> 31) & 1;
        return (int32_t)x >> 31;
    } else if (shift != 0) {
        env->CF = (x >> (shift - 1)) & 1;
        return (int32_t)x >> shift;
    }
    return x;
}

uint32_t HELPER(ror_cc)(CPUUniCore32State *env, uint32_t x, uint32_t i)
{
    int shift1, shift;
    shift1 = i & 0xff;
    shift = shift1 & 0x1f;
    if (shift == 0) {
        if (shift1 != 0) {
            env->CF = (x >> 31) & 1;
        }
        return x;
    } else {
        env->CF = (x >> (shift - 1)) & 1;
        return ((uint32_t)x >> shift) | (x << (32 - shift));
    }
}

#ifndef CONFIG_USER_ONLY
void tlb_fill(CPUState *cs, target_ulong addr, int is_write,
              int mmu_idx, uintptr_t retaddr)
{
    int ret;

    ret = uc32_cpu_handle_mmu_fault(cs, addr, is_write, mmu_idx);
    if (unlikely(ret)) {
        if (retaddr) {
            /* now we have a real cpu fault */
            cpu_restore_state(cs, retaddr);
        }
        cpu_loop_exit(cs);
    }
}
#endif

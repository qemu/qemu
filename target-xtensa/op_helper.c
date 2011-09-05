/*
 * Copyright (c) 2011, Max Filippov, Open Source and Linux Lab.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the Open Source and Linux Lab nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "cpu.h"
#include "dyngen-exec.h"
#include "helpers.h"
#include "host-utils.h"

#define MMUSUFFIX _mmu

#define SHIFT 0
#include "softmmu_template.h"

#define SHIFT 1
#include "softmmu_template.h"

#define SHIFT 2
#include "softmmu_template.h"

#define SHIFT 3
#include "softmmu_template.h"

void tlb_fill(target_ulong addr, int is_write, int mmu_idx, void *retaddr)
{
    tlb_set_page(cpu_single_env,
            addr & ~(TARGET_PAGE_SIZE - 1),
            addr & ~(TARGET_PAGE_SIZE - 1),
            PAGE_READ | PAGE_WRITE | PAGE_EXEC,
            mmu_idx, TARGET_PAGE_SIZE);
}

void HELPER(exception)(uint32_t excp)
{
    env->exception_index = excp;
    cpu_loop_exit(env);
}

void HELPER(exception_cause)(uint32_t pc, uint32_t cause)
{
    uint32_t vector;

    env->pc = pc;
    if (env->sregs[PS] & PS_EXCM) {
        if (env->config->ndepc) {
            env->sregs[DEPC] = pc;
        } else {
            env->sregs[EPC1] = pc;
        }
        vector = EXC_DOUBLE;
    } else {
        env->sregs[EPC1] = pc;
        vector = (env->sregs[PS] & PS_UM) ? EXC_USER : EXC_KERNEL;
    }

    env->sregs[EXCCAUSE] = cause;
    env->sregs[PS] |= PS_EXCM;

    HELPER(exception)(vector);
}

void HELPER(exception_cause_vaddr)(uint32_t pc, uint32_t cause, uint32_t vaddr)
{
    env->sregs[EXCVADDR] = vaddr;
    HELPER(exception_cause)(pc, cause);
}

uint32_t HELPER(nsa)(uint32_t v)
{
    if (v & 0x80000000) {
        v = ~v;
    }
    return v ? clz32(v) - 1 : 31;
}

uint32_t HELPER(nsau)(uint32_t v)
{
    return v ? clz32(v) : 32;
}

static void copy_window_from_phys(CPUState *env,
        uint32_t window, uint32_t phys, uint32_t n)
{
    assert(phys < env->config->nareg);
    if (phys + n <= env->config->nareg) {
        memcpy(env->regs + window, env->phys_regs + phys,
                n * sizeof(uint32_t));
    } else {
        uint32_t n1 = env->config->nareg - phys;
        memcpy(env->regs + window, env->phys_regs + phys,
                n1 * sizeof(uint32_t));
        memcpy(env->regs + window + n1, env->phys_regs,
                (n - n1) * sizeof(uint32_t));
    }
}

static void copy_phys_from_window(CPUState *env,
        uint32_t phys, uint32_t window, uint32_t n)
{
    assert(phys < env->config->nareg);
    if (phys + n <= env->config->nareg) {
        memcpy(env->phys_regs + phys, env->regs + window,
                n * sizeof(uint32_t));
    } else {
        uint32_t n1 = env->config->nareg - phys;
        memcpy(env->phys_regs + phys, env->regs + window,
                n1 * sizeof(uint32_t));
        memcpy(env->phys_regs, env->regs + window + n1,
                (n - n1) * sizeof(uint32_t));
    }
}


static inline unsigned windowbase_bound(unsigned a, const CPUState *env)
{
    return a & (env->config->nareg / 4 - 1);
}

static inline unsigned windowstart_bit(unsigned a, const CPUState *env)
{
    return 1 << windowbase_bound(a, env);
}

void xtensa_sync_window_from_phys(CPUState *env)
{
    copy_window_from_phys(env, 0, env->sregs[WINDOW_BASE] * 4, 16);
}

void xtensa_sync_phys_from_window(CPUState *env)
{
    copy_phys_from_window(env, env->sregs[WINDOW_BASE] * 4, 0, 16);
}

static void rotate_window_abs(uint32_t position)
{
    xtensa_sync_phys_from_window(env);
    env->sregs[WINDOW_BASE] = windowbase_bound(position, env);
    xtensa_sync_window_from_phys(env);
}

static void rotate_window(uint32_t delta)
{
    rotate_window_abs(env->sregs[WINDOW_BASE] + delta);
}

void HELPER(wsr_windowbase)(uint32_t v)
{
    rotate_window_abs(v);
}

void HELPER(entry)(uint32_t pc, uint32_t s, uint32_t imm)
{
    int callinc = (env->sregs[PS] & PS_CALLINC) >> PS_CALLINC_SHIFT;
    if (s > 3 || ((env->sregs[PS] & (PS_WOE | PS_EXCM)) ^ PS_WOE) != 0) {
        qemu_log("Illegal entry instruction(pc = %08x), PS = %08x\n",
                pc, env->sregs[PS]);
        HELPER(exception_cause)(pc, ILLEGAL_INSTRUCTION_CAUSE);
    } else {
        env->regs[(callinc << 2) | (s & 3)] = env->regs[s] - (imm << 3);
        rotate_window(callinc);
        env->sregs[WINDOW_START] |=
            windowstart_bit(env->sregs[WINDOW_BASE], env);
    }
}

void HELPER(window_check)(uint32_t pc, uint32_t w)
{
    uint32_t windowbase = windowbase_bound(env->sregs[WINDOW_BASE], env);
    uint32_t windowstart = env->sregs[WINDOW_START];
    uint32_t m, n;

    if ((env->sregs[PS] & (PS_WOE | PS_EXCM)) ^ PS_WOE) {
        return;
    }

    for (n = 1; ; ++n) {
        if (n > w) {
            return;
        }
        if (windowstart & windowstart_bit(windowbase + n, env)) {
            break;
        }
    }

    m = windowbase_bound(windowbase + n, env);
    rotate_window(n);
    env->sregs[PS] = (env->sregs[PS] & ~PS_OWB) |
        (windowbase << PS_OWB_SHIFT) | PS_EXCM;
    env->sregs[EPC1] = env->pc = pc;

    if (windowstart & windowstart_bit(m + 1, env)) {
        HELPER(exception)(EXC_WINDOW_OVERFLOW4);
    } else if (windowstart & windowstart_bit(m + 2, env)) {
        HELPER(exception)(EXC_WINDOW_OVERFLOW8);
    } else {
        HELPER(exception)(EXC_WINDOW_OVERFLOW12);
    }
}

uint32_t HELPER(retw)(uint32_t pc)
{
    int n = (env->regs[0] >> 30) & 0x3;
    int m = 0;
    uint32_t windowbase = windowbase_bound(env->sregs[WINDOW_BASE], env);
    uint32_t windowstart = env->sregs[WINDOW_START];
    uint32_t ret_pc = 0;

    if (windowstart & windowstart_bit(windowbase - 1, env)) {
        m = 1;
    } else if (windowstart & windowstart_bit(windowbase - 2, env)) {
        m = 2;
    } else if (windowstart & windowstart_bit(windowbase - 3, env)) {
        m = 3;
    }

    if (n == 0 || (m != 0 && m != n) ||
            ((env->sregs[PS] & (PS_WOE | PS_EXCM)) ^ PS_WOE) != 0) {
        qemu_log("Illegal retw instruction(pc = %08x), "
                "PS = %08x, m = %d, n = %d\n",
                pc, env->sregs[PS], m, n);
        HELPER(exception_cause)(pc, ILLEGAL_INSTRUCTION_CAUSE);
    } else {
        int owb = windowbase;

        ret_pc = (pc & 0xc0000000) | (env->regs[0] & 0x3fffffff);

        rotate_window(-n);
        if (windowstart & windowstart_bit(env->sregs[WINDOW_BASE], env)) {
            env->sregs[WINDOW_START] &= ~windowstart_bit(owb, env);
        } else {
            /* window underflow */
            env->sregs[PS] = (env->sregs[PS] & ~PS_OWB) |
                (windowbase << PS_OWB_SHIFT) | PS_EXCM;
            env->sregs[EPC1] = env->pc = pc;

            if (n == 1) {
                HELPER(exception)(EXC_WINDOW_UNDERFLOW4);
            } else if (n == 2) {
                HELPER(exception)(EXC_WINDOW_UNDERFLOW8);
            } else if (n == 3) {
                HELPER(exception)(EXC_WINDOW_UNDERFLOW12);
            }
        }
    }
    return ret_pc;
}

void HELPER(rotw)(uint32_t imm4)
{
    rotate_window(imm4);
}

void HELPER(restore_owb)(void)
{
    rotate_window_abs((env->sregs[PS] & PS_OWB) >> PS_OWB_SHIFT);
}

void HELPER(movsp)(uint32_t pc)
{
    if ((env->sregs[WINDOW_START] &
            (windowstart_bit(env->sregs[WINDOW_BASE] - 3, env) |
             windowstart_bit(env->sregs[WINDOW_BASE] - 2, env) |
             windowstart_bit(env->sregs[WINDOW_BASE] - 1, env))) == 0) {
        HELPER(exception_cause)(pc, ALLOCA_CAUSE);
    }
}

void HELPER(dump_state)(void)
{
    cpu_dump_state(env, stderr, fprintf, 0);
}

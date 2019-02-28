/*
 * Copyright (c) 2011 - 2019, Max Filippov, Open Source and Linux Lab.
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

#include "qemu/osdep.h"
#include "qemu/main-loop.h"
#include "cpu.h"
#include "exec/helper-proto.h"
#include "qemu/host-utils.h"
#include "exec/exec-all.h"

static void copy_window_from_phys(CPUXtensaState *env,
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

static void copy_phys_from_window(CPUXtensaState *env,
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

static inline unsigned windowbase_bound(unsigned a, const CPUXtensaState *env)
{
    return a & (env->config->nareg / 4 - 1);
}

static inline unsigned windowstart_bit(unsigned a, const CPUXtensaState *env)
{
    return 1 << windowbase_bound(a, env);
}

void xtensa_sync_window_from_phys(CPUXtensaState *env)
{
    copy_window_from_phys(env, 0, env->sregs[WINDOW_BASE] * 4, 16);
}

void xtensa_sync_phys_from_window(CPUXtensaState *env)
{
    copy_phys_from_window(env, env->sregs[WINDOW_BASE] * 4, 0, 16);
}

static void xtensa_rotate_window_abs(CPUXtensaState *env, uint32_t position)
{
    xtensa_sync_phys_from_window(env);
    env->sregs[WINDOW_BASE] = windowbase_bound(position, env);
    xtensa_sync_window_from_phys(env);
}

void xtensa_rotate_window(CPUXtensaState *env, uint32_t delta)
{
    xtensa_rotate_window_abs(env, env->sregs[WINDOW_BASE] + delta);
}

void HELPER(sync_windowbase)(CPUXtensaState *env)
{
    xtensa_rotate_window_abs(env, env->windowbase_next);
}

void HELPER(entry)(CPUXtensaState *env, uint32_t pc, uint32_t s, uint32_t imm)
{
    int callinc = (env->sregs[PS] & PS_CALLINC) >> PS_CALLINC_SHIFT;

    env->regs[(callinc << 2) | (s & 3)] = env->regs[s] - imm;
    env->windowbase_next = env->sregs[WINDOW_BASE] + callinc;
    env->sregs[WINDOW_START] |= windowstart_bit(env->windowbase_next, env);
}

void HELPER(window_check)(CPUXtensaState *env, uint32_t pc, uint32_t w)
{
    uint32_t windowbase = windowbase_bound(env->sregs[WINDOW_BASE], env);
    uint32_t windowstart = xtensa_replicate_windowstart(env) >>
        (env->sregs[WINDOW_BASE] + 1);
    uint32_t n = ctz32(windowstart) + 1;

    assert(n <= w);

    xtensa_rotate_window(env, n);
    env->sregs[PS] = (env->sregs[PS] & ~PS_OWB) |
        (windowbase << PS_OWB_SHIFT) | PS_EXCM;
    env->sregs[EPC1] = env->pc = pc;

    switch (ctz32(windowstart >> n)) {
    case 0:
        HELPER(exception)(env, EXC_WINDOW_OVERFLOW4);
        break;
    case 1:
        HELPER(exception)(env, EXC_WINDOW_OVERFLOW8);
        break;
    default:
        HELPER(exception)(env, EXC_WINDOW_OVERFLOW12);
        break;
    }
}

void HELPER(test_ill_retw)(CPUXtensaState *env, uint32_t pc)
{
    int n = (env->regs[0] >> 30) & 0x3;
    int m = 0;
    uint32_t windowbase = windowbase_bound(env->sregs[WINDOW_BASE], env);
    uint32_t windowstart = env->sregs[WINDOW_START];

    if (windowstart & windowstart_bit(windowbase - 1, env)) {
        m = 1;
    } else if (windowstart & windowstart_bit(windowbase - 2, env)) {
        m = 2;
    } else if (windowstart & windowstart_bit(windowbase - 3, env)) {
        m = 3;
    }

    if (n == 0 || (m != 0 && m != n)) {
        qemu_log_mask(LOG_GUEST_ERROR, "Illegal retw instruction(pc = %08x), "
                      "PS = %08x, m = %d, n = %d\n",
                      pc, env->sregs[PS], m, n);
        HELPER(exception_cause)(env, pc, ILLEGAL_INSTRUCTION_CAUSE);
    }
}

void HELPER(test_underflow_retw)(CPUXtensaState *env, uint32_t pc)
{
    int n = (env->regs[0] >> 30) & 0x3;

    if (!(env->sregs[WINDOW_START] &
          windowstart_bit(env->sregs[WINDOW_BASE] - n, env))) {
        uint32_t windowbase = windowbase_bound(env->sregs[WINDOW_BASE], env);

        xtensa_rotate_window(env, -n);
        /* window underflow */
        env->sregs[PS] = (env->sregs[PS] & ~PS_OWB) |
            (windowbase << PS_OWB_SHIFT) | PS_EXCM;
        env->sregs[EPC1] = env->pc = pc;

        if (n == 1) {
            HELPER(exception)(env, EXC_WINDOW_UNDERFLOW4);
        } else if (n == 2) {
            HELPER(exception)(env, EXC_WINDOW_UNDERFLOW8);
        } else if (n == 3) {
            HELPER(exception)(env, EXC_WINDOW_UNDERFLOW12);
        }
    }
}

void HELPER(retw)(CPUXtensaState *env, uint32_t a0)
{
    int n = (a0 >> 30) & 0x3;

    xtensa_rotate_window(env, -n);
}

void xtensa_restore_owb(CPUXtensaState *env)
{
    xtensa_rotate_window_abs(env, (env->sregs[PS] & PS_OWB) >> PS_OWB_SHIFT);
}

void HELPER(restore_owb)(CPUXtensaState *env)
{
    xtensa_restore_owb(env);
}

void HELPER(movsp)(CPUXtensaState *env, uint32_t pc)
{
    if ((env->sregs[WINDOW_START] &
         (windowstart_bit(env->sregs[WINDOW_BASE] - 3, env) |
          windowstart_bit(env->sregs[WINDOW_BASE] - 2, env) |
          windowstart_bit(env->sregs[WINDOW_BASE] - 1, env))) == 0) {
        HELPER(exception_cause)(env, pc, ALLOCA_CAUSE);
    }
}

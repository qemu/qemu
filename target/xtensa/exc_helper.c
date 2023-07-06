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
#include "qemu/log.h"
#include "qemu/main-loop.h"
#include "cpu.h"
#include "exec/helper-proto.h"
#include "qemu/host-utils.h"
#include "exec/exec-all.h"

void HELPER(exception)(CPUXtensaState *env, uint32_t excp)
{
    CPUState *cs = env_cpu(env);

    cs->exception_index = excp;
    if (excp == EXCP_YIELD) {
        env->yield_needed = 0;
    }
    cpu_loop_exit(cs);
}

void HELPER(exception_cause)(CPUXtensaState *env, uint32_t pc, uint32_t cause)
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

    HELPER(exception)(env, vector);
}

void HELPER(exception_cause_vaddr)(CPUXtensaState *env,
                                   uint32_t pc, uint32_t cause, uint32_t vaddr)
{
    env->sregs[EXCVADDR] = vaddr;
    HELPER(exception_cause)(env, pc, cause);
}

void debug_exception_env(CPUXtensaState *env, uint32_t cause)
{
    if (xtensa_get_cintlevel(env) < env->config->debug_level) {
        HELPER(debug_exception)(env, env->pc, cause);
    }
}

void HELPER(debug_exception)(CPUXtensaState *env, uint32_t pc, uint32_t cause)
{
    unsigned level = env->config->debug_level;

    env->pc = pc;
    env->sregs[DEBUGCAUSE] = cause;
    env->sregs[EPC1 + level - 1] = pc;
    env->sregs[EPS2 + level - 2] = env->sregs[PS];
    env->sregs[PS] = (env->sregs[PS] & ~PS_INTLEVEL) | PS_EXCM |
        (level << PS_INTLEVEL_SHIFT);
    HELPER(exception)(env, EXC_DEBUG);
}

#ifndef CONFIG_USER_ONLY

void HELPER(waiti)(CPUXtensaState *env, uint32_t pc, uint32_t intlevel)
{
    CPUState *cpu = env_cpu(env);

    env->pc = pc;
    env->sregs[PS] = (env->sregs[PS] & ~PS_INTLEVEL) |
        (intlevel << PS_INTLEVEL_SHIFT);

    qemu_mutex_lock_iothread();
    check_interrupts(env);
    qemu_mutex_unlock_iothread();

    if (env->pending_irq_level) {
        cpu_loop_exit(cpu);
        return;
    }

    cpu->halted = 1;
    HELPER(exception)(env, EXCP_HLT);
}

void HELPER(check_interrupts)(CPUXtensaState *env)
{
    qemu_mutex_lock_iothread();
    check_interrupts(env);
    qemu_mutex_unlock_iothread();
}

void HELPER(intset)(CPUXtensaState *env, uint32_t v)
{
    qatomic_or(&env->sregs[INTSET],
              v & env->config->inttype_mask[INTTYPE_SOFTWARE]);
}

static void intclear(CPUXtensaState *env, uint32_t v)
{
    qatomic_and(&env->sregs[INTSET], ~v);
}

void HELPER(intclear)(CPUXtensaState *env, uint32_t v)
{
    intclear(env, v & (env->config->inttype_mask[INTTYPE_SOFTWARE] |
                       env->config->inttype_mask[INTTYPE_EDGE]));
}

static uint32_t relocated_vector(CPUXtensaState *env, uint32_t vector)
{
    if (xtensa_option_enabled(env->config,
                              XTENSA_OPTION_RELOCATABLE_VECTOR)) {
        return vector - env->config->vecbase + env->sregs[VECBASE];
    } else {
        return vector;
    }
}

/*!
 * Handle penging IRQ.
 * For the high priority interrupt jump to the corresponding interrupt vector.
 * For the level-1 interrupt convert it to either user, kernel or double
 * exception with the 'level-1 interrupt' exception cause.
 */
static void handle_interrupt(CPUXtensaState *env)
{
    int level = env->pending_irq_level;

    if ((level > xtensa_get_cintlevel(env) &&
         level <= env->config->nlevel &&
         (env->config->level_mask[level] &
          env->sregs[INTSET] & env->sregs[INTENABLE])) ||
        level == env->config->nmi_level) {
        CPUState *cs = env_cpu(env);

        if (level > 1) {
            /* env->config->nlevel check should have ensured this */
            assert(level < sizeof(env->config->interrupt_vector));

            env->sregs[EPC1 + level - 1] = env->pc;
            env->sregs[EPS2 + level - 2] = env->sregs[PS];
            env->sregs[PS] =
                (env->sregs[PS] & ~PS_INTLEVEL) | level | PS_EXCM;
            env->pc = relocated_vector(env,
                                       env->config->interrupt_vector[level]);
            if (level == env->config->nmi_level) {
                intclear(env, env->config->inttype_mask[INTTYPE_NMI]);
            }
        } else {
            env->sregs[EXCCAUSE] = LEVEL1_INTERRUPT_CAUSE;

            if (env->sregs[PS] & PS_EXCM) {
                if (env->config->ndepc) {
                    env->sregs[DEPC] = env->pc;
                } else {
                    env->sregs[EPC1] = env->pc;
                }
                cs->exception_index = EXC_DOUBLE;
            } else {
                env->sregs[EPC1] = env->pc;
                cs->exception_index =
                    (env->sregs[PS] & PS_UM) ? EXC_USER : EXC_KERNEL;
            }
            env->sregs[PS] |= PS_EXCM;
        }
    }
}

/* Called from cpu_handle_interrupt with BQL held */
void xtensa_cpu_do_interrupt(CPUState *cs)
{
    XtensaCPU *cpu = XTENSA_CPU(cs);
    CPUXtensaState *env = &cpu->env;

    if (cs->exception_index == EXC_IRQ) {
        qemu_log_mask(CPU_LOG_INT,
                      "%s(EXC_IRQ) level = %d, cintlevel = %d, "
                      "pc = %08x, a0 = %08x, ps = %08x, "
                      "intset = %08x, intenable = %08x, "
                      "ccount = %08x\n",
                      __func__, env->pending_irq_level,
                      xtensa_get_cintlevel(env),
                      env->pc, env->regs[0], env->sregs[PS],
                      env->sregs[INTSET], env->sregs[INTENABLE],
                      env->sregs[CCOUNT]);
        handle_interrupt(env);
    }

    switch (cs->exception_index) {
    case EXC_WINDOW_OVERFLOW4:
    case EXC_WINDOW_UNDERFLOW4:
    case EXC_WINDOW_OVERFLOW8:
    case EXC_WINDOW_UNDERFLOW8:
    case EXC_WINDOW_OVERFLOW12:
    case EXC_WINDOW_UNDERFLOW12:
    case EXC_KERNEL:
    case EXC_USER:
    case EXC_DOUBLE:
    case EXC_DEBUG:
        qemu_log_mask(CPU_LOG_INT, "%s(%d) "
                      "pc = %08x, a0 = %08x, ps = %08x, ccount = %08x\n",
                      __func__, cs->exception_index,
                      env->pc, env->regs[0], env->sregs[PS],
                      env->sregs[CCOUNT]);
        if (env->config->exception_vector[cs->exception_index]) {
            uint32_t vector;

            vector = env->config->exception_vector[cs->exception_index];
            env->pc = relocated_vector(env, vector);
        } else {
            qemu_log_mask(CPU_LOG_INT,
                          "%s(pc = %08x) bad exception_index: %d\n",
                          __func__, env->pc, cs->exception_index);
        }
        break;

    case EXC_IRQ:
        break;

    default:
        qemu_log("%s(pc = %08x) unknown exception_index: %d\n",
                 __func__, env->pc, cs->exception_index);
        break;
    }
    check_interrupts(env);
}

bool xtensa_cpu_exec_interrupt(CPUState *cs, int interrupt_request)
{
    if (interrupt_request & CPU_INTERRUPT_HARD) {
        cs->exception_index = EXC_IRQ;
        xtensa_cpu_do_interrupt(cs);
        return true;
    }
    return false;
}

#endif /* !CONFIG_USER_ONLY */

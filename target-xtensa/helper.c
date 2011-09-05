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
#include "exec-all.h"
#include "gdbstub.h"
#include "qemu-common.h"
#include "host-utils.h"
#if !defined(CONFIG_USER_ONLY)
#include "hw/loader.h"
#endif

void cpu_reset(CPUXtensaState *env)
{
    env->exception_taken = 0;
    env->pc = env->config->exception_vector[EXC_RESET];
    env->sregs[LITBASE] &= ~1;
    env->sregs[PS] = xtensa_option_enabled(env->config,
            XTENSA_OPTION_INTERRUPT) ? 0x1f : 0x10;
    env->sregs[VECBASE] = env->config->vecbase;

    env->pending_irq_level = 0;
}

static const XtensaConfig core_config[] = {
    {
        .name = "sample-xtensa-core",
        .options = -1 ^
            (XTENSA_OPTION_BIT(XTENSA_OPTION_HW_ALIGNMENT) |
             XTENSA_OPTION_BIT(XTENSA_OPTION_MMU)),
        .nareg = 64,
        .ndepc = 1,
        .excm_level = 16,
        .vecbase = 0x5fff8400,
        .exception_vector = {
            [EXC_RESET] = 0x5fff8000,
            [EXC_WINDOW_OVERFLOW4] = 0x5fff8400,
            [EXC_WINDOW_UNDERFLOW4] = 0x5fff8440,
            [EXC_WINDOW_OVERFLOW8] = 0x5fff8480,
            [EXC_WINDOW_UNDERFLOW8] = 0x5fff84c0,
            [EXC_WINDOW_OVERFLOW12] = 0x5fff8500,
            [EXC_WINDOW_UNDERFLOW12] = 0x5fff8540,
            [EXC_KERNEL] = 0x5fff861c,
            [EXC_USER] = 0x5fff863c,
            [EXC_DOUBLE] = 0x5fff865c,
        },
        .ninterrupt = 13,
        .nlevel = 6,
        .interrupt_vector = {
            0,
            0,
            0x5fff857c,
            0x5fff859c,
            0x5fff85bc,
            0x5fff85dc,
            0x5fff85fc,
        },
        .level_mask = {
            [4] = 1,
        },
        .interrupt = {
            [0] = {
                .level = 4,
                .inttype = INTTYPE_TIMER,
            },
        },
        .nccompare = 1,
        .timerint = {
            [0] = 0,
        },
        .clock_freq_khz = 912000,
    },
};

CPUXtensaState *cpu_xtensa_init(const char *cpu_model)
{
    static int tcg_inited;
    CPUXtensaState *env;
    const XtensaConfig *config = NULL;
    int i;

    for (i = 0; i < ARRAY_SIZE(core_config); ++i)
        if (strcmp(core_config[i].name, cpu_model) == 0) {
            config = core_config + i;
            break;
        }

    if (config == NULL) {
        return NULL;
    }

    env = g_malloc0(sizeof(*env));
    env->config = config;
    cpu_exec_init(env);

    if (!tcg_inited) {
        tcg_inited = 1;
        xtensa_translate_init();
    }

    xtensa_irq_init(env);
    qemu_init_vcpu(env);
    return env;
}


void xtensa_cpu_list(FILE *f, fprintf_function cpu_fprintf)
{
    int i;
    cpu_fprintf(f, "Available CPUs:\n");
    for (i = 0; i < ARRAY_SIZE(core_config); ++i) {
        cpu_fprintf(f, "  %s\n", core_config[i].name);
    }
}

target_phys_addr_t cpu_get_phys_page_debug(CPUState *env, target_ulong addr)
{
    return addr;
}

static uint32_t relocated_vector(CPUState *env, uint32_t vector)
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
static void handle_interrupt(CPUState *env)
{
    int level = env->pending_irq_level;

    if (level > xtensa_get_cintlevel(env) &&
            level <= env->config->nlevel &&
            (env->config->level_mask[level] &
             env->sregs[INTSET] &
             env->sregs[INTENABLE])) {
        if (level > 1) {
            env->sregs[EPC1 + level - 1] = env->pc;
            env->sregs[EPS2 + level - 2] = env->sregs[PS];
            env->sregs[PS] =
                (env->sregs[PS] & ~PS_INTLEVEL) | level | PS_EXCM;
            env->pc = relocated_vector(env,
                    env->config->interrupt_vector[level]);
        } else {
            env->sregs[EXCCAUSE] = LEVEL1_INTERRUPT_CAUSE;

            if (env->sregs[PS] & PS_EXCM) {
                if (env->config->ndepc) {
                    env->sregs[DEPC] = env->pc;
                } else {
                    env->sregs[EPC1] = env->pc;
                }
                env->exception_index = EXC_DOUBLE;
            } else {
                env->sregs[EPC1] = env->pc;
                env->exception_index =
                    (env->sregs[PS] & PS_UM) ? EXC_USER : EXC_KERNEL;
            }
            env->sregs[PS] |= PS_EXCM;
        }
        env->exception_taken = 1;
    }
}

void do_interrupt(CPUState *env)
{
    if (env->exception_index == EXC_IRQ) {
        qemu_log_mask(CPU_LOG_INT,
                "%s(EXC_IRQ) level = %d, cintlevel = %d, "
                "pc = %08x, a0 = %08x, ps = %08x, "
                "intset = %08x, intenable = %08x, "
                "ccount = %08x\n",
                __func__, env->pending_irq_level, xtensa_get_cintlevel(env),
                env->pc, env->regs[0], env->sregs[PS],
                env->sregs[INTSET], env->sregs[INTENABLE],
                env->sregs[CCOUNT]);
        handle_interrupt(env);
    }

    switch (env->exception_index) {
    case EXC_WINDOW_OVERFLOW4:
    case EXC_WINDOW_UNDERFLOW4:
    case EXC_WINDOW_OVERFLOW8:
    case EXC_WINDOW_UNDERFLOW8:
    case EXC_WINDOW_OVERFLOW12:
    case EXC_WINDOW_UNDERFLOW12:
    case EXC_KERNEL:
    case EXC_USER:
    case EXC_DOUBLE:
        qemu_log_mask(CPU_LOG_INT, "%s(%d) "
                "pc = %08x, a0 = %08x, ps = %08x, ccount = %08x\n",
                __func__, env->exception_index,
                env->pc, env->regs[0], env->sregs[PS], env->sregs[CCOUNT]);
        if (env->config->exception_vector[env->exception_index]) {
            env->pc = relocated_vector(env,
                    env->config->exception_vector[env->exception_index]);
            env->exception_taken = 1;
        } else {
            qemu_log("%s(pc = %08x) bad exception_index: %d\n",
                    __func__, env->pc, env->exception_index);
        }
        break;

    case EXC_IRQ:
        break;

    default:
        qemu_log("%s(pc = %08x) unknown exception_index: %d\n",
                __func__, env->pc, env->exception_index);
        break;
    }
    check_interrupts(env);
}

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
    env->sregs[PS] = 0x1f;
}

static const XtensaConfig core_config[] = {
    {
        .name = "sample-xtensa-core",
        .options = -1,
        .nareg = 64,
        .ndepc = 1,
        .excm_level = 16,
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

void do_interrupt(CPUState *env)
{
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
        if (env->config->exception_vector[env->exception_index]) {
            env->pc = env->config->exception_vector[env->exception_index];
            env->exception_taken = 1;
        } else {
            qemu_log("%s(pc = %08x) bad exception_index: %d\n",
                    __func__, env->pc, env->exception_index);
        }
        break;

    }
}

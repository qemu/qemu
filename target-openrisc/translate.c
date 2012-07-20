/*
 * OpenRISC translation
 *
 * Copyright (c) 2011-2012 Jia Liu <proljc@gmail.com>
 *                         Feng Gao <gf91597@gmail.com>
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
#include "exec-all.h"
#include "disas.h"
#include "tcg-op.h"
#include "qemu-common.h"
#include "qemu-log.h"
#include "config.h"

#define OPENRISC_DISAS

#ifdef OPENRISC_DISAS
#  define LOG_DIS(...) qemu_log_mask(CPU_LOG_TB_IN_ASM, ## __VA_ARGS__)
#else
#  define LOG_DIS(...) do { } while (0)
#endif

void openrisc_translate_init(void)
{
}

static inline void gen_intermediate_code_internal(OpenRISCCPU *cpu,
                                                  TranslationBlock *tb,
                                                  int search_pc)
{
}

void gen_intermediate_code(CPUOpenRISCState *env, struct TranslationBlock *tb)
{
    gen_intermediate_code_internal(openrisc_env_get_cpu(env), tb, 0);
}

void gen_intermediate_code_pc(CPUOpenRISCState *env,
                              struct TranslationBlock *tb)
{
    gen_intermediate_code_internal(openrisc_env_get_cpu(env), tb, 1);
}

void cpu_dump_state(CPUOpenRISCState *env, FILE *f,
                    fprintf_function cpu_fprintf,
                    int flags)
{
    int i;
    uint32_t *regs = env->gpr;
    cpu_fprintf(f, "PC=%08x\n", env->pc);
    for (i = 0; i < 32; ++i) {
        cpu_fprintf(f, "R%02d=%08x%c", i, regs[i],
                    (i % 4) == 3 ? '\n' : ' ');
    }
}

void restore_state_to_opc(CPUOpenRISCState *env, TranslationBlock *tb,
                          int pc_pos)
{
    env->pc = gen_opc_pc[pc_pos];
}

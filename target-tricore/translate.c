/*
 *  TriCore emulation for qemu: main translation routines.
 *
 *  Copyright (c) 2013-2014 Bastian Koppelmann C-Lab/University Paderborn
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
#include "disas/disas.h"
#include "tcg-op.h"
#include "exec/cpu_ldst.h"

#include "exec/helper-proto.h"
#include "exec/helper-gen.h"


static const char *regnames_a[] = {
      "a0"  , "a1"  , "a2"  , "a3" , "a4"  , "a5" ,
      "a6"  , "a7"  , "a8"  , "a9" , "sp" , "a11" ,
      "a12" , "a13" , "a14" , "a15",
    };

static const char *regnames_d[] = {
      "d0"  , "d1"  , "d2"  , "d3" , "d4"  , "d5"  ,
      "d6"  , "d7"  , "d8"  , "d9" , "d10" , "d11" ,
      "d12" , "d13" , "d14" , "d15",
    };

void tricore_cpu_dump_state(CPUState *cs, FILE *f,
                            fprintf_function cpu_fprintf, int flags)
{
    TriCoreCPU *cpu = TRICORE_CPU(cs);
    CPUTriCoreState *env = &cpu->env;
    int i;

    cpu_fprintf(f, "PC=%08x\n", env->PC);
    for (i = 0; i < 16; ++i) {
        if ((i & 3) == 0) {
            cpu_fprintf(f, "GPR A%02d:", i);
        }
        cpu_fprintf(f, " %s " TARGET_FMT_lx, regnames_a[i], env->gpr_a[i]);
    }
    for (i = 0; i < 16; ++i) {
        if ((i & 3) == 0) {
            cpu_fprintf(f, "GPR D%02d:", i);
        }
        cpu_fprintf(f, " %s " TARGET_FMT_lx, regnames_d[i], env->gpr_d[i]);
    }

}

static inline void
gen_intermediate_code_internal(TriCoreCPU *cpu, struct TranslationBlock *tb,
                              int search_pc)
{
}

void
gen_intermediate_code(CPUTriCoreState *env, struct TranslationBlock *tb)
{
    gen_intermediate_code_internal(tricore_env_get_cpu(env), tb, false);
}

void
gen_intermediate_code_pc(CPUTriCoreState *env, struct TranslationBlock *tb)
{
    gen_intermediate_code_internal(tricore_env_get_cpu(env), tb, true);
}

void
restore_state_to_opc(CPUTriCoreState *env, TranslationBlock *tb, int pc_pos)
{
    env->PC = tcg_ctx.gen_opc_pc[pc_pos];
}
/*
 *
 * Initialization
 *
 */

void cpu_state_reset(CPUTriCoreState *env)
{
}

void tricore_tcg_init(void)
{
}

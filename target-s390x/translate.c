/*
 *  S/390 translation
 *
 *  Copyright (c) 2009 Ulrich Hecht
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
#include "qemu-log.h"

void cpu_dump_state(CPUState *env, FILE *f, fprintf_function cpu_fprintf,
                    int flags)
{
    int i;
    for (i = 0; i < 16; i++) {
        cpu_fprintf(f, "R%02d=%016lx", i, env->regs[i]);
        if ((i % 4) == 3) {
            cpu_fprintf(f, "\n");
        } else {
            cpu_fprintf(f, " ");
        }
    }
    for (i = 0; i < 16; i++) {
        cpu_fprintf(f, "F%02d=%016lx", i, (long)env->fregs[i].i);
        if ((i % 4) == 3) {
            cpu_fprintf(f, "\n");
        } else {
            cpu_fprintf(f, " ");
        }
    }
    cpu_fprintf(f, "PSW=mask %016lx addr %016lx cc %02x\n", env->psw.mask, env->psw.addr, env->cc);
}

void gen_intermediate_code (CPUState *env, struct TranslationBlock *tb)
{
}

void gen_intermediate_code_pc (CPUState *env, struct TranslationBlock *tb)
{
}

void gen_pc_load(CPUState *env, TranslationBlock *tb,
                unsigned long searched_pc, int pc_pos, void *puc)
{
    env->psw.addr = gen_opc_pc[pc_pos];
}

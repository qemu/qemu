/*
 * S/390 CPU dump to FILE
 *
 *  Copyright (c) 2009 Ulrich Hecht
 *  Copyright (c) 2011 Alexander Graf
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
 *
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "s390x-internal.h"
#include "qemu/qemu-print.h"
#include "system/tcg.h"

void s390_cpu_dump_state(CPUState *cs, FILE *f, int flags)
{
    CPUS390XState *env = cpu_env(cs);
    int i;

    qemu_fprintf(f, "PSW=mask %016" PRIx64 " addr %016" PRIx64,
                 s390_cpu_get_psw_mask(env), env->psw.addr);
    if (!tcg_enabled()) {
        qemu_fprintf(f, "\n");
    } else if (env->cc_op > 3) {
        qemu_fprintf(f, " cc %15s\n", cc_name(env->cc_op));
    } else {
        qemu_fprintf(f, " cc %02x\n", env->cc_op);
    }

    for (i = 0; i < 16; i++) {
        qemu_fprintf(f, "R%02d=%016" PRIx64, i, env->regs[i]);
        if ((i % 4) == 3) {
            qemu_fprintf(f, "\n");
        } else {
            qemu_fprintf(f, " ");
        }
    }

    if (flags & CPU_DUMP_FPU) {
        if (s390_has_feat(S390_FEAT_VECTOR)) {
            for (i = 0; i < 32; i++) {
                qemu_fprintf(f, "V%02d=%016" PRIx64 "%016" PRIx64 "%c",
                             i, env->vregs[i][0], env->vregs[i][1],
                             i % 2 ? '\n' : ' ');
            }
        } else {
            for (i = 0; i < 16; i++) {
                qemu_fprintf(f, "F%02d=%016" PRIx64 "%c",
                             i, *get_freg(env, i),
                             (i % 4) == 3 ? '\n' : ' ');
            }
        }
    }

#ifndef CONFIG_USER_ONLY
    for (i = 0; i < 16; i++) {
        qemu_fprintf(f, "C%02d=%016" PRIx64, i, env->cregs[i]);
        if ((i % 4) == 3) {
            qemu_fprintf(f, "\n");
        } else {
            qemu_fprintf(f, " ");
        }
    }
#endif

#ifdef DEBUG_INLINE_BRANCHES
    for (i = 0; i < CC_OP_MAX; i++) {
        qemu_fprintf(f, "  %15s = %10ld\t%10ld\n", cc_name(i),
                     inline_branch_miss[i], inline_branch_hit[i]);
    }
#endif

    qemu_fprintf(f, "\n");
}

const char *cc_name(enum cc_op cc_op)
{
    static const char * const cc_names[] = {
        [CC_OP_CONST0]    = "CC_OP_CONST0",
        [CC_OP_CONST1]    = "CC_OP_CONST1",
        [CC_OP_CONST2]    = "CC_OP_CONST2",
        [CC_OP_CONST3]    = "CC_OP_CONST3",
        [CC_OP_DYNAMIC]   = "CC_OP_DYNAMIC",
        [CC_OP_STATIC]    = "CC_OP_STATIC",
        [CC_OP_NZ]        = "CC_OP_NZ",
        [CC_OP_ADDU]      = "CC_OP_ADDU",
        [CC_OP_SUBU]      = "CC_OP_SUBU",
        [CC_OP_LTGT_32]   = "CC_OP_LTGT_32",
        [CC_OP_LTGT_64]   = "CC_OP_LTGT_64",
        [CC_OP_LTUGTU_32] = "CC_OP_LTUGTU_32",
        [CC_OP_LTUGTU_64] = "CC_OP_LTUGTU_64",
        [CC_OP_LTGT0_32]  = "CC_OP_LTGT0_32",
        [CC_OP_LTGT0_64]  = "CC_OP_LTGT0_64",
        [CC_OP_ADD_64]    = "CC_OP_ADD_64",
        [CC_OP_SUB_64]    = "CC_OP_SUB_64",
        [CC_OP_ABS_64]    = "CC_OP_ABS_64",
        [CC_OP_NABS_64]   = "CC_OP_NABS_64",
        [CC_OP_ADD_32]    = "CC_OP_ADD_32",
        [CC_OP_SUB_32]    = "CC_OP_SUB_32",
        [CC_OP_ABS_32]    = "CC_OP_ABS_32",
        [CC_OP_NABS_32]   = "CC_OP_NABS_32",
        [CC_OP_COMP_32]   = "CC_OP_COMP_32",
        [CC_OP_COMP_64]   = "CC_OP_COMP_64",
        [CC_OP_TM_32]     = "CC_OP_TM_32",
        [CC_OP_TM_64]     = "CC_OP_TM_64",
        [CC_OP_NZ_F32]    = "CC_OP_NZ_F32",
        [CC_OP_NZ_F64]    = "CC_OP_NZ_F64",
        [CC_OP_NZ_F128]   = "CC_OP_NZ_F128",
        [CC_OP_ICM]       = "CC_OP_ICM",
        [CC_OP_SLA]       = "CC_OP_SLA",
        [CC_OP_FLOGR]     = "CC_OP_FLOGR",
        [CC_OP_LCBB]      = "CC_OP_LCBB",
        [CC_OP_VC]        = "CC_OP_VC",
        [CC_OP_MULS_32]   = "CC_OP_MULS_32",
        [CC_OP_MULS_64]   = "CC_OP_MULS_64",
    };

    return cc_names[cc_op];
}

/*
 *  FreeBSD arm64 register structures
 *
 *  Copyright (c) 2015 Stacey Son
 *  All rights reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef TARGET_ARCH_REG_H
#define TARGET_ARCH_REG_H

/* See sys/arm64/include/reg.h */
typedef struct target_reg {
    uint64_t        x[30];
    uint64_t        lr;
    uint64_t        sp;
    uint64_t        elr;
    uint64_t        spsr;
} target_reg_t;

typedef struct target_fpreg {
    Int128          fp_q[32];
    uint32_t        fp_sr;
    uint32_t        fp_cr;
} target_fpreg_t;

#define tswapreg(ptr)   tswapal(ptr)

static inline void target_copy_regs(target_reg_t *regs, CPUARMState *env)
{
    int i;

    for (i = 0; i < 30; i++) {
        regs->x[i] = tswapreg(env->xregs[i]);
    }
    regs->lr = tswapreg(env->xregs[30]);
    regs->sp = tswapreg(env->xregs[31]);
    regs->elr = tswapreg(env->pc);
    regs->spsr = tswapreg(pstate_read(env));
}

#undef tswapreg

#endif /* TARGET_ARCH_REG_H */

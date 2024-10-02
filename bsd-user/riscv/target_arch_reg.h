/*
 *  RISC-V register structures
 *
 *  Copyright (c) 2019 Mark Corbin
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

/* Compare with riscv/include/reg.h */
typedef struct target_reg {
    uint64_t ra;            /* return address */
    uint64_t sp;            /* stack pointer */
    uint64_t gp;            /* global pointer */
    uint64_t tp;            /* thread pointer */
    uint64_t t[7];          /* temporaries */
    uint64_t s[12];         /* saved registers */
    uint64_t a[8];          /* function arguments */
    uint64_t sepc;          /* exception program counter */
    uint64_t sstatus;       /* status register */
} target_reg_t;

typedef struct target_fpreg {
    uint64_t        fp_x[32][2];    /* Floating point registers */
    uint64_t        fp_fcsr;        /* Floating point control reg */
} target_fpreg_t;

#define tswapreg(ptr)   tswapal(ptr)

/* Compare with struct trapframe in riscv/include/frame.h */
static inline void target_copy_regs(target_reg_t *regs,
                                    const CPURISCVState *env)
{

    regs->ra = tswapreg(env->gpr[1]);
    regs->sp = tswapreg(env->gpr[2]);
    regs->gp = tswapreg(env->gpr[3]);
    regs->tp = tswapreg(env->gpr[4]);

    regs->t[0] = tswapreg(env->gpr[5]);
    regs->t[1] = tswapreg(env->gpr[6]);
    regs->t[2] = tswapreg(env->gpr[7]);
    regs->t[3] = tswapreg(env->gpr[28]);
    regs->t[4] = tswapreg(env->gpr[29]);
    regs->t[5] = tswapreg(env->gpr[30]);
    regs->t[6] = tswapreg(env->gpr[31]);

    regs->s[0] = tswapreg(env->gpr[8]);
    regs->s[1] = tswapreg(env->gpr[9]);
    regs->s[2] = tswapreg(env->gpr[18]);
    regs->s[3] = tswapreg(env->gpr[19]);
    regs->s[4] = tswapreg(env->gpr[20]);
    regs->s[5] = tswapreg(env->gpr[21]);
    regs->s[6] = tswapreg(env->gpr[22]);
    regs->s[7] = tswapreg(env->gpr[23]);
    regs->s[8] = tswapreg(env->gpr[24]);
    regs->s[9] = tswapreg(env->gpr[25]);
    regs->s[10] = tswapreg(env->gpr[26]);
    regs->s[11] = tswapreg(env->gpr[27]);

    regs->a[0] = tswapreg(env->gpr[10]);
    regs->a[1] = tswapreg(env->gpr[11]);
    regs->a[2] = tswapreg(env->gpr[12]);
    regs->a[3] = tswapreg(env->gpr[13]);
    regs->a[4] = tswapreg(env->gpr[14]);
    regs->a[5] = tswapreg(env->gpr[15]);
    regs->a[6] = tswapreg(env->gpr[16]);
    regs->a[7] = tswapreg(env->gpr[17]);

    regs->sepc = tswapreg(env->pc);
}

#undef tswapreg

#endif /* TARGET_ARCH_REG_H */

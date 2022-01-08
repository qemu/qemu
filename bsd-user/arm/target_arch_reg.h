/*
 *  FreeBSD arm register structures
 *
 *  Copyright (c) 2015 Stacey Son
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

#ifndef _TARGET_ARCH_REG_H_
#define _TARGET_ARCH_REG_H_

/* See sys/arm/include/reg.h */
typedef struct target_reg {
    uint32_t        r[13];
    uint32_t        r_sp;
    uint32_t        r_lr;
    uint32_t        r_pc;
    uint32_t        r_cpsr;
} target_reg_t;

typedef struct target_fp_reg {
    uint32_t        fp_exponent;
    uint32_t        fp_mantissa_hi;
    u_int32_t       fp_mantissa_lo;
} target_fp_reg_t;

typedef struct target_fpreg {
    uint32_t        fpr_fpsr;
    target_fp_reg_t fpr[8];
} target_fpreg_t;

#define tswapreg(ptr)   tswapal(ptr)

static inline void target_copy_regs(target_reg_t *regs, const CPUARMState *env)
{
    int i;

    for (i = 0; i < 13; i++) {
        regs->r[i] = tswapreg(env->regs[i + 1]);
    }
    regs->r_sp = tswapreg(env->regs[13]);
    regs->r_lr = tswapreg(env->regs[14]);
    regs->r_pc = tswapreg(env->regs[15]);
    regs->r_cpsr = tswapreg(cpsr_read((CPUARMState *)env));
}

#undef tswapreg

#endif /* !_TARGET_ARCH_REG_H_ */

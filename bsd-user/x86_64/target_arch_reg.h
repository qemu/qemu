/*
 *  FreeBSD amd64 register structures
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

#ifndef _TARGET_ARCH_REG_H_
#define _TARGET_ARCH_REG_H_

/* See sys/amd64/include/reg.h */
typedef struct target_reg {
    uint64_t        r_r15;
    uint64_t        r_r14;
    uint64_t        r_r13;
    uint64_t        r_r12;
    uint64_t        r_r11;
    uint64_t        r_r10;
    uint64_t        r_r9;
    uint64_t        r_r8;
    uint64_t        r_rdi;
    uint64_t        r_rsi;
    uint64_t        r_rbp;
    uint64_t        r_rbx;
    uint64_t        r_rdx;
    uint64_t        r_rcx;
    uint64_t        r_rax;
    uint32_t        r_trapno;
    uint16_t        r_fs;
    uint16_t        r_gs;
    uint32_t        r_err;
    uint16_t        r_es;
    uint16_t        r_ds;
    uint64_t        r_rip;
    uint64_t        r_cs;
    uint64_t        r_rflags;
    uint64_t        r_rsp;
    uint64_t        r_ss;
} target_reg_t;

typedef struct target_fpreg {
    uint64_t        fpr_env[4];
    uint8_t         fpr_acc[8][16];
    uint8_t         fpr_xacc[16][16];
    uint64_t        fpr_spare[12];
} target_fpreg_t;

static inline void target_copy_regs(target_reg_t *regs, const CPUX86State *env)
{

    regs->r_r15 = env->regs[15];
    regs->r_r14 = env->regs[14];
    regs->r_r13 = env->regs[13];
    regs->r_r12 = env->regs[12];
    regs->r_r11 = env->regs[11];
    regs->r_r10 = env->regs[10];
    regs->r_r9 = env->regs[9];
    regs->r_r8 = env->regs[8];
    regs->r_rdi = env->regs[R_EDI];
    regs->r_rsi = env->regs[R_ESI];
    regs->r_rbp = env->regs[R_EBP];
    regs->r_rbx = env->regs[R_EBX];
    regs->r_rdx = env->regs[R_EDX];
    regs->r_rcx = env->regs[R_ECX];
    regs->r_rax = env->regs[R_EAX];
    /* regs->r_trapno = env->regs[R_TRAPNO]; XXX */
    regs->r_fs = env->segs[R_FS].selector & 0xffff;
    regs->r_gs = env->segs[R_GS].selector & 0xffff;
    regs->r_err = env->error_code;  /* XXX ? */
    regs->r_es = env->segs[R_ES].selector & 0xffff;
    regs->r_ds = env->segs[R_DS].selector & 0xffff;
    regs->r_rip = env->eip;
    regs->r_cs = env->segs[R_CS].selector & 0xffff;
    regs->r_rflags = env->eflags;
    regs->r_rsp = env->regs[R_ESP];
    regs->r_ss = env->segs[R_SS].selector & 0xffff;
}

#endif /* !_TARGET_ARCH_REG_H_ */

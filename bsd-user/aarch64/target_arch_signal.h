/*
 * ARM AArch64 specific signal definitions for bsd-user
 *
 * Copyright (c) 2015 Stacey D. Son <sson at FreeBSD>
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

#ifndef TARGET_ARCH_SIGNAL_H
#define TARGET_ARCH_SIGNAL_H

#include "cpu.h"

#define TARGET_REG_X0   0
#define TARGET_REG_X30  30
#define TARGET_REG_X31  31
#define TARGET_REG_LR   TARGET_REG_X30
#define TARGET_REG_SP   TARGET_REG_X31

#define TARGET_INSN_SIZE    4       /* arm64 instruction size */

/* Size of the signal trampolin code. See _sigtramp(). */
#define TARGET_SZSIGCODE    ((abi_ulong)(9 * TARGET_INSN_SIZE))

/* compare to sys/arm64/include/_limits.h */
#define TARGET_MINSIGSTKSZ  (1024 * 4)                  /* min sig stack size */
#define TARGET_SIGSTKSZ     (TARGET_MINSIGSTKSZ + 32768)  /* recommended size */

/* struct __mcontext in sys/arm64/include/ucontext.h */

struct target_gpregs {
    uint64_t    gp_x[30];
    uint64_t    gp_lr;
    uint64_t    gp_sp;
    uint64_t    gp_elr;
    uint32_t    gp_spsr;
    uint32_t    gp_pad;
};

struct target_fpregs {
    Int128      fp_q[32];
    uint32_t    fp_sr;
    uint32_t    fp_cr;
    uint32_t    fp_flags;
    uint32_t    fp_pad;
};

struct target__mcontext {
    struct target_gpregs mc_gpregs;
    struct target_fpregs mc_fpregs;
    uint32_t    mc_flags;
#define TARGET_MC_FP_VALID  0x1
    uint32_t    mc_pad;
    uint64_t    mc_spare[8];
};

typedef struct target__mcontext target_mcontext_t;

#define TARGET_MCONTEXT_SIZE 880
#define TARGET_UCONTEXT_SIZE 960

#include "target_os_ucontext.h"

struct target_sigframe {
    target_siginfo_t    sf_si;  /* saved siginfo */
    target_ucontext_t   sf_uc;  /* saved ucontext */
};

#define TARGET_SIGSTACK_ALIGN 16

#endif /* TARGET_ARCH_SIGNAL_H */

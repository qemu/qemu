/*
 *  x86_64 signal definitions
 *
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
#ifndef _TARGET_ARCH_SIGNAL_H_
#define _TARGET_ARCH_SIGNAL_H_

#include "cpu.h"

/* Size of the signal trampolin code placed on the stack. */
#define TARGET_SZSIGCODE    0

/* compare to  x86/include/_limits.h */
#define TARGET_MINSIGSTKSZ  (512 * 4)               /* min sig stack size */
#define TARGET_SIGSTKSZ     (MINSIGSTKSZ + 32768)   /* recommended size */

typedef struct target_mcontext {
    abi_ulong   mc_onstack;     /* XXX - sigcontext compat. */
    abi_ulong   mc_rdi;         /* machine state (struct trapframe) */
    abi_ulong   mc_rsi;
    abi_ulong   mc_rdx;
    abi_ulong   mc_rcx;
    abi_ulong   mc_r8;
    abi_ulong   mc_r9;
    abi_ulong   mc_rax;
    abi_ulong   mc_rbx;
    abi_ulong   mc_rbp;
    abi_ulong   mc_r10;
    abi_ulong   mc_r11;
    abi_ulong   mc_r12;
    abi_ulong   mc_r13;
    abi_ulong   mc_r14;
    abi_ulong   mc_r15;
    uint32_t    mc_trapno;
    uint16_t    mc_fs;
    uint16_t    mc_gs;
    abi_ulong   mc_addr;
    uint32_t    mc_flags;
    uint16_t    mc_es;
    uint16_t    mc_ds;
    abi_ulong   mc_err;
    abi_ulong   mc_rip;
    abi_ulong   mc_cs;
    abi_ulong   mc_rflags;
    abi_ulong   mc_rsp;
    abi_ulong   mc_ss;

    abi_long    mc_len;                 /* sizeof(mcontext_t) */

#define _MC_FPFMT_NODEV         0x10000 /* device not present or configured */
#define _MC_FPFMT_XMM           0x10002
    abi_long    mc_fpformat;
#define _MC_FPOWNED_NONE        0x20000 /* FP state not used */
#define _MC_FPOWNED_FPU         0x20001 /* FP state came from FPU */
#define _MC_FPOWNED_PCB         0x20002 /* FP state came from PCB */
    abi_long    mc_ownedfp;
    /*
     * See <machine/fpu.h> for the internals of mc_fpstate[].
     */
    abi_long    mc_fpstate[64] __aligned(16);

    abi_ulong   mc_fsbase;
    abi_ulong   mc_gsbase;

    abi_ulong   mc_xfpustate;
    abi_ulong   mc_xfpustate_len;

    abi_long    mc_spare[4];
} target_mcontext_t;

#define TARGET_MCONTEXT_SIZE 800
#define TARGET_UCONTEXT_SIZE 880

#include "target_os_ucontext.h"

struct target_sigframe {
    abi_ulong   sf_signum;
    abi_ulong   sf_siginfo;    /* code or pointer to sf_si */
    abi_ulong   sf_ucontext;   /* points to sf_uc */
    abi_ulong   sf_addr;       /* undocumented 4th arg */
    target_ucontext_t   sf_uc; /* = *sf_uncontext */
    target_siginfo_t    sf_si; /* = *sf_siginfo (SA_SIGINFO case)*/
    uint32_t    __spare__[2];
};

#endif /* !TARGET_ARCH_SIGNAL_H_ */

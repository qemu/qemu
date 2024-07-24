/*
 *  i386 dependent signal definitions
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
#ifndef TARGET_ARCH_SIGNAL_H
#define TARGET_ARCH_SIGNAL_H

#include "cpu.h"

/* Size of the signal trampolin code placed on the stack. */
#define TARGET_SZSIGCODE    0

/* compare to  x86/include/_limits.h */
#define TARGET_MINSIGSTKSZ  (512 * 4)               /* min sig stack size */
#define TARGET_SIGSTKSZ     (MINSIGSTKSZ + 32768)   /* recommended size */

typedef struct target_mcontext {
    abi_ulong   mc_onstack;     /* XXX - sigcontext compat. */
    abi_ulong   mc_gs;          /* machine state (struct trapframe) */
    abi_ulong   mc_fs;
    abi_ulong   mc_es;
    abi_ulong   mc_ds;
    abi_ulong   mc_edi;
    abi_ulong   mc_esi;
    abi_ulong   mc_ebp;
    abi_ulong   mc_isp;
    abi_ulong   mc_ebx;
    abi_ulong   mc_edx;
    abi_ulong   mc_ecx;
    abi_ulong   mc_eax;
    abi_ulong   mc_trapno;
    abi_ulong   mc_err;
    abi_ulong   mc_eip;
    abi_ulong   mc_cs;
    abi_ulong   mc_eflags;
    abi_ulong   mc_esp;
    abi_ulong   mc_ss;

    int32_t     mc_len;                 /* sizeof(mcontext_t) */
#define _MC_FPFMT_NODEV         0x10000 /* device not present or configured */
#define _MC_FPFMT_387           0x10001
#define _MC_FPFMT_XMM           0x10002
    int32_t     mc_fpformat;
#define _MC_FPOWNED_NONE        0x20000 /* FP state not used */
#define _MC_FPOWNED_FPU         0x20001 /* FP state came from FPU */
#define _MC_FPOWNED_PCB         0x20002 /* FP state came from PCB */
    int32_t     mc_ownedfp;
    abi_ulong mc_flags;
        /*
         * See <machine/npx.h> for the internals of mc_fpstate[].
         */
    int32_t     mc_fpstate[128] __aligned(16);

    abi_ulong mc_fsbase;
    abi_ulong mc_gsbase;

    abi_ulong mc_xfpustate;
    abi_ulong mc_xfpustate_len;

    int32_t     mc_spare2[4];
} target_mcontext_t;

#define TARGET_MCONTEXT_SIZE 640
#define TARGET_UCONTEXT_SIZE 704

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

#define TARGET_SIGSTACK_ALIGN 8

#endif /* TARGET_ARCH_SIGNAL_H */

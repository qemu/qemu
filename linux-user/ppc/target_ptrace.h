/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef PPC_TARGET_PTRACE_H
#define PPC_TARGET_PTRACE_H

struct target_pt_regs {
    abi_ulong gpr[32];
    abi_ulong nip;
    abi_ulong msr;
    abi_ulong orig_gpr3;        /* Used for restarting system calls */
    abi_ulong ctr;
    abi_ulong link;
    abi_ulong xer;
    abi_ulong ccr;
#if defined(TARGET_PPC64)
    abi_ulong softe;
#else
    abi_ulong mq;               /* 601 only (not used at present) */
#endif
    abi_ulong trap;             /* Reason for being here */
    abi_ulong dar;              /* Fault registers */
    abi_ulong dsisr;
    abi_ulong result;           /* Result of a system call */
};

#endif /* PPC_TARGET_PTRACE_H */

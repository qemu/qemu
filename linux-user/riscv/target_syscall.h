/*
 * This struct defines the way the registers are stored on the
 *  stack during a system call.
 *
 * Reference: linux/arch/riscv/include/uapi/asm/ptrace.h
 */

#ifndef LINUX_USER_RISCV_TARGET_SYSCALL_H
#define LINUX_USER_RISCV_TARGET_SYSCALL_H

struct target_pt_regs {
    abi_long sepc;
    abi_long ra;
    abi_long sp;
    abi_long gp;
    abi_long tp;
    abi_long t0;
    abi_long t1;
    abi_long t2;
    abi_long s0;
    abi_long s1;
    abi_long a0;
    abi_long a1;
    abi_long a2;
    abi_long a3;
    abi_long a4;
    abi_long a5;
    abi_long a6;
    abi_long a7;
    abi_long s2;
    abi_long s3;
    abi_long s4;
    abi_long s5;
    abi_long s6;
    abi_long s7;
    abi_long s8;
    abi_long s9;
    abi_long s10;
    abi_long s11;
    abi_long t3;
    abi_long t4;
    abi_long t5;
    abi_long t6;
};

#ifdef TARGET_RISCV32
#define UNAME_MACHINE "riscv32"
#define UNAME_MINIMUM_RELEASE "5.4.0"
#else
#define UNAME_MACHINE "riscv64"
#define UNAME_MINIMUM_RELEASE "4.15.0"
#endif

#define TARGET_MCL_CURRENT 1
#define TARGET_MCL_FUTURE  2
#define TARGET_MCL_ONFAULT 4

/* clone(flags, newsp, ptidptr, tls, ctidptr) for RISC-V */
/* This comes from linux/kernel/fork.c, CONFIG_CLONE_BACKWARDS */
#define TARGET_CLONE_BACKWARDS

#endif

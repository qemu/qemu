/*
 * This struct defines the way the registers are stored on the
 *  stack during a system call.
 *
 * Reference: linux/arch/riscv/include/uapi/asm/ptrace.h
 */

#ifndef LINUX_USER_RISCV_TARGET_SYSCALL_H
#define LINUX_USER_RISCV_TARGET_SYSCALL_H

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

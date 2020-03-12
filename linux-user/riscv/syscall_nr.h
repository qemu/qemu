/*
 * Syscall numbers from asm-generic, common for most
 * of recently-added arches including RISC-V.
 */

#ifndef LINUX_USER_RISCV_SYSCALL_NR_H
#define LINUX_USER_RISCV_SYSCALL_NR_H

#ifdef TARGET_RISCV32
# include "syscall32_nr.h"
#else
# include "syscall64_nr.h"
#endif

#endif

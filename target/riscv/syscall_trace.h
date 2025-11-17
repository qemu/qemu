/*
 * Helper for tracing linux-syscall.
 */

#ifndef RISCV_SYSCALL_TRACE_H
#define RISCV_SYSCALL_TRACE_H

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "cpu.h"

#define LK_NR_getcwd        17
#define LK_NR_ioctl         29
#define LK_NR_unlinkat      35
#define LK_NR_faccessat     48
#define LK_NR_chdir         49
#define LK_NR_openat        56
#define LK_NR_read          63
#define LK_NR_write         64
#define LK_NR_writev        66
#define LK_NR_fstatat       79
#define LK_NR_exit          93
#define LK_NR_rt_sigaction  134
#define LK_NR_rt_sigprocmask 135

#define LK_NR_set_tid_address 96
#define LK_NR_set_robust_list 99

#define LK_NR_uname         160
#define LK_NR_brk           214
#define LK_NR_execve        221
#define LK_NR_mmap          222
#define LK_NR_mprotect      226
#define LK_NR_prlimit64     261
#define LK_NR_getrandom     278

void handle_payload_in(CPUState *cs, trace_event_t *evt, FILE *f);
void handle_payload_out(CPUState *cs, trace_event_t *evt, FILE *f);

#endif /* RISCV_SYSCALL_TRACE_H */

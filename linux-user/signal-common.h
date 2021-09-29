/*
 *  Emulation of Linux signals
 *
 *  Copyright (c) 2003 Fabrice Bellard
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

#ifndef SIGNAL_COMMON_H
#define SIGNAL_COMMON_H

/* Fallback addresses into sigtramp page. */
extern abi_ulong default_sigreturn;
extern abi_ulong default_rt_sigreturn;

void setup_sigtramp(abi_ulong tramp_page);

int on_sig_stack(unsigned long sp);
int sas_ss_flags(unsigned long sp);
abi_ulong target_sigsp(abi_ulong sp, struct target_sigaction *ka);
void target_save_altstack(target_stack_t *uss, CPUArchState *env);
abi_long target_restore_altstack(target_stack_t *uss, CPUArchState *env);

static inline void target_sigemptyset(target_sigset_t *set)
{
    memset(set, 0, sizeof(*set));
}

void host_to_target_sigset_internal(target_sigset_t *d,
                                    const sigset_t *s);
void target_to_host_sigset_internal(sigset_t *d,
                                    const target_sigset_t *s);
void tswap_siginfo(target_siginfo_t *tinfo,
                   const target_siginfo_t *info);
void set_sigmask(const sigset_t *set);
void force_sig(int sig);
void force_sigsegv(int oldsig);
void force_sig_fault(int sig, int code, abi_ulong addr);
#if defined(TARGET_ARCH_HAS_SETUP_FRAME)
void setup_frame(int sig, struct target_sigaction *ka,
                 target_sigset_t *set, CPUArchState *env);
#endif
void setup_rt_frame(int sig, struct target_sigaction *ka,
                    target_siginfo_t *info,
                    target_sigset_t *set, CPUArchState *env);

void process_pending_signals(CPUArchState *cpu_env);
void signal_init(void);
int queue_signal(CPUArchState *env, int sig, int si_type,
                 target_siginfo_t *info);
void host_to_target_siginfo(target_siginfo_t *tinfo, const siginfo_t *info);
void target_to_host_siginfo(siginfo_t *info, const target_siginfo_t *tinfo);
int target_to_host_signal(int sig);
int host_to_target_signal(int sig);
long do_sigreturn(CPUArchState *env);
long do_rt_sigreturn(CPUArchState *env);
abi_long do_sigaltstack(abi_ulong uss_addr, abi_ulong uoss_addr,
                        CPUArchState *env);
int do_sigprocmask(int how, const sigset_t *set, sigset_t *oldset);
abi_long do_swapcontext(CPUArchState *env, abi_ulong uold_ctx,
                        abi_ulong unew_ctx, abi_long ctx_size);
/**
 * block_signals: block all signals while handling this guest syscall
 *
 * Block all signals, and arrange that the signal mask is returned to
 * its correct value for the guest before we resume execution of guest code.
 * If this function returns non-zero, then the caller should immediately
 * return -TARGET_ERESTARTSYS to the main loop, which will take the pending
 * signal and restart execution of the syscall.
 * If block_signals() returns zero, then the caller can continue with
 * emulation of the system call knowing that no signals can be taken
 * (and therefore that no race conditions will result).
 * This should only be called once, because if it is called a second time
 * it will always return non-zero. (Think of it like a mutex that can't
 * be recursively locked.)
 * Signals will be unblocked again by process_pending_signals().
 *
 * Return value: non-zero if there was a pending signal, zero if not.
 */
int block_signals(void); /* Returns non zero if signal pending */

#endif

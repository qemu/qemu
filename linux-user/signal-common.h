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

#include "special-errno.h"

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
void signal_init(const char *rtsig_map);
void queue_signal(CPUArchState *env, int sig, int si_type,
                  target_siginfo_t *info);
void host_to_target_siginfo(target_siginfo_t *tinfo, const siginfo_t *info);
void target_to_host_siginfo(siginfo_t *info, const target_siginfo_t *tinfo);
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
 * return -QEMU_ERESTARTSYS to the main loop, which will take the pending
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

/**
 * process_sigsuspend_mask: read and apply syscall-local signal mask
 *
 * Read the guest signal mask from @sigset, length @sigsize.
 * Convert that to a host signal mask and save it to sigpending_mask.
 *
 * Return value: negative target errno, or zero;
 *               store &sigpending_mask into *pset on success.
 */
int process_sigsuspend_mask(sigset_t **pset, target_ulong sigset,
                            target_ulong sigsize);

/**
 * finish_sigsuspend_mask: finish a sigsuspend-like syscall
 *
 * Set in_sigsuspend if we need to use the modified sigset
 * during process_pending_signals.
 */
static inline void finish_sigsuspend_mask(int ret)
{
    if (ret != -QEMU_ERESTARTSYS) {
        TaskState *ts = get_task_state(thread_cpu);
        ts->in_sigsuspend = 1;
    }
}

#if defined(SIGSTKFLT) && defined(TARGET_SIGSTKFLT)
#define MAKE_SIG_ENTRY_SIGSTKFLT        MAKE_SIG_ENTRY(SIGSTKFLT)
#else
#define MAKE_SIG_ENTRY_SIGSTKFLT
#endif

#if defined(SIGIOT) && defined(TARGET_SIGIOT)
#define MAKE_SIG_ENTRY_SIGIOT           MAKE_SIG_ENTRY(SIGIOT)
#else
#define MAKE_SIG_ENTRY_SIGIOT
#endif

#define MAKE_SIGNAL_LIST \
        MAKE_SIG_ENTRY(SIGHUP) \
        MAKE_SIG_ENTRY(SIGINT) \
        MAKE_SIG_ENTRY(SIGQUIT) \
        MAKE_SIG_ENTRY(SIGILL) \
        MAKE_SIG_ENTRY(SIGTRAP) \
        MAKE_SIG_ENTRY(SIGABRT) \
        MAKE_SIG_ENTRY(SIGBUS) \
        MAKE_SIG_ENTRY(SIGFPE) \
        MAKE_SIG_ENTRY(SIGKILL) \
        MAKE_SIG_ENTRY(SIGUSR1) \
        MAKE_SIG_ENTRY(SIGSEGV) \
        MAKE_SIG_ENTRY(SIGUSR2) \
        MAKE_SIG_ENTRY(SIGPIPE) \
        MAKE_SIG_ENTRY(SIGALRM) \
        MAKE_SIG_ENTRY(SIGTERM) \
        MAKE_SIG_ENTRY(SIGCHLD) \
        MAKE_SIG_ENTRY(SIGCONT) \
        MAKE_SIG_ENTRY(SIGSTOP) \
        MAKE_SIG_ENTRY(SIGTSTP) \
        MAKE_SIG_ENTRY(SIGTTIN) \
        MAKE_SIG_ENTRY(SIGTTOU) \
        MAKE_SIG_ENTRY(SIGURG) \
        MAKE_SIG_ENTRY(SIGXCPU) \
        MAKE_SIG_ENTRY(SIGXFSZ) \
        MAKE_SIG_ENTRY(SIGVTALRM) \
        MAKE_SIG_ENTRY(SIGPROF) \
        MAKE_SIG_ENTRY(SIGWINCH) \
        MAKE_SIG_ENTRY(SIGIO) \
        MAKE_SIG_ENTRY(SIGPWR) \
        MAKE_SIG_ENTRY(SIGSYS) \
        MAKE_SIG_ENTRY_SIGSTKFLT \
        MAKE_SIG_ENTRY_SIGIOT

#endif

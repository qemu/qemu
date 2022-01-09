/*
 * Emulation of BSD signals
 *
 * Copyright (c) 2013 Stacey Son
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef SIGNAL_COMMON_H
#define SIGNAL_COMMON_H

long do_rt_sigreturn(CPUArchState *env);
abi_long do_sigaltstack(abi_ulong uss_addr, abi_ulong uoss_addr, abi_ulong sp);
long do_sigreturn(CPUArchState *env);
void force_sig_fault(int sig, int code, abi_ulong addr);
int host_to_target_signal(int sig);
void host_to_target_sigset(target_sigset_t *d, const sigset_t *s);
void process_pending_signals(CPUArchState *env);
void queue_signal(CPUArchState *env, int sig, int si_type,
                  target_siginfo_t *info);
void signal_init(void);
int target_to_host_signal(int sig);
void target_to_host_sigset(sigset_t *d, const target_sigset_t *s);

/*
 * Within QEMU the top 8 bits of si_code indicate which of the parts of the
 * union in target_siginfo is valid. This only applies between
 * host_to_target_siginfo_noswap() and tswap_siginfo(); it does not appear
 * either within host siginfo_t or in target_siginfo structures which we get
 * from the guest userspace program. Linux kenrels use this internally, but BSD
 * kernels don't do this, but its a useful abstraction.
 *
 * The linux-user version of this uses the top 16 bits, but FreeBSD's SI_USER
 * and other signal indepenent SI_ codes have bit 16 set, so we only use the top
 * byte instead.
 *
 * For FreeBSD, we have si_pid, si_uid, si_status, and si_addr always. Linux and
 * {Open,Net}BSD have a different approach (where their reason field is larger,
 * but whose siginfo has fewer fields always).
 */
#define QEMU_SI_NOINFO   0      /* nothing other than si_signo valid */
#define QEMU_SI_FAULT    1      /* _fault is valid in _reason */
#define QEMU_SI_TIMER    2      /* _timer is valid in _reason */
#define QEMU_SI_MESGQ    3      /* _mesgq is valid in _reason */
#define QEMU_SI_POLL     4      /* _poll is valid in _reason */
#define QEMU_SI_CAPSICUM 5      /* _capsicum is valid in _reason */

#endif

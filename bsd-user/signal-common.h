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
void process_pending_signals(CPUArchState *env);
void queue_signal(CPUArchState *env, int sig, target_siginfo_t *info);
void signal_init(void);

#endif

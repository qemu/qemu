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
extern struct target_sigaltstack target_sigaltstack_used;

static inline int on_sig_stack(unsigned long sp)
{
    return (sp - target_sigaltstack_used.ss_sp
            < target_sigaltstack_used.ss_size);
}

static inline int sas_ss_flags(unsigned long sp)
{
    return (target_sigaltstack_used.ss_size == 0 ? SS_DISABLE
            : on_sig_stack(sp) ? SS_ONSTACK : 0);
}

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
#endif

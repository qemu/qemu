/*
 *  Emulation of Linux signal handling
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
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <signal.h>
#include <sys/ucontext.h>

/* Algorithm strongly inspired from em86 : we queue the signals so
   that we can handle them at precise points in the emulated code. */

struct emulated_sigaction {
    struct target_sigaction sa;
    int nb_pending;
    struct target_siginfo info;
};

struct emulated_sigaction sigact_table[NSIG];
int signal_pending;

static inline int host_to_target_signal(int sig)
{
    return sig;
}

static inline int target_to_host_signal(int sig)
{
    return sig;
}

void signal_init(void)
{
    struct sigaction act;
    int i;

    /* set all host signal handlers */
    sigemptyset(&act.sa_mask);
    act.sa_flags = SA_SIGINFO;
    act.sa_sigaction = host_signal_handler;
    for(i = 1; i < NSIG; i++) {
	sigaction(i, &sa, NULL);
    }
    
    memset(sigact_table, 0, sizeof(sigact_table));
}

static void host_signal_handler(int host_signum, siginfo_t *info, 
                                void *puc)
{
    struct ucontext *uc = puc;
    int signum;
    /* get target signal number */
    signum = host_to_target(host_signum);
    if (signum >= TARGET_NSIG)
        return;
    /* we save the old mask */
    
    
}


void process_pending_signals(void)
{
    int signum;
    target_ulong _sa_handler;

    struct emulated_sigaction *esig;

    if (!signal_pending)
        return;

    esig = sigact_table;
    for(signum = 1; signum < TARGET_NSIG; signum++) {
        if (esig->nb_pending != 0)
            goto handle_signal;
        esig++;
    }
    /* if no signal is pending, just return */
    signal_pending = 0;
    return;
 handle_signal:
    _sa_handler = esig->sa._sa_handler;
    if (_sa_handler == TARGET_SIG_DFL) {
        /* default handling
    }


}

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
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston,
 *  MA 02110-1301, USA.
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/ucontext.h>

#ifdef __ia64__
#undef uc_mcontext
#undef uc_sigmask
#undef uc_stack
#undef uc_link
#endif

#include <signal.h>

#include "qemu.h"
#include "qemu-common.h"

#define DEBUG_SIGNAL

#define MAX_SIGQUEUE_SIZE 1024

struct sigqueue {
    struct sigqueue *next;
    target_siginfo_t info;
};

struct emulated_sigaction {
    struct target_sigaction sa;
    int pending; /* true if signal is pending */
    struct sigqueue *first;
    struct sigqueue info; /* in order to always have memory for the
                             first signal, we put it here */
};

static struct sigaltstack target_sigaltstack_used = {
    0, 0, SA_DISABLE
};

static struct emulated_sigaction sigact_table[NSIG];
static struct sigqueue sigqueue_table[MAX_SIGQUEUE_SIZE]; /* siginfo queue */
static struct sigqueue *first_free; /* first free siginfo queue entry */
static int signal_pending; /* non zero if a signal may be pending */

static void host_signal_handler(int host_signum, siginfo_t *info,
                                void *puc);


static inline int host_to_target_signal(int sig)
{
    return sig;
}

static inline int target_to_host_signal(int sig)
{
    return sig;
}

/* siginfo conversion */



void host_to_target_siginfo(target_siginfo_t *tinfo, const siginfo_t *info)
{

}

void target_to_host_siginfo(siginfo_t *info, const target_siginfo_t *tinfo)
{

}

void signal_init(void)
{
    struct sigaction act;
    int i;

    /* set all host signal handlers. ALL signals are blocked during
       the handlers to serialize them. */
    sigfillset(&act.sa_mask);
    act.sa_flags = SA_SIGINFO;
    act.sa_sigaction = host_signal_handler;
    for(i = 1; i < NSIG; i++) {
        sigaction(i, &act, NULL);
    }

    memset(sigact_table, 0, sizeof(sigact_table));

    first_free = &sigqueue_table[0];
    for(i = 0; i < MAX_SIGQUEUE_SIZE - 1; i++)
        sigqueue_table[i].next = &sigqueue_table[i + 1];
    sigqueue_table[MAX_SIGQUEUE_SIZE - 1].next = NULL;
}

/* signal queue handling */

static inline struct sigqueue *alloc_sigqueue(void)
{
    struct sigqueue *q = first_free;
    if (!q)
        return NULL;
    first_free = q->next;
    return q;
}

static inline void free_sigqueue(struct sigqueue *q)
{
    q->next = first_free;
    first_free = q;
}

/* abort execution with signal */
void QEMU_NORETURN force_sig(int sig)
{
    int host_sig;
    host_sig = target_to_host_signal(sig);
    fprintf(stderr, "qemu: uncaught target signal %d (%s) - exiting\n",
            sig, strsignal(host_sig));
    _exit(-host_sig);
}

/* queue a signal so that it will be send to the virtual CPU as soon
   as possible */
int queue_signal(int sig, target_siginfo_t *info)
{
    struct emulated_sigaction *k;
    struct sigqueue *q, **pq;
    target_ulong handler;

#if defined(DEBUG_SIGNAL)
    fprintf(stderr, "queue_signal: sig=%d\n",
            sig);
#endif
    k = &sigact_table[sig - 1];
    handler = (target_ulong)k->sa.sa_handler;
    if (handler == SIG_DFL) {
        /* default handler : ignore some signal. The other are fatal */
        if (sig != SIGCHLD &&
            sig != SIGURG &&
            sig != SIGWINCH) {
            force_sig(sig);
        } else {
            return 0; /* indicate ignored */
        }
    } else if (handler == host_to_target_signal(SIG_IGN)) {
        /* ignore signal */
        return 0;
    } else if (handler == host_to_target_signal(SIG_ERR)) {
        force_sig(sig);
    } else {
        pq = &k->first;
        if (!k->pending) {
            /* first signal */
            q = &k->info;
        } else {
            q = alloc_sigqueue();
            if (!q)
                return -EAGAIN;
            while (*pq != NULL)
                pq = &(*pq)->next;
        }
        *pq = q;
        q->info = *info;
        q->next = NULL;
        k->pending = 1;
        /* signal that a new signal is pending */
        signal_pending = 1;
        return 1; /* indicates that the signal was queued */
    }
}

static void host_signal_handler(int host_signum, siginfo_t *info,
                                void *puc)
{
    int sig;
    target_siginfo_t tinfo;

    /* the CPU emulator uses some host signals to detect exceptions,
       we we forward to it some signals */
    if (host_signum == SIGSEGV || host_signum == SIGBUS) {
        if (cpu_signal_handler(host_signum, (void*)info, puc))
            return;
    }

    /* get target signal number */
    sig = host_to_target_signal(host_signum);
    if (sig < 1 || sig > NSIG)
        return;

#if defined(DEBUG_SIGNAL)
	fprintf(stderr, "qemu: got signal %d\n", sig);
#endif
    if (queue_signal(sig, &tinfo) == 1) {
        /* interrupt the virtual CPU as soon as possible */
        cpu_exit(global_env);
    }
}

int do_sigaltstack(const struct sigaltstack *ss, struct sigaltstack *oss)
{
    /* XXX: test errors */
    if(oss)
    {
        oss->ss_sp = tswap32(target_sigaltstack_used.ss_sp);
        oss->ss_size = tswap32(target_sigaltstack_used.ss_size);
        oss->ss_flags = tswap32(target_sigaltstack_used.ss_flags);
    }
    if(ss)
    {
        target_sigaltstack_used.ss_sp = tswap32(ss->ss_sp);
        target_sigaltstack_used.ss_size = tswap32(ss->ss_size);
        target_sigaltstack_used.ss_flags = tswap32(ss->ss_flags);
    }
    return 0;
}

int do_sigaction(int sig, const struct sigaction *act,
                 struct sigaction *oact)
{
    struct emulated_sigaction *k;
    struct sigaction act1;
    int host_sig;

    if (sig < 1 || sig > NSIG)
        return -EINVAL;

    k = &sigact_table[sig - 1];
#if defined(DEBUG_SIGNAL)
    fprintf(stderr, "sigaction 1 sig=%d act=0x%08x, oact=0x%08x\n",
            sig, (int)act, (int)oact);
#endif
    if (oact) {
#if defined(DEBUG_SIGNAL)
    fprintf(stderr, "sigaction 1 sig=%d act=0x%08x, oact=0x%08x\n",
            sig, (int)act, (int)oact);
#endif

        oact->sa_handler = tswapl(k->sa.sa_handler);
        oact->sa_flags = tswapl(k->sa.sa_flags);
        oact->sa_mask = tswapl(k->sa.sa_mask);
    }
    if (act) {
#if defined(DEBUG_SIGNAL)
    fprintf(stderr, "sigaction handler 0x%x flag 0x%x mask 0x%x\n",
            act->sa_handler, act->sa_flags, act->sa_mask);
#endif

        k->sa.sa_handler = tswapl(act->sa_handler);
        k->sa.sa_flags = tswapl(act->sa_flags);
        k->sa.sa_mask = tswapl(act->sa_mask);
        /* we update the host signal state */
        host_sig = target_to_host_signal(sig);
        if (host_sig != SIGSEGV && host_sig != SIGBUS) {
#if defined(DEBUG_SIGNAL)
    fprintf(stderr, "sigaction handler going to call sigaction\n",
            act->sa_handler, act->sa_flags, act->sa_mask);
#endif

            sigfillset(&act1.sa_mask);
            act1.sa_flags = SA_SIGINFO;
            if (k->sa.sa_flags & SA_RESTART)
                act1.sa_flags |= SA_RESTART;
            /* NOTE: it is important to update the host kernel signal
               ignore state to avoid getting unexpected interrupted
               syscalls */
            if (k->sa.sa_handler == SIG_IGN) {
                act1.sa_sigaction = (void *)SIG_IGN;
            } else if (k->sa.sa_handler == SIG_DFL) {
                act1.sa_sigaction = (void *)SIG_DFL;
            } else {
                act1.sa_sigaction = host_signal_handler;
            }
            sigaction(host_sig, &act1, NULL);
        }
    }
    return 0;
}


#ifdef TARGET_I386

static inline void *
get_sigframe(struct emulated_sigaction *ka, CPUX86State *env, size_t frame_size)
{
    /* XXX Fix that */
    if(target_sigaltstack_used.ss_flags & SA_DISABLE)
    {
        int esp;
        /* Default to using normal stack */
        esp = env->regs[R_ESP];

        return (void *)((esp - frame_size) & -8ul);
    }
    else
    {
        return target_sigaltstack_used.ss_sp;
    }
}

static void setup_frame(int sig, struct emulated_sigaction *ka,
			void *set, CPUState *env)
{
	void *frame;
	int i, err = 0;

    fprintf(stderr, "setup_frame %d\n", sig);
	frame = get_sigframe(ka, env, sizeof(*frame));

	/* Set up registers for signal handler */
	env->regs[R_ESP] = (unsigned long) frame;
	env->eip = (unsigned long) ka->sa.sa_handler;

	env->eflags &= ~TF_MASK;

	return;

give_sigsegv:
	if (sig == SIGSEGV)
		ka->sa.sa_handler = SIG_DFL;
	force_sig(SIGSEGV /* , current */);
}

long do_sigreturn(CPUState *env, int num)
{
    int i = 0;
    struct target_sigcontext *scp = get_int_arg(&i, env);
    /* XXX Get current signal number */
    /* XXX Adjust accordin to sc_onstack, sc_mask */
    if(tswapl(scp->sc_onstack) & 0x1)
        target_sigaltstack_used.ss_flags |= ~SA_DISABLE;
    else
        target_sigaltstack_used.ss_flags &=  SA_DISABLE;
    int set = tswapl(scp->sc_eax);
    sigprocmask(SIG_SETMASK, &set, NULL);

    fprintf(stderr, "do_sigreturn: partially implemented %x EAX:%x EBX:%x\n", scp->sc_mask, tswapl(scp->sc_eax), tswapl(scp->sc_ebx));
    fprintf(stderr, "ECX:%x EDX:%x EDI:%x\n", scp->sc_ecx, tswapl(scp->sc_edx), tswapl(scp->sc_edi));
    fprintf(stderr, "EIP:%x\n", tswapl(scp->sc_eip));

    env->regs[R_EAX] = tswapl(scp->sc_eax);
    env->regs[R_EBX] = tswapl(scp->sc_ebx);
    env->regs[R_ECX] = tswapl(scp->sc_ecx);
    env->regs[R_EDX] = tswapl(scp->sc_edx);
    env->regs[R_EDI] = tswapl(scp->sc_edi);
    env->regs[R_ESI] = tswapl(scp->sc_esi);
    env->regs[R_EBP] = tswapl(scp->sc_ebp);
    env->regs[R_ESP] = tswapl(scp->sc_esp);
    env->segs[R_SS].selector = (void*)tswapl(scp->sc_ss);
    env->eflags = tswapl(scp->sc_eflags);
    env->eip = tswapl(scp->sc_eip);
    env->segs[R_CS].selector = (void*)tswapl(scp->sc_cs);
    env->segs[R_DS].selector = (void*)tswapl(scp->sc_ds);
    env->segs[R_ES].selector = (void*)tswapl(scp->sc_es);
    env->segs[R_FS].selector = (void*)tswapl(scp->sc_fs);
    env->segs[R_GS].selector = (void*)tswapl(scp->sc_gs);

    /* Again, because our caller's caller will reset EAX */
    return env->regs[R_EAX];
}

#else

static void setup_frame(int sig, struct emulated_sigaction *ka,
			void *set, CPUState *env)
{
    fprintf(stderr, "setup_frame: not implemented\n");
}

long do_sigreturn(CPUState *env, int num)
{
    int i = 0;
    struct target_sigcontext *scp = get_int_arg(&i, env);
    fprintf(stderr, "do_sigreturn: not implemented\n");
    return -ENOSYS;
}

#endif

void process_pending_signals(void *cpu_env)
{
    struct emulated_sigaction *k;
    struct sigqueue *q;
    target_ulong handler;
    int sig;

    if (!signal_pending)
        return;

    k = sigact_table;

    for(sig = 1; sig <= NSIG; sig++) {
        if (k->pending)
            goto handle_signal;
        k++;
    }

    /* if no signal is pending, just return */
    signal_pending = 0;
    return;
handle_signal:
    #ifdef DEBUG_SIGNAL
    fprintf(stderr, "qemu: process signal %d\n", sig);
    #endif
    /* dequeue signal */
    q = k->first;
    k->first = q->next;
    if (!k->first)
        k->pending = 0;

    sig = gdb_handlesig (cpu_env, sig);
    if (!sig) {
        fprintf (stderr, "Lost signal\n");
        abort();
    }

    handler = k->sa.sa_handler;
    if (handler == SIG_DFL) {
        /* default handler : ignore some signal. The other are fatal */
        if (sig != SIGCHLD &&
            sig != SIGURG &&
            sig != SIGWINCH) {
            force_sig(sig);
        }
    } else if (handler == SIG_IGN) {
        /* ignore sig */
    } else if (handler == SIG_ERR) {
        force_sig(sig);
    } else {

        setup_frame(sig, k, 0, cpu_env);
	if (k->sa.sa_flags & SA_RESETHAND)
            k->sa.sa_handler = SIG_DFL;
    }
    if (q != &k->info)
        free_sigqueue(q);
}

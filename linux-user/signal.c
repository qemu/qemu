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
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>
#include <sys/ucontext.h>
#include <sys/resource.h>

#include "qemu.h"
#include "qemu-common.h"
#include "target_signal.h"

//#define DEBUG_SIGNAL

static struct target_sigaltstack target_sigaltstack_used = {
    .ss_sp = 0,
    .ss_size = 0,
    .ss_flags = TARGET_SS_DISABLE,
};

static struct target_sigaction sigact_table[TARGET_NSIG];

static void host_signal_handler(int host_signum, siginfo_t *info,
                                void *puc);

static uint8_t host_to_target_signal_table[_NSIG] = {
    [SIGHUP] = TARGET_SIGHUP,
    [SIGINT] = TARGET_SIGINT,
    [SIGQUIT] = TARGET_SIGQUIT,
    [SIGILL] = TARGET_SIGILL,
    [SIGTRAP] = TARGET_SIGTRAP,
    [SIGABRT] = TARGET_SIGABRT,
/*    [SIGIOT] = TARGET_SIGIOT,*/
    [SIGBUS] = TARGET_SIGBUS,
    [SIGFPE] = TARGET_SIGFPE,
    [SIGKILL] = TARGET_SIGKILL,
    [SIGUSR1] = TARGET_SIGUSR1,
    [SIGSEGV] = TARGET_SIGSEGV,
    [SIGUSR2] = TARGET_SIGUSR2,
    [SIGPIPE] = TARGET_SIGPIPE,
    [SIGALRM] = TARGET_SIGALRM,
    [SIGTERM] = TARGET_SIGTERM,
#ifdef SIGSTKFLT
    [SIGSTKFLT] = TARGET_SIGSTKFLT,
#endif
    [SIGCHLD] = TARGET_SIGCHLD,
    [SIGCONT] = TARGET_SIGCONT,
    [SIGSTOP] = TARGET_SIGSTOP,
    [SIGTSTP] = TARGET_SIGTSTP,
    [SIGTTIN] = TARGET_SIGTTIN,
    [SIGTTOU] = TARGET_SIGTTOU,
    [SIGURG] = TARGET_SIGURG,
    [SIGXCPU] = TARGET_SIGXCPU,
    [SIGXFSZ] = TARGET_SIGXFSZ,
    [SIGVTALRM] = TARGET_SIGVTALRM,
    [SIGPROF] = TARGET_SIGPROF,
    [SIGWINCH] = TARGET_SIGWINCH,
    [SIGIO] = TARGET_SIGIO,
    [SIGPWR] = TARGET_SIGPWR,
    [SIGSYS] = TARGET_SIGSYS,
    /* next signals stay the same */
    /* Nasty hack: Reverse SIGRTMIN and SIGRTMAX to avoid overlap with
       host libpthread signals.  This assumes noone actually uses SIGRTMAX :-/
       To fix this properly we need to do manual signal delivery multiplexed
       over a single host signal.  */
    [__SIGRTMIN] = __SIGRTMAX,
    [__SIGRTMAX] = __SIGRTMIN,
};
static uint8_t target_to_host_signal_table[_NSIG];

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

int host_to_target_signal(int sig)
{
    if (sig >= _NSIG)
        return sig;
    return host_to_target_signal_table[sig];
}

int target_to_host_signal(int sig)
{
    if (sig >= _NSIG)
        return sig;
    return target_to_host_signal_table[sig];
}

static inline void target_sigemptyset(target_sigset_t *set)
{
    memset(set, 0, sizeof(*set));
}

static inline void target_sigaddset(target_sigset_t *set, int signum)
{
    signum--;
    abi_ulong mask = (abi_ulong)1 << (signum % TARGET_NSIG_BPW);
    set->sig[signum / TARGET_NSIG_BPW] |= mask;
}

static inline int target_sigismember(const target_sigset_t *set, int signum)
{
    signum--;
    abi_ulong mask = (abi_ulong)1 << (signum % TARGET_NSIG_BPW);
    return ((set->sig[signum / TARGET_NSIG_BPW] & mask) != 0);
}

static void host_to_target_sigset_internal(target_sigset_t *d,
                                           const sigset_t *s)
{
    int i;
    target_sigemptyset(d);
    for (i = 1; i <= TARGET_NSIG; i++) {
        if (sigismember(s, i)) {
            target_sigaddset(d, host_to_target_signal(i));
        }
    }
}

void host_to_target_sigset(target_sigset_t *d, const sigset_t *s)
{
    target_sigset_t d1;
    int i;

    host_to_target_sigset_internal(&d1, s);
    for(i = 0;i < TARGET_NSIG_WORDS; i++)
        d->sig[i] = tswapal(d1.sig[i]);
}

static void target_to_host_sigset_internal(sigset_t *d,
                                           const target_sigset_t *s)
{
    int i;
    sigemptyset(d);
    for (i = 1; i <= TARGET_NSIG; i++) {
        if (target_sigismember(s, i)) {
            sigaddset(d, target_to_host_signal(i));
        }
     }
}

void target_to_host_sigset(sigset_t *d, const target_sigset_t *s)
{
    target_sigset_t s1;
    int i;

    for(i = 0;i < TARGET_NSIG_WORDS; i++)
        s1.sig[i] = tswapal(s->sig[i]);
    target_to_host_sigset_internal(d, &s1);
}

void host_to_target_old_sigset(abi_ulong *old_sigset,
                               const sigset_t *sigset)
{
    target_sigset_t d;
    host_to_target_sigset(&d, sigset);
    *old_sigset = d.sig[0];
}

void target_to_host_old_sigset(sigset_t *sigset,
                               const abi_ulong *old_sigset)
{
    target_sigset_t d;
    int i;

    d.sig[0] = *old_sigset;
    for(i = 1;i < TARGET_NSIG_WORDS; i++)
        d.sig[i] = 0;
    target_to_host_sigset(sigset, &d);
}

/* siginfo conversion */

static inline void host_to_target_siginfo_noswap(target_siginfo_t *tinfo,
                                                 const siginfo_t *info)
{
    int sig;
    sig = host_to_target_signal(info->si_signo);
    tinfo->si_signo = sig;
    tinfo->si_errno = 0;
    tinfo->si_code = info->si_code;
    if (sig == SIGILL || sig == SIGFPE || sig == SIGSEGV ||
        sig == SIGBUS || sig == SIGTRAP) {
        /* should never come here, but who knows. The information for
           the target is irrelevant */
        tinfo->_sifields._sigfault._addr = 0;
    } else if (sig == SIGIO) {
	tinfo->_sifields._sigpoll._fd = info->si_fd;
    } else if (sig >= TARGET_SIGRTMIN) {
        tinfo->_sifields._rt._pid = info->si_pid;
        tinfo->_sifields._rt._uid = info->si_uid;
        /* XXX: potential problem if 64 bit */
        tinfo->_sifields._rt._sigval.sival_ptr =
            (abi_ulong)(unsigned long)info->si_value.sival_ptr;
    }
}

static void tswap_siginfo(target_siginfo_t *tinfo,
                          const target_siginfo_t *info)
{
    int sig;
    sig = info->si_signo;
    tinfo->si_signo = tswap32(sig);
    tinfo->si_errno = tswap32(info->si_errno);
    tinfo->si_code = tswap32(info->si_code);
    if (sig == SIGILL || sig == SIGFPE || sig == SIGSEGV ||
        sig == SIGBUS || sig == SIGTRAP) {
        tinfo->_sifields._sigfault._addr =
            tswapal(info->_sifields._sigfault._addr);
    } else if (sig == SIGIO) {
	tinfo->_sifields._sigpoll._fd = tswap32(info->_sifields._sigpoll._fd);
    } else if (sig >= TARGET_SIGRTMIN) {
        tinfo->_sifields._rt._pid = tswap32(info->_sifields._rt._pid);
        tinfo->_sifields._rt._uid = tswap32(info->_sifields._rt._uid);
        tinfo->_sifields._rt._sigval.sival_ptr =
            tswapal(info->_sifields._rt._sigval.sival_ptr);
    }
}


void host_to_target_siginfo(target_siginfo_t *tinfo, const siginfo_t *info)
{
    host_to_target_siginfo_noswap(tinfo, info);
    tswap_siginfo(tinfo, tinfo);
}

/* XXX: we support only POSIX RT signals are used. */
/* XXX: find a solution for 64 bit (additional malloced data is needed) */
void target_to_host_siginfo(siginfo_t *info, const target_siginfo_t *tinfo)
{
    info->si_signo = tswap32(tinfo->si_signo);
    info->si_errno = tswap32(tinfo->si_errno);
    info->si_code = tswap32(tinfo->si_code);
    info->si_pid = tswap32(tinfo->_sifields._rt._pid);
    info->si_uid = tswap32(tinfo->_sifields._rt._uid);
    info->si_value.sival_ptr =
            (void *)(long)tswapal(tinfo->_sifields._rt._sigval.sival_ptr);
}

static int fatal_signal (int sig)
{
    switch (sig) {
    case TARGET_SIGCHLD:
    case TARGET_SIGURG:
    case TARGET_SIGWINCH:
        /* Ignored by default.  */
        return 0;
    case TARGET_SIGCONT:
    case TARGET_SIGSTOP:
    case TARGET_SIGTSTP:
    case TARGET_SIGTTIN:
    case TARGET_SIGTTOU:
        /* Job control signals.  */
        return 0;
    default:
        return 1;
    }
}

/* returns 1 if given signal should dump core if not handled */
static int core_dump_signal(int sig)
{
    switch (sig) {
    case TARGET_SIGABRT:
    case TARGET_SIGFPE:
    case TARGET_SIGILL:
    case TARGET_SIGQUIT:
    case TARGET_SIGSEGV:
    case TARGET_SIGTRAP:
    case TARGET_SIGBUS:
        return (1);
    default:
        return (0);
    }
}

void signal_init(void)
{
    struct sigaction act;
    struct sigaction oact;
    int i, j;
    int host_sig;

    /* generate signal conversion tables */
    for(i = 1; i < _NSIG; i++) {
        if (host_to_target_signal_table[i] == 0)
            host_to_target_signal_table[i] = i;
    }
    for(i = 1; i < _NSIG; i++) {
        j = host_to_target_signal_table[i];
        target_to_host_signal_table[j] = i;
    }

    /* set all host signal handlers. ALL signals are blocked during
       the handlers to serialize them. */
    memset(sigact_table, 0, sizeof(sigact_table));

    sigfillset(&act.sa_mask);
    act.sa_flags = SA_SIGINFO;
    act.sa_sigaction = host_signal_handler;
    for(i = 1; i <= TARGET_NSIG; i++) {
        host_sig = target_to_host_signal(i);
        sigaction(host_sig, NULL, &oact);
        if (oact.sa_sigaction == (void *)SIG_IGN) {
            sigact_table[i - 1]._sa_handler = TARGET_SIG_IGN;
        } else if (oact.sa_sigaction == (void *)SIG_DFL) {
            sigact_table[i - 1]._sa_handler = TARGET_SIG_DFL;
        }
        /* If there's already a handler installed then something has
           gone horribly wrong, so don't even try to handle that case.  */
        /* Install some handlers for our own use.  We need at least
           SIGSEGV and SIGBUS, to detect exceptions.  We can not just
           trap all signals because it affects syscall interrupt
           behavior.  But do trap all default-fatal signals.  */
        if (fatal_signal (i))
            sigaction(host_sig, &act, NULL);
    }
}

/* signal queue handling */

static inline struct sigqueue *alloc_sigqueue(CPUState *env)
{
    TaskState *ts = env->opaque;
    struct sigqueue *q = ts->first_free;
    if (!q)
        return NULL;
    ts->first_free = q->next;
    return q;
}

static inline void free_sigqueue(CPUState *env, struct sigqueue *q)
{
    TaskState *ts = env->opaque;
    q->next = ts->first_free;
    ts->first_free = q;
}

/* abort execution with signal */
static void QEMU_NORETURN force_sig(int target_sig)
{
    TaskState *ts = (TaskState *)thread_env->opaque;
    int host_sig, core_dumped = 0;
    struct sigaction act;
    host_sig = target_to_host_signal(target_sig);
    gdb_signalled(thread_env, target_sig);

    /* dump core if supported by target binary format */
    if (core_dump_signal(target_sig) && (ts->bprm->core_dump != NULL)) {
        stop_all_tasks();
        core_dumped =
            ((*ts->bprm->core_dump)(target_sig, thread_env) == 0);
    }
    if (core_dumped) {
        /* we already dumped the core of target process, we don't want
         * a coredump of qemu itself */
        struct rlimit nodump;
        getrlimit(RLIMIT_CORE, &nodump);
        nodump.rlim_cur=0;
        setrlimit(RLIMIT_CORE, &nodump);
        (void) fprintf(stderr, "qemu: uncaught target signal %d (%s) - %s\n",
            target_sig, strsignal(host_sig), "core dumped" );
    }

    /* The proper exit code for dying from an uncaught signal is
     * -<signal>.  The kernel doesn't allow exit() or _exit() to pass
     * a negative value.  To get the proper exit code we need to
     * actually die from an uncaught signal.  Here the default signal
     * handler is installed, we send ourself a signal and we wait for
     * it to arrive. */
    sigfillset(&act.sa_mask);
    act.sa_handler = SIG_DFL;
    sigaction(host_sig, &act, NULL);

    /* For some reason raise(host_sig) doesn't send the signal when
     * statically linked on x86-64. */
    kill(getpid(), host_sig);

    /* Make sure the signal isn't masked (just reuse the mask inside
    of act) */
    sigdelset(&act.sa_mask, host_sig);
    sigsuspend(&act.sa_mask);

    /* unreachable */
    abort();
}

/* queue a signal so that it will be send to the virtual CPU as soon
   as possible */
int queue_signal(CPUState *env, int sig, target_siginfo_t *info)
{
    TaskState *ts = env->opaque;
    struct emulated_sigtable *k;
    struct sigqueue *q, **pq;
    abi_ulong handler;
    int queue;

#if defined(DEBUG_SIGNAL)
    fprintf(stderr, "queue_signal: sig=%d\n",
            sig);
#endif
    k = &ts->sigtab[sig - 1];
    queue = gdb_queuesig ();
    handler = sigact_table[sig - 1]._sa_handler;
    if (!queue && handler == TARGET_SIG_DFL) {
        if (sig == TARGET_SIGTSTP || sig == TARGET_SIGTTIN || sig == TARGET_SIGTTOU) {
            kill(getpid(),SIGSTOP);
            return 0;
        } else
        /* default handler : ignore some signal. The other are fatal */
        if (sig != TARGET_SIGCHLD &&
            sig != TARGET_SIGURG &&
            sig != TARGET_SIGWINCH &&
            sig != TARGET_SIGCONT) {
            force_sig(sig);
        } else {
            return 0; /* indicate ignored */
        }
    } else if (!queue && handler == TARGET_SIG_IGN) {
        /* ignore signal */
        return 0;
    } else if (!queue && handler == TARGET_SIG_ERR) {
        force_sig(sig);
    } else {
        pq = &k->first;
        if (sig < TARGET_SIGRTMIN) {
            /* if non real time signal, we queue exactly one signal */
            if (!k->pending)
                q = &k->info;
            else
                return 0;
        } else {
            if (!k->pending) {
                /* first signal */
                q = &k->info;
            } else {
                q = alloc_sigqueue(env);
                if (!q)
                    return -EAGAIN;
                while (*pq != NULL)
                    pq = &(*pq)->next;
            }
        }
        *pq = q;
        q->info = *info;
        q->next = NULL;
        k->pending = 1;
        /* signal that a new signal is pending */
        ts->signal_pending = 1;
        return 1; /* indicates that the signal was queued */
    }
}

static void host_signal_handler(int host_signum, siginfo_t *info,
                                void *puc)
{
    int sig;
    target_siginfo_t tinfo;

    /* the CPU emulator uses some host signals to detect exceptions,
       we forward to it some signals */
    if ((host_signum == SIGSEGV || host_signum == SIGBUS)
        && info->si_code > 0) {
        if (cpu_signal_handler(host_signum, info, puc))
            return;
    }

    /* get target signal number */
    sig = host_to_target_signal(host_signum);
    if (sig < 1 || sig > TARGET_NSIG)
        return;
#if defined(DEBUG_SIGNAL)
    fprintf(stderr, "qemu: got signal %d\n", sig);
#endif
    host_to_target_siginfo_noswap(&tinfo, info);
    if (queue_signal(thread_env, sig, &tinfo) == 1) {
        /* interrupt the virtual CPU as soon as possible */
        cpu_exit(thread_env);
    }
}

/* do_sigaltstack() returns target values and errnos. */
/* compare linux/kernel/signal.c:do_sigaltstack() */
abi_long do_sigaltstack(abi_ulong uss_addr, abi_ulong uoss_addr, abi_ulong sp)
{
    int ret;
    struct target_sigaltstack oss;

    /* XXX: test errors */
    if(uoss_addr)
    {
        __put_user(target_sigaltstack_used.ss_sp, &oss.ss_sp);
        __put_user(target_sigaltstack_used.ss_size, &oss.ss_size);
        __put_user(sas_ss_flags(sp), &oss.ss_flags);
    }

    if(uss_addr)
    {
        struct target_sigaltstack *uss;
        struct target_sigaltstack ss;

	ret = -TARGET_EFAULT;
        if (!lock_user_struct(VERIFY_READ, uss, uss_addr, 1)
	    || __get_user(ss.ss_sp, &uss->ss_sp)
	    || __get_user(ss.ss_size, &uss->ss_size)
	    || __get_user(ss.ss_flags, &uss->ss_flags))
            goto out;
        unlock_user_struct(uss, uss_addr, 0);

	ret = -TARGET_EPERM;
	if (on_sig_stack(sp))
            goto out;

	ret = -TARGET_EINVAL;
	if (ss.ss_flags != TARGET_SS_DISABLE
            && ss.ss_flags != TARGET_SS_ONSTACK
            && ss.ss_flags != 0)
            goto out;

	if (ss.ss_flags == TARGET_SS_DISABLE) {
            ss.ss_size = 0;
            ss.ss_sp = 0;
	} else {
            ret = -TARGET_ENOMEM;
            if (ss.ss_size < MINSIGSTKSZ)
                goto out;
	}

        target_sigaltstack_used.ss_sp = ss.ss_sp;
        target_sigaltstack_used.ss_size = ss.ss_size;
    }

    if (uoss_addr) {
        ret = -TARGET_EFAULT;
        if (copy_to_user(uoss_addr, &oss, sizeof(oss)))
            goto out;
    }

    ret = 0;
out:
    return ret;
}

/* do_sigaction() return host values and errnos */
int do_sigaction(int sig, const struct target_sigaction *act,
                 struct target_sigaction *oact)
{
    struct target_sigaction *k;
    struct sigaction act1;
    int host_sig;
    int ret = 0;

    if (sig < 1 || sig > TARGET_NSIG || sig == TARGET_SIGKILL || sig == TARGET_SIGSTOP)
        return -EINVAL;
    k = &sigact_table[sig - 1];
#if defined(DEBUG_SIGNAL)
    fprintf(stderr, "sigaction sig=%d act=0x%p, oact=0x%p\n",
            sig, act, oact);
#endif
    if (oact) {
        oact->_sa_handler = tswapal(k->_sa_handler);
        oact->sa_flags = tswapal(k->sa_flags);
#if !defined(TARGET_MIPS)
        oact->sa_restorer = tswapal(k->sa_restorer);
#endif
        oact->sa_mask = k->sa_mask;
    }
    if (act) {
        /* FIXME: This is not threadsafe.  */
        k->_sa_handler = tswapal(act->_sa_handler);
        k->sa_flags = tswapal(act->sa_flags);
#if !defined(TARGET_MIPS)
        k->sa_restorer = tswapal(act->sa_restorer);
#endif
        k->sa_mask = act->sa_mask;

        /* we update the host linux signal state */
        host_sig = target_to_host_signal(sig);
        if (host_sig != SIGSEGV && host_sig != SIGBUS) {
            sigfillset(&act1.sa_mask);
            act1.sa_flags = SA_SIGINFO;
            if (k->sa_flags & TARGET_SA_RESTART)
                act1.sa_flags |= SA_RESTART;
            /* NOTE: it is important to update the host kernel signal
               ignore state to avoid getting unexpected interrupted
               syscalls */
            if (k->_sa_handler == TARGET_SIG_IGN) {
                act1.sa_sigaction = (void *)SIG_IGN;
            } else if (k->_sa_handler == TARGET_SIG_DFL) {
                if (fatal_signal (sig))
                    act1.sa_sigaction = host_signal_handler;
                else
                    act1.sa_sigaction = (void *)SIG_DFL;
            } else {
                act1.sa_sigaction = host_signal_handler;
            }
            ret = sigaction(host_sig, &act1, NULL);
        }
    }
    return ret;
}

static inline int copy_siginfo_to_user(target_siginfo_t *tinfo,
                                       const target_siginfo_t *info)
{
    tswap_siginfo(tinfo, info);
    return 0;
}

static inline int current_exec_domain_sig(int sig)
{
    return /* current->exec_domain && current->exec_domain->signal_invmap
	      && sig < 32 ? current->exec_domain->signal_invmap[sig] : */ sig;
}

#if defined(TARGET_I386) && TARGET_ABI_BITS == 32

/* from the Linux kernel */

struct target_fpreg {
	uint16_t significand[4];
	uint16_t exponent;
};

struct target_fpxreg {
	uint16_t significand[4];
	uint16_t exponent;
	uint16_t padding[3];
};

struct target_xmmreg {
	abi_ulong element[4];
};

struct target_fpstate {
	/* Regular FPU environment */
        abi_ulong       cw;
        abi_ulong       sw;
        abi_ulong       tag;
        abi_ulong       ipoff;
        abi_ulong       cssel;
        abi_ulong       dataoff;
        abi_ulong       datasel;
	struct target_fpreg	_st[8];
	uint16_t	status;
	uint16_t	magic;		/* 0xffff = regular FPU data only */

	/* FXSR FPU environment */
        abi_ulong       _fxsr_env[6];   /* FXSR FPU env is ignored */
        abi_ulong       mxcsr;
        abi_ulong       reserved;
	struct target_fpxreg	_fxsr_st[8];	/* FXSR FPU reg data is ignored */
	struct target_xmmreg	_xmm[8];
        abi_ulong       padding[56];
};

#define X86_FXSR_MAGIC		0x0000

struct target_sigcontext {
	uint16_t gs, __gsh;
	uint16_t fs, __fsh;
	uint16_t es, __esh;
	uint16_t ds, __dsh;
        abi_ulong edi;
        abi_ulong esi;
        abi_ulong ebp;
        abi_ulong esp;
        abi_ulong ebx;
        abi_ulong edx;
        abi_ulong ecx;
        abi_ulong eax;
        abi_ulong trapno;
        abi_ulong err;
        abi_ulong eip;
	uint16_t cs, __csh;
        abi_ulong eflags;
        abi_ulong esp_at_signal;
	uint16_t ss, __ssh;
        abi_ulong fpstate; /* pointer */
        abi_ulong oldmask;
        abi_ulong cr2;
};

struct target_ucontext {
        abi_ulong         tuc_flags;
        abi_ulong         tuc_link;
	target_stack_t	  tuc_stack;
	struct target_sigcontext tuc_mcontext;
	target_sigset_t	  tuc_sigmask;	/* mask last for extensibility */
};

struct sigframe
{
    abi_ulong pretcode;
    int sig;
    struct target_sigcontext sc;
    struct target_fpstate fpstate;
    abi_ulong extramask[TARGET_NSIG_WORDS-1];
    char retcode[8];
};

struct rt_sigframe
{
    abi_ulong pretcode;
    int sig;
    abi_ulong pinfo;
    abi_ulong puc;
    struct target_siginfo info;
    struct target_ucontext uc;
    struct target_fpstate fpstate;
    char retcode[8];
};

/*
 * Set up a signal frame.
 */

/* XXX: save x87 state */
static int
setup_sigcontext(struct target_sigcontext *sc, struct target_fpstate *fpstate,
		 CPUX86State *env, abi_ulong mask, abi_ulong fpstate_addr)
{
	int err = 0;
        uint16_t magic;

	/* already locked in setup_frame() */
	err |= __put_user(env->segs[R_GS].selector, (unsigned int *)&sc->gs);
	err |= __put_user(env->segs[R_FS].selector, (unsigned int *)&sc->fs);
	err |= __put_user(env->segs[R_ES].selector, (unsigned int *)&sc->es);
	err |= __put_user(env->segs[R_DS].selector, (unsigned int *)&sc->ds);
	err |= __put_user(env->regs[R_EDI], &sc->edi);
	err |= __put_user(env->regs[R_ESI], &sc->esi);
	err |= __put_user(env->regs[R_EBP], &sc->ebp);
	err |= __put_user(env->regs[R_ESP], &sc->esp);
	err |= __put_user(env->regs[R_EBX], &sc->ebx);
	err |= __put_user(env->regs[R_EDX], &sc->edx);
	err |= __put_user(env->regs[R_ECX], &sc->ecx);
	err |= __put_user(env->regs[R_EAX], &sc->eax);
	err |= __put_user(env->exception_index, &sc->trapno);
	err |= __put_user(env->error_code, &sc->err);
	err |= __put_user(env->eip, &sc->eip);
	err |= __put_user(env->segs[R_CS].selector, (unsigned int *)&sc->cs);
	err |= __put_user(env->eflags, &sc->eflags);
	err |= __put_user(env->regs[R_ESP], &sc->esp_at_signal);
	err |= __put_user(env->segs[R_SS].selector, (unsigned int *)&sc->ss);

        cpu_x86_fsave(env, fpstate_addr, 1);
        fpstate->status = fpstate->sw;
        magic = 0xffff;
        err |= __put_user(magic, &fpstate->magic);
        err |= __put_user(fpstate_addr, &sc->fpstate);

	/* non-iBCS2 extensions.. */
	err |= __put_user(mask, &sc->oldmask);
	err |= __put_user(env->cr[2], &sc->cr2);
	return err;
}

/*
 * Determine which stack to use..
 */

static inline abi_ulong
get_sigframe(struct target_sigaction *ka, CPUX86State *env, size_t frame_size)
{
	unsigned long esp;

	/* Default to using normal stack */
	esp = env->regs[R_ESP];
	/* This is the X/Open sanctioned signal stack switching.  */
        if (ka->sa_flags & TARGET_SA_ONSTACK) {
            if (sas_ss_flags(esp) == 0)
                esp = target_sigaltstack_used.ss_sp + target_sigaltstack_used.ss_size;
        }

	/* This is the legacy signal stack switching. */
	else
        if ((env->segs[R_SS].selector & 0xffff) != __USER_DS &&
            !(ka->sa_flags & TARGET_SA_RESTORER) &&
            ka->sa_restorer) {
            esp = (unsigned long) ka->sa_restorer;
	}
        return (esp - frame_size) & -8ul;
}

/* compare linux/arch/i386/kernel/signal.c:setup_frame() */
static void setup_frame(int sig, struct target_sigaction *ka,
			target_sigset_t *set, CPUX86State *env)
{
	abi_ulong frame_addr;
	struct sigframe *frame;
	int i, err = 0;

	frame_addr = get_sigframe(ka, env, sizeof(*frame));

	if (!lock_user_struct(VERIFY_WRITE, frame, frame_addr, 0))
		goto give_sigsegv;

	err |= __put_user(current_exec_domain_sig(sig),
		          &frame->sig);
	if (err)
		goto give_sigsegv;

	setup_sigcontext(&frame->sc, &frame->fpstate, env, set->sig[0],
                         frame_addr + offsetof(struct sigframe, fpstate));
	if (err)
		goto give_sigsegv;

        for(i = 1; i < TARGET_NSIG_WORDS; i++) {
            if (__put_user(set->sig[i], &frame->extramask[i - 1]))
                goto give_sigsegv;
        }

	/* Set up to return from userspace.  If provided, use a stub
	   already in userspace.  */
	if (ka->sa_flags & TARGET_SA_RESTORER) {
		err |= __put_user(ka->sa_restorer, &frame->pretcode);
	} else {
                uint16_t val16;
                abi_ulong retcode_addr;
                retcode_addr = frame_addr + offsetof(struct sigframe, retcode);
		err |= __put_user(retcode_addr, &frame->pretcode);
		/* This is popl %eax ; movl $,%eax ; int $0x80 */
                val16 = 0xb858;
		err |= __put_user(val16, (uint16_t *)(frame->retcode+0));
		err |= __put_user(TARGET_NR_sigreturn, (int *)(frame->retcode+2));
                val16 = 0x80cd;
		err |= __put_user(val16, (uint16_t *)(frame->retcode+6));
	}

	if (err)
		goto give_sigsegv;

	/* Set up registers for signal handler */
	env->regs[R_ESP] = frame_addr;
	env->eip = ka->_sa_handler;

        cpu_x86_load_seg(env, R_DS, __USER_DS);
        cpu_x86_load_seg(env, R_ES, __USER_DS);
        cpu_x86_load_seg(env, R_SS, __USER_DS);
        cpu_x86_load_seg(env, R_CS, __USER_CS);
	env->eflags &= ~TF_MASK;

	unlock_user_struct(frame, frame_addr, 1);

	return;

give_sigsegv:
	unlock_user_struct(frame, frame_addr, 1);
	if (sig == TARGET_SIGSEGV)
		ka->_sa_handler = TARGET_SIG_DFL;
	force_sig(TARGET_SIGSEGV /* , current */);
}

/* compare linux/arch/i386/kernel/signal.c:setup_rt_frame() */
static void setup_rt_frame(int sig, struct target_sigaction *ka,
                           target_siginfo_t *info,
			   target_sigset_t *set, CPUX86State *env)
{
        abi_ulong frame_addr, addr;
	struct rt_sigframe *frame;
	int i, err = 0;

	frame_addr = get_sigframe(ka, env, sizeof(*frame));

	if (!lock_user_struct(VERIFY_WRITE, frame, frame_addr, 0))
		goto give_sigsegv;

	err |= __put_user(current_exec_domain_sig(sig),
			  &frame->sig);
        addr = frame_addr + offsetof(struct rt_sigframe, info);
	err |= __put_user(addr, &frame->pinfo);
        addr = frame_addr + offsetof(struct rt_sigframe, uc);
	err |= __put_user(addr, &frame->puc);
	err |= copy_siginfo_to_user(&frame->info, info);
	if (err)
		goto give_sigsegv;

	/* Create the ucontext.  */
	err |= __put_user(0, &frame->uc.tuc_flags);
	err |= __put_user(0, &frame->uc.tuc_link);
	err |= __put_user(target_sigaltstack_used.ss_sp,
			  &frame->uc.tuc_stack.ss_sp);
	err |= __put_user(sas_ss_flags(get_sp_from_cpustate(env)),
			  &frame->uc.tuc_stack.ss_flags);
	err |= __put_user(target_sigaltstack_used.ss_size,
			  &frame->uc.tuc_stack.ss_size);
	err |= setup_sigcontext(&frame->uc.tuc_mcontext, &frame->fpstate,
			        env, set->sig[0], 
                                frame_addr + offsetof(struct rt_sigframe, fpstate));
        for(i = 0; i < TARGET_NSIG_WORDS; i++) {
            if (__put_user(set->sig[i], &frame->uc.tuc_sigmask.sig[i]))
                goto give_sigsegv;
        }

	/* Set up to return from userspace.  If provided, use a stub
	   already in userspace.  */
	if (ka->sa_flags & TARGET_SA_RESTORER) {
		err |= __put_user(ka->sa_restorer, &frame->pretcode);
	} else {
                uint16_t val16;
                addr = frame_addr + offsetof(struct rt_sigframe, retcode);
		err |= __put_user(addr, &frame->pretcode);
		/* This is movl $,%eax ; int $0x80 */
                err |= __put_user(0xb8, (char *)(frame->retcode+0));
		err |= __put_user(TARGET_NR_rt_sigreturn, (int *)(frame->retcode+1));
                val16 = 0x80cd;
                err |= __put_user(val16, (uint16_t *)(frame->retcode+5));
	}

	if (err)
		goto give_sigsegv;

	/* Set up registers for signal handler */
	env->regs[R_ESP] = frame_addr;
	env->eip = ka->_sa_handler;

        cpu_x86_load_seg(env, R_DS, __USER_DS);
        cpu_x86_load_seg(env, R_ES, __USER_DS);
        cpu_x86_load_seg(env, R_SS, __USER_DS);
        cpu_x86_load_seg(env, R_CS, __USER_CS);
	env->eflags &= ~TF_MASK;

	unlock_user_struct(frame, frame_addr, 1);

	return;

give_sigsegv:
	unlock_user_struct(frame, frame_addr, 1);
	if (sig == TARGET_SIGSEGV)
		ka->_sa_handler = TARGET_SIG_DFL;
	force_sig(TARGET_SIGSEGV /* , current */);
}

static int
restore_sigcontext(CPUX86State *env, struct target_sigcontext *sc, int *peax)
{
	unsigned int err = 0;
        abi_ulong fpstate_addr;
        unsigned int tmpflags;

        cpu_x86_load_seg(env, R_GS, tswap16(sc->gs));
        cpu_x86_load_seg(env, R_FS, tswap16(sc->fs));
        cpu_x86_load_seg(env, R_ES, tswap16(sc->es));
        cpu_x86_load_seg(env, R_DS, tswap16(sc->ds));

        env->regs[R_EDI] = tswapl(sc->edi);
        env->regs[R_ESI] = tswapl(sc->esi);
        env->regs[R_EBP] = tswapl(sc->ebp);
        env->regs[R_ESP] = tswapl(sc->esp);
        env->regs[R_EBX] = tswapl(sc->ebx);
        env->regs[R_EDX] = tswapl(sc->edx);
        env->regs[R_ECX] = tswapl(sc->ecx);
        env->eip = tswapl(sc->eip);

        cpu_x86_load_seg(env, R_CS, lduw_p(&sc->cs) | 3);
        cpu_x86_load_seg(env, R_SS, lduw_p(&sc->ss) | 3);

        tmpflags = tswapl(sc->eflags);
        env->eflags = (env->eflags & ~0x40DD5) | (tmpflags & 0x40DD5);
        //		regs->orig_eax = -1;		/* disable syscall checks */

        fpstate_addr = tswapl(sc->fpstate);
	if (fpstate_addr != 0) {
                if (!access_ok(VERIFY_READ, fpstate_addr, 
                               sizeof(struct target_fpstate)))
                        goto badframe;
                cpu_x86_frstor(env, fpstate_addr, 1);
	}

        *peax = tswapl(sc->eax);
	return err;
badframe:
	return 1;
}

long do_sigreturn(CPUX86State *env)
{
    struct sigframe *frame;
    abi_ulong frame_addr = env->regs[R_ESP] - 8;
    target_sigset_t target_set;
    sigset_t set;
    int eax, i;

#if defined(DEBUG_SIGNAL)
    fprintf(stderr, "do_sigreturn\n");
#endif
    if (!lock_user_struct(VERIFY_READ, frame, frame_addr, 1))
        goto badframe;
    /* set blocked signals */
    if (__get_user(target_set.sig[0], &frame->sc.oldmask))
        goto badframe;
    for(i = 1; i < TARGET_NSIG_WORDS; i++) {
        if (__get_user(target_set.sig[i], &frame->extramask[i - 1]))
            goto badframe;
    }

    target_to_host_sigset_internal(&set, &target_set);
    sigprocmask(SIG_SETMASK, &set, NULL);

    /* restore registers */
    if (restore_sigcontext(env, &frame->sc, &eax))
        goto badframe;
    unlock_user_struct(frame, frame_addr, 0);
    return eax;

badframe:
    unlock_user_struct(frame, frame_addr, 0);
    force_sig(TARGET_SIGSEGV);
    return 0;
}

long do_rt_sigreturn(CPUX86State *env)
{
        abi_ulong frame_addr;
	struct rt_sigframe *frame;
        sigset_t set;
	int eax;

        frame_addr = env->regs[R_ESP] - 4;
        if (!lock_user_struct(VERIFY_READ, frame, frame_addr, 1))
                goto badframe;
        target_to_host_sigset(&set, &frame->uc.tuc_sigmask);
        sigprocmask(SIG_SETMASK, &set, NULL);

	if (restore_sigcontext(env, &frame->uc.tuc_mcontext, &eax))
		goto badframe;

	if (do_sigaltstack(frame_addr + offsetof(struct rt_sigframe, uc.tuc_stack), 0, 
                           get_sp_from_cpustate(env)) == -EFAULT)
		goto badframe;

        unlock_user_struct(frame, frame_addr, 0);
	return eax;

badframe:
        unlock_user_struct(frame, frame_addr, 0);
        force_sig(TARGET_SIGSEGV);
	return 0;
}

#elif defined(TARGET_ARM)

struct target_sigcontext {
	abi_ulong trap_no;
	abi_ulong error_code;
	abi_ulong oldmask;
	abi_ulong arm_r0;
	abi_ulong arm_r1;
	abi_ulong arm_r2;
	abi_ulong arm_r3;
	abi_ulong arm_r4;
	abi_ulong arm_r5;
	abi_ulong arm_r6;
	abi_ulong arm_r7;
	abi_ulong arm_r8;
	abi_ulong arm_r9;
	abi_ulong arm_r10;
	abi_ulong arm_fp;
	abi_ulong arm_ip;
	abi_ulong arm_sp;
	abi_ulong arm_lr;
	abi_ulong arm_pc;
	abi_ulong arm_cpsr;
	abi_ulong fault_address;
};

struct target_ucontext_v1 {
    abi_ulong tuc_flags;
    abi_ulong tuc_link;
    target_stack_t tuc_stack;
    struct target_sigcontext tuc_mcontext;
    target_sigset_t  tuc_sigmask;	/* mask last for extensibility */
};

struct target_ucontext_v2 {
    abi_ulong tuc_flags;
    abi_ulong tuc_link;
    target_stack_t tuc_stack;
    struct target_sigcontext tuc_mcontext;
    target_sigset_t  tuc_sigmask;	/* mask last for extensibility */
    char __unused[128 - sizeof(target_sigset_t)];
    abi_ulong tuc_regspace[128] __attribute__((__aligned__(8)));
};

struct target_user_vfp {
    uint64_t fpregs[32];
    abi_ulong fpscr;
};

struct target_user_vfp_exc {
    abi_ulong fpexc;
    abi_ulong fpinst;
    abi_ulong fpinst2;
};

struct target_vfp_sigframe {
    abi_ulong magic;
    abi_ulong size;
    struct target_user_vfp ufp;
    struct target_user_vfp_exc ufp_exc;
} __attribute__((__aligned__(8)));

struct target_iwmmxt_sigframe {
    abi_ulong magic;
    abi_ulong size;
    uint64_t regs[16];
    /* Note that not all the coprocessor control registers are stored here */
    uint32_t wcssf;
    uint32_t wcasf;
    uint32_t wcgr0;
    uint32_t wcgr1;
    uint32_t wcgr2;
    uint32_t wcgr3;
} __attribute__((__aligned__(8)));

#define TARGET_VFP_MAGIC 0x56465001
#define TARGET_IWMMXT_MAGIC 0x12ef842a

struct sigframe_v1
{
    struct target_sigcontext sc;
    abi_ulong extramask[TARGET_NSIG_WORDS-1];
    abi_ulong retcode;
};

struct sigframe_v2
{
    struct target_ucontext_v2 uc;
    abi_ulong retcode;
};

struct rt_sigframe_v1
{
    abi_ulong pinfo;
    abi_ulong puc;
    struct target_siginfo info;
    struct target_ucontext_v1 uc;
    abi_ulong retcode;
};

struct rt_sigframe_v2
{
    struct target_siginfo info;
    struct target_ucontext_v2 uc;
    abi_ulong retcode;
};

#define TARGET_CONFIG_CPU_32 1

/*
 * For ARM syscalls, we encode the syscall number into the instruction.
 */
#define SWI_SYS_SIGRETURN	(0xef000000|(TARGET_NR_sigreturn + ARM_SYSCALL_BASE))
#define SWI_SYS_RT_SIGRETURN	(0xef000000|(TARGET_NR_rt_sigreturn + ARM_SYSCALL_BASE))

/*
 * For Thumb syscalls, we pass the syscall number via r7.  We therefore
 * need two 16-bit instructions.
 */
#define SWI_THUMB_SIGRETURN	(0xdf00 << 16 | 0x2700 | (TARGET_NR_sigreturn))
#define SWI_THUMB_RT_SIGRETURN	(0xdf00 << 16 | 0x2700 | (TARGET_NR_rt_sigreturn))

static const abi_ulong retcodes[4] = {
	SWI_SYS_SIGRETURN,	SWI_THUMB_SIGRETURN,
	SWI_SYS_RT_SIGRETURN,	SWI_THUMB_RT_SIGRETURN
};


#define __get_user_error(x,p,e) __get_user(x, p)

static inline int valid_user_regs(CPUState *regs)
{
    return 1;
}

static void
setup_sigcontext(struct target_sigcontext *sc, /*struct _fpstate *fpstate,*/
		 CPUState *env, abi_ulong mask)
{
	__put_user(env->regs[0], &sc->arm_r0);
	__put_user(env->regs[1], &sc->arm_r1);
	__put_user(env->regs[2], &sc->arm_r2);
	__put_user(env->regs[3], &sc->arm_r3);
	__put_user(env->regs[4], &sc->arm_r4);
	__put_user(env->regs[5], &sc->arm_r5);
	__put_user(env->regs[6], &sc->arm_r6);
	__put_user(env->regs[7], &sc->arm_r7);
	__put_user(env->regs[8], &sc->arm_r8);
	__put_user(env->regs[9], &sc->arm_r9);
	__put_user(env->regs[10], &sc->arm_r10);
	__put_user(env->regs[11], &sc->arm_fp);
	__put_user(env->regs[12], &sc->arm_ip);
	__put_user(env->regs[13], &sc->arm_sp);
	__put_user(env->regs[14], &sc->arm_lr);
	__put_user(env->regs[15], &sc->arm_pc);
#ifdef TARGET_CONFIG_CPU_32
	__put_user(cpsr_read(env), &sc->arm_cpsr);
#endif

	__put_user(/* current->thread.trap_no */ 0, &sc->trap_no);
	__put_user(/* current->thread.error_code */ 0, &sc->error_code);
	__put_user(/* current->thread.address */ 0, &sc->fault_address);
	__put_user(mask, &sc->oldmask);
}

static inline abi_ulong
get_sigframe(struct target_sigaction *ka, CPUState *regs, int framesize)
{
	unsigned long sp = regs->regs[13];

	/*
	 * This is the X/Open sanctioned signal stack switching.
	 */
	if ((ka->sa_flags & TARGET_SA_ONSTACK) && !sas_ss_flags(sp))
            sp = target_sigaltstack_used.ss_sp + target_sigaltstack_used.ss_size;
	/*
	 * ATPCS B01 mandates 8-byte alignment
	 */
	return (sp - framesize) & ~7;
}

static int
setup_return(CPUState *env, struct target_sigaction *ka,
	     abi_ulong *rc, abi_ulong frame_addr, int usig, abi_ulong rc_addr)
{
	abi_ulong handler = ka->_sa_handler;
	abi_ulong retcode;
	int thumb = handler & 1;
	uint32_t cpsr = cpsr_read(env);

	cpsr &= ~CPSR_IT;
	if (thumb) {
		cpsr |= CPSR_T;
	} else {
		cpsr &= ~CPSR_T;
	}

	if (ka->sa_flags & TARGET_SA_RESTORER) {
		retcode = ka->sa_restorer;
	} else {
		unsigned int idx = thumb;

		if (ka->sa_flags & TARGET_SA_SIGINFO)
			idx += 2;

		if (__put_user(retcodes[idx], rc))
			return 1;

		retcode = rc_addr + thumb;
	}

	env->regs[0] = usig;
	env->regs[13] = frame_addr;
	env->regs[14] = retcode;
	env->regs[15] = handler & (thumb ? ~1 : ~3);
	cpsr_write(env, cpsr, 0xffffffff);

	return 0;
}

static abi_ulong *setup_sigframe_v2_vfp(abi_ulong *regspace, CPUState *env)
{
    int i;
    struct target_vfp_sigframe *vfpframe;
    vfpframe = (struct target_vfp_sigframe *)regspace;
    __put_user(TARGET_VFP_MAGIC, &vfpframe->magic);
    __put_user(sizeof(*vfpframe), &vfpframe->size);
    for (i = 0; i < 32; i++) {
        __put_user(float64_val(env->vfp.regs[i]), &vfpframe->ufp.fpregs[i]);
    }
    __put_user(vfp_get_fpscr(env), &vfpframe->ufp.fpscr);
    __put_user(env->vfp.xregs[ARM_VFP_FPEXC], &vfpframe->ufp_exc.fpexc);
    __put_user(env->vfp.xregs[ARM_VFP_FPINST], &vfpframe->ufp_exc.fpinst);
    __put_user(env->vfp.xregs[ARM_VFP_FPINST2], &vfpframe->ufp_exc.fpinst2);
    return (abi_ulong*)(vfpframe+1);
}

static abi_ulong *setup_sigframe_v2_iwmmxt(abi_ulong *regspace, CPUState *env)
{
    int i;
    struct target_iwmmxt_sigframe *iwmmxtframe;
    iwmmxtframe = (struct target_iwmmxt_sigframe *)regspace;
    __put_user(TARGET_IWMMXT_MAGIC, &iwmmxtframe->magic);
    __put_user(sizeof(*iwmmxtframe), &iwmmxtframe->size);
    for (i = 0; i < 16; i++) {
        __put_user(env->iwmmxt.regs[i], &iwmmxtframe->regs[i]);
    }
    __put_user(env->vfp.xregs[ARM_IWMMXT_wCSSF], &iwmmxtframe->wcssf);
    __put_user(env->vfp.xregs[ARM_IWMMXT_wCASF], &iwmmxtframe->wcssf);
    __put_user(env->vfp.xregs[ARM_IWMMXT_wCGR0], &iwmmxtframe->wcgr0);
    __put_user(env->vfp.xregs[ARM_IWMMXT_wCGR1], &iwmmxtframe->wcgr1);
    __put_user(env->vfp.xregs[ARM_IWMMXT_wCGR2], &iwmmxtframe->wcgr2);
    __put_user(env->vfp.xregs[ARM_IWMMXT_wCGR3], &iwmmxtframe->wcgr3);
    return (abi_ulong*)(iwmmxtframe+1);
}

static void setup_sigframe_v2(struct target_ucontext_v2 *uc,
                              target_sigset_t *set, CPUState *env)
{
    struct target_sigaltstack stack;
    int i;
    abi_ulong *regspace;

    /* Clear all the bits of the ucontext we don't use.  */
    memset(uc, 0, offsetof(struct target_ucontext_v2, tuc_mcontext));

    memset(&stack, 0, sizeof(stack));
    __put_user(target_sigaltstack_used.ss_sp, &stack.ss_sp);
    __put_user(target_sigaltstack_used.ss_size, &stack.ss_size);
    __put_user(sas_ss_flags(get_sp_from_cpustate(env)), &stack.ss_flags);
    memcpy(&uc->tuc_stack, &stack, sizeof(stack));

    setup_sigcontext(&uc->tuc_mcontext, env, set->sig[0]);
    /* Save coprocessor signal frame.  */
    regspace = uc->tuc_regspace;
    if (arm_feature(env, ARM_FEATURE_VFP)) {
        regspace = setup_sigframe_v2_vfp(regspace, env);
    }
    if (arm_feature(env, ARM_FEATURE_IWMMXT)) {
        regspace = setup_sigframe_v2_iwmmxt(regspace, env);
    }

    /* Write terminating magic word */
    __put_user(0, regspace);

    for(i = 0; i < TARGET_NSIG_WORDS; i++) {
        __put_user(set->sig[i], &uc->tuc_sigmask.sig[i]);
    }
}

/* compare linux/arch/arm/kernel/signal.c:setup_frame() */
static void setup_frame_v1(int usig, struct target_sigaction *ka,
			   target_sigset_t *set, CPUState *regs)
{
	struct sigframe_v1 *frame;
	abi_ulong frame_addr = get_sigframe(ka, regs, sizeof(*frame));
	int i;

	if (!lock_user_struct(VERIFY_WRITE, frame, frame_addr, 0))
		return;

	setup_sigcontext(&frame->sc, regs, set->sig[0]);

        for(i = 1; i < TARGET_NSIG_WORDS; i++) {
            if (__put_user(set->sig[i], &frame->extramask[i - 1]))
                goto end;
	}

        setup_return(regs, ka, &frame->retcode, frame_addr, usig,
                     frame_addr + offsetof(struct sigframe_v1, retcode));

end:
	unlock_user_struct(frame, frame_addr, 1);
}

static void setup_frame_v2(int usig, struct target_sigaction *ka,
			   target_sigset_t *set, CPUState *regs)
{
	struct sigframe_v2 *frame;
	abi_ulong frame_addr = get_sigframe(ka, regs, sizeof(*frame));

	if (!lock_user_struct(VERIFY_WRITE, frame, frame_addr, 0))
		return;

        setup_sigframe_v2(&frame->uc, set, regs);

        setup_return(regs, ka, &frame->retcode, frame_addr, usig,
                     frame_addr + offsetof(struct sigframe_v2, retcode));

	unlock_user_struct(frame, frame_addr, 1);
}

static void setup_frame(int usig, struct target_sigaction *ka,
			target_sigset_t *set, CPUState *regs)
{
    if (get_osversion() >= 0x020612) {
        setup_frame_v2(usig, ka, set, regs);
    } else {
        setup_frame_v1(usig, ka, set, regs);
    }
}

/* compare linux/arch/arm/kernel/signal.c:setup_rt_frame() */
static void setup_rt_frame_v1(int usig, struct target_sigaction *ka,
                              target_siginfo_t *info,
			      target_sigset_t *set, CPUState *env)
{
	struct rt_sigframe_v1 *frame;
	abi_ulong frame_addr = get_sigframe(ka, env, sizeof(*frame));
	struct target_sigaltstack stack;
	int i;
        abi_ulong info_addr, uc_addr;

	if (!lock_user_struct(VERIFY_WRITE, frame, frame_addr, 0))
            return /* 1 */;

        info_addr = frame_addr + offsetof(struct rt_sigframe_v1, info);
	__put_user(info_addr, &frame->pinfo);
        uc_addr = frame_addr + offsetof(struct rt_sigframe_v1, uc);
	__put_user(uc_addr, &frame->puc);
	copy_siginfo_to_user(&frame->info, info);

	/* Clear all the bits of the ucontext we don't use.  */
	memset(&frame->uc, 0, offsetof(struct target_ucontext_v1, tuc_mcontext));

        memset(&stack, 0, sizeof(stack));
        __put_user(target_sigaltstack_used.ss_sp, &stack.ss_sp);
        __put_user(target_sigaltstack_used.ss_size, &stack.ss_size);
        __put_user(sas_ss_flags(get_sp_from_cpustate(env)), &stack.ss_flags);
        memcpy(&frame->uc.tuc_stack, &stack, sizeof(stack));

	setup_sigcontext(&frame->uc.tuc_mcontext, env, set->sig[0]);
        for(i = 0; i < TARGET_NSIG_WORDS; i++) {
            if (__put_user(set->sig[i], &frame->uc.tuc_sigmask.sig[i]))
                goto end;
        }

        setup_return(env, ka, &frame->retcode, frame_addr, usig,
                     frame_addr + offsetof(struct rt_sigframe_v1, retcode));

        env->regs[1] = info_addr;
        env->regs[2] = uc_addr;

end:
	unlock_user_struct(frame, frame_addr, 1);
}

static void setup_rt_frame_v2(int usig, struct target_sigaction *ka,
                              target_siginfo_t *info,
                              target_sigset_t *set, CPUState *env)
{
	struct rt_sigframe_v2 *frame;
	abi_ulong frame_addr = get_sigframe(ka, env, sizeof(*frame));
        abi_ulong info_addr, uc_addr;

	if (!lock_user_struct(VERIFY_WRITE, frame, frame_addr, 0))
            return /* 1 */;

        info_addr = frame_addr + offsetof(struct rt_sigframe_v2, info);
        uc_addr = frame_addr + offsetof(struct rt_sigframe_v2, uc);
	copy_siginfo_to_user(&frame->info, info);

        setup_sigframe_v2(&frame->uc, set, env);

        setup_return(env, ka, &frame->retcode, frame_addr, usig,
                     frame_addr + offsetof(struct rt_sigframe_v2, retcode));

        env->regs[1] = info_addr;
        env->regs[2] = uc_addr;

	unlock_user_struct(frame, frame_addr, 1);
}

static void setup_rt_frame(int usig, struct target_sigaction *ka,
                           target_siginfo_t *info,
			   target_sigset_t *set, CPUState *env)
{
    if (get_osversion() >= 0x020612) {
        setup_rt_frame_v2(usig, ka, info, set, env);
    } else {
        setup_rt_frame_v1(usig, ka, info, set, env);
    }
}

static int
restore_sigcontext(CPUState *env, struct target_sigcontext *sc)
{
	int err = 0;
        uint32_t cpsr;

	__get_user_error(env->regs[0], &sc->arm_r0, err);
	__get_user_error(env->regs[1], &sc->arm_r1, err);
	__get_user_error(env->regs[2], &sc->arm_r2, err);
	__get_user_error(env->regs[3], &sc->arm_r3, err);
	__get_user_error(env->regs[4], &sc->arm_r4, err);
	__get_user_error(env->regs[5], &sc->arm_r5, err);
	__get_user_error(env->regs[6], &sc->arm_r6, err);
	__get_user_error(env->regs[7], &sc->arm_r7, err);
	__get_user_error(env->regs[8], &sc->arm_r8, err);
	__get_user_error(env->regs[9], &sc->arm_r9, err);
	__get_user_error(env->regs[10], &sc->arm_r10, err);
	__get_user_error(env->regs[11], &sc->arm_fp, err);
	__get_user_error(env->regs[12], &sc->arm_ip, err);
	__get_user_error(env->regs[13], &sc->arm_sp, err);
	__get_user_error(env->regs[14], &sc->arm_lr, err);
	__get_user_error(env->regs[15], &sc->arm_pc, err);
#ifdef TARGET_CONFIG_CPU_32
	__get_user_error(cpsr, &sc->arm_cpsr, err);
        cpsr_write(env, cpsr, CPSR_USER | CPSR_EXEC);
#endif

	err |= !valid_user_regs(env);

	return err;
}

static long do_sigreturn_v1(CPUState *env)
{
        abi_ulong frame_addr;
	struct sigframe_v1 *frame;
	target_sigset_t set;
        sigset_t host_set;
        int i;

	/*
	 * Since we stacked the signal on a 64-bit boundary,
	 * then 'sp' should be word aligned here.  If it's
	 * not, then the user is trying to mess with us.
	 */
	if (env->regs[13] & 7)
		goto badframe;

        frame_addr = env->regs[13];
	if (!lock_user_struct(VERIFY_READ, frame, frame_addr, 1))
                goto badframe;

	if (__get_user(set.sig[0], &frame->sc.oldmask))
            goto badframe;
        for(i = 1; i < TARGET_NSIG_WORDS; i++) {
            if (__get_user(set.sig[i], &frame->extramask[i - 1]))
                goto badframe;
        }

        target_to_host_sigset_internal(&host_set, &set);
        sigprocmask(SIG_SETMASK, &host_set, NULL);

	if (restore_sigcontext(env, &frame->sc))
		goto badframe;

#if 0
	/* Send SIGTRAP if we're single-stepping */
	if (ptrace_cancel_bpt(current))
		send_sig(SIGTRAP, current, 1);
#endif
	unlock_user_struct(frame, frame_addr, 0);
        return env->regs[0];

badframe:
	unlock_user_struct(frame, frame_addr, 0);
        force_sig(TARGET_SIGSEGV /* , current */);
	return 0;
}

static abi_ulong *restore_sigframe_v2_vfp(CPUState *env, abi_ulong *regspace)
{
    int i;
    abi_ulong magic, sz;
    uint32_t fpscr, fpexc;
    struct target_vfp_sigframe *vfpframe;
    vfpframe = (struct target_vfp_sigframe *)regspace;

    __get_user(magic, &vfpframe->magic);
    __get_user(sz, &vfpframe->size);
    if (magic != TARGET_VFP_MAGIC || sz != sizeof(*vfpframe)) {
        return 0;
    }
    for (i = 0; i < 32; i++) {
        __get_user(float64_val(env->vfp.regs[i]), &vfpframe->ufp.fpregs[i]);
    }
    __get_user(fpscr, &vfpframe->ufp.fpscr);
    vfp_set_fpscr(env, fpscr);
    __get_user(fpexc, &vfpframe->ufp_exc.fpexc);
    /* Sanitise FPEXC: ensure VFP is enabled, FPINST2 is invalid
     * and the exception flag is cleared
     */
    fpexc |= (1 << 30);
    fpexc &= ~((1 << 31) | (1 << 28));
    env->vfp.xregs[ARM_VFP_FPEXC] = fpexc;
    __get_user(env->vfp.xregs[ARM_VFP_FPINST], &vfpframe->ufp_exc.fpinst);
    __get_user(env->vfp.xregs[ARM_VFP_FPINST2], &vfpframe->ufp_exc.fpinst2);
    return (abi_ulong*)(vfpframe + 1);
}

static abi_ulong *restore_sigframe_v2_iwmmxt(CPUState *env, abi_ulong *regspace)
{
    int i;
    abi_ulong magic, sz;
    struct target_iwmmxt_sigframe *iwmmxtframe;
    iwmmxtframe = (struct target_iwmmxt_sigframe *)regspace;

    __get_user(magic, &iwmmxtframe->magic);
    __get_user(sz, &iwmmxtframe->size);
    if (magic != TARGET_IWMMXT_MAGIC || sz != sizeof(*iwmmxtframe)) {
        return 0;
    }
    for (i = 0; i < 16; i++) {
        __get_user(env->iwmmxt.regs[i], &iwmmxtframe->regs[i]);
    }
    __get_user(env->vfp.xregs[ARM_IWMMXT_wCSSF], &iwmmxtframe->wcssf);
    __get_user(env->vfp.xregs[ARM_IWMMXT_wCASF], &iwmmxtframe->wcssf);
    __get_user(env->vfp.xregs[ARM_IWMMXT_wCGR0], &iwmmxtframe->wcgr0);
    __get_user(env->vfp.xregs[ARM_IWMMXT_wCGR1], &iwmmxtframe->wcgr1);
    __get_user(env->vfp.xregs[ARM_IWMMXT_wCGR2], &iwmmxtframe->wcgr2);
    __get_user(env->vfp.xregs[ARM_IWMMXT_wCGR3], &iwmmxtframe->wcgr3);
    return (abi_ulong*)(iwmmxtframe + 1);
}

static int do_sigframe_return_v2(CPUState *env, target_ulong frame_addr,
                                 struct target_ucontext_v2 *uc)
{
    sigset_t host_set;
    abi_ulong *regspace;

    target_to_host_sigset(&host_set, &uc->tuc_sigmask);
    sigprocmask(SIG_SETMASK, &host_set, NULL);

    if (restore_sigcontext(env, &uc->tuc_mcontext))
        return 1;

    /* Restore coprocessor signal frame */
    regspace = uc->tuc_regspace;
    if (arm_feature(env, ARM_FEATURE_VFP)) {
        regspace = restore_sigframe_v2_vfp(env, regspace);
        if (!regspace) {
            return 1;
        }
    }
    if (arm_feature(env, ARM_FEATURE_IWMMXT)) {
        regspace = restore_sigframe_v2_iwmmxt(env, regspace);
        if (!regspace) {
            return 1;
        }
    }

    if (do_sigaltstack(frame_addr + offsetof(struct target_ucontext_v2, tuc_stack), 0, get_sp_from_cpustate(env)) == -EFAULT)
        return 1;

#if 0
    /* Send SIGTRAP if we're single-stepping */
    if (ptrace_cancel_bpt(current))
            send_sig(SIGTRAP, current, 1);
#endif

    return 0;
}

static long do_sigreturn_v2(CPUState *env)
{
        abi_ulong frame_addr;
	struct sigframe_v2 *frame;

	/*
	 * Since we stacked the signal on a 64-bit boundary,
	 * then 'sp' should be word aligned here.  If it's
	 * not, then the user is trying to mess with us.
	 */
	if (env->regs[13] & 7)
		goto badframe;

        frame_addr = env->regs[13];
	if (!lock_user_struct(VERIFY_READ, frame, frame_addr, 1))
                goto badframe;

        if (do_sigframe_return_v2(env, frame_addr, &frame->uc))
                goto badframe;

	unlock_user_struct(frame, frame_addr, 0);
	return env->regs[0];

badframe:
	unlock_user_struct(frame, frame_addr, 0);
        force_sig(TARGET_SIGSEGV /* , current */);
	return 0;
}

long do_sigreturn(CPUState *env)
{
    if (get_osversion() >= 0x020612) {
        return do_sigreturn_v2(env);
    } else {
        return do_sigreturn_v1(env);
    }
}

static long do_rt_sigreturn_v1(CPUState *env)
{
        abi_ulong frame_addr;
	struct rt_sigframe_v1 *frame;
        sigset_t host_set;

	/*
	 * Since we stacked the signal on a 64-bit boundary,
	 * then 'sp' should be word aligned here.  If it's
	 * not, then the user is trying to mess with us.
	 */
	if (env->regs[13] & 7)
		goto badframe;

        frame_addr = env->regs[13];
	if (!lock_user_struct(VERIFY_READ, frame, frame_addr, 1))
                goto badframe;

        target_to_host_sigset(&host_set, &frame->uc.tuc_sigmask);
        sigprocmask(SIG_SETMASK, &host_set, NULL);

	if (restore_sigcontext(env, &frame->uc.tuc_mcontext))
		goto badframe;

	if (do_sigaltstack(frame_addr + offsetof(struct rt_sigframe_v1, uc.tuc_stack), 0, get_sp_from_cpustate(env)) == -EFAULT)
		goto badframe;

#if 0
	/* Send SIGTRAP if we're single-stepping */
	if (ptrace_cancel_bpt(current))
		send_sig(SIGTRAP, current, 1);
#endif
	unlock_user_struct(frame, frame_addr, 0);
	return env->regs[0];

badframe:
	unlock_user_struct(frame, frame_addr, 0);
        force_sig(TARGET_SIGSEGV /* , current */);
	return 0;
}

static long do_rt_sigreturn_v2(CPUState *env)
{
        abi_ulong frame_addr;
	struct rt_sigframe_v2 *frame;

	/*
	 * Since we stacked the signal on a 64-bit boundary,
	 * then 'sp' should be word aligned here.  If it's
	 * not, then the user is trying to mess with us.
	 */
	if (env->regs[13] & 7)
		goto badframe;

        frame_addr = env->regs[13];
	if (!lock_user_struct(VERIFY_READ, frame, frame_addr, 1))
                goto badframe;

        if (do_sigframe_return_v2(env, frame_addr, &frame->uc))
                goto badframe;

	unlock_user_struct(frame, frame_addr, 0);
	return env->regs[0];

badframe:
	unlock_user_struct(frame, frame_addr, 0);
        force_sig(TARGET_SIGSEGV /* , current */);
	return 0;
}

long do_rt_sigreturn(CPUState *env)
{
    if (get_osversion() >= 0x020612) {
        return do_rt_sigreturn_v2(env);
    } else {
        return do_rt_sigreturn_v1(env);
    }
}

#elif defined(TARGET_SPARC)

#define __SUNOS_MAXWIN   31

/* This is what SunOS does, so shall I. */
struct target_sigcontext {
        abi_ulong sigc_onstack;      /* state to restore */

        abi_ulong sigc_mask;         /* sigmask to restore */
        abi_ulong sigc_sp;           /* stack pointer */
        abi_ulong sigc_pc;           /* program counter */
        abi_ulong sigc_npc;          /* next program counter */
        abi_ulong sigc_psr;          /* for condition codes etc */
        abi_ulong sigc_g1;           /* User uses these two registers */
        abi_ulong sigc_o0;           /* within the trampoline code. */

        /* Now comes information regarding the users window set
         * at the time of the signal.
         */
        abi_ulong sigc_oswins;       /* outstanding windows */

        /* stack ptrs for each regwin buf */
        char *sigc_spbuf[__SUNOS_MAXWIN];

        /* Windows to restore after signal */
        struct {
                abi_ulong locals[8];
                abi_ulong ins[8];
        } sigc_wbuf[__SUNOS_MAXWIN];
};
/* A Sparc stack frame */
struct sparc_stackf {
        abi_ulong locals[8];
        abi_ulong ins[8];
        /* It's simpler to treat fp and callers_pc as elements of ins[]
         * since we never need to access them ourselves.
         */
        char *structptr;
        abi_ulong xargs[6];
        abi_ulong xxargs[1];
};

typedef struct {
        struct {
                abi_ulong psr;
                abi_ulong pc;
                abi_ulong npc;
                abi_ulong y;
                abi_ulong u_regs[16]; /* globals and ins */
        }               si_regs;
        int             si_mask;
} __siginfo_t;

typedef struct {
        unsigned   long si_float_regs [32];
        unsigned   long si_fsr;
        unsigned   long si_fpqdepth;
        struct {
                unsigned long *insn_addr;
                unsigned long insn;
        } si_fpqueue [16];
} qemu_siginfo_fpu_t;


struct target_signal_frame {
	struct sparc_stackf	ss;
	__siginfo_t		info;
	abi_ulong               fpu_save;
	abi_ulong		insns[2] __attribute__ ((aligned (8)));
	abi_ulong		extramask[TARGET_NSIG_WORDS - 1];
	abi_ulong		extra_size; /* Should be 0 */
	qemu_siginfo_fpu_t	fpu_state;
};
struct target_rt_signal_frame {
	struct sparc_stackf	ss;
	siginfo_t		info;
	abi_ulong		regs[20];
	sigset_t		mask;
	abi_ulong               fpu_save;
	unsigned int		insns[2];
	stack_t			stack;
	unsigned int		extra_size; /* Should be 0 */
	qemu_siginfo_fpu_t	fpu_state;
};

#define UREG_O0        16
#define UREG_O6        22
#define UREG_I0        0
#define UREG_I1        1
#define UREG_I2        2
#define UREG_I3        3
#define UREG_I4        4
#define UREG_I5        5
#define UREG_I6        6
#define UREG_I7        7
#define UREG_L0	       8
#define UREG_FP        UREG_I6
#define UREG_SP        UREG_O6

static inline abi_ulong get_sigframe(struct target_sigaction *sa, 
                                     CPUState *env, unsigned long framesize)
{
	abi_ulong sp;

	sp = env->regwptr[UREG_FP];

	/* This is the X/Open sanctioned signal stack switching.  */
	if (sa->sa_flags & TARGET_SA_ONSTACK) {
            if (!on_sig_stack(sp)
                && !((target_sigaltstack_used.ss_sp + target_sigaltstack_used.ss_size) & 7))
                sp = target_sigaltstack_used.ss_sp + target_sigaltstack_used.ss_size;
	}
	return sp - framesize;
}

static int
setup___siginfo(__siginfo_t *si, CPUState *env, abi_ulong mask)
{
	int err = 0, i;

	err |= __put_user(env->psr, &si->si_regs.psr);
	err |= __put_user(env->pc, &si->si_regs.pc);
	err |= __put_user(env->npc, &si->si_regs.npc);
	err |= __put_user(env->y, &si->si_regs.y);
	for (i=0; i < 8; i++) {
		err |= __put_user(env->gregs[i], &si->si_regs.u_regs[i]);
	}
	for (i=0; i < 8; i++) {
		err |= __put_user(env->regwptr[UREG_I0 + i], &si->si_regs.u_regs[i+8]);
	}
	err |= __put_user(mask, &si->si_mask);
	return err;
}

#if 0
static int
setup_sigcontext(struct target_sigcontext *sc, /*struct _fpstate *fpstate,*/
		 CPUState *env, unsigned long mask)
{
	int err = 0;

	err |= __put_user(mask, &sc->sigc_mask);
	err |= __put_user(env->regwptr[UREG_SP], &sc->sigc_sp);
	err |= __put_user(env->pc, &sc->sigc_pc);
	err |= __put_user(env->npc, &sc->sigc_npc);
	err |= __put_user(env->psr, &sc->sigc_psr);
	err |= __put_user(env->gregs[1], &sc->sigc_g1);
	err |= __put_user(env->regwptr[UREG_O0], &sc->sigc_o0);

	return err;
}
#endif
#define NF_ALIGNEDSZ  (((sizeof(struct target_signal_frame) + 7) & (~7)))

static void setup_frame(int sig, struct target_sigaction *ka,
			target_sigset_t *set, CPUState *env)
{
        abi_ulong sf_addr;
	struct target_signal_frame *sf;
	int sigframe_size, err, i;

	/* 1. Make sure everything is clean */
	//synchronize_user_stack();

        sigframe_size = NF_ALIGNEDSZ;
	sf_addr = get_sigframe(ka, env, sigframe_size);

        sf = lock_user(VERIFY_WRITE, sf_addr, 
                       sizeof(struct target_signal_frame), 0);
        if (!sf)
		goto sigsegv;
                
	//fprintf(stderr, "sf: %x pc %x fp %x sp %x\n", sf, env->pc, env->regwptr[UREG_FP], env->regwptr[UREG_SP]);
#if 0
	if (invalid_frame_pointer(sf, sigframe_size))
		goto sigill_and_return;
#endif
	/* 2. Save the current process state */
	err = setup___siginfo(&sf->info, env, set->sig[0]);
	err |= __put_user(0, &sf->extra_size);

	//err |= save_fpu_state(regs, &sf->fpu_state);
	//err |= __put_user(&sf->fpu_state, &sf->fpu_save);

	err |= __put_user(set->sig[0], &sf->info.si_mask);
	for (i = 0; i < TARGET_NSIG_WORDS - 1; i++) {
		err |= __put_user(set->sig[i + 1], &sf->extramask[i]);
	}

	for (i = 0; i < 8; i++) {
	  	err |= __put_user(env->regwptr[i + UREG_L0], &sf->ss.locals[i]);
	}
	for (i = 0; i < 8; i++) {
	  	err |= __put_user(env->regwptr[i + UREG_I0], &sf->ss.ins[i]);
	}
	if (err)
		goto sigsegv;

	/* 3. signal handler back-trampoline and parameters */
	env->regwptr[UREG_FP] = sf_addr;
	env->regwptr[UREG_I0] = sig;
	env->regwptr[UREG_I1] = sf_addr + 
                offsetof(struct target_signal_frame, info);
	env->regwptr[UREG_I2] = sf_addr + 
                offsetof(struct target_signal_frame, info);

	/* 4. signal handler */
	env->pc = ka->_sa_handler;
	env->npc = (env->pc + 4);
	/* 5. return to kernel instructions */
	if (ka->sa_restorer)
		env->regwptr[UREG_I7] = ka->sa_restorer;
	else {
                uint32_t val32;

		env->regwptr[UREG_I7] = sf_addr + 
                        offsetof(struct target_signal_frame, insns) - 2 * 4;

		/* mov __NR_sigreturn, %g1 */
                val32 = 0x821020d8;
		err |= __put_user(val32, &sf->insns[0]);

		/* t 0x10 */
                val32 = 0x91d02010;
		err |= __put_user(val32, &sf->insns[1]);
		if (err)
			goto sigsegv;

		/* Flush instruction space. */
		//flush_sig_insns(current->mm, (unsigned long) &(sf->insns[0]));
                //		tb_flush(env);
	}
        unlock_user(sf, sf_addr, sizeof(struct target_signal_frame));
	return;
#if 0
sigill_and_return:
	force_sig(TARGET_SIGILL);
#endif
sigsegv:
	//fprintf(stderr, "force_sig\n");
        unlock_user(sf, sf_addr, sizeof(struct target_signal_frame));
	force_sig(TARGET_SIGSEGV);
}
static inline int
restore_fpu_state(CPUState *env, qemu_siginfo_fpu_t *fpu)
{
        int err;
#if 0
#ifdef CONFIG_SMP
        if (current->flags & PF_USEDFPU)
                regs->psr &= ~PSR_EF;
#else
        if (current == last_task_used_math) {
                last_task_used_math = 0;
                regs->psr &= ~PSR_EF;
        }
#endif
        current->used_math = 1;
        current->flags &= ~PF_USEDFPU;
#endif
#if 0
        if (verify_area (VERIFY_READ, fpu, sizeof(*fpu)))
                return -EFAULT;
#endif

#if 0
        /* XXX: incorrect */
        err = __copy_from_user(&env->fpr[0], &fpu->si_float_regs[0],
	                             (sizeof(unsigned long) * 32));
#endif
        err |= __get_user(env->fsr, &fpu->si_fsr);
#if 0
        err |= __get_user(current->thread.fpqdepth, &fpu->si_fpqdepth);
        if (current->thread.fpqdepth != 0)
                err |= __copy_from_user(&current->thread.fpqueue[0],
                                        &fpu->si_fpqueue[0],
                                        ((sizeof(unsigned long) +
                                        (sizeof(unsigned long *)))*16));
#endif
        return err;
}


static void setup_rt_frame(int sig, struct target_sigaction *ka,
                           target_siginfo_t *info,
			   target_sigset_t *set, CPUState *env)
{
    fprintf(stderr, "setup_rt_frame: not implemented\n");
}

long do_sigreturn(CPUState *env)
{
        abi_ulong sf_addr;
        struct target_signal_frame *sf;
        uint32_t up_psr, pc, npc;
        target_sigset_t set;
        sigset_t host_set;
        int err, i;

        sf_addr = env->regwptr[UREG_FP];
        if (!lock_user_struct(VERIFY_READ, sf, sf_addr, 1))
                goto segv_and_exit;
#if 0
	fprintf(stderr, "sigreturn\n");
	fprintf(stderr, "sf: %x pc %x fp %x sp %x\n", sf, env->pc, env->regwptr[UREG_FP], env->regwptr[UREG_SP]);
#endif
	//cpu_dump_state(env, stderr, fprintf, 0);

        /* 1. Make sure we are not getting garbage from the user */

        if (sf_addr & 3)
                goto segv_and_exit;

        err = __get_user(pc,  &sf->info.si_regs.pc);
        err |= __get_user(npc, &sf->info.si_regs.npc);

        if ((pc | npc) & 3)
                goto segv_and_exit;

        /* 2. Restore the state */
        err |= __get_user(up_psr, &sf->info.si_regs.psr);

        /* User can only change condition codes and FPU enabling in %psr. */
        env->psr = (up_psr & (PSR_ICC /* | PSR_EF */))
                  | (env->psr & ~(PSR_ICC /* | PSR_EF */));

	env->pc = pc;
	env->npc = npc;
        err |= __get_user(env->y, &sf->info.si_regs.y);
	for (i=0; i < 8; i++) {
		err |= __get_user(env->gregs[i], &sf->info.si_regs.u_regs[i]);
	}
	for (i=0; i < 8; i++) {
		err |= __get_user(env->regwptr[i + UREG_I0], &sf->info.si_regs.u_regs[i+8]);
	}

        /* FIXME: implement FPU save/restore:
         * __get_user(fpu_save, &sf->fpu_save);
         * if (fpu_save)
         *        err |= restore_fpu_state(env, fpu_save);
         */

        /* This is pretty much atomic, no amount locking would prevent
         * the races which exist anyways.
         */
        err |= __get_user(set.sig[0], &sf->info.si_mask);
        for(i = 1; i < TARGET_NSIG_WORDS; i++) {
            err |= (__get_user(set.sig[i], &sf->extramask[i - 1]));
        }

        target_to_host_sigset_internal(&host_set, &set);
        sigprocmask(SIG_SETMASK, &host_set, NULL);

        if (err)
                goto segv_and_exit;
        unlock_user_struct(sf, sf_addr, 0);
        return env->regwptr[0];

segv_and_exit:
        unlock_user_struct(sf, sf_addr, 0);
	force_sig(TARGET_SIGSEGV);
}

long do_rt_sigreturn(CPUState *env)
{
    fprintf(stderr, "do_rt_sigreturn: not implemented\n");
    return -TARGET_ENOSYS;
}

#if defined(TARGET_SPARC64) && !defined(TARGET_ABI32)
#define MC_TSTATE 0
#define MC_PC 1
#define MC_NPC 2
#define MC_Y 3
#define MC_G1 4
#define MC_G2 5
#define MC_G3 6
#define MC_G4 7
#define MC_G5 8
#define MC_G6 9
#define MC_G7 10
#define MC_O0 11
#define MC_O1 12
#define MC_O2 13
#define MC_O3 14
#define MC_O4 15
#define MC_O5 16
#define MC_O6 17
#define MC_O7 18
#define MC_NGREG 19

typedef abi_ulong target_mc_greg_t;
typedef target_mc_greg_t target_mc_gregset_t[MC_NGREG];

struct target_mc_fq {
    abi_ulong *mcfq_addr;
    uint32_t mcfq_insn;
};

struct target_mc_fpu {
    union {
        uint32_t sregs[32];
        uint64_t dregs[32];
        //uint128_t qregs[16];
    } mcfpu_fregs;
    abi_ulong mcfpu_fsr;
    abi_ulong mcfpu_fprs;
    abi_ulong mcfpu_gsr;
    struct target_mc_fq *mcfpu_fq;
    unsigned char mcfpu_qcnt;
    unsigned char mcfpu_qentsz;
    unsigned char mcfpu_enab;
};
typedef struct target_mc_fpu target_mc_fpu_t;

typedef struct {
    target_mc_gregset_t mc_gregs;
    target_mc_greg_t mc_fp;
    target_mc_greg_t mc_i7;
    target_mc_fpu_t mc_fpregs;
} target_mcontext_t;

struct target_ucontext {
    struct target_ucontext *tuc_link;
    abi_ulong tuc_flags;
    target_sigset_t tuc_sigmask;
    target_mcontext_t tuc_mcontext;
};

/* A V9 register window */
struct target_reg_window {
    abi_ulong locals[8];
    abi_ulong ins[8];
};

#define TARGET_STACK_BIAS 2047

/* {set, get}context() needed for 64-bit SparcLinux userland. */
void sparc64_set_context(CPUSPARCState *env)
{
    abi_ulong ucp_addr;
    struct target_ucontext *ucp;
    target_mc_gregset_t *grp;
    abi_ulong pc, npc, tstate;
    abi_ulong fp, i7, w_addr;
    int err;
    unsigned int i;

    ucp_addr = env->regwptr[UREG_I0];
    if (!lock_user_struct(VERIFY_READ, ucp, ucp_addr, 1))
        goto do_sigsegv;
    grp  = &ucp->tuc_mcontext.mc_gregs;
    err  = __get_user(pc, &((*grp)[MC_PC]));
    err |= __get_user(npc, &((*grp)[MC_NPC]));
    if (err || ((pc | npc) & 3))
        goto do_sigsegv;
    if (env->regwptr[UREG_I1]) {
        target_sigset_t target_set;
        sigset_t set;

        if (TARGET_NSIG_WORDS == 1) {
            if (__get_user(target_set.sig[0], &ucp->tuc_sigmask.sig[0]))
                goto do_sigsegv;
        } else {
            abi_ulong *src, *dst;
            src = ucp->tuc_sigmask.sig;
            dst = target_set.sig;
            for (i = 0; i < sizeof(target_sigset_t) / sizeof(abi_ulong);
                 i++, dst++, src++)
                err |= __get_user(*dst, src);
            if (err)
                goto do_sigsegv;
        }
        target_to_host_sigset_internal(&set, &target_set);
        sigprocmask(SIG_SETMASK, &set, NULL);
    }
    env->pc = pc;
    env->npc = npc;
    err |= __get_user(env->y, &((*grp)[MC_Y]));
    err |= __get_user(tstate, &((*grp)[MC_TSTATE]));
    env->asi = (tstate >> 24) & 0xff;
    cpu_put_ccr(env, tstate >> 32);
    cpu_put_cwp64(env, tstate & 0x1f);
    err |= __get_user(env->gregs[1], (&(*grp)[MC_G1]));
    err |= __get_user(env->gregs[2], (&(*grp)[MC_G2]));
    err |= __get_user(env->gregs[3], (&(*grp)[MC_G3]));
    err |= __get_user(env->gregs[4], (&(*grp)[MC_G4]));
    err |= __get_user(env->gregs[5], (&(*grp)[MC_G5]));
    err |= __get_user(env->gregs[6], (&(*grp)[MC_G6]));
    err |= __get_user(env->gregs[7], (&(*grp)[MC_G7]));
    err |= __get_user(env->regwptr[UREG_I0], (&(*grp)[MC_O0]));
    err |= __get_user(env->regwptr[UREG_I1], (&(*grp)[MC_O1]));
    err |= __get_user(env->regwptr[UREG_I2], (&(*grp)[MC_O2]));
    err |= __get_user(env->regwptr[UREG_I3], (&(*grp)[MC_O3]));
    err |= __get_user(env->regwptr[UREG_I4], (&(*grp)[MC_O4]));
    err |= __get_user(env->regwptr[UREG_I5], (&(*grp)[MC_O5]));
    err |= __get_user(env->regwptr[UREG_I6], (&(*grp)[MC_O6]));
    err |= __get_user(env->regwptr[UREG_I7], (&(*grp)[MC_O7]));

    err |= __get_user(fp, &(ucp->tuc_mcontext.mc_fp));
    err |= __get_user(i7, &(ucp->tuc_mcontext.mc_i7));

    w_addr = TARGET_STACK_BIAS+env->regwptr[UREG_I6];
    if (put_user(fp, w_addr + offsetof(struct target_reg_window, ins[6]), 
                 abi_ulong) != 0)
        goto do_sigsegv;
    if (put_user(i7, w_addr + offsetof(struct target_reg_window, ins[7]), 
                 abi_ulong) != 0)
        goto do_sigsegv;
    /* FIXME this does not match how the kernel handles the FPU in
     * its sparc64_set_context implementation. In particular the FPU
     * is only restored if fenab is non-zero in:
     *   __get_user(fenab, &(ucp->tuc_mcontext.mc_fpregs.mcfpu_enab));
     */
    err |= __get_user(env->fprs, &(ucp->tuc_mcontext.mc_fpregs.mcfpu_fprs));
    {
        uint32_t *src = ucp->tuc_mcontext.mc_fpregs.mcfpu_fregs.sregs;
        for (i = 0; i < 64; i++, src++) {
            if (i & 1) {
                err |= __get_user(env->fpr[i/2].l.lower, src);
            } else {
                err |= __get_user(env->fpr[i/2].l.upper, src);
            }
        }
    }
    err |= __get_user(env->fsr,
                      &(ucp->tuc_mcontext.mc_fpregs.mcfpu_fsr));
    err |= __get_user(env->gsr,
                      &(ucp->tuc_mcontext.mc_fpregs.mcfpu_gsr));
    if (err)
        goto do_sigsegv;
    unlock_user_struct(ucp, ucp_addr, 0);
    return;
 do_sigsegv:
    unlock_user_struct(ucp, ucp_addr, 0);
    force_sig(TARGET_SIGSEGV);
}

void sparc64_get_context(CPUSPARCState *env)
{
    abi_ulong ucp_addr;
    struct target_ucontext *ucp;
    target_mc_gregset_t *grp;
    target_mcontext_t *mcp;
    abi_ulong fp, i7, w_addr;
    int err;
    unsigned int i;
    target_sigset_t target_set;
    sigset_t set;

    ucp_addr = env->regwptr[UREG_I0];
    if (!lock_user_struct(VERIFY_WRITE, ucp, ucp_addr, 0))
        goto do_sigsegv;
    
    mcp = &ucp->tuc_mcontext;
    grp = &mcp->mc_gregs;

    /* Skip over the trap instruction, first. */
    env->pc = env->npc;
    env->npc += 4;

    err = 0;

    sigprocmask(0, NULL, &set);
    host_to_target_sigset_internal(&target_set, &set);
    if (TARGET_NSIG_WORDS == 1) {
        err |= __put_user(target_set.sig[0],
                          (abi_ulong *)&ucp->tuc_sigmask);
    } else {
        abi_ulong *src, *dst;
        src = target_set.sig;
        dst = ucp->tuc_sigmask.sig;
        for (i = 0; i < sizeof(target_sigset_t) / sizeof(abi_ulong);
             i++, dst++, src++)
            err |= __put_user(*src, dst);
        if (err)
            goto do_sigsegv;
    }

    /* XXX: tstate must be saved properly */
    //    err |= __put_user(env->tstate, &((*grp)[MC_TSTATE]));
    err |= __put_user(env->pc, &((*grp)[MC_PC]));
    err |= __put_user(env->npc, &((*grp)[MC_NPC]));
    err |= __put_user(env->y, &((*grp)[MC_Y]));
    err |= __put_user(env->gregs[1], &((*grp)[MC_G1]));
    err |= __put_user(env->gregs[2], &((*grp)[MC_G2]));
    err |= __put_user(env->gregs[3], &((*grp)[MC_G3]));
    err |= __put_user(env->gregs[4], &((*grp)[MC_G4]));
    err |= __put_user(env->gregs[5], &((*grp)[MC_G5]));
    err |= __put_user(env->gregs[6], &((*grp)[MC_G6]));
    err |= __put_user(env->gregs[7], &((*grp)[MC_G7]));
    err |= __put_user(env->regwptr[UREG_I0], &((*grp)[MC_O0]));
    err |= __put_user(env->regwptr[UREG_I1], &((*grp)[MC_O1]));
    err |= __put_user(env->regwptr[UREG_I2], &((*grp)[MC_O2]));
    err |= __put_user(env->regwptr[UREG_I3], &((*grp)[MC_O3]));
    err |= __put_user(env->regwptr[UREG_I4], &((*grp)[MC_O4]));
    err |= __put_user(env->regwptr[UREG_I5], &((*grp)[MC_O5]));
    err |= __put_user(env->regwptr[UREG_I6], &((*grp)[MC_O6]));
    err |= __put_user(env->regwptr[UREG_I7], &((*grp)[MC_O7]));

    w_addr = TARGET_STACK_BIAS+env->regwptr[UREG_I6];
    fp = i7 = 0;
    if (get_user(fp, w_addr + offsetof(struct target_reg_window, ins[6]), 
                 abi_ulong) != 0)
        goto do_sigsegv;
    if (get_user(i7, w_addr + offsetof(struct target_reg_window, ins[7]), 
                 abi_ulong) != 0)
        goto do_sigsegv;
    err |= __put_user(fp, &(mcp->mc_fp));
    err |= __put_user(i7, &(mcp->mc_i7));

    {
        uint32_t *dst = ucp->tuc_mcontext.mc_fpregs.mcfpu_fregs.sregs;
        for (i = 0; i < 64; i++, dst++) {
            if (i & 1) {
                err |= __put_user(env->fpr[i/2].l.lower, dst);
            } else {
                err |= __put_user(env->fpr[i/2].l.upper, dst);
            }
        }
    }
    err |= __put_user(env->fsr, &(mcp->mc_fpregs.mcfpu_fsr));
    err |= __put_user(env->gsr, &(mcp->mc_fpregs.mcfpu_gsr));
    err |= __put_user(env->fprs, &(mcp->mc_fpregs.mcfpu_fprs));

    if (err)
        goto do_sigsegv;
    unlock_user_struct(ucp, ucp_addr, 1);
    return;
 do_sigsegv:
    unlock_user_struct(ucp, ucp_addr, 1);
    force_sig(TARGET_SIGSEGV);
}
#endif
#elif defined(TARGET_ABI_MIPSN64)

# warning signal handling not implemented

static void setup_frame(int sig, struct target_sigaction *ka,
			target_sigset_t *set, CPUState *env)
{
    fprintf(stderr, "setup_frame: not implemented\n");
}

static void setup_rt_frame(int sig, struct target_sigaction *ka,
                           target_siginfo_t *info,
			   target_sigset_t *set, CPUState *env)
{
    fprintf(stderr, "setup_rt_frame: not implemented\n");
}

long do_sigreturn(CPUState *env)
{
    fprintf(stderr, "do_sigreturn: not implemented\n");
    return -TARGET_ENOSYS;
}

long do_rt_sigreturn(CPUState *env)
{
    fprintf(stderr, "do_rt_sigreturn: not implemented\n");
    return -TARGET_ENOSYS;
}

#elif defined(TARGET_ABI_MIPSN32)

# warning signal handling not implemented

static void setup_frame(int sig, struct target_sigaction *ka,
			target_sigset_t *set, CPUState *env)
{
    fprintf(stderr, "setup_frame: not implemented\n");
}

static void setup_rt_frame(int sig, struct target_sigaction *ka,
                           target_siginfo_t *info,
			   target_sigset_t *set, CPUState *env)
{
    fprintf(stderr, "setup_rt_frame: not implemented\n");
}

long do_sigreturn(CPUState *env)
{
    fprintf(stderr, "do_sigreturn: not implemented\n");
    return -TARGET_ENOSYS;
}

long do_rt_sigreturn(CPUState *env)
{
    fprintf(stderr, "do_rt_sigreturn: not implemented\n");
    return -TARGET_ENOSYS;
}

#elif defined(TARGET_ABI_MIPSO32)

struct target_sigcontext {
    uint32_t   sc_regmask;     /* Unused */
    uint32_t   sc_status;
    uint64_t   sc_pc;
    uint64_t   sc_regs[32];
    uint64_t   sc_fpregs[32];
    uint32_t   sc_ownedfp;     /* Unused */
    uint32_t   sc_fpc_csr;
    uint32_t   sc_fpc_eir;     /* Unused */
    uint32_t   sc_used_math;
    uint32_t   sc_dsp;         /* dsp status, was sc_ssflags */
    uint32_t   pad0;
    uint64_t   sc_mdhi;
    uint64_t   sc_mdlo;
    target_ulong   sc_hi1;         /* Was sc_cause */
    target_ulong   sc_lo1;         /* Was sc_badvaddr */
    target_ulong   sc_hi2;         /* Was sc_sigset[4] */
    target_ulong   sc_lo2;
    target_ulong   sc_hi3;
    target_ulong   sc_lo3;
};

struct sigframe {
    uint32_t sf_ass[4];			/* argument save space for o32 */
    uint32_t sf_code[2];			/* signal trampoline */
    struct target_sigcontext sf_sc;
    target_sigset_t sf_mask;
};

struct target_ucontext {
    target_ulong tuc_flags;
    target_ulong tuc_link;
    target_stack_t tuc_stack;
    target_ulong pad0;
    struct target_sigcontext tuc_mcontext;
    target_sigset_t tuc_sigmask;
};

struct target_rt_sigframe {
    uint32_t rs_ass[4];               /* argument save space for o32 */
    uint32_t rs_code[2];              /* signal trampoline */
    struct target_siginfo rs_info;
    struct target_ucontext rs_uc;
};

/* Install trampoline to jump back from signal handler */
static inline int install_sigtramp(unsigned int *tramp,   unsigned int syscall)
{
    int err;

    /*
    * Set up the return code ...
    *
    *         li      v0, __NR__foo_sigreturn
    *         syscall
    */

    err = __put_user(0x24020000 + syscall, tramp + 0);
    err |= __put_user(0x0000000c          , tramp + 1);
    /* flush_cache_sigtramp((unsigned long) tramp); */
    return err;
}

static inline int
setup_sigcontext(CPUState *regs, struct target_sigcontext *sc)
{
    int err = 0;

    err |= __put_user(regs->active_tc.PC, &sc->sc_pc);

#define save_gp_reg(i) do {   						\
        err |= __put_user(regs->active_tc.gpr[i], &sc->sc_regs[i]);	\
    } while(0)
    __put_user(0, &sc->sc_regs[0]); save_gp_reg(1); save_gp_reg(2);
    save_gp_reg(3); save_gp_reg(4); save_gp_reg(5); save_gp_reg(6);
    save_gp_reg(7); save_gp_reg(8); save_gp_reg(9); save_gp_reg(10);
    save_gp_reg(11); save_gp_reg(12); save_gp_reg(13); save_gp_reg(14);
    save_gp_reg(15); save_gp_reg(16); save_gp_reg(17); save_gp_reg(18);
    save_gp_reg(19); save_gp_reg(20); save_gp_reg(21); save_gp_reg(22);
    save_gp_reg(23); save_gp_reg(24); save_gp_reg(25); save_gp_reg(26);
    save_gp_reg(27); save_gp_reg(28); save_gp_reg(29); save_gp_reg(30);
    save_gp_reg(31);
#undef save_gp_reg

    err |= __put_user(regs->active_tc.HI[0], &sc->sc_mdhi);
    err |= __put_user(regs->active_tc.LO[0], &sc->sc_mdlo);

    /* Not used yet, but might be useful if we ever have DSP suppport */
#if 0
    if (cpu_has_dsp) {
	err |= __put_user(mfhi1(), &sc->sc_hi1);
	err |= __put_user(mflo1(), &sc->sc_lo1);
	err |= __put_user(mfhi2(), &sc->sc_hi2);
	err |= __put_user(mflo2(), &sc->sc_lo2);
	err |= __put_user(mfhi3(), &sc->sc_hi3);
	err |= __put_user(mflo3(), &sc->sc_lo3);
	err |= __put_user(rddsp(DSP_MASK), &sc->sc_dsp);
    }
    /* same with 64 bit */
#ifdef CONFIG_64BIT
    err |= __put_user(regs->hi, &sc->sc_hi[0]);
    err |= __put_user(regs->lo, &sc->sc_lo[0]);
    if (cpu_has_dsp) {
	err |= __put_user(mfhi1(), &sc->sc_hi[1]);
	err |= __put_user(mflo1(), &sc->sc_lo[1]);
	err |= __put_user(mfhi2(), &sc->sc_hi[2]);
	err |= __put_user(mflo2(), &sc->sc_lo[2]);
	err |= __put_user(mfhi3(), &sc->sc_hi[3]);
	err |= __put_user(mflo3(), &sc->sc_lo[3]);
	err |= __put_user(rddsp(DSP_MASK), &sc->sc_dsp);
    }
#endif
#endif

#if 0
    err |= __put_user(!!used_math(), &sc->sc_used_math);

    if (!used_math())
	goto out;

    /*
    * Save FPU state to signal context.  Signal handler will "inherit"
    * current FPU state.
    */
    preempt_disable();

    if (!is_fpu_owner()) {
	own_fpu();
	restore_fp(current);
    }
    err |= save_fp_context(sc);

    preempt_enable();
    out:
#endif
    return err;
}

static inline int
restore_sigcontext(CPUState *regs, struct target_sigcontext *sc)
{
    int err = 0;

    err |= __get_user(regs->CP0_EPC, &sc->sc_pc);

    err |= __get_user(regs->active_tc.HI[0], &sc->sc_mdhi);
    err |= __get_user(regs->active_tc.LO[0], &sc->sc_mdlo);

#define restore_gp_reg(i) do {   							\
        err |= __get_user(regs->active_tc.gpr[i], &sc->sc_regs[i]);		\
    } while(0)
    restore_gp_reg( 1); restore_gp_reg( 2); restore_gp_reg( 3);
    restore_gp_reg( 4); restore_gp_reg( 5); restore_gp_reg( 6);
    restore_gp_reg( 7); restore_gp_reg( 8); restore_gp_reg( 9);
    restore_gp_reg(10); restore_gp_reg(11); restore_gp_reg(12);
    restore_gp_reg(13); restore_gp_reg(14); restore_gp_reg(15);
    restore_gp_reg(16); restore_gp_reg(17); restore_gp_reg(18);
    restore_gp_reg(19); restore_gp_reg(20); restore_gp_reg(21);
    restore_gp_reg(22); restore_gp_reg(23); restore_gp_reg(24);
    restore_gp_reg(25); restore_gp_reg(26); restore_gp_reg(27);
    restore_gp_reg(28); restore_gp_reg(29); restore_gp_reg(30);
    restore_gp_reg(31);
#undef restore_gp_reg

#if 0
    if (cpu_has_dsp) {
	err |= __get_user(treg, &sc->sc_hi1); mthi1(treg);
	err |= __get_user(treg, &sc->sc_lo1); mtlo1(treg);
	err |= __get_user(treg, &sc->sc_hi2); mthi2(treg);
	err |= __get_user(treg, &sc->sc_lo2); mtlo2(treg);
	err |= __get_user(treg, &sc->sc_hi3); mthi3(treg);
	err |= __get_user(treg, &sc->sc_lo3); mtlo3(treg);
	err |= __get_user(treg, &sc->sc_dsp); wrdsp(treg, DSP_MASK);
    }
#ifdef CONFIG_64BIT
    err |= __get_user(regs->hi, &sc->sc_hi[0]);
    err |= __get_user(regs->lo, &sc->sc_lo[0]);
    if (cpu_has_dsp) {
	err |= __get_user(treg, &sc->sc_hi[1]); mthi1(treg);
	err |= __get_user(treg, &sc->sc_lo[1]); mthi1(treg);
	err |= __get_user(treg, &sc->sc_hi[2]); mthi2(treg);
	err |= __get_user(treg, &sc->sc_lo[2]); mthi2(treg);
	err |= __get_user(treg, &sc->sc_hi[3]); mthi3(treg);
	err |= __get_user(treg, &sc->sc_lo[3]); mthi3(treg);
	err |= __get_user(treg, &sc->sc_dsp); wrdsp(treg, DSP_MASK);
    }
#endif

    err |= __get_user(used_math, &sc->sc_used_math);
    conditional_used_math(used_math);

    preempt_disable();

    if (used_math()) {
	/* restore fpu context if we have used it before */
	own_fpu();
	err |= restore_fp_context(sc);
    } else {
	/* signal handler may have used FPU.  Give it up. */
	lose_fpu();
    }

    preempt_enable();
#endif
    return err;
}
/*
 * Determine which stack to use..
 */
static inline abi_ulong
get_sigframe(struct target_sigaction *ka, CPUState *regs, size_t frame_size)
{
    unsigned long sp;

    /* Default to using normal stack */
    sp = regs->active_tc.gpr[29];

    /*
     * FPU emulator may have it's own trampoline active just
     * above the user stack, 16-bytes before the next lowest
     * 16 byte boundary.  Try to avoid trashing it.
     */
    sp -= 32;

    /* This is the X/Open sanctioned signal stack switching.  */
    if ((ka->sa_flags & TARGET_SA_ONSTACK) && (sas_ss_flags (sp) == 0)) {
        sp = target_sigaltstack_used.ss_sp + target_sigaltstack_used.ss_size;
    }

    return (sp - frame_size) & ~7;
}

/* compare linux/arch/mips/kernel/signal.c:setup_frame() */
static void setup_frame(int sig, struct target_sigaction * ka,
                        target_sigset_t *set, CPUState *regs)
{
    struct sigframe *frame;
    abi_ulong frame_addr;
    int i;

    frame_addr = get_sigframe(ka, regs, sizeof(*frame));
    if (!lock_user_struct(VERIFY_WRITE, frame, frame_addr, 0))
	goto give_sigsegv;

    install_sigtramp(frame->sf_code, TARGET_NR_sigreturn);

    if(setup_sigcontext(regs, &frame->sf_sc))
	goto give_sigsegv;

    for(i = 0; i < TARGET_NSIG_WORDS; i++) {
	if(__put_user(set->sig[i], &frame->sf_mask.sig[i]))
	    goto give_sigsegv;
    }

    /*
    * Arguments to signal handler:
    *
    *   a0 = signal number
    *   a1 = 0 (should be cause)
    *   a2 = pointer to struct sigcontext
    *
    * $25 and PC point to the signal handler, $29 points to the
    * struct sigframe.
    */
    regs->active_tc.gpr[ 4] = sig;
    regs->active_tc.gpr[ 5] = 0;
    regs->active_tc.gpr[ 6] = frame_addr + offsetof(struct sigframe, sf_sc);
    regs->active_tc.gpr[29] = frame_addr;
    regs->active_tc.gpr[31] = frame_addr + offsetof(struct sigframe, sf_code);
    /* The original kernel code sets CP0_EPC to the handler
    * since it returns to userland using eret
    * we cannot do this here, and we must set PC directly */
    regs->active_tc.PC = regs->active_tc.gpr[25] = ka->_sa_handler;
    unlock_user_struct(frame, frame_addr, 1);
    return;

give_sigsegv:
    unlock_user_struct(frame, frame_addr, 1);
    force_sig(TARGET_SIGSEGV/*, current*/);
    return;
}

long do_sigreturn(CPUState *regs)
{
    struct sigframe *frame;
    abi_ulong frame_addr;
    sigset_t blocked;
    target_sigset_t target_set;
    int i;

#if defined(DEBUG_SIGNAL)
    fprintf(stderr, "do_sigreturn\n");
#endif
    frame_addr = regs->active_tc.gpr[29];
    if (!lock_user_struct(VERIFY_READ, frame, frame_addr, 1))
   	goto badframe;

    for(i = 0; i < TARGET_NSIG_WORDS; i++) {
   	if(__get_user(target_set.sig[i], &frame->sf_mask.sig[i]))
	    goto badframe;
    }

    target_to_host_sigset_internal(&blocked, &target_set);
    sigprocmask(SIG_SETMASK, &blocked, NULL);

    if (restore_sigcontext(regs, &frame->sf_sc))
   	goto badframe;

#if 0
    /*
     * Don't let your children do this ...
     */
    __asm__ __volatile__(
   	"move\t$29, %0\n\t"
   	"j\tsyscall_exit"
   	:/* no outputs */
   	:"r" (&regs));
    /* Unreached */
#endif

    regs->active_tc.PC = regs->CP0_EPC;
    /* I am not sure this is right, but it seems to work
    * maybe a problem with nested signals ? */
    regs->CP0_EPC = 0;
    return -TARGET_QEMU_ESIGRETURN;

badframe:
    force_sig(TARGET_SIGSEGV/*, current*/);
    return 0;
}

static void setup_rt_frame(int sig, struct target_sigaction *ka,
                           target_siginfo_t *info,
			   target_sigset_t *set, CPUState *env)
{
    struct target_rt_sigframe *frame;
    abi_ulong frame_addr;
    int i;

    frame_addr = get_sigframe(ka, env, sizeof(*frame));
    if (!lock_user_struct(VERIFY_WRITE, frame, frame_addr, 0))
	goto give_sigsegv;

    install_sigtramp(frame->rs_code, TARGET_NR_rt_sigreturn);

    copy_siginfo_to_user(&frame->rs_info, info);

    __put_user(0, &frame->rs_uc.tuc_flags);
    __put_user(0, &frame->rs_uc.tuc_link);
    __put_user(target_sigaltstack_used.ss_sp, &frame->rs_uc.tuc_stack.ss_sp);
    __put_user(target_sigaltstack_used.ss_size, &frame->rs_uc.tuc_stack.ss_size);
    __put_user(sas_ss_flags(get_sp_from_cpustate(env)),
               &frame->rs_uc.tuc_stack.ss_flags);

    setup_sigcontext(env, &frame->rs_uc.tuc_mcontext);

    for(i = 0; i < TARGET_NSIG_WORDS; i++) {
        __put_user(set->sig[i], &frame->rs_uc.tuc_sigmask.sig[i]);
    }

    /*
    * Arguments to signal handler:
    *
    *   a0 = signal number
    *   a1 = pointer to struct siginfo
    *   a2 = pointer to struct ucontext
    *
    * $25 and PC point to the signal handler, $29 points to the
    * struct sigframe.
    */
    env->active_tc.gpr[ 4] = sig;
    env->active_tc.gpr[ 5] = frame_addr
                             + offsetof(struct target_rt_sigframe, rs_info);
    env->active_tc.gpr[ 6] = frame_addr
                             + offsetof(struct target_rt_sigframe, rs_uc);
    env->active_tc.gpr[29] = frame_addr;
    env->active_tc.gpr[31] = frame_addr
                             + offsetof(struct target_rt_sigframe, rs_code);
    /* The original kernel code sets CP0_EPC to the handler
    * since it returns to userland using eret
    * we cannot do this here, and we must set PC directly */
    env->active_tc.PC = env->active_tc.gpr[25] = ka->_sa_handler;
    unlock_user_struct(frame, frame_addr, 1);
    return;

give_sigsegv:
    unlock_user_struct(frame, frame_addr, 1);
    force_sig(TARGET_SIGSEGV/*, current*/);
    return;
}

long do_rt_sigreturn(CPUState *env)
{
    struct target_rt_sigframe *frame;
    abi_ulong frame_addr;
    sigset_t blocked;

#if defined(DEBUG_SIGNAL)
    fprintf(stderr, "do_rt_sigreturn\n");
#endif
    frame_addr = env->active_tc.gpr[29];
    if (!lock_user_struct(VERIFY_READ, frame, frame_addr, 1))
   	goto badframe;

    target_to_host_sigset(&blocked, &frame->rs_uc.tuc_sigmask);
    sigprocmask(SIG_SETMASK, &blocked, NULL);

    if (restore_sigcontext(env, &frame->rs_uc.tuc_mcontext))
        goto badframe;

    if (do_sigaltstack(frame_addr +
		       offsetof(struct target_rt_sigframe, rs_uc.tuc_stack),
		       0, get_sp_from_cpustate(env)) == -EFAULT)
        goto badframe;

    env->active_tc.PC = env->CP0_EPC;
    /* I am not sure this is right, but it seems to work
    * maybe a problem with nested signals ? */
    env->CP0_EPC = 0;
    return -TARGET_QEMU_ESIGRETURN;

badframe:
    force_sig(TARGET_SIGSEGV/*, current*/);
    return 0;
}

#elif defined(TARGET_SH4)

/*
 * code and data structures from linux kernel:
 * include/asm-sh/sigcontext.h
 * arch/sh/kernel/signal.c
 */

struct target_sigcontext {
    target_ulong  oldmask;

    /* CPU registers */
    target_ulong  sc_gregs[16];
    target_ulong  sc_pc;
    target_ulong  sc_pr;
    target_ulong  sc_sr;
    target_ulong  sc_gbr;
    target_ulong  sc_mach;
    target_ulong  sc_macl;

    /* FPU registers */
    target_ulong  sc_fpregs[16];
    target_ulong  sc_xfpregs[16];
    unsigned int sc_fpscr;
    unsigned int sc_fpul;
    unsigned int sc_ownedfp;
};

struct target_sigframe
{
    struct target_sigcontext sc;
    target_ulong extramask[TARGET_NSIG_WORDS-1];
    uint16_t retcode[3];
};


struct target_ucontext {
    target_ulong tuc_flags;
    struct target_ucontext *tuc_link;
    target_stack_t tuc_stack;
    struct target_sigcontext tuc_mcontext;
    target_sigset_t tuc_sigmask;	/* mask last for extensibility */
};

struct target_rt_sigframe
{
    struct target_siginfo info;
    struct target_ucontext uc;
    uint16_t retcode[3];
};


#define MOVW(n)  (0x9300|((n)-2)) /* Move mem word at PC+n to R3 */
#define TRAP_NOARG 0xc310         /* Syscall w/no args (NR in R3) SH3/4 */

static abi_ulong get_sigframe(struct target_sigaction *ka,
                         unsigned long sp, size_t frame_size)
{
    if ((ka->sa_flags & TARGET_SA_ONSTACK) && (sas_ss_flags(sp) == 0)) {
        sp = target_sigaltstack_used.ss_sp + target_sigaltstack_used.ss_size;
    }

    return (sp - frame_size) & -8ul;
}

static int setup_sigcontext(struct target_sigcontext *sc,
			    CPUState *regs, unsigned long mask)
{
    int err = 0;
    int i;

#define COPY(x)         err |= __put_user(regs->x, &sc->sc_##x)
    COPY(gregs[0]); COPY(gregs[1]);
    COPY(gregs[2]); COPY(gregs[3]);
    COPY(gregs[4]); COPY(gregs[5]);
    COPY(gregs[6]); COPY(gregs[7]);
    COPY(gregs[8]); COPY(gregs[9]);
    COPY(gregs[10]); COPY(gregs[11]);
    COPY(gregs[12]); COPY(gregs[13]);
    COPY(gregs[14]); COPY(gregs[15]);
    COPY(gbr); COPY(mach);
    COPY(macl); COPY(pr);
    COPY(sr); COPY(pc);
#undef COPY

    for (i=0; i<16; i++) {
        err |= __put_user(regs->fregs[i], &sc->sc_fpregs[i]);
    }
    err |= __put_user(regs->fpscr, &sc->sc_fpscr);
    err |= __put_user(regs->fpul, &sc->sc_fpul);

    /* non-iBCS2 extensions.. */
    err |= __put_user(mask, &sc->oldmask);

    return err;
}

static int restore_sigcontext(CPUState *regs, struct target_sigcontext *sc,
                              target_ulong *r0_p)
{
    unsigned int err = 0;
    int i;

#define COPY(x)         err |= __get_user(regs->x, &sc->sc_##x)
    COPY(gregs[1]);
    COPY(gregs[2]); COPY(gregs[3]);
    COPY(gregs[4]); COPY(gregs[5]);
    COPY(gregs[6]); COPY(gregs[7]);
    COPY(gregs[8]); COPY(gregs[9]);
    COPY(gregs[10]); COPY(gregs[11]);
    COPY(gregs[12]); COPY(gregs[13]);
    COPY(gregs[14]); COPY(gregs[15]);
    COPY(gbr); COPY(mach);
    COPY(macl); COPY(pr);
    COPY(sr); COPY(pc);
#undef COPY

    for (i=0; i<16; i++) {
        err |= __get_user(regs->fregs[i], &sc->sc_fpregs[i]);
    }
    err |= __get_user(regs->fpscr, &sc->sc_fpscr);
    err |= __get_user(regs->fpul, &sc->sc_fpul);

    regs->tra = -1;         /* disable syscall checks */
    err |= __get_user(*r0_p, &sc->sc_gregs[0]);
    return err;
}

static void setup_frame(int sig, struct target_sigaction *ka,
			target_sigset_t *set, CPUState *regs)
{
    struct target_sigframe *frame;
    abi_ulong frame_addr;
    int i;
    int err = 0;
    int signal;

    frame_addr = get_sigframe(ka, regs->gregs[15], sizeof(*frame));
    if (!lock_user_struct(VERIFY_WRITE, frame, frame_addr, 0))
	goto give_sigsegv;

    signal = current_exec_domain_sig(sig);

    err |= setup_sigcontext(&frame->sc, regs, set->sig[0]);

    for (i = 0; i < TARGET_NSIG_WORDS - 1; i++) {
        err |= __put_user(set->sig[i + 1], &frame->extramask[i]);
    }

    /* Set up to return from userspace.  If provided, use a stub
       already in userspace.  */
    if (ka->sa_flags & TARGET_SA_RESTORER) {
        regs->pr = (unsigned long) ka->sa_restorer;
    } else {
        /* Generate return code (system call to sigreturn) */
        err |= __put_user(MOVW(2), &frame->retcode[0]);
        err |= __put_user(TRAP_NOARG, &frame->retcode[1]);
        err |= __put_user((TARGET_NR_sigreturn), &frame->retcode[2]);
        regs->pr = (unsigned long) frame->retcode;
    }

    if (err)
        goto give_sigsegv;

    /* Set up registers for signal handler */
    regs->gregs[15] = frame_addr;
    regs->gregs[4] = signal; /* Arg for signal handler */
    regs->gregs[5] = 0;
    regs->gregs[6] = frame_addr += offsetof(typeof(*frame), sc);
    regs->pc = (unsigned long) ka->_sa_handler;

    unlock_user_struct(frame, frame_addr, 1);
    return;

give_sigsegv:
    unlock_user_struct(frame, frame_addr, 1);
    force_sig(TARGET_SIGSEGV);
}

static void setup_rt_frame(int sig, struct target_sigaction *ka,
                           target_siginfo_t *info,
			   target_sigset_t *set, CPUState *regs)
{
    struct target_rt_sigframe *frame;
    abi_ulong frame_addr;
    int i;
    int err = 0;
    int signal;

    frame_addr = get_sigframe(ka, regs->gregs[15], sizeof(*frame));
    if (!lock_user_struct(VERIFY_WRITE, frame, frame_addr, 0))
	goto give_sigsegv;

    signal = current_exec_domain_sig(sig);

    err |= copy_siginfo_to_user(&frame->info, info);

    /* Create the ucontext.  */
    err |= __put_user(0, &frame->uc.tuc_flags);
    err |= __put_user(0, (unsigned long *)&frame->uc.tuc_link);
    err |= __put_user((unsigned long)target_sigaltstack_used.ss_sp,
		      &frame->uc.tuc_stack.ss_sp);
    err |= __put_user(sas_ss_flags(regs->gregs[15]),
		      &frame->uc.tuc_stack.ss_flags);
    err |= __put_user(target_sigaltstack_used.ss_size,
		      &frame->uc.tuc_stack.ss_size);
    err |= setup_sigcontext(&frame->uc.tuc_mcontext,
			    regs, set->sig[0]);
    for(i = 0; i < TARGET_NSIG_WORDS; i++) {
        err |= __put_user(set->sig[i], &frame->uc.tuc_sigmask.sig[i]);
    }

    /* Set up to return from userspace.  If provided, use a stub
       already in userspace.  */
    if (ka->sa_flags & TARGET_SA_RESTORER) {
        regs->pr = (unsigned long) ka->sa_restorer;
    } else {
        /* Generate return code (system call to sigreturn) */
        err |= __put_user(MOVW(2), &frame->retcode[0]);
        err |= __put_user(TRAP_NOARG, &frame->retcode[1]);
        err |= __put_user((TARGET_NR_rt_sigreturn), &frame->retcode[2]);
        regs->pr = (unsigned long) frame->retcode;
    }

    if (err)
        goto give_sigsegv;

    /* Set up registers for signal handler */
    regs->gregs[15] = frame_addr;
    regs->gregs[4] = signal; /* Arg for signal handler */
    regs->gregs[5] = frame_addr + offsetof(typeof(*frame), info);
    regs->gregs[6] = frame_addr + offsetof(typeof(*frame), uc);
    regs->pc = (unsigned long) ka->_sa_handler;

    unlock_user_struct(frame, frame_addr, 1);
    return;

give_sigsegv:
    unlock_user_struct(frame, frame_addr, 1);
    force_sig(TARGET_SIGSEGV);
}

long do_sigreturn(CPUState *regs)
{
    struct target_sigframe *frame;
    abi_ulong frame_addr;
    sigset_t blocked;
    target_sigset_t target_set;
    target_ulong r0;
    int i;
    int err = 0;

#if defined(DEBUG_SIGNAL)
    fprintf(stderr, "do_sigreturn\n");
#endif
    frame_addr = regs->gregs[15];
    if (!lock_user_struct(VERIFY_READ, frame, frame_addr, 1))
   	goto badframe;

    err |= __get_user(target_set.sig[0], &frame->sc.oldmask);
    for(i = 1; i < TARGET_NSIG_WORDS; i++) {
        err |= (__get_user(target_set.sig[i], &frame->extramask[i - 1]));
    }

    if (err)
        goto badframe;

    target_to_host_sigset_internal(&blocked, &target_set);
    sigprocmask(SIG_SETMASK, &blocked, NULL);

    if (restore_sigcontext(regs, &frame->sc, &r0))
        goto badframe;

    unlock_user_struct(frame, frame_addr, 0);
    return r0;

badframe:
    unlock_user_struct(frame, frame_addr, 0);
    force_sig(TARGET_SIGSEGV);
    return 0;
}

long do_rt_sigreturn(CPUState *regs)
{
    struct target_rt_sigframe *frame;
    abi_ulong frame_addr;
    sigset_t blocked;
    target_ulong r0;

#if defined(DEBUG_SIGNAL)
    fprintf(stderr, "do_rt_sigreturn\n");
#endif
    frame_addr = regs->gregs[15];
    if (!lock_user_struct(VERIFY_READ, frame, frame_addr, 1))
   	goto badframe;

    target_to_host_sigset(&blocked, &frame->uc.tuc_sigmask);
    sigprocmask(SIG_SETMASK, &blocked, NULL);

    if (restore_sigcontext(regs, &frame->uc.tuc_mcontext, &r0))
        goto badframe;

    if (do_sigaltstack(frame_addr +
		       offsetof(struct target_rt_sigframe, uc.tuc_stack),
		       0, get_sp_from_cpustate(regs)) == -EFAULT)
        goto badframe;

    unlock_user_struct(frame, frame_addr, 0);
    return r0;

badframe:
    unlock_user_struct(frame, frame_addr, 0);
    force_sig(TARGET_SIGSEGV);
    return 0;
}
#elif defined(TARGET_MICROBLAZE)

struct target_sigcontext {
    struct target_pt_regs regs;  /* needs to be first */
    uint32_t oldmask;
};

struct target_stack_t {
    abi_ulong ss_sp;
    int ss_flags;
    unsigned int ss_size;
};

struct target_ucontext {
    abi_ulong tuc_flags;
    abi_ulong tuc_link;
    struct target_stack_t tuc_stack;
    struct target_sigcontext tuc_mcontext;
    uint32_t tuc_extramask[TARGET_NSIG_WORDS - 1];
};

/* Signal frames. */
struct target_signal_frame {
    struct target_ucontext uc;
    uint32_t extramask[TARGET_NSIG_WORDS - 1];
    uint32_t tramp[2];
};

struct rt_signal_frame {
    struct siginfo info;
    struct ucontext uc;
    uint32_t tramp[2];
};

static void setup_sigcontext(struct target_sigcontext *sc, CPUState *env)
{
    __put_user(env->regs[0], &sc->regs.r0);
    __put_user(env->regs[1], &sc->regs.r1);
    __put_user(env->regs[2], &sc->regs.r2);
    __put_user(env->regs[3], &sc->regs.r3);
    __put_user(env->regs[4], &sc->regs.r4);
    __put_user(env->regs[5], &sc->regs.r5);
    __put_user(env->regs[6], &sc->regs.r6);
    __put_user(env->regs[7], &sc->regs.r7);
    __put_user(env->regs[8], &sc->regs.r8);
    __put_user(env->regs[9], &sc->regs.r9);
    __put_user(env->regs[10], &sc->regs.r10);
    __put_user(env->regs[11], &sc->regs.r11);
    __put_user(env->regs[12], &sc->regs.r12);
    __put_user(env->regs[13], &sc->regs.r13);
    __put_user(env->regs[14], &sc->regs.r14);
    __put_user(env->regs[15], &sc->regs.r15);
    __put_user(env->regs[16], &sc->regs.r16);
    __put_user(env->regs[17], &sc->regs.r17);
    __put_user(env->regs[18], &sc->regs.r18);
    __put_user(env->regs[19], &sc->regs.r19);
    __put_user(env->regs[20], &sc->regs.r20);
    __put_user(env->regs[21], &sc->regs.r21);
    __put_user(env->regs[22], &sc->regs.r22);
    __put_user(env->regs[23], &sc->regs.r23);
    __put_user(env->regs[24], &sc->regs.r24);
    __put_user(env->regs[25], &sc->regs.r25);
    __put_user(env->regs[26], &sc->regs.r26);
    __put_user(env->regs[27], &sc->regs.r27);
    __put_user(env->regs[28], &sc->regs.r28);
    __put_user(env->regs[29], &sc->regs.r29);
    __put_user(env->regs[30], &sc->regs.r30);
    __put_user(env->regs[31], &sc->regs.r31);
    __put_user(env->sregs[SR_PC], &sc->regs.pc);
}

static void restore_sigcontext(struct target_sigcontext *sc, CPUState *env)
{
    __get_user(env->regs[0], &sc->regs.r0);
    __get_user(env->regs[1], &sc->regs.r1);
    __get_user(env->regs[2], &sc->regs.r2);
    __get_user(env->regs[3], &sc->regs.r3);
    __get_user(env->regs[4], &sc->regs.r4);
    __get_user(env->regs[5], &sc->regs.r5);
    __get_user(env->regs[6], &sc->regs.r6);
    __get_user(env->regs[7], &sc->regs.r7);
    __get_user(env->regs[8], &sc->regs.r8);
    __get_user(env->regs[9], &sc->regs.r9);
    __get_user(env->regs[10], &sc->regs.r10);
    __get_user(env->regs[11], &sc->regs.r11);
    __get_user(env->regs[12], &sc->regs.r12);
    __get_user(env->regs[13], &sc->regs.r13);
    __get_user(env->regs[14], &sc->regs.r14);
    __get_user(env->regs[15], &sc->regs.r15);
    __get_user(env->regs[16], &sc->regs.r16);
    __get_user(env->regs[17], &sc->regs.r17);
    __get_user(env->regs[18], &sc->regs.r18);
    __get_user(env->regs[19], &sc->regs.r19);
    __get_user(env->regs[20], &sc->regs.r20);
    __get_user(env->regs[21], &sc->regs.r21);
    __get_user(env->regs[22], &sc->regs.r22);
    __get_user(env->regs[23], &sc->regs.r23);
    __get_user(env->regs[24], &sc->regs.r24);
    __get_user(env->regs[25], &sc->regs.r25);
    __get_user(env->regs[26], &sc->regs.r26);
    __get_user(env->regs[27], &sc->regs.r27);
    __get_user(env->regs[28], &sc->regs.r28);
    __get_user(env->regs[29], &sc->regs.r29);
    __get_user(env->regs[30], &sc->regs.r30);
    __get_user(env->regs[31], &sc->regs.r31);
    __get_user(env->sregs[SR_PC], &sc->regs.pc);
}

static abi_ulong get_sigframe(struct target_sigaction *ka,
                              CPUState *env, int frame_size)
{
    abi_ulong sp = env->regs[1];

    if ((ka->sa_flags & SA_ONSTACK) != 0 && !on_sig_stack(sp))
        sp = target_sigaltstack_used.ss_sp + target_sigaltstack_used.ss_size;

    return ((sp - frame_size) & -8UL);
}

static void setup_frame(int sig, struct target_sigaction *ka,
			target_sigset_t *set, CPUState *env)
{
    struct target_signal_frame *frame;
    abi_ulong frame_addr;
    int err = 0;
    int i;

    frame_addr = get_sigframe(ka, env, sizeof *frame);
    if (!lock_user_struct(VERIFY_WRITE, frame, frame_addr, 0))
        goto badframe;

    /* Save the mask.  */
    err |= __put_user(set->sig[0], &frame->uc.tuc_mcontext.oldmask);
    if (err)
        goto badframe;

    for(i = 1; i < TARGET_NSIG_WORDS; i++) {
        if (__put_user(set->sig[i], &frame->extramask[i - 1]))
            goto badframe;
    }

    setup_sigcontext(&frame->uc.tuc_mcontext, env);

    /* Set up to return from userspace. If provided, use a stub
       already in userspace. */
    /* minus 8 is offset to cater for "rtsd r15,8" offset */
    if (ka->sa_flags & TARGET_SA_RESTORER) {
        env->regs[15] = ((unsigned long)ka->sa_restorer)-8;
    } else {
        uint32_t t;
        /* Note, these encodings are _big endian_! */
        /* addi r12, r0, __NR_sigreturn */
        t = 0x31800000UL | TARGET_NR_sigreturn;
        err |= __put_user(t, frame->tramp + 0);
        /* brki r14, 0x8 */
        t = 0xb9cc0008UL;
        err |= __put_user(t, frame->tramp + 1);

        /* Return from sighandler will jump to the tramp.
           Negative 8 offset because return is rtsd r15, 8 */
        env->regs[15] = ((unsigned long)frame->tramp) - 8;
    }

    if (err)
        goto badframe;

    /* Set up registers for signal handler */
    env->regs[1] = frame_addr;
    /* Signal handler args: */
    env->regs[5] = sig; /* Arg 0: signum */
    env->regs[6] = 0;
    /* arg 1: sigcontext */
    env->regs[7] = frame_addr += offsetof(typeof(*frame), uc);

    /* Offset of 4 to handle microblaze rtid r14, 0 */
    env->sregs[SR_PC] = (unsigned long)ka->_sa_handler;

    unlock_user_struct(frame, frame_addr, 1);
    return;
  badframe:
    unlock_user_struct(frame, frame_addr, 1);
    force_sig(TARGET_SIGSEGV);
}

static void setup_rt_frame(int sig, struct target_sigaction *ka,
                           target_siginfo_t *info,
			   target_sigset_t *set, CPUState *env)
{
    fprintf(stderr, "Microblaze setup_rt_frame: not implemented\n");
}

long do_sigreturn(CPUState *env)
{
    struct target_signal_frame *frame;
    abi_ulong frame_addr;
    target_sigset_t target_set;
    sigset_t set;
    int i;

    frame_addr = env->regs[R_SP];
    /* Make sure the guest isn't playing games.  */
    if (!lock_user_struct(VERIFY_WRITE, frame, frame_addr, 1))
        goto badframe;

    /* Restore blocked signals */
    if (__get_user(target_set.sig[0], &frame->uc.tuc_mcontext.oldmask))
        goto badframe;
    for(i = 1; i < TARGET_NSIG_WORDS; i++) {
        if (__get_user(target_set.sig[i], &frame->extramask[i - 1]))
            goto badframe;
    }
    target_to_host_sigset_internal(&set, &target_set);
    sigprocmask(SIG_SETMASK, &set, NULL);

    restore_sigcontext(&frame->uc.tuc_mcontext, env);
    /* We got here through a sigreturn syscall, our path back is via an
       rtb insn so setup r14 for that.  */
    env->regs[14] = env->sregs[SR_PC];
 
    unlock_user_struct(frame, frame_addr, 0);
    return env->regs[10];
  badframe:
    unlock_user_struct(frame, frame_addr, 0);
    force_sig(TARGET_SIGSEGV);
}

long do_rt_sigreturn(CPUState *env)
{
    fprintf(stderr, "Microblaze do_rt_sigreturn: not implemented\n");
    return -TARGET_ENOSYS;
}

#elif defined(TARGET_CRIS)

struct target_sigcontext {
        struct target_pt_regs regs;  /* needs to be first */
        uint32_t oldmask;
        uint32_t usp;    /* usp before stacking this gunk on it */
};

/* Signal frames. */
struct target_signal_frame {
        struct target_sigcontext sc;
        uint32_t extramask[TARGET_NSIG_WORDS - 1];
        uint8_t retcode[8];       /* Trampoline code. */
};

struct rt_signal_frame {
        struct siginfo *pinfo;
        void *puc;
        struct siginfo info;
        struct ucontext uc;
        uint8_t retcode[8];       /* Trampoline code. */
};

static void setup_sigcontext(struct target_sigcontext *sc, CPUState *env)
{
	__put_user(env->regs[0], &sc->regs.r0);
	__put_user(env->regs[1], &sc->regs.r1);
	__put_user(env->regs[2], &sc->regs.r2);
	__put_user(env->regs[3], &sc->regs.r3);
	__put_user(env->regs[4], &sc->regs.r4);
	__put_user(env->regs[5], &sc->regs.r5);
	__put_user(env->regs[6], &sc->regs.r6);
	__put_user(env->regs[7], &sc->regs.r7);
	__put_user(env->regs[8], &sc->regs.r8);
	__put_user(env->regs[9], &sc->regs.r9);
	__put_user(env->regs[10], &sc->regs.r10);
	__put_user(env->regs[11], &sc->regs.r11);
	__put_user(env->regs[12], &sc->regs.r12);
	__put_user(env->regs[13], &sc->regs.r13);
	__put_user(env->regs[14], &sc->usp);
	__put_user(env->regs[15], &sc->regs.acr);
	__put_user(env->pregs[PR_MOF], &sc->regs.mof);
	__put_user(env->pregs[PR_SRP], &sc->regs.srp);
	__put_user(env->pc, &sc->regs.erp);
}

static void restore_sigcontext(struct target_sigcontext *sc, CPUState *env)
{
	__get_user(env->regs[0], &sc->regs.r0);
	__get_user(env->regs[1], &sc->regs.r1);
	__get_user(env->regs[2], &sc->regs.r2);
	__get_user(env->regs[3], &sc->regs.r3);
	__get_user(env->regs[4], &sc->regs.r4);
	__get_user(env->regs[5], &sc->regs.r5);
	__get_user(env->regs[6], &sc->regs.r6);
	__get_user(env->regs[7], &sc->regs.r7);
	__get_user(env->regs[8], &sc->regs.r8);
	__get_user(env->regs[9], &sc->regs.r9);
	__get_user(env->regs[10], &sc->regs.r10);
	__get_user(env->regs[11], &sc->regs.r11);
	__get_user(env->regs[12], &sc->regs.r12);
	__get_user(env->regs[13], &sc->regs.r13);
	__get_user(env->regs[14], &sc->usp);
	__get_user(env->regs[15], &sc->regs.acr);
	__get_user(env->pregs[PR_MOF], &sc->regs.mof);
	__get_user(env->pregs[PR_SRP], &sc->regs.srp);
	__get_user(env->pc, &sc->regs.erp);
}

static abi_ulong get_sigframe(CPUState *env, int framesize)
{
	abi_ulong sp;
	/* Align the stack downwards to 4.  */
	sp = (env->regs[R_SP] & ~3);
	return sp - framesize;
}

static void setup_frame(int sig, struct target_sigaction *ka,
			target_sigset_t *set, CPUState *env)
{
	struct target_signal_frame *frame;
	abi_ulong frame_addr;
	int err = 0;
	int i;

	frame_addr = get_sigframe(env, sizeof *frame);
	if (!lock_user_struct(VERIFY_WRITE, frame, frame_addr, 0))
		goto badframe;

	/*
	 * The CRIS signal return trampoline. A real linux/CRIS kernel doesn't
	 * use this trampoline anymore but it sets it up for GDB.
	 * In QEMU, using the trampoline simplifies things a bit so we use it.
	 *
	 * This is movu.w __NR_sigreturn, r9; break 13;
	 */
	err |= __put_user(0x9c5f, frame->retcode+0);
	err |= __put_user(TARGET_NR_sigreturn, 
			  frame->retcode+2);
	err |= __put_user(0xe93d, frame->retcode+4);

	/* Save the mask.  */
	err |= __put_user(set->sig[0], &frame->sc.oldmask);
	if (err)
		goto badframe;

	for(i = 1; i < TARGET_NSIG_WORDS; i++) {
		if (__put_user(set->sig[i], &frame->extramask[i - 1]))
			goto badframe;
	}

	setup_sigcontext(&frame->sc, env);

	/* Move the stack and setup the arguments for the handler.  */
	env->regs[R_SP] = frame_addr;
	env->regs[10] = sig;
	env->pc = (unsigned long) ka->_sa_handler;
	/* Link SRP so the guest returns through the trampoline.  */
	env->pregs[PR_SRP] = frame_addr + offsetof(typeof(*frame), retcode);

	unlock_user_struct(frame, frame_addr, 1);
	return;
  badframe:
	unlock_user_struct(frame, frame_addr, 1);
	force_sig(TARGET_SIGSEGV);
}

static void setup_rt_frame(int sig, struct target_sigaction *ka,
                           target_siginfo_t *info,
			   target_sigset_t *set, CPUState *env)
{
    fprintf(stderr, "CRIS setup_rt_frame: not implemented\n");
}

long do_sigreturn(CPUState *env)
{
	struct target_signal_frame *frame;
	abi_ulong frame_addr;
	target_sigset_t target_set;
	sigset_t set;
	int i;

	frame_addr = env->regs[R_SP];
	/* Make sure the guest isn't playing games.  */
	if (!lock_user_struct(VERIFY_WRITE, frame, frame_addr, 1))
		goto badframe;

	/* Restore blocked signals */
	if (__get_user(target_set.sig[0], &frame->sc.oldmask))
		goto badframe;
	for(i = 1; i < TARGET_NSIG_WORDS; i++) {
		if (__get_user(target_set.sig[i], &frame->extramask[i - 1]))
			goto badframe;
	}
	target_to_host_sigset_internal(&set, &target_set);
	sigprocmask(SIG_SETMASK, &set, NULL);

	restore_sigcontext(&frame->sc, env);
	unlock_user_struct(frame, frame_addr, 0);
	return env->regs[10];
  badframe:
	unlock_user_struct(frame, frame_addr, 0);
	force_sig(TARGET_SIGSEGV);
}

long do_rt_sigreturn(CPUState *env)
{
    fprintf(stderr, "CRIS do_rt_sigreturn: not implemented\n");
    return -TARGET_ENOSYS;
}

#elif defined(TARGET_S390X)

#define __NUM_GPRS 16
#define __NUM_FPRS 16
#define __NUM_ACRS 16

#define S390_SYSCALL_SIZE   2
#define __SIGNAL_FRAMESIZE      160 /* FIXME: 31-bit mode -> 96 */

#define _SIGCONTEXT_NSIG        64
#define _SIGCONTEXT_NSIG_BPW    64 /* FIXME: 31-bit mode -> 32 */
#define _SIGCONTEXT_NSIG_WORDS  (_SIGCONTEXT_NSIG / _SIGCONTEXT_NSIG_BPW)
#define _SIGMASK_COPY_SIZE    (sizeof(unsigned long)*_SIGCONTEXT_NSIG_WORDS)
#define PSW_ADDR_AMODE            0x0000000000000000UL /* 0x80000000UL for 31-bit */
#define S390_SYSCALL_OPCODE ((uint16_t)0x0a00)

typedef struct {
    target_psw_t psw;
    target_ulong gprs[__NUM_GPRS];
    unsigned int acrs[__NUM_ACRS];
} target_s390_regs_common;

typedef struct {
    unsigned int fpc;
    double   fprs[__NUM_FPRS];
} target_s390_fp_regs;

typedef struct {
    target_s390_regs_common regs;
    target_s390_fp_regs     fpregs;
} target_sigregs;

struct target_sigcontext {
    target_ulong   oldmask[_SIGCONTEXT_NSIG_WORDS];
    target_sigregs *sregs;
};

typedef struct {
    uint8_t callee_used_stack[__SIGNAL_FRAMESIZE];
    struct target_sigcontext sc;
    target_sigregs sregs;
    int signo;
    uint8_t retcode[S390_SYSCALL_SIZE];
} sigframe;

struct target_ucontext {
    target_ulong tuc_flags;
    struct target_ucontext *tuc_link;
    target_stack_t tuc_stack;
    target_sigregs tuc_mcontext;
    target_sigset_t tuc_sigmask;   /* mask last for extensibility */
};

typedef struct {
    uint8_t callee_used_stack[__SIGNAL_FRAMESIZE];
    uint8_t retcode[S390_SYSCALL_SIZE];
    struct target_siginfo info;
    struct target_ucontext uc;
} rt_sigframe;

static inline abi_ulong
get_sigframe(struct target_sigaction *ka, CPUState *env, size_t frame_size)
{
    abi_ulong sp;

    /* Default to using normal stack */
    sp = env->regs[15];

    /* This is the X/Open sanctioned signal stack switching.  */
    if (ka->sa_flags & TARGET_SA_ONSTACK) {
        if (!sas_ss_flags(sp)) {
            sp = target_sigaltstack_used.ss_sp +
                 target_sigaltstack_used.ss_size;
        }
    }

    /* This is the legacy signal stack switching. */
    else if (/* FIXME !user_mode(regs) */ 0 &&
             !(ka->sa_flags & TARGET_SA_RESTORER) &&
             ka->sa_restorer) {
        sp = (abi_ulong) ka->sa_restorer;
    }

    return (sp - frame_size) & -8ul;
}

static void save_sigregs(CPUState *env, target_sigregs *sregs)
{
    int i;
    //save_access_regs(current->thread.acrs); FIXME

    /* Copy a 'clean' PSW mask to the user to avoid leaking
       information about whether PER is currently on.  */
    __put_user(env->psw.mask, &sregs->regs.psw.mask);
    __put_user(env->psw.addr, &sregs->regs.psw.addr);
    for (i = 0; i < 16; i++) {
        __put_user(env->regs[i], &sregs->regs.gprs[i]);
    }
    for (i = 0; i < 16; i++) {
        __put_user(env->aregs[i], &sregs->regs.acrs[i]);
    }
    /*
     * We have to store the fp registers to current->thread.fp_regs
     * to merge them with the emulated registers.
     */
    //save_fp_regs(&current->thread.fp_regs); FIXME
    for (i = 0; i < 16; i++) {
        __put_user(env->fregs[i].ll, &sregs->fpregs.fprs[i]);
    }
}

static void setup_frame(int sig, struct target_sigaction *ka,
			target_sigset_t *set, CPUState *env)
{
    sigframe *frame;
    abi_ulong frame_addr;

    frame_addr = get_sigframe(ka, env, sizeof(*frame));
    qemu_log("%s: frame_addr 0x%llx\n", __FUNCTION__,
             (unsigned long long)frame_addr);
    if (!lock_user_struct(VERIFY_WRITE, frame, frame_addr, 0)) {
            goto give_sigsegv;
    }

    qemu_log("%s: 1\n", __FUNCTION__);
    if (__put_user(set->sig[0], &frame->sc.oldmask[0])) {
              goto give_sigsegv;
    }

    save_sigregs(env, &frame->sregs);

    __put_user((abi_ulong)(unsigned long)&frame->sregs,
               (abi_ulong *)&frame->sc.sregs);

    /* Set up to return from userspace.  If provided, use a stub
       already in userspace.  */
    if (ka->sa_flags & TARGET_SA_RESTORER) {
            env->regs[14] = (unsigned long)
                    ka->sa_restorer | PSW_ADDR_AMODE;
    } else {
            env->regs[14] = (unsigned long)
                    frame->retcode | PSW_ADDR_AMODE;
            if (__put_user(S390_SYSCALL_OPCODE | TARGET_NR_sigreturn,
                           (uint16_t *)(frame->retcode)))
                    goto give_sigsegv;
    }

    /* Set up backchain. */
    if (__put_user(env->regs[15], (abi_ulong *) frame)) {
            goto give_sigsegv;
    }

    /* Set up registers for signal handler */
    env->regs[15] = frame_addr;
    env->psw.addr = (target_ulong) ka->_sa_handler | PSW_ADDR_AMODE;

    env->regs[2] = sig; //map_signal(sig);
    env->regs[3] = frame_addr += offsetof(typeof(*frame), sc);

    /* We forgot to include these in the sigcontext.
       To avoid breaking binary compatibility, they are passed as args. */
    env->regs[4] = 0; // FIXME: no clue... current->thread.trap_no;
    env->regs[5] = 0; // FIXME: no clue... current->thread.prot_addr;

    /* Place signal number on stack to allow backtrace from handler.  */
    if (__put_user(env->regs[2], (int *) &frame->signo)) {
            goto give_sigsegv;
    }
    unlock_user_struct(frame, frame_addr, 1);
    return;

give_sigsegv:
    qemu_log("%s: give_sigsegv\n", __FUNCTION__);
    unlock_user_struct(frame, frame_addr, 1);
    force_sig(TARGET_SIGSEGV);
}

static void setup_rt_frame(int sig, struct target_sigaction *ka,
                           target_siginfo_t *info,
                           target_sigset_t *set, CPUState *env)
{
    int i;
    rt_sigframe *frame;
    abi_ulong frame_addr;

    frame_addr = get_sigframe(ka, env, sizeof *frame);
    qemu_log("%s: frame_addr 0x%llx\n", __FUNCTION__,
             (unsigned long long)frame_addr);
    if (!lock_user_struct(VERIFY_WRITE, frame, frame_addr, 0)) {
        goto give_sigsegv;
    }

    qemu_log("%s: 1\n", __FUNCTION__);
    if (copy_siginfo_to_user(&frame->info, info)) {
        goto give_sigsegv;
    }

    /* Create the ucontext.  */
    __put_user(0, &frame->uc.tuc_flags);
    __put_user((abi_ulong)0, (abi_ulong *)&frame->uc.tuc_link);
    __put_user(target_sigaltstack_used.ss_sp, &frame->uc.tuc_stack.ss_sp);
    __put_user(sas_ss_flags(get_sp_from_cpustate(env)),
                      &frame->uc.tuc_stack.ss_flags);
    __put_user(target_sigaltstack_used.ss_size, &frame->uc.tuc_stack.ss_size);
    save_sigregs(env, &frame->uc.tuc_mcontext);
    for (i = 0; i < TARGET_NSIG_WORDS; i++) {
        __put_user((abi_ulong)set->sig[i],
        (abi_ulong *)&frame->uc.tuc_sigmask.sig[i]);
    }

    /* Set up to return from userspace.  If provided, use a stub
       already in userspace.  */
    if (ka->sa_flags & TARGET_SA_RESTORER) {
        env->regs[14] = (unsigned long) ka->sa_restorer | PSW_ADDR_AMODE;
    } else {
        env->regs[14] = (unsigned long) frame->retcode | PSW_ADDR_AMODE;
        if (__put_user(S390_SYSCALL_OPCODE | TARGET_NR_rt_sigreturn,
                       (uint16_t *)(frame->retcode))) {
            goto give_sigsegv;
        }
    }

    /* Set up backchain. */
    if (__put_user(env->regs[15], (abi_ulong *) frame)) {
        goto give_sigsegv;
    }

    /* Set up registers for signal handler */
    env->regs[15] = frame_addr;
    env->psw.addr = (target_ulong) ka->_sa_handler | PSW_ADDR_AMODE;

    env->regs[2] = sig; //map_signal(sig);
    env->regs[3] = frame_addr + offsetof(typeof(*frame), info);
    env->regs[4] = frame_addr + offsetof(typeof(*frame), uc);
    return;

give_sigsegv:
    qemu_log("%s: give_sigsegv\n", __FUNCTION__);
    unlock_user_struct(frame, frame_addr, 1);
    force_sig(TARGET_SIGSEGV);
}

static int
restore_sigregs(CPUState *env, target_sigregs *sc)
{
    int err = 0;
    int i;

    for (i = 0; i < 16; i++) {
        err |= __get_user(env->regs[i], &sc->regs.gprs[i]);
    }

    err |= __get_user(env->psw.mask, &sc->regs.psw.mask);
    qemu_log("%s: sc->regs.psw.addr 0x%llx env->psw.addr 0x%llx\n",
             __FUNCTION__, (unsigned long long)sc->regs.psw.addr,
             (unsigned long long)env->psw.addr);
    err |= __get_user(env->psw.addr, &sc->regs.psw.addr);
    /* FIXME: 31-bit -> | PSW_ADDR_AMODE */

    for (i = 0; i < 16; i++) {
        err |= __get_user(env->aregs[i], &sc->regs.acrs[i]);
    }
    for (i = 0; i < 16; i++) {
        err |= __get_user(env->fregs[i].ll, &sc->fpregs.fprs[i]);
    }

    return err;
}

long do_sigreturn(CPUState *env)
{
    sigframe *frame;
    abi_ulong frame_addr = env->regs[15];
    qemu_log("%s: frame_addr 0x%llx\n", __FUNCTION__,
             (unsigned long long)frame_addr);
    target_sigset_t target_set;
    sigset_t set;

    if (!lock_user_struct(VERIFY_READ, frame, frame_addr, 1)) {
        goto badframe;
    }
    if (__get_user(target_set.sig[0], &frame->sc.oldmask[0])) {
        goto badframe;
    }

    target_to_host_sigset_internal(&set, &target_set);
    sigprocmask(SIG_SETMASK, &set, NULL); /* ~_BLOCKABLE? */

    if (restore_sigregs(env, &frame->sregs)) {
        goto badframe;
    }

    unlock_user_struct(frame, frame_addr, 0);
    return env->regs[2];

badframe:
    unlock_user_struct(frame, frame_addr, 0);
    force_sig(TARGET_SIGSEGV);
    return 0;
}

long do_rt_sigreturn(CPUState *env)
{
    rt_sigframe *frame;
    abi_ulong frame_addr = env->regs[15];
    qemu_log("%s: frame_addr 0x%llx\n", __FUNCTION__,
             (unsigned long long)frame_addr);
    sigset_t set;

    if (!lock_user_struct(VERIFY_READ, frame, frame_addr, 1)) {
        goto badframe;
    }
    target_to_host_sigset(&set, &frame->uc.tuc_sigmask);

    sigprocmask(SIG_SETMASK, &set, NULL); /* ~_BLOCKABLE? */

    if (restore_sigregs(env, &frame->uc.tuc_mcontext)) {
        goto badframe;
    }

    if (do_sigaltstack(frame_addr + offsetof(rt_sigframe, uc.tuc_stack), 0,
                       get_sp_from_cpustate(env)) == -EFAULT) {
        goto badframe;
    }
    unlock_user_struct(frame, frame_addr, 0);
    return env->regs[2];

badframe:
    unlock_user_struct(frame, frame_addr, 0);
    force_sig(TARGET_SIGSEGV);
    return 0;
}

#elif defined(TARGET_PPC) && !defined(TARGET_PPC64)

/* FIXME: Many of the structures are defined for both PPC and PPC64, but
   the signal handling is different enough that we haven't implemented
   support for PPC64 yet.  Hence the restriction above.

   There are various #if'd blocks for code for TARGET_PPC64.  These
   blocks should go away so that we can successfully run 32-bit and
   64-bit binaries on a QEMU configured for PPC64.  */

/* Size of dummy stack frame allocated when calling signal handler.
   See arch/powerpc/include/asm/ptrace.h.  */
#if defined(TARGET_PPC64)
#define SIGNAL_FRAMESIZE 128
#else
#define SIGNAL_FRAMESIZE 64
#endif

/* See arch/powerpc/include/asm/sigcontext.h.  */
struct target_sigcontext {
    target_ulong _unused[4];
    int32_t signal;
#if defined(TARGET_PPC64)
    int32_t pad0;
#endif
    target_ulong handler;
    target_ulong oldmask;
    target_ulong regs;      /* struct pt_regs __user * */
    /* TODO: PPC64 includes extra bits here.  */
};

/* Indices for target_mcontext.mc_gregs, below.
   See arch/powerpc/include/asm/ptrace.h for details.  */
enum {
    TARGET_PT_R0 = 0,
    TARGET_PT_R1 = 1,
    TARGET_PT_R2 = 2,
    TARGET_PT_R3 = 3,
    TARGET_PT_R4 = 4,
    TARGET_PT_R5 = 5,
    TARGET_PT_R6 = 6,
    TARGET_PT_R7 = 7,
    TARGET_PT_R8 = 8,
    TARGET_PT_R9 = 9,
    TARGET_PT_R10 = 10,
    TARGET_PT_R11 = 11,
    TARGET_PT_R12 = 12,
    TARGET_PT_R13 = 13,
    TARGET_PT_R14 = 14,
    TARGET_PT_R15 = 15,
    TARGET_PT_R16 = 16,
    TARGET_PT_R17 = 17,
    TARGET_PT_R18 = 18,
    TARGET_PT_R19 = 19,
    TARGET_PT_R20 = 20,
    TARGET_PT_R21 = 21,
    TARGET_PT_R22 = 22,
    TARGET_PT_R23 = 23,
    TARGET_PT_R24 = 24,
    TARGET_PT_R25 = 25,
    TARGET_PT_R26 = 26,
    TARGET_PT_R27 = 27,
    TARGET_PT_R28 = 28,
    TARGET_PT_R29 = 29,
    TARGET_PT_R30 = 30,
    TARGET_PT_R31 = 31,
    TARGET_PT_NIP = 32,
    TARGET_PT_MSR = 33,
    TARGET_PT_ORIG_R3 = 34,
    TARGET_PT_CTR = 35,
    TARGET_PT_LNK = 36,
    TARGET_PT_XER = 37,
    TARGET_PT_CCR = 38,
    /* Yes, there are two registers with #39.  One is 64-bit only.  */
    TARGET_PT_MQ = 39,
    TARGET_PT_SOFTE = 39,
    TARGET_PT_TRAP = 40,
    TARGET_PT_DAR = 41,
    TARGET_PT_DSISR = 42,
    TARGET_PT_RESULT = 43,
    TARGET_PT_REGS_COUNT = 44
};

/* See arch/powerpc/include/asm/ucontext.h.  Only used for 32-bit PPC;
   on 64-bit PPC, sigcontext and mcontext are one and the same.  */
struct target_mcontext {
    target_ulong mc_gregs[48];
    /* Includes fpscr.  */
    uint64_t mc_fregs[33];
    target_ulong mc_pad[2];
    /* We need to handle Altivec and SPE at the same time, which no
       kernel needs to do.  Fortunately, the kernel defines this bit to
       be Altivec-register-large all the time, rather than trying to
       twiddle it based on the specific platform.  */
    union {
        /* SPE vector registers.  One extra for SPEFSCR.  */
        uint32_t spe[33];
        /* Altivec vector registers.  The packing of VSCR and VRSAVE
           varies depending on whether we're PPC64 or not: PPC64 splits
           them apart; PPC32 stuffs them together.  */
#if defined(TARGET_PPC64)
#define QEMU_NVRREG 34
#else
#define QEMU_NVRREG 33
#endif
        ppc_avr_t altivec[QEMU_NVRREG];
#undef QEMU_NVRREG
    } mc_vregs __attribute__((__aligned__(16)));
};

struct target_ucontext {
    target_ulong tuc_flags;
    target_ulong tuc_link;    /* struct ucontext __user * */
    struct target_sigaltstack tuc_stack;
#if !defined(TARGET_PPC64)
    int32_t tuc_pad[7];
    target_ulong tuc_regs;    /* struct mcontext __user *
                                points to uc_mcontext field */
#endif
    target_sigset_t tuc_sigmask;
#if defined(TARGET_PPC64)
    target_sigset_t unused[15]; /* Allow for uc_sigmask growth */
    struct target_sigcontext tuc_mcontext;
#else
    int32_t tuc_maskext[30];
    int32_t tuc_pad2[3];
    struct target_mcontext tuc_mcontext;
#endif
};

/* See arch/powerpc/kernel/signal_32.c.  */
struct target_sigframe {
    struct target_sigcontext sctx;
    struct target_mcontext mctx;
    int32_t abigap[56];
};

struct target_rt_sigframe {
    struct target_siginfo info;
    struct target_ucontext uc;
    int32_t abigap[56];
};

/* We use the mc_pad field for the signal return trampoline.  */
#define tramp mc_pad

/* See arch/powerpc/kernel/signal.c.  */
static target_ulong get_sigframe(struct target_sigaction *ka,
                                 CPUState *env,
                                 int frame_size)
{
    target_ulong oldsp, newsp;

    oldsp = env->gpr[1];

    if ((ka->sa_flags & TARGET_SA_ONSTACK) &&
        (sas_ss_flags(oldsp))) {
        oldsp = (target_sigaltstack_used.ss_sp
                 + target_sigaltstack_used.ss_size);
    }

    newsp = (oldsp - frame_size) & ~0xFUL;

    return newsp;
}

static int save_user_regs(CPUState *env, struct target_mcontext *frame,
                          int sigret)
{
    target_ulong msr = env->msr;
    int i;
    target_ulong ccr = 0;

    /* In general, the kernel attempts to be intelligent about what it
       needs to save for Altivec/FP/SPE registers.  We don't care that
       much, so we just go ahead and save everything.  */

    /* Save general registers.  */
    for (i = 0; i < ARRAY_SIZE(env->gpr); i++) {
        if (__put_user(env->gpr[i], &frame->mc_gregs[i])) {
            return 1;
        }
    }
    if (__put_user(env->nip, &frame->mc_gregs[TARGET_PT_NIP])
        || __put_user(env->ctr, &frame->mc_gregs[TARGET_PT_CTR])
        || __put_user(env->lr, &frame->mc_gregs[TARGET_PT_LNK])
        || __put_user(env->xer, &frame->mc_gregs[TARGET_PT_XER]))
        return 1;

    for (i = 0; i < ARRAY_SIZE(env->crf); i++) {
        ccr |= env->crf[i] << (32 - ((i + 1) * 4));
    }
    if (__put_user(ccr, &frame->mc_gregs[TARGET_PT_CCR]))
        return 1;

    /* Save Altivec registers if necessary.  */
    if (env->insns_flags & PPC_ALTIVEC) {
        for (i = 0; i < ARRAY_SIZE(env->avr); i++) {
            ppc_avr_t *avr = &env->avr[i];
            ppc_avr_t *vreg = &frame->mc_vregs.altivec[i];

            if (__put_user(avr->u64[0], &vreg->u64[0]) ||
                __put_user(avr->u64[1], &vreg->u64[1])) {
                return 1;
            }
        }
        /* Set MSR_VR in the saved MSR value to indicate that
           frame->mc_vregs contains valid data.  */
        msr |= MSR_VR;
        if (__put_user((uint32_t)env->spr[SPR_VRSAVE],
                       &frame->mc_vregs.altivec[32].u32[3]))
            return 1;
    }

    /* Save floating point registers.  */
    if (env->insns_flags & PPC_FLOAT) {
        for (i = 0; i < ARRAY_SIZE(env->fpr); i++) {
            if (__put_user(env->fpr[i], &frame->mc_fregs[i])) {
                return 1;
            }
        }
        if (__put_user((uint64_t) env->fpscr, &frame->mc_fregs[32]))
            return 1;
    }

    /* Save SPE registers.  The kernel only saves the high half.  */
    if (env->insns_flags & PPC_SPE) {
#if defined(TARGET_PPC64)
        for (i = 0; i < ARRAY_SIZE(env->gpr); i++) {
            if (__put_user(env->gpr[i] >> 32, &frame->mc_vregs.spe[i])) {
                return 1;
            }
        }
#else
        for (i = 0; i < ARRAY_SIZE(env->gprh); i++) {
            if (__put_user(env->gprh[i], &frame->mc_vregs.spe[i])) {
                return 1;
            }
        }
#endif
        /* Set MSR_SPE in the saved MSR value to indicate that
           frame->mc_vregs contains valid data.  */
        msr |= MSR_SPE;
        if (__put_user(env->spe_fscr, &frame->mc_vregs.spe[32]))
            return 1;
    }

    /* Store MSR.  */
    if (__put_user(msr, &frame->mc_gregs[TARGET_PT_MSR]))
        return 1;

    /* Set up the sigreturn trampoline: li r0,sigret; sc.  */
    if (sigret) {
        if (__put_user(0x38000000UL | sigret, &frame->tramp[0]) ||
            __put_user(0x44000002UL, &frame->tramp[1])) {
            return 1;
        }
    }

    return 0;
}

static int restore_user_regs(CPUState *env,
                             struct target_mcontext *frame, int sig)
{
    target_ulong save_r2 = 0;
    target_ulong msr;
    target_ulong ccr;

    int i;

    if (!sig) {
        save_r2 = env->gpr[2];
    }

    /* Restore general registers.  */
    for (i = 0; i < ARRAY_SIZE(env->gpr); i++) {
        if (__get_user(env->gpr[i], &frame->mc_gregs[i])) {
            return 1;
        }
    }
    if (__get_user(env->nip, &frame->mc_gregs[TARGET_PT_NIP])
        || __get_user(env->ctr, &frame->mc_gregs[TARGET_PT_CTR])
        || __get_user(env->lr, &frame->mc_gregs[TARGET_PT_LNK])
        || __get_user(env->xer, &frame->mc_gregs[TARGET_PT_XER]))
        return 1;
    if (__get_user(ccr, &frame->mc_gregs[TARGET_PT_CCR]))
        return 1;

    for (i = 0; i < ARRAY_SIZE(env->crf); i++) {
        env->crf[i] = (ccr >> (32 - ((i + 1) * 4))) & 0xf;
    }

    if (!sig) {
        env->gpr[2] = save_r2;
    }
    /* Restore MSR.  */
    if (__get_user(msr, &frame->mc_gregs[TARGET_PT_MSR]))
        return 1;

    /* If doing signal return, restore the previous little-endian mode.  */
    if (sig)
        env->msr = (env->msr & ~MSR_LE) | (msr & MSR_LE);

    /* Restore Altivec registers if necessary.  */
    if (env->insns_flags & PPC_ALTIVEC) {
        for (i = 0; i < ARRAY_SIZE(env->avr); i++) {
            ppc_avr_t *avr = &env->avr[i];
            ppc_avr_t *vreg = &frame->mc_vregs.altivec[i];

            if (__get_user(avr->u64[0], &vreg->u64[0]) ||
                __get_user(avr->u64[1], &vreg->u64[1])) {
                return 1;
            }
        }
        /* Set MSR_VEC in the saved MSR value to indicate that
           frame->mc_vregs contains valid data.  */
        if (__get_user(env->spr[SPR_VRSAVE],
                       (target_ulong *)(&frame->mc_vregs.altivec[32].u32[3])))
            return 1;
    }

    /* Restore floating point registers.  */
    if (env->insns_flags & PPC_FLOAT) {
        uint64_t fpscr;
        for (i = 0; i < ARRAY_SIZE(env->fpr); i++) {
            if (__get_user(env->fpr[i], &frame->mc_fregs[i])) {
                return 1;
            }
        }
        if (__get_user(fpscr, &frame->mc_fregs[32]))
            return 1;
        env->fpscr = (uint32_t) fpscr;
    }

    /* Save SPE registers.  The kernel only saves the high half.  */
    if (env->insns_flags & PPC_SPE) {
#if defined(TARGET_PPC64)
        for (i = 0; i < ARRAY_SIZE(env->gpr); i++) {
            uint32_t hi;

            if (__get_user(hi, &frame->mc_vregs.spe[i])) {
                return 1;
            }
            env->gpr[i] = ((uint64_t)hi << 32) | ((uint32_t) env->gpr[i]);
        }
#else
        for (i = 0; i < ARRAY_SIZE(env->gprh); i++) {
            if (__get_user(env->gprh[i], &frame->mc_vregs.spe[i])) {
                return 1;
            }
        }
#endif
        if (__get_user(env->spe_fscr, &frame->mc_vregs.spe[32]))
            return 1;
    }

    return 0;
}

static void setup_frame(int sig, struct target_sigaction *ka,
                        target_sigset_t *set, CPUState *env)
{
    struct target_sigframe *frame;
    struct target_sigcontext *sc;
    target_ulong frame_addr, newsp;
    int err = 0;
    int signal;

    frame_addr = get_sigframe(ka, env, sizeof(*frame));
    if (!lock_user_struct(VERIFY_WRITE, frame, frame_addr, 1))
        goto sigsegv;
    sc = &frame->sctx;

    signal = current_exec_domain_sig(sig);

    err |= __put_user(h2g(ka->_sa_handler), &sc->handler);
    err |= __put_user(set->sig[0], &sc->oldmask);
#if defined(TARGET_PPC64)
    err |= __put_user(set->sig[0] >> 32, &sc->_unused[3]);
#else
    err |= __put_user(set->sig[1], &sc->_unused[3]);
#endif
    err |= __put_user(h2g(&frame->mctx), &sc->regs);
    err |= __put_user(sig, &sc->signal);

    /* Save user regs.  */
    err |= save_user_regs(env, &frame->mctx, TARGET_NR_sigreturn);

    /* The kernel checks for the presence of a VDSO here.  We don't
       emulate a vdso, so use a sigreturn system call.  */
    env->lr = (target_ulong) h2g(frame->mctx.tramp);

    /* Turn off all fp exceptions.  */
    env->fpscr = 0;

    /* Create a stack frame for the caller of the handler.  */
    newsp = frame_addr - SIGNAL_FRAMESIZE;
    err |= __put_user(env->gpr[1], (target_ulong *)(uintptr_t) newsp);

    if (err)
        goto sigsegv;

    /* Set up registers for signal handler.  */
    env->gpr[1] = newsp;
    env->gpr[3] = signal;
    env->gpr[4] = (target_ulong) h2g(sc);
    env->nip = (target_ulong) ka->_sa_handler;
    /* Signal handlers are entered in big-endian mode.  */
    env->msr &= ~MSR_LE;

    unlock_user_struct(frame, frame_addr, 1);
    return;

sigsegv:
    unlock_user_struct(frame, frame_addr, 1);
    if (logfile)
        fprintf (logfile, "segfaulting from setup_frame\n");
    force_sig(TARGET_SIGSEGV);
}

static void setup_rt_frame(int sig, struct target_sigaction *ka,
                           target_siginfo_t *info,
                           target_sigset_t *set, CPUState *env)
{
    struct target_rt_sigframe *rt_sf;
    struct target_mcontext *frame;
    target_ulong rt_sf_addr, newsp = 0;
    int i, err = 0;
    int signal;

    rt_sf_addr = get_sigframe(ka, env, sizeof(*rt_sf));
    if (!lock_user_struct(VERIFY_WRITE, rt_sf, rt_sf_addr, 1))
        goto sigsegv;

    signal = current_exec_domain_sig(sig);

    err |= copy_siginfo_to_user(&rt_sf->info, info);

    err |= __put_user(0, &rt_sf->uc.tuc_flags);
    err |= __put_user(0, &rt_sf->uc.tuc_link);
    err |= __put_user((target_ulong)target_sigaltstack_used.ss_sp,
                      &rt_sf->uc.tuc_stack.ss_sp);
    err |= __put_user(sas_ss_flags(env->gpr[1]),
                      &rt_sf->uc.tuc_stack.ss_flags);
    err |= __put_user(target_sigaltstack_used.ss_size,
                      &rt_sf->uc.tuc_stack.ss_size);
    err |= __put_user(h2g (&rt_sf->uc.tuc_mcontext),
                      &rt_sf->uc.tuc_regs);
    for(i = 0; i < TARGET_NSIG_WORDS; i++) {
        err |= __put_user(set->sig[i], &rt_sf->uc.tuc_sigmask.sig[i]);
    }

    frame = &rt_sf->uc.tuc_mcontext;
    err |= save_user_regs(env, frame, TARGET_NR_rt_sigreturn);

    /* The kernel checks for the presence of a VDSO here.  We don't
       emulate a vdso, so use a sigreturn system call.  */
    env->lr = (target_ulong) h2g(frame->tramp);

    /* Turn off all fp exceptions.  */
    env->fpscr = 0;

    /* Create a stack frame for the caller of the handler.  */
    newsp = rt_sf_addr - (SIGNAL_FRAMESIZE + 16);
    err |= __put_user(env->gpr[1], (target_ulong *)(uintptr_t) newsp);

    if (err)
        goto sigsegv;

    /* Set up registers for signal handler.  */
    env->gpr[1] = newsp;
    env->gpr[3] = (target_ulong) signal;
    env->gpr[4] = (target_ulong) h2g(&rt_sf->info);
    env->gpr[5] = (target_ulong) h2g(&rt_sf->uc);
    env->gpr[6] = (target_ulong) h2g(rt_sf);
    env->nip = (target_ulong) ka->_sa_handler;
    /* Signal handlers are entered in big-endian mode.  */
    env->msr &= ~MSR_LE;

    unlock_user_struct(rt_sf, rt_sf_addr, 1);
    return;

sigsegv:
    unlock_user_struct(rt_sf, rt_sf_addr, 1);
    if (logfile)
        fprintf (logfile, "segfaulting from setup_rt_frame\n");
    force_sig(TARGET_SIGSEGV);

}

long do_sigreturn(CPUState *env)
{
    struct target_sigcontext *sc = NULL;
    struct target_mcontext *sr = NULL;
    target_ulong sr_addr, sc_addr;
    sigset_t blocked;
    target_sigset_t set;

    sc_addr = env->gpr[1] + SIGNAL_FRAMESIZE;
    if (!lock_user_struct(VERIFY_READ, sc, sc_addr, 1))
        goto sigsegv;

#if defined(TARGET_PPC64)
    set.sig[0] = sc->oldmask + ((long)(sc->_unused[3]) << 32);
#else
    if(__get_user(set.sig[0], &sc->oldmask) ||
       __get_user(set.sig[1], &sc->_unused[3]))
       goto sigsegv;
#endif
    target_to_host_sigset_internal(&blocked, &set);
    sigprocmask(SIG_SETMASK, &blocked, NULL);

    if (__get_user(sr_addr, &sc->regs))
        goto sigsegv;
    if (!lock_user_struct(VERIFY_READ, sr, sr_addr, 1))
        goto sigsegv;
    if (restore_user_regs(env, sr, 1))
        goto sigsegv;

    unlock_user_struct(sr, sr_addr, 1);
    unlock_user_struct(sc, sc_addr, 1);
    return -TARGET_QEMU_ESIGRETURN;

sigsegv:
    unlock_user_struct(sr, sr_addr, 1);
    unlock_user_struct(sc, sc_addr, 1);
    if (logfile)
        fprintf (logfile, "segfaulting from do_sigreturn\n");
    force_sig(TARGET_SIGSEGV);
    return 0;
}

/* See arch/powerpc/kernel/signal_32.c.  */
static int do_setcontext(struct target_ucontext *ucp, CPUState *env, int sig)
{
    struct target_mcontext *mcp;
    target_ulong mcp_addr;
    sigset_t blocked;
    target_sigset_t set;

    if (copy_from_user(&set, h2g(ucp) + offsetof(struct target_ucontext, tuc_sigmask),
                       sizeof (set)))
        return 1;

#if defined(TARGET_PPC64)
    fprintf (stderr, "do_setcontext: not implemented\n");
    return 0;
#else
    if (__get_user(mcp_addr, &ucp->tuc_regs))
        return 1;

    if (!lock_user_struct(VERIFY_READ, mcp, mcp_addr, 1))
        return 1;

    target_to_host_sigset_internal(&blocked, &set);
    sigprocmask(SIG_SETMASK, &blocked, NULL);
    if (restore_user_regs(env, mcp, sig))
        goto sigsegv;

    unlock_user_struct(mcp, mcp_addr, 1);
    return 0;

sigsegv:
    unlock_user_struct(mcp, mcp_addr, 1);
    return 1;
#endif
}

long do_rt_sigreturn(CPUState *env)
{
    struct target_rt_sigframe *rt_sf = NULL;
    target_ulong rt_sf_addr;

    rt_sf_addr = env->gpr[1] + SIGNAL_FRAMESIZE + 16;
    if (!lock_user_struct(VERIFY_READ, rt_sf, rt_sf_addr, 1))
        goto sigsegv;

    if (do_setcontext(&rt_sf->uc, env, 1))
        goto sigsegv;

    do_sigaltstack(rt_sf_addr
                   + offsetof(struct target_rt_sigframe, uc.tuc_stack),
                   0, env->gpr[1]);

    unlock_user_struct(rt_sf, rt_sf_addr, 1);
    return -TARGET_QEMU_ESIGRETURN;

sigsegv:
    unlock_user_struct(rt_sf, rt_sf_addr, 1);
    if (logfile)
        fprintf (logfile, "segfaulting from do_rt_sigreturn\n");
    force_sig(TARGET_SIGSEGV);
    return 0;
}

#elif defined(TARGET_M68K)

struct target_sigcontext {
    abi_ulong  sc_mask;
    abi_ulong  sc_usp;
    abi_ulong  sc_d0;
    abi_ulong  sc_d1;
    abi_ulong  sc_a0;
    abi_ulong  sc_a1;
    unsigned short sc_sr;
    abi_ulong  sc_pc;
};

struct target_sigframe
{
    abi_ulong pretcode;
    int sig;
    int code;
    abi_ulong psc;
    char retcode[8];
    abi_ulong extramask[TARGET_NSIG_WORDS-1];
    struct target_sigcontext sc;
};
 
typedef int target_greg_t;
#define TARGET_NGREG 18
typedef target_greg_t target_gregset_t[TARGET_NGREG];

typedef struct target_fpregset {
    int f_fpcntl[3];
    int f_fpregs[8*3];
} target_fpregset_t;

struct target_mcontext {
    int version;
    target_gregset_t gregs;
    target_fpregset_t fpregs;
};

#define TARGET_MCONTEXT_VERSION 2

struct target_ucontext {
    abi_ulong tuc_flags;
    abi_ulong tuc_link;
    target_stack_t tuc_stack;
    struct target_mcontext tuc_mcontext;
    abi_long tuc_filler[80];
    target_sigset_t tuc_sigmask;
};

struct target_rt_sigframe
{
    abi_ulong pretcode;
    int sig;
    abi_ulong pinfo;
    abi_ulong puc;
    char retcode[8];
    struct target_siginfo info;
    struct target_ucontext uc;
};

static int
setup_sigcontext(struct target_sigcontext *sc, CPUState *env, abi_ulong mask)
{
    int err = 0;

    err |= __put_user(mask, &sc->sc_mask);
    err |= __put_user(env->aregs[7], &sc->sc_usp);
    err |= __put_user(env->dregs[0], &sc->sc_d0);
    err |= __put_user(env->dregs[1], &sc->sc_d1);
    err |= __put_user(env->aregs[0], &sc->sc_a0);
    err |= __put_user(env->aregs[1], &sc->sc_a1);
    err |= __put_user(env->sr, &sc->sc_sr);
    err |= __put_user(env->pc, &sc->sc_pc);

    return err;
}

static int
restore_sigcontext(CPUState *env, struct target_sigcontext *sc, int *pd0)
{
    int err = 0;
    int temp;

    err |= __get_user(env->aregs[7], &sc->sc_usp);
    err |= __get_user(env->dregs[1], &sc->sc_d1);
    err |= __get_user(env->aregs[0], &sc->sc_a0);
    err |= __get_user(env->aregs[1], &sc->sc_a1);
    err |= __get_user(env->pc, &sc->sc_pc);
    err |= __get_user(temp, &sc->sc_sr);
    env->sr = (env->sr & 0xff00) | (temp & 0xff);

    *pd0 = tswapl(sc->sc_d0);

    return err;
}

/*
 * Determine which stack to use..
 */
static inline abi_ulong
get_sigframe(struct target_sigaction *ka, CPUState *regs, size_t frame_size)
{
    unsigned long sp;

    sp = regs->aregs[7];

    /* This is the X/Open sanctioned signal stack switching.  */
    if ((ka->sa_flags & TARGET_SA_ONSTACK) && (sas_ss_flags (sp) == 0)) {
        sp = target_sigaltstack_used.ss_sp + target_sigaltstack_used.ss_size;
    }

    return ((sp - frame_size) & -8UL);
}

static void setup_frame(int sig, struct target_sigaction *ka,
			target_sigset_t *set, CPUState *env)
{
    struct target_sigframe *frame;
    abi_ulong frame_addr;
    abi_ulong retcode_addr;
    abi_ulong sc_addr;
    int err = 0;
    int i;

    frame_addr = get_sigframe(ka, env, sizeof *frame);
    if (!lock_user_struct(VERIFY_WRITE, frame, frame_addr, 0))
	goto give_sigsegv;

    err |= __put_user(sig, &frame->sig);

    sc_addr = frame_addr + offsetof(struct target_sigframe, sc);
    err |= __put_user(sc_addr, &frame->psc);

    err |= setup_sigcontext(&frame->sc, env, set->sig[0]);
    if (err)
	goto give_sigsegv;

    for(i = 1; i < TARGET_NSIG_WORDS; i++) {
        if (__put_user(set->sig[i], &frame->extramask[i - 1]))
            goto give_sigsegv;
    }

    /* Set up to return from userspace.  */

    retcode_addr = frame_addr + offsetof(struct target_sigframe, retcode);
    err |= __put_user(retcode_addr, &frame->pretcode);

    /* moveq #,d0; trap #0 */

    err |= __put_user(0x70004e40 + (TARGET_NR_sigreturn << 16),
                      (long *)(frame->retcode));

    if (err)
        goto give_sigsegv;

    /* Set up to return from userspace */

    env->aregs[7] = frame_addr;
    env->pc = ka->_sa_handler;

    unlock_user_struct(frame, frame_addr, 1);
    return;

give_sigsegv:
    unlock_user_struct(frame, frame_addr, 1);
    force_sig(TARGET_SIGSEGV);
}

static inline int target_rt_setup_ucontext(struct target_ucontext *uc,
                                           CPUState *env)
{
    target_greg_t *gregs = uc->tuc_mcontext.gregs;
    int err;

    err = __put_user(TARGET_MCONTEXT_VERSION, &uc->tuc_mcontext.version);
    err |= __put_user(env->dregs[0], &gregs[0]);
    err |= __put_user(env->dregs[1], &gregs[1]);
    err |= __put_user(env->dregs[2], &gregs[2]);
    err |= __put_user(env->dregs[3], &gregs[3]);
    err |= __put_user(env->dregs[4], &gregs[4]);
    err |= __put_user(env->dregs[5], &gregs[5]);
    err |= __put_user(env->dregs[6], &gregs[6]);
    err |= __put_user(env->dregs[7], &gregs[7]);
    err |= __put_user(env->aregs[0], &gregs[8]);
    err |= __put_user(env->aregs[1], &gregs[9]);
    err |= __put_user(env->aregs[2], &gregs[10]);
    err |= __put_user(env->aregs[3], &gregs[11]);
    err |= __put_user(env->aregs[4], &gregs[12]);
    err |= __put_user(env->aregs[5], &gregs[13]);
    err |= __put_user(env->aregs[6], &gregs[14]);
    err |= __put_user(env->aregs[7], &gregs[15]);
    err |= __put_user(env->pc, &gregs[16]);
    err |= __put_user(env->sr, &gregs[17]);

    return err;
}
 
static inline int target_rt_restore_ucontext(CPUState *env,
                                             struct target_ucontext *uc,
                                             int *pd0)
{
    int temp;
    int err;
    target_greg_t *gregs = uc->tuc_mcontext.gregs;
    
    err = __get_user(temp, &uc->tuc_mcontext.version);
    if (temp != TARGET_MCONTEXT_VERSION)
        goto badframe;

    /* restore passed registers */
    err |= __get_user(env->dregs[0], &gregs[0]);
    err |= __get_user(env->dregs[1], &gregs[1]);
    err |= __get_user(env->dregs[2], &gregs[2]);
    err |= __get_user(env->dregs[3], &gregs[3]);
    err |= __get_user(env->dregs[4], &gregs[4]);
    err |= __get_user(env->dregs[5], &gregs[5]);
    err |= __get_user(env->dregs[6], &gregs[6]);
    err |= __get_user(env->dregs[7], &gregs[7]);
    err |= __get_user(env->aregs[0], &gregs[8]);
    err |= __get_user(env->aregs[1], &gregs[9]);
    err |= __get_user(env->aregs[2], &gregs[10]);
    err |= __get_user(env->aregs[3], &gregs[11]);
    err |= __get_user(env->aregs[4], &gregs[12]);
    err |= __get_user(env->aregs[5], &gregs[13]);
    err |= __get_user(env->aregs[6], &gregs[14]);
    err |= __get_user(env->aregs[7], &gregs[15]);
    err |= __get_user(env->pc, &gregs[16]);
    err |= __get_user(temp, &gregs[17]);
    env->sr = (env->sr & 0xff00) | (temp & 0xff);

    *pd0 = env->dregs[0];
    return err;

badframe:
    return 1;
}

static void setup_rt_frame(int sig, struct target_sigaction *ka,
                           target_siginfo_t *info,
			   target_sigset_t *set, CPUState *env)
{
    struct target_rt_sigframe *frame;
    abi_ulong frame_addr;
    abi_ulong retcode_addr;
    abi_ulong info_addr;
    abi_ulong uc_addr;
    int err = 0;
    int i;

    frame_addr = get_sigframe(ka, env, sizeof *frame);
    if (!lock_user_struct(VERIFY_WRITE, frame, frame_addr, 0))
	goto give_sigsegv;

    err |= __put_user(sig, &frame->sig);

    info_addr = frame_addr + offsetof(struct target_rt_sigframe, info);
    err |= __put_user(info_addr, &frame->pinfo);

    uc_addr = frame_addr + offsetof(struct target_rt_sigframe, uc);
    err |= __put_user(uc_addr, &frame->puc);

    err |= copy_siginfo_to_user(&frame->info, info);

    /* Create the ucontext */

    err |= __put_user(0, &frame->uc.tuc_flags);
    err |= __put_user(0, &frame->uc.tuc_link);
    err |= __put_user(target_sigaltstack_used.ss_sp,
                      &frame->uc.tuc_stack.ss_sp);
    err |= __put_user(sas_ss_flags(env->aregs[7]),
                      &frame->uc.tuc_stack.ss_flags);
    err |= __put_user(target_sigaltstack_used.ss_size,
                      &frame->uc.tuc_stack.ss_size);
    err |= target_rt_setup_ucontext(&frame->uc, env);

    if (err)
            goto give_sigsegv;

    for(i = 0; i < TARGET_NSIG_WORDS; i++) {
        if (__put_user(set->sig[i], &frame->uc.tuc_sigmask.sig[i]))
            goto give_sigsegv;
    }

    /* Set up to return from userspace.  */

    retcode_addr = frame_addr + offsetof(struct target_sigframe, retcode);
    err |= __put_user(retcode_addr, &frame->pretcode);

    /* moveq #,d0; notb d0; trap #0 */

    err |= __put_user(0x70004600 + ((TARGET_NR_rt_sigreturn ^ 0xff) << 16),
                      (long *)(frame->retcode + 0));
    err |= __put_user(0x4e40, (short *)(frame->retcode + 4));

    if (err)
        goto give_sigsegv;

    /* Set up to return from userspace */

    env->aregs[7] = frame_addr;
    env->pc = ka->_sa_handler;

    unlock_user_struct(frame, frame_addr, 1);
    return;

give_sigsegv:
    unlock_user_struct(frame, frame_addr, 1);
    force_sig(TARGET_SIGSEGV);
}

long do_sigreturn(CPUState *env)
{
    struct target_sigframe *frame;
    abi_ulong frame_addr = env->aregs[7] - 4;
    target_sigset_t target_set;
    sigset_t set;
    int d0, i;

    if (!lock_user_struct(VERIFY_READ, frame, frame_addr, 1))
        goto badframe;

    /* set blocked signals */

    if (__get_user(target_set.sig[0], &frame->sc.sc_mask))
        goto badframe;

    for(i = 1; i < TARGET_NSIG_WORDS; i++) {
        if (__get_user(target_set.sig[i], &frame->extramask[i - 1]))
            goto badframe;
    }

    target_to_host_sigset_internal(&set, &target_set);
    sigprocmask(SIG_SETMASK, &set, NULL);

    /* restore registers */

    if (restore_sigcontext(env, &frame->sc, &d0))
        goto badframe;

    unlock_user_struct(frame, frame_addr, 0);
    return d0;

badframe:
    unlock_user_struct(frame, frame_addr, 0);
    force_sig(TARGET_SIGSEGV);
    return 0;
}

long do_rt_sigreturn(CPUState *env)
{
    struct target_rt_sigframe *frame;
    abi_ulong frame_addr = env->aregs[7] - 4;
    target_sigset_t target_set;
    sigset_t set;
    int d0;

    if (!lock_user_struct(VERIFY_READ, frame, frame_addr, 1))
        goto badframe;

    target_to_host_sigset_internal(&set, &target_set);
    sigprocmask(SIG_SETMASK, &set, NULL);

    /* restore registers */

    if (target_rt_restore_ucontext(env, &frame->uc, &d0))
        goto badframe;

    if (do_sigaltstack(frame_addr +
                       offsetof(struct target_rt_sigframe, uc.tuc_stack),
                       0, get_sp_from_cpustate(env)) == -EFAULT)
        goto badframe;

    unlock_user_struct(frame, frame_addr, 0);
    return d0;

badframe:
    unlock_user_struct(frame, frame_addr, 0);
    force_sig(TARGET_SIGSEGV);
    return 0;
}

#elif defined(TARGET_ALPHA)

struct target_sigcontext {
    abi_long sc_onstack;
    abi_long sc_mask;
    abi_long sc_pc;
    abi_long sc_ps;
    abi_long sc_regs[32];
    abi_long sc_ownedfp;
    abi_long sc_fpregs[32];
    abi_ulong sc_fpcr;
    abi_ulong sc_fp_control;
    abi_ulong sc_reserved1;
    abi_ulong sc_reserved2;
    abi_ulong sc_ssize;
    abi_ulong sc_sbase;
    abi_ulong sc_traparg_a0;
    abi_ulong sc_traparg_a1;
    abi_ulong sc_traparg_a2;
    abi_ulong sc_fp_trap_pc;
    abi_ulong sc_fp_trigger_sum;
    abi_ulong sc_fp_trigger_inst;
};

struct target_ucontext {
    abi_ulong tuc_flags;
    abi_ulong tuc_link;
    abi_ulong tuc_osf_sigmask;
    target_stack_t tuc_stack;
    struct target_sigcontext tuc_mcontext;
    target_sigset_t tuc_sigmask;
};

struct target_sigframe {
    struct target_sigcontext sc;
    unsigned int retcode[3];
};

struct target_rt_sigframe {
    target_siginfo_t info;
    struct target_ucontext uc;
    unsigned int retcode[3];
};

#define INSN_MOV_R30_R16        0x47fe0410
#define INSN_LDI_R0             0x201f0000
#define INSN_CALLSYS            0x00000083

static int setup_sigcontext(struct target_sigcontext *sc, CPUState *env,
                            abi_ulong frame_addr, target_sigset_t *set)
{
    int i, err = 0;

    err |= __put_user(on_sig_stack(frame_addr), &sc->sc_onstack);
    err |= __put_user(set->sig[0], &sc->sc_mask);
    err |= __put_user(env->pc, &sc->sc_pc);
    err |= __put_user(8, &sc->sc_ps);

    for (i = 0; i < 31; ++i) {
        err |= __put_user(env->ir[i], &sc->sc_regs[i]);
    }
    err |= __put_user(0, &sc->sc_regs[31]);

    for (i = 0; i < 31; ++i) {
        err |= __put_user(env->fir[i], &sc->sc_fpregs[i]);
    }
    err |= __put_user(0, &sc->sc_fpregs[31]);
    err |= __put_user(cpu_alpha_load_fpcr(env), &sc->sc_fpcr);

    err |= __put_user(0, &sc->sc_traparg_a0); /* FIXME */
    err |= __put_user(0, &sc->sc_traparg_a1); /* FIXME */
    err |= __put_user(0, &sc->sc_traparg_a2); /* FIXME */

    return err;
}

static int restore_sigcontext(CPUState *env, struct target_sigcontext *sc)
{
    uint64_t fpcr;
    int i, err = 0;

    err |= __get_user(env->pc, &sc->sc_pc);

    for (i = 0; i < 31; ++i) {
        err |= __get_user(env->ir[i], &sc->sc_regs[i]);
    }
    for (i = 0; i < 31; ++i) {
        err |= __get_user(env->fir[i], &sc->sc_fpregs[i]);
    }

    err |= __get_user(fpcr, &sc->sc_fpcr);
    cpu_alpha_store_fpcr(env, fpcr);

    return err;
}

static inline abi_ulong get_sigframe(struct target_sigaction *sa,
                                     CPUState *env, unsigned long framesize)
{
    abi_ulong sp = env->ir[IR_SP];

    /* This is the X/Open sanctioned signal stack switching.  */
    if ((sa->sa_flags & TARGET_SA_ONSTACK) != 0 && !sas_ss_flags(sp)) {
        sp = target_sigaltstack_used.ss_sp + target_sigaltstack_used.ss_size;
    }
    return (sp - framesize) & -32;
}

static void setup_frame(int sig, struct target_sigaction *ka,
                        target_sigset_t *set, CPUState *env)
{
    abi_ulong frame_addr, r26;
    struct target_sigframe *frame;
    int err = 0;

    frame_addr = get_sigframe(ka, env, sizeof(*frame));
    if (!lock_user_struct(VERIFY_WRITE, frame, frame_addr, 0)) {
        goto give_sigsegv;
    }

    err |= setup_sigcontext(&frame->sc, env, frame_addr, set);

    if (ka->sa_restorer) {
        r26 = ka->sa_restorer;
    } else {
        err |= __put_user(INSN_MOV_R30_R16, &frame->retcode[0]);
        err |= __put_user(INSN_LDI_R0 + TARGET_NR_sigreturn,
                          &frame->retcode[1]);
        err |= __put_user(INSN_CALLSYS, &frame->retcode[2]);
        /* imb() */
        r26 = frame_addr;
    }

    unlock_user_struct(frame, frame_addr, 1);

    if (err) {
    give_sigsegv:
        if (sig == TARGET_SIGSEGV) {
            ka->_sa_handler = TARGET_SIG_DFL;
        }
        force_sig(TARGET_SIGSEGV);
    }

    env->ir[IR_RA] = r26;
    env->ir[IR_PV] = env->pc = ka->_sa_handler;
    env->ir[IR_A0] = sig;
    env->ir[IR_A1] = 0;
    env->ir[IR_A2] = frame_addr + offsetof(struct target_sigframe, sc);
    env->ir[IR_SP] = frame_addr;
}

static void setup_rt_frame(int sig, struct target_sigaction *ka,
                           target_siginfo_t *info,
			   target_sigset_t *set, CPUState *env)
{
    abi_ulong frame_addr, r26;
    struct target_rt_sigframe *frame;
    int i, err = 0;

    frame_addr = get_sigframe(ka, env, sizeof(*frame));
    if (!lock_user_struct(VERIFY_WRITE, frame, frame_addr, 0)) {
        goto give_sigsegv;
    }

    err |= copy_siginfo_to_user(&frame->info, info);

    err |= __put_user(0, &frame->uc.tuc_flags);
    err |= __put_user(0, &frame->uc.tuc_link);
    err |= __put_user(set->sig[0], &frame->uc.tuc_osf_sigmask);
    err |= __put_user(target_sigaltstack_used.ss_sp,
                      &frame->uc.tuc_stack.ss_sp);
    err |= __put_user(sas_ss_flags(env->ir[IR_SP]),
                      &frame->uc.tuc_stack.ss_flags);
    err |= __put_user(target_sigaltstack_used.ss_size,
                      &frame->uc.tuc_stack.ss_size);
    err |= setup_sigcontext(&frame->uc.tuc_mcontext, env, frame_addr, set);
    for (i = 0; i < TARGET_NSIG_WORDS; ++i) {
        err |= __put_user(set->sig[i], &frame->uc.tuc_sigmask.sig[i]);
    }

    if (ka->sa_restorer) {
        r26 = ka->sa_restorer;
    } else {
        err |= __put_user(INSN_MOV_R30_R16, &frame->retcode[0]);
        err |= __put_user(INSN_LDI_R0 + TARGET_NR_rt_sigreturn,
                          &frame->retcode[1]);
        err |= __put_user(INSN_CALLSYS, &frame->retcode[2]);
        /* imb(); */
        r26 = frame_addr;
    }

    if (err) {
    give_sigsegv:
       if (sig == TARGET_SIGSEGV) {
            ka->_sa_handler = TARGET_SIG_DFL;
        }
        force_sig(TARGET_SIGSEGV);
    }

    env->ir[IR_RA] = r26;
    env->ir[IR_PV] = env->pc = ka->_sa_handler;
    env->ir[IR_A0] = sig;
    env->ir[IR_A1] = frame_addr + offsetof(struct target_rt_sigframe, info);
    env->ir[IR_A2] = frame_addr + offsetof(struct target_rt_sigframe, uc);
    env->ir[IR_SP] = frame_addr;
}

long do_sigreturn(CPUState *env)
{
    struct target_sigcontext *sc;
    abi_ulong sc_addr = env->ir[IR_A0];
    target_sigset_t target_set;
    sigset_t set;

    if (!lock_user_struct(VERIFY_READ, sc, sc_addr, 1)) {
        goto badframe;
    }

    target_sigemptyset(&target_set);
    if (__get_user(target_set.sig[0], &sc->sc_mask)) {
        goto badframe;
    }

    target_to_host_sigset_internal(&set, &target_set);
    sigprocmask(SIG_SETMASK, &set, NULL);

    if (restore_sigcontext(env, sc)) {
        goto badframe;
    }
    unlock_user_struct(sc, sc_addr, 0);
    return env->ir[IR_V0];

 badframe:
    unlock_user_struct(sc, sc_addr, 0);
    force_sig(TARGET_SIGSEGV);
}

long do_rt_sigreturn(CPUState *env)
{
    abi_ulong frame_addr = env->ir[IR_A0];
    struct target_rt_sigframe *frame;
    sigset_t set;

    if (!lock_user_struct(VERIFY_READ, frame, frame_addr, 1)) {
        goto badframe;
    }
    target_to_host_sigset(&set, &frame->uc.tuc_sigmask);
    sigprocmask(SIG_SETMASK, &set, NULL);

    if (restore_sigcontext(env, &frame->uc.tuc_mcontext)) {
        goto badframe;
    }
    if (do_sigaltstack(frame_addr + offsetof(struct target_rt_sigframe,
                                             uc.tuc_stack),
                       0, env->ir[IR_SP]) == -EFAULT) {
        goto badframe;
    }

    unlock_user_struct(frame, frame_addr, 0);
    return env->ir[IR_V0];


 badframe:
    unlock_user_struct(frame, frame_addr, 0);
    force_sig(TARGET_SIGSEGV);
}

#else

static void setup_frame(int sig, struct target_sigaction *ka,
			target_sigset_t *set, CPUState *env)
{
    fprintf(stderr, "setup_frame: not implemented\n");
}

static void setup_rt_frame(int sig, struct target_sigaction *ka,
                           target_siginfo_t *info,
			   target_sigset_t *set, CPUState *env)
{
    fprintf(stderr, "setup_rt_frame: not implemented\n");
}

long do_sigreturn(CPUState *env)
{
    fprintf(stderr, "do_sigreturn: not implemented\n");
    return -TARGET_ENOSYS;
}

long do_rt_sigreturn(CPUState *env)
{
    fprintf(stderr, "do_rt_sigreturn: not implemented\n");
    return -TARGET_ENOSYS;
}

#endif

void process_pending_signals(CPUState *cpu_env)
{
    int sig;
    abi_ulong handler;
    sigset_t set, old_set;
    target_sigset_t target_old_set;
    struct emulated_sigtable *k;
    struct target_sigaction *sa;
    struct sigqueue *q;
    TaskState *ts = cpu_env->opaque;

    if (!ts->signal_pending)
        return;

    /* FIXME: This is not threadsafe.  */
    k = ts->sigtab;
    for(sig = 1; sig <= TARGET_NSIG; sig++) {
        if (k->pending)
            goto handle_signal;
        k++;
    }
    /* if no signal is pending, just return */
    ts->signal_pending = 0;
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
        sa = NULL;
        handler = TARGET_SIG_IGN;
    } else {
        sa = &sigact_table[sig - 1];
        handler = sa->_sa_handler;
    }

    if (handler == TARGET_SIG_DFL) {
        /* default handler : ignore some signal. The other are job control or fatal */
        if (sig == TARGET_SIGTSTP || sig == TARGET_SIGTTIN || sig == TARGET_SIGTTOU) {
            kill(getpid(),SIGSTOP);
        } else if (sig != TARGET_SIGCHLD &&
                   sig != TARGET_SIGURG &&
                   sig != TARGET_SIGWINCH &&
                   sig != TARGET_SIGCONT) {
            force_sig(sig);
        }
    } else if (handler == TARGET_SIG_IGN) {
        /* ignore sig */
    } else if (handler == TARGET_SIG_ERR) {
        force_sig(sig);
    } else {
        /* compute the blocked signals during the handler execution */
        target_to_host_sigset(&set, &sa->sa_mask);
        /* SA_NODEFER indicates that the current signal should not be
           blocked during the handler */
        if (!(sa->sa_flags & TARGET_SA_NODEFER))
            sigaddset(&set, target_to_host_signal(sig));

        /* block signals in the handler using Linux */
        sigprocmask(SIG_BLOCK, &set, &old_set);
        /* save the previous blocked signal state to restore it at the
           end of the signal execution (see do_sigreturn) */
        host_to_target_sigset_internal(&target_old_set, &old_set);

        /* if the CPU is in VM86 mode, we restore the 32 bit values */
#if defined(TARGET_I386) && !defined(TARGET_X86_64)
        {
            CPUX86State *env = cpu_env;
            if (env->eflags & VM_MASK)
                save_v86_state(env);
        }
#endif
        /* prepare the stack frame of the virtual CPU */
        if (sa->sa_flags & TARGET_SA_SIGINFO)
            setup_rt_frame(sig, sa, &q->info, &target_old_set, cpu_env);
        else
            setup_frame(sig, sa, &target_old_set, cpu_env);
	if (sa->sa_flags & TARGET_SA_RESETHAND)
            sa->_sa_handler = TARGET_SIG_DFL;
    }
    if (q != &k->info)
        free_sigqueue(cpu_env, q);
}

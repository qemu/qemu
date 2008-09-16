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
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/ucontext.h>

#include "qemu.h"
#include "target_signal.h"

//#define DEBUG_SIGNAL

struct target_sigaltstack target_sigaltstack_used = {
    .ss_sp = 0,
    .ss_size = 0,
    .ss_flags = TARGET_SS_DISABLE,
};

static struct target_sigaction sigact_table[TARGET_NSIG];

static void host_signal_handler(int host_signum, siginfo_t *info,
                                void *puc);

static uint8_t host_to_target_signal_table[65] = {
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
static uint8_t target_to_host_signal_table[65];

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

static inline int host_to_target_signal(int sig)
{
    if (sig > 64)
        return sig;
    return host_to_target_signal_table[sig];
}

int target_to_host_signal(int sig)
{
    if (sig > 64)
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
        d->sig[i] = tswapl(d1.sig[i]);
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
        s1.sig[i] = tswapl(s->sig[i]);
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
            tswapl(info->_sifields._sigfault._addr);
    } else if (sig == SIGIO) {
	tinfo->_sifields._sigpoll._fd = tswap32(info->_sifields._sigpoll._fd);
    } else if (sig >= TARGET_SIGRTMIN) {
        tinfo->_sifields._rt._pid = tswap32(info->_sifields._rt._pid);
        tinfo->_sifields._rt._uid = tswap32(info->_sifields._rt._uid);
        tinfo->_sifields._rt._sigval.sival_ptr =
            tswapl(info->_sifields._rt._sigval.sival_ptr);
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
            (void *)(long)tswapl(tinfo->_sifields._rt._sigval.sival_ptr);
}

void signal_init(void)
{
    struct sigaction act;
    struct sigaction oact;
    int i, j;
    int host_sig;

    /* generate signal conversion tables */
    for(i = 1; i <= 64; i++) {
        if (host_to_target_signal_table[i] == 0)
            host_to_target_signal_table[i] = i;
    }
    for(i = 1; i <= 64; i++) {
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
        /* Install some handlers for our own use.  */
        if (host_sig == SIGSEGV || host_sig == SIGBUS) {
            sigaction(host_sig, &act, NULL);
        }
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
static void __attribute((noreturn)) force_sig(int sig)
{
    int host_sig;
    host_sig = target_to_host_signal(sig);
    fprintf(stderr, "qemu: uncaught target signal %d (%s) - exiting\n",
            sig, strsignal(host_sig));
#if 1
    _exit(-host_sig);
#else
    {
        struct sigaction act;
        sigemptyset(&act.sa_mask);
        act.sa_flags = SA_SIGINFO;
        act.sa_sigaction = SIG_DFL;
        sigaction(SIGABRT, &act, NULL);
        abort();
    }
#endif
}

/* queue a signal so that it will be send to the virtual CPU as soon
   as possible */
int queue_signal(CPUState *env, int sig, target_siginfo_t *info)
{
    TaskState *ts = env->opaque;
    struct emulated_sigtable *k;
    struct sigqueue *q, **pq;
    abi_ulong handler;

#if defined(DEBUG_SIGNAL)
    fprintf(stderr, "queue_signal: sig=%d\n",
            sig);
#endif
    k = &ts->sigtab[sig - 1];
    handler = sigact_table[sig - 1]._sa_handler;
    if (handler == TARGET_SIG_DFL) {
        /* default handler : ignore some signal. The other are fatal */
        if (sig != TARGET_SIGCHLD &&
            sig != TARGET_SIGURG &&
            sig != TARGET_SIGWINCH) {
            force_sig(sig);
        } else {
            return 0; /* indicate ignored */
        }
    } else if (handler == TARGET_SIG_IGN) {
        /* ignore signal */
        return 0;
    } else if (handler == TARGET_SIG_ERR) {
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
       we we forward to it some signals */
    if (host_signum == SIGSEGV || host_signum == SIGBUS) {
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
        cpu_interrupt(thread_env, CPU_INTERRUPT_EXIT);
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

    if (sig < 1 || sig > TARGET_NSIG || sig == SIGKILL || sig == SIGSTOP)
        return -EINVAL;
    k = &sigact_table[sig - 1];
#if defined(DEBUG_SIGNAL)
    fprintf(stderr, "sigaction sig=%d act=0x%08x, oact=0x%08x\n",
            sig, (int)act, (int)oact);
#endif
    if (oact) {
        oact->_sa_handler = tswapl(k->_sa_handler);
        oact->sa_flags = tswapl(k->sa_flags);
#if !defined(TARGET_MIPS)
        oact->sa_restorer = tswapl(k->sa_restorer);
#endif
        oact->sa_mask = k->sa_mask;
    }
    if (act) {
        /* FIXME: This is not threadsafe.  */
        k->_sa_handler = tswapl(act->_sa_handler);
        k->sa_flags = tswapl(act->sa_flags);
#if !defined(TARGET_MIPS)
        k->sa_restorer = tswapl(act->sa_restorer);
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

        cpu_x86_load_seg(env, R_CS, lduw(&sc->cs) | 3);
        cpu_x86_load_seg(env, R_SS, lduw(&sc->ss) | 3);

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
    char __unused[128 - sizeof(sigset_t)];
    abi_ulong tuc_regspace[128] __attribute__((__aligned__(8)));
};

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

	if (ka->sa_flags & TARGET_SA_RESTORER) {
		retcode = ka->sa_restorer;
	} else {
		unsigned int idx = thumb;

		if (ka->sa_flags & TARGET_SA_SIGINFO)
			idx += 2;

		if (__put_user(retcodes[idx], rc))
			return 1;
#if 0
		flush_icache_range((abi_ulong)rc,
				   (abi_ulong)(rc + 1));
#endif
		retcode = rc_addr + thumb;
	}

	env->regs[0] = usig;
	env->regs[13] = frame_addr;
	env->regs[14] = retcode;
	env->regs[15] = handler & (thumb ? ~1 : ~3);
	env->thumb = thumb;

#if 0
#ifdef TARGET_CONFIG_CPU_32
	env->cpsr = cpsr;
#endif
#endif

	return 0;
}

static void setup_sigframe_v2(struct target_ucontext_v2 *uc,
                              target_sigset_t *set, CPUState *env)
{
    struct target_sigaltstack stack;
    int i;

    /* Clear all the bits of the ucontext we don't use.  */
    memset(uc, 0, offsetof(struct target_ucontext_v2, tuc_mcontext));

    memset(&stack, 0, sizeof(stack));
    __put_user(target_sigaltstack_used.ss_sp, &stack.ss_sp);
    __put_user(target_sigaltstack_used.ss_size, &stack.ss_size);
    __put_user(sas_ss_flags(get_sp_from_cpustate(env)), &stack.ss_flags);
    memcpy(&uc->tuc_stack, &stack, sizeof(stack));

    setup_sigcontext(&uc->tuc_mcontext, env, set->sig[0]);
    /* FIXME: Save coprocessor signal frame.  */
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

long do_sigreturn_v1(CPUState *env)
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
        force_sig(SIGSEGV /* , current */);
	return 0;
}

static int do_sigframe_return_v2(CPUState *env, target_ulong frame_addr,
                                 struct target_ucontext_v2 *uc)
{
    sigset_t host_set;

    target_to_host_sigset(&host_set, &uc->tuc_sigmask);
    sigprocmask(SIG_SETMASK, &host_set, NULL);

    if (restore_sigcontext(env, &uc->tuc_mcontext))
        return 1;

    if (do_sigaltstack(frame_addr + offsetof(struct target_ucontext_v2, tuc_stack), 0, get_sp_from_cpustate(env)) == -EFAULT)
        return 1;

#if 0
    /* Send SIGTRAP if we're single-stepping */
    if (ptrace_cancel_bpt(current))
            send_sig(SIGTRAP, current, 1);
#endif

    return 0;
}

long do_sigreturn_v2(CPUState *env)
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
        force_sig(SIGSEGV /* , current */);
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

long do_rt_sigreturn_v1(CPUState *env)
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
        force_sig(SIGSEGV /* , current */);
	return 0;
}

long do_rt_sigreturn_v2(CPUState *env)
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
        force_sig(SIGSEGV /* , current */);
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
        abi_ulong ins[6];
        struct sparc_stackf *fp;
        abi_ulong callers_pc;
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
        abi_ulong fpu_save_addr;
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

        err |= __get_user(fpu_save_addr, &sf->fpu_save);

        //if (fpu_save)
        //        err |= restore_fpu_state(env, fpu_save);

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
    struct target_ucontext *uc_link;
    abi_ulong uc_flags;
    target_sigset_t uc_sigmask;
    target_mcontext_t uc_mcontext;
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
    unsigned char fenab;
    int err;
    unsigned int i;

    ucp_addr = env->regwptr[UREG_I0];
    if (!lock_user_struct(VERIFY_READ, ucp, ucp_addr, 1))
        goto do_sigsegv;
    grp  = &ucp->uc_mcontext.mc_gregs;
    err  = __get_user(pc, &((*grp)[MC_PC]));
    err |= __get_user(npc, &((*grp)[MC_NPC]));
    if (err || ((pc | npc) & 3))
        goto do_sigsegv;
    if (env->regwptr[UREG_I1]) {
        target_sigset_t target_set;
        sigset_t set;

        if (TARGET_NSIG_WORDS == 1) {
            if (__get_user(target_set.sig[0], &ucp->uc_sigmask.sig[0]))
                goto do_sigsegv;
        } else {
            abi_ulong *src, *dst;
            src = ucp->uc_sigmask.sig;
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
    PUT_CCR(env, tstate >> 32);
    PUT_CWP64(env, tstate & 0x1f);
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

    err |= __get_user(fp, &(ucp->uc_mcontext.mc_fp));
    err |= __get_user(i7, &(ucp->uc_mcontext.mc_i7));

    w_addr = TARGET_STACK_BIAS+env->regwptr[UREG_I6];
    if (put_user(fp, w_addr + offsetof(struct target_reg_window, ins[6]), 
                 abi_ulong) != 0)
        goto do_sigsegv;
    if (put_user(i7, w_addr + offsetof(struct target_reg_window, ins[7]), 
                 abi_ulong) != 0)
        goto do_sigsegv;
    err |= __get_user(fenab, &(ucp->uc_mcontext.mc_fpregs.mcfpu_enab));
    err |= __get_user(env->fprs, &(ucp->uc_mcontext.mc_fpregs.mcfpu_fprs));
    {
        uint32_t *src, *dst;
        src = ucp->uc_mcontext.mc_fpregs.mcfpu_fregs.sregs;
        dst = env->fpr;
        /* XXX: check that the CPU storage is the same as user context */
        for (i = 0; i < 64; i++, dst++, src++)
            err |= __get_user(*dst, src);
    }
    err |= __get_user(env->fsr,
                      &(ucp->uc_mcontext.mc_fpregs.mcfpu_fsr));
    err |= __get_user(env->gsr,
                      &(ucp->uc_mcontext.mc_fpregs.mcfpu_gsr));
    if (err)
        goto do_sigsegv;
    unlock_user_struct(ucp, ucp_addr, 0);
    return;
 do_sigsegv:
    unlock_user_struct(ucp, ucp_addr, 0);
    force_sig(SIGSEGV);
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
    
    mcp = &ucp->uc_mcontext;
    grp = &mcp->mc_gregs;

    /* Skip over the trap instruction, first. */
    env->pc = env->npc;
    env->npc += 4;

    err = 0;

    sigprocmask(0, NULL, &set);
    host_to_target_sigset_internal(&target_set, &set);
    if (TARGET_NSIG_WORDS == 1) {
        err |= __put_user(target_set.sig[0],
                          (abi_ulong *)&ucp->uc_sigmask);
    } else {
        abi_ulong *src, *dst;
        src = target_set.sig;
        dst = ucp->uc_sigmask.sig;
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
        uint32_t *src, *dst;
        src = env->fpr;
        dst = ucp->uc_mcontext.mc_fpregs.mcfpu_fregs.sregs;
        /* XXX: check that the CPU storage is the same as user context */
        for (i = 0; i < 64; i++, dst++, src++)
            err |= __put_user(*src, dst);
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
    force_sig(SIGSEGV);
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
    return 0;

badframe:
    force_sig(TARGET_SIGSEGV/*, current*/);
    return 0;
}

static void setup_rt_frame(int sig, struct target_sigaction *ka,
                           target_siginfo_t *info,
			   target_sigset_t *set, CPUState *env)
{
    fprintf(stderr, "setup_rt_frame: not implemented\n");
}

long do_rt_sigreturn(CPUState *env)
{
    fprintf(stderr, "do_rt_sigreturn: not implemented\n");
    return -TARGET_ENOSYS;
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
    target_ulong uc_flags;
    struct target_ucontext *uc_link;
    target_stack_t uc_stack;
    struct target_sigcontext uc_mcontext;
    target_sigset_t uc_sigmask;	/* mask last for extensibility */
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

    /* todo: save FPU registers here */

    /* non-iBCS2 extensions.. */
    err |= __put_user(mask, &sc->oldmask);

    return err;
}

static int restore_sigcontext(struct CPUState *regs,
			      struct target_sigcontext *sc)
{
    unsigned int err = 0;

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

    /* todo: restore FPU registers here */

    regs->tra = -1;         /* disable syscall checks */
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
    regs->gregs[15] = (unsigned long) frame;
    regs->gregs[4] = signal; /* Arg for signal handler */
    regs->gregs[5] = 0;
    regs->gregs[6] = (unsigned long) &frame->sc;
    regs->pc = (unsigned long) ka->_sa_handler;

    unlock_user_struct(frame, frame_addr, 1);
    return;

give_sigsegv:
    unlock_user_struct(frame, frame_addr, 1);
    force_sig(SIGSEGV);
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
    err |= __put_user(0, &frame->uc.uc_flags);
    err |= __put_user(0, (unsigned long *)&frame->uc.uc_link);
    err |= __put_user((unsigned long)target_sigaltstack_used.ss_sp,
		      &frame->uc.uc_stack.ss_sp);
    err |= __put_user(sas_ss_flags(regs->gregs[15]),
		      &frame->uc.uc_stack.ss_flags);
    err |= __put_user(target_sigaltstack_used.ss_size,
		      &frame->uc.uc_stack.ss_size);
    err |= setup_sigcontext(&frame->uc.uc_mcontext,
			    regs, set->sig[0]);
    for(i = 0; i < TARGET_NSIG_WORDS; i++) {
        err |= __put_user(set->sig[i], &frame->uc.uc_sigmask.sig[i]);
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
    regs->gregs[15] = (unsigned long) frame;
    regs->gregs[4] = signal; /* Arg for signal handler */
    regs->gregs[5] = (unsigned long) &frame->info;
    regs->gregs[6] = (unsigned long) &frame->uc;
    regs->pc = (unsigned long) ka->_sa_handler;

    unlock_user_struct(frame, frame_addr, 1);
    return;

give_sigsegv:
    unlock_user_struct(frame, frame_addr, 1);
    force_sig(SIGSEGV);
}

long do_sigreturn(CPUState *regs)
{
    struct target_sigframe *frame;
    abi_ulong frame_addr;
    sigset_t blocked;
    target_sigset_t target_set;
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

    if (restore_sigcontext(regs, &frame->sc))
        goto badframe;

    unlock_user_struct(frame, frame_addr, 0);
    return regs->gregs[0];

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

#if defined(DEBUG_SIGNAL)
    fprintf(stderr, "do_rt_sigreturn\n");
#endif
    frame_addr = regs->gregs[15];
    if (!lock_user_struct(VERIFY_READ, frame, frame_addr, 1))
   	goto badframe;

    target_to_host_sigset(&blocked, &frame->uc.uc_sigmask);
    sigprocmask(SIG_SETMASK, &blocked, NULL);

    if (restore_sigcontext(regs, &frame->uc.uc_mcontext))
        goto badframe;

    if (do_sigaltstack(frame_addr +
		       offsetof(struct target_rt_sigframe, uc.uc_stack),
		       0, get_sp_from_cpustate(regs)) == -EFAULT)
        goto badframe;

    unlock_user_struct(frame, frame_addr, 0);
    return regs->gregs[0];

badframe:
    unlock_user_struct(frame, frame_addr, 0);
    force_sig(TARGET_SIGSEGV);
    return 0;
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
	env->regs[R_SP] = (uint32_t) (unsigned long) frame;
	env->regs[10] = sig;
	env->pc = (unsigned long) ka->_sa_handler;
	/* Link SRP so the guest returns through the trampoline.  */
	env->pregs[PR_SRP] = (uint32_t) (unsigned long) &frame->retcode[0];

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
	/* Compensate for the syscall return path advancing brk.  */
	env->pc -= 2;

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
        fprintf (stderr, "Lost signal\n");
        abort();
    }

    sa = &sigact_table[sig - 1];
    handler = sa->_sa_handler;
    if (handler == TARGET_SIG_DFL) {
        /* default handler : ignore some signal. The other are fatal */
        if (sig != TARGET_SIGCHLD &&
            sig != TARGET_SIGURG &&
            sig != TARGET_SIGWINCH) {
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

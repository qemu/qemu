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

//#define DEBUG_SIGNAL

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

static struct emulated_sigaction sigact_table[TARGET_NSIG];
static struct sigqueue sigqueue_table[MAX_SIGQUEUE_SIZE]; /* siginfo queue */
static struct sigqueue *first_free; /* first free siginfo queue entry */
static int signal_pending; /* non zero if a signal may be pending */

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
};
static uint8_t target_to_host_signal_table[65];

static inline int host_to_target_signal(int sig)
{
    return host_to_target_signal_table[sig];
}

static inline int target_to_host_signal(int sig)
{
    return target_to_host_signal_table[sig];
}

static void host_to_target_sigset_internal(target_sigset_t *d, 
                                           const sigset_t *s)
{
    int i;
    unsigned long sigmask;
    uint32_t target_sigmask;
    
    sigmask = ((unsigned long *)s)[0];
    target_sigmask = 0;
    for(i = 0; i < 32; i++) {
        if (sigmask & (1 << i)) 
            target_sigmask |= 1 << (host_to_target_signal(i + 1) - 1);
    }
#if TARGET_LONG_BITS == 32 && HOST_LONG_BITS == 32
    d->sig[0] = target_sigmask;
    for(i = 1;i < TARGET_NSIG_WORDS; i++) {
        d->sig[i] = ((unsigned long *)s)[i];
    }
#elif TARGET_LONG_BITS == 32 && HOST_LONG_BITS == 64 && TARGET_NSIG_WORDS == 2
    d->sig[0] = target_sigmask;
    d->sig[1] = sigmask >> 32;
#else
#warning host_to_target_sigset
#endif
}

void host_to_target_sigset(target_sigset_t *d, const sigset_t *s)
{
    target_sigset_t d1;
    int i;

    host_to_target_sigset_internal(&d1, s);
    for(i = 0;i < TARGET_NSIG_WORDS; i++)
        d->sig[i] = tswapl(d1.sig[i]);
}

void target_to_host_sigset_internal(sigset_t *d, const target_sigset_t *s)
{
    int i;
    unsigned long sigmask;
    target_ulong target_sigmask;

    target_sigmask = s->sig[0];
    sigmask = 0;
    for(i = 0; i < 32; i++) {
        if (target_sigmask & (1 << i)) 
            sigmask |= 1 << (target_to_host_signal(i + 1) - 1);
    }
#if TARGET_LONG_BITS == 32 && HOST_LONG_BITS == 32
    ((unsigned long *)d)[0] = sigmask;
    for(i = 1;i < TARGET_NSIG_WORDS; i++) {
        ((unsigned long *)d)[i] = s->sig[i];
    }
#elif TARGET_LONG_BITS == 32 && HOST_LONG_BITS == 64 && TARGET_NSIG_WORDS == 2
    ((unsigned long *)d)[0] = sigmask | ((unsigned long)(s->sig[1]) << 32);
#else
#warning target_to_host_sigset
#endif /* TARGET_LONG_BITS */
}

void target_to_host_sigset(sigset_t *d, const target_sigset_t *s)
{
    target_sigset_t s1;
    int i;

    for(i = 0;i < TARGET_NSIG_WORDS; i++)
        s1.sig[i] = tswapl(s->sig[i]);
    target_to_host_sigset_internal(d, &s1);
}
    
void host_to_target_old_sigset(target_ulong *old_sigset, 
                               const sigset_t *sigset)
{
    target_sigset_t d;
    host_to_target_sigset(&d, sigset);
    *old_sigset = d.sig[0];
}

void target_to_host_old_sigset(sigset_t *sigset, 
                               const target_ulong *old_sigset)
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
    tinfo->si_code = 0;
    if (sig == SIGILL || sig == SIGFPE || sig == SIGSEGV || 
        sig == SIGBUS || sig == SIGTRAP) {
        /* should never come here, but who knows. The information for
           the target is irrelevant */
        tinfo->_sifields._sigfault._addr = 0;
    } else if (sig >= TARGET_SIGRTMIN) {
        tinfo->_sifields._rt._pid = info->si_pid;
        tinfo->_sifields._rt._uid = info->si_uid;
        /* XXX: potential problem if 64 bit */
        tinfo->_sifields._rt._sigval.sival_ptr = 
            (target_ulong)info->si_value.sival_ptr;
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
/* XXX: find a solution for 64 bit (additionnal malloced data is needed) */
void target_to_host_siginfo(siginfo_t *info, const target_siginfo_t *tinfo)
{
    info->si_signo = tswap32(tinfo->si_signo);
    info->si_errno = tswap32(tinfo->si_errno);
    info->si_code = tswap32(tinfo->si_code);
    info->si_pid = tswap32(tinfo->_sifields._rt._pid);
    info->si_uid = tswap32(tinfo->_sifields._rt._uid);
    info->si_value.sival_ptr = 
        (void *)tswapl(tinfo->_sifields._rt._sigval.sival_ptr);
}

void signal_init(void)
{
    struct sigaction act;
    int i, j;

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
void __attribute((noreturn)) force_sig(int sig)
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
    handler = k->sa._sa_handler;
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
                q = alloc_sigqueue();
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
    if (host_signum == SIGSEGV || host_signum == SIGBUS 
#if defined(TARGET_I386) && defined(USE_CODE_COPY)
        || host_signum == SIGFPE
#endif
        ) {
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
    if (queue_signal(sig, &tinfo) == 1) {
        /* interrupt the virtual CPU as soon as possible */
        cpu_interrupt(global_env, CPU_INTERRUPT_EXIT);
    }
}

int do_sigaction(int sig, const struct target_sigaction *act,
                 struct target_sigaction *oact)
{
    struct emulated_sigaction *k;
    struct sigaction act1;
    int host_sig;

    if (sig < 1 || sig > TARGET_NSIG)
        return -EINVAL;
    k = &sigact_table[sig - 1];
#if defined(DEBUG_SIGNAL)
    fprintf(stderr, "sigaction sig=%d act=0x%08x, oact=0x%08x\n", 
            sig, (int)act, (int)oact);
#endif
    if (oact) {
        oact->_sa_handler = tswapl(k->sa._sa_handler);
        oact->sa_flags = tswapl(k->sa.sa_flags);
	#if !defined(TARGET_MIPS)
        	oact->sa_restorer = tswapl(k->sa.sa_restorer);
	#endif
        oact->sa_mask = k->sa.sa_mask;
    }
    if (act) {
        k->sa._sa_handler = tswapl(act->_sa_handler);
        k->sa.sa_flags = tswapl(act->sa_flags);
	#if !defined(TARGET_MIPS)
        	k->sa.sa_restorer = tswapl(act->sa_restorer);
	#endif
        k->sa.sa_mask = act->sa_mask;

        /* we update the host linux signal state */
        host_sig = target_to_host_signal(sig);
        if (host_sig != SIGSEGV && host_sig != SIGBUS) {
            sigfillset(&act1.sa_mask);
            act1.sa_flags = SA_SIGINFO;
            if (k->sa.sa_flags & TARGET_SA_RESTART)
                act1.sa_flags |= SA_RESTART;
            /* NOTE: it is important to update the host kernel signal
               ignore state to avoid getting unexpected interrupted
               syscalls */
            if (k->sa._sa_handler == TARGET_SIG_IGN) {
                act1.sa_sigaction = (void *)SIG_IGN;
            } else if (k->sa._sa_handler == TARGET_SIG_DFL) {
                act1.sa_sigaction = (void *)SIG_DFL;
            } else {
                act1.sa_sigaction = host_signal_handler;
            }
            sigaction(host_sig, &act1, NULL);
        }
    }
    return 0;
}

#ifndef offsetof
#define offsetof(type, field) ((size_t) &((type *)0)->field)
#endif

static inline int copy_siginfo_to_user(target_siginfo_t *tinfo, 
                                       const target_siginfo_t *info)
{
    tswap_siginfo(tinfo, info);
    return 0;
}

#ifdef TARGET_I386

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
	target_ulong element[4];
};

struct target_fpstate {
	/* Regular FPU environment */
	target_ulong 	cw;
	target_ulong	sw;
	target_ulong	tag;
	target_ulong	ipoff;
	target_ulong	cssel;
	target_ulong	dataoff;
	target_ulong	datasel;
	struct target_fpreg	_st[8];
	uint16_t	status;
	uint16_t	magic;		/* 0xffff = regular FPU data only */

	/* FXSR FPU environment */
	target_ulong	_fxsr_env[6];	/* FXSR FPU env is ignored */
	target_ulong	mxcsr;
	target_ulong	reserved;
	struct target_fpxreg	_fxsr_st[8];	/* FXSR FPU reg data is ignored */
	struct target_xmmreg	_xmm[8];
	target_ulong	padding[56];
};

#define X86_FXSR_MAGIC		0x0000

struct target_sigcontext {
	uint16_t gs, __gsh;
	uint16_t fs, __fsh;
	uint16_t es, __esh;
	uint16_t ds, __dsh;
	target_ulong edi;
	target_ulong esi;
	target_ulong ebp;
	target_ulong esp;
	target_ulong ebx;
	target_ulong edx;
	target_ulong ecx;
	target_ulong eax;
	target_ulong trapno;
	target_ulong err;
	target_ulong eip;
	uint16_t cs, __csh;
	target_ulong eflags;
	target_ulong esp_at_signal;
	uint16_t ss, __ssh;
        target_ulong fpstate; /* pointer */
	target_ulong oldmask;
	target_ulong cr2;
};

typedef struct target_sigaltstack {
	target_ulong ss_sp;
	int ss_flags;
	target_ulong ss_size;
} target_stack_t;

struct target_ucontext {
        target_ulong	  tuc_flags;
	target_ulong      tuc_link;
	target_stack_t	  tuc_stack;
	struct target_sigcontext tuc_mcontext;
	target_sigset_t	  tuc_sigmask;	/* mask last for extensibility */
};

struct sigframe
{
    target_ulong pretcode;
    int sig;
    struct target_sigcontext sc;
    struct target_fpstate fpstate;
    target_ulong extramask[TARGET_NSIG_WORDS-1];
    char retcode[8];
};

struct rt_sigframe
{
    target_ulong pretcode;
    int sig;
    target_ulong pinfo;
    target_ulong puc;
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
		 CPUX86State *env, unsigned long mask)
{
	int err = 0;

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

        cpu_x86_fsave(env, (void *)fpstate, 1);
        fpstate->status = fpstate->sw;
        err |= __put_user(0xffff, &fpstate->magic);
        err |= __put_user(fpstate, &sc->fpstate);

	/* non-iBCS2 extensions.. */
	err |= __put_user(mask, &sc->oldmask);
	err |= __put_user(env->cr[2], &sc->cr2);
	return err;
}

/*
 * Determine which stack to use..
 */

static inline void *
get_sigframe(struct emulated_sigaction *ka, CPUX86State *env, size_t frame_size)
{
	unsigned long esp;

	/* Default to using normal stack */
	esp = env->regs[R_ESP];
#if 0
	/* This is the X/Open sanctioned signal stack switching.  */
	if (ka->sa.sa_flags & SA_ONSTACK) {
		if (sas_ss_flags(esp) == 0)
			esp = current->sas_ss_sp + current->sas_ss_size;
	}

	/* This is the legacy signal stack switching. */
	else 
#endif
        if ((env->segs[R_SS].selector & 0xffff) != __USER_DS &&
            !(ka->sa.sa_flags & TARGET_SA_RESTORER) &&
            ka->sa.sa_restorer) {
            esp = (unsigned long) ka->sa.sa_restorer;
	}
        return g2h((esp - frame_size) & -8ul);
}

static void setup_frame(int sig, struct emulated_sigaction *ka,
			target_sigset_t *set, CPUX86State *env)
{
	struct sigframe *frame;
	int i, err = 0;

	frame = get_sigframe(ka, env, sizeof(*frame));

	if (!access_ok(VERIFY_WRITE, frame, sizeof(*frame)))
		goto give_sigsegv;
	err |= __put_user((/*current->exec_domain
		           && current->exec_domain->signal_invmap
		           && sig < 32
		           ? current->exec_domain->signal_invmap[sig]
		           : */ sig),
		          &frame->sig);
	if (err)
		goto give_sigsegv;

	setup_sigcontext(&frame->sc, &frame->fpstate, env, set->sig[0]);
	if (err)
		goto give_sigsegv;

        for(i = 1; i < TARGET_NSIG_WORDS; i++) {
            if (__put_user(set->sig[i], &frame->extramask[i - 1]))
                goto give_sigsegv;
        }

	/* Set up to return from userspace.  If provided, use a stub
	   already in userspace.  */
	if (ka->sa.sa_flags & TARGET_SA_RESTORER) {
		err |= __put_user(ka->sa.sa_restorer, &frame->pretcode);
	} else {
		err |= __put_user(frame->retcode, &frame->pretcode);
		/* This is popl %eax ; movl $,%eax ; int $0x80 */
		err |= __put_user(0xb858, (short *)(frame->retcode+0));
		err |= __put_user(TARGET_NR_sigreturn, (int *)(frame->retcode+2));
		err |= __put_user(0x80cd, (short *)(frame->retcode+6));
	}

	if (err)
		goto give_sigsegv;

	/* Set up registers for signal handler */
	env->regs[R_ESP] = h2g(frame);
	env->eip = (unsigned long) ka->sa._sa_handler;

        cpu_x86_load_seg(env, R_DS, __USER_DS);
        cpu_x86_load_seg(env, R_ES, __USER_DS);
        cpu_x86_load_seg(env, R_SS, __USER_DS);
        cpu_x86_load_seg(env, R_CS, __USER_CS);
	env->eflags &= ~TF_MASK;

	return;

give_sigsegv:
	if (sig == TARGET_SIGSEGV)
		ka->sa._sa_handler = TARGET_SIG_DFL;
	force_sig(TARGET_SIGSEGV /* , current */);
}

static void setup_rt_frame(int sig, struct emulated_sigaction *ka, 
                           target_siginfo_t *info,
			   target_sigset_t *set, CPUX86State *env)
{
	struct rt_sigframe *frame;
	int i, err = 0;

	frame = get_sigframe(ka, env, sizeof(*frame));

	if (!access_ok(VERIFY_WRITE, frame, sizeof(*frame)))
		goto give_sigsegv;

	err |= __put_user((/*current->exec_domain
		    	   && current->exec_domain->signal_invmap
		    	   && sig < 32
		    	   ? current->exec_domain->signal_invmap[sig]
			   : */sig),
			  &frame->sig);
	err |= __put_user((target_ulong)&frame->info, &frame->pinfo);
	err |= __put_user((target_ulong)&frame->uc, &frame->puc);
	err |= copy_siginfo_to_user(&frame->info, info);
	if (err)
		goto give_sigsegv;

	/* Create the ucontext.  */
	err |= __put_user(0, &frame->uc.tuc_flags);
	err |= __put_user(0, &frame->uc.tuc_link);
	err |= __put_user(/*current->sas_ss_sp*/ 0,
			  &frame->uc.tuc_stack.ss_sp);
	err |= __put_user(/* sas_ss_flags(regs->esp) */ 0,
			  &frame->uc.tuc_stack.ss_flags);
	err |= __put_user(/* current->sas_ss_size */ 0,
			  &frame->uc.tuc_stack.ss_size);
	err |= setup_sigcontext(&frame->uc.tuc_mcontext, &frame->fpstate,
			        env, set->sig[0]);
        for(i = 0; i < TARGET_NSIG_WORDS; i++) {
            if (__put_user(set->sig[i], &frame->uc.tuc_sigmask.sig[i]))
                goto give_sigsegv;
        }

	/* Set up to return from userspace.  If provided, use a stub
	   already in userspace.  */
	if (ka->sa.sa_flags & TARGET_SA_RESTORER) {
		err |= __put_user(ka->sa.sa_restorer, &frame->pretcode);
	} else {
		err |= __put_user(frame->retcode, &frame->pretcode);
		/* This is movl $,%eax ; int $0x80 */
		err |= __put_user(0xb8, (char *)(frame->retcode+0));
		err |= __put_user(TARGET_NR_rt_sigreturn, (int *)(frame->retcode+1));
		err |= __put_user(0x80cd, (short *)(frame->retcode+5));
	}

	if (err)
		goto give_sigsegv;

	/* Set up registers for signal handler */
	env->regs[R_ESP] = (unsigned long) frame;
	env->eip = (unsigned long) ka->sa._sa_handler;

        cpu_x86_load_seg(env, R_DS, __USER_DS);
        cpu_x86_load_seg(env, R_ES, __USER_DS);
        cpu_x86_load_seg(env, R_SS, __USER_DS);
        cpu_x86_load_seg(env, R_CS, __USER_CS);
	env->eflags &= ~TF_MASK;

	return;

give_sigsegv:
	if (sig == TARGET_SIGSEGV)
		ka->sa._sa_handler = TARGET_SIG_DFL;
	force_sig(TARGET_SIGSEGV /* , current */);
}

static int
restore_sigcontext(CPUX86State *env, struct target_sigcontext *sc, int *peax)
{
	unsigned int err = 0;

        cpu_x86_load_seg(env, R_GS, lduw(&sc->gs));
        cpu_x86_load_seg(env, R_FS, lduw(&sc->fs));
        cpu_x86_load_seg(env, R_ES, lduw(&sc->es));
        cpu_x86_load_seg(env, R_DS, lduw(&sc->ds));

        env->regs[R_EDI] = ldl(&sc->edi);
        env->regs[R_ESI] = ldl(&sc->esi);
        env->regs[R_EBP] = ldl(&sc->ebp);
        env->regs[R_ESP] = ldl(&sc->esp);
        env->regs[R_EBX] = ldl(&sc->ebx);
        env->regs[R_EDX] = ldl(&sc->edx);
        env->regs[R_ECX] = ldl(&sc->ecx);
        env->eip = ldl(&sc->eip);

        cpu_x86_load_seg(env, R_CS, lduw(&sc->cs) | 3);
        cpu_x86_load_seg(env, R_SS, lduw(&sc->ss) | 3);
	
	{
		unsigned int tmpflags;
                tmpflags = ldl(&sc->eflags);
		env->eflags = (env->eflags & ~0x40DD5) | (tmpflags & 0x40DD5);
                //		regs->orig_eax = -1;		/* disable syscall checks */
	}

	{
		struct _fpstate * buf;
                buf = (void *)ldl(&sc->fpstate);
		if (buf) {
#if 0
			if (verify_area(VERIFY_READ, buf, sizeof(*buf)))
				goto badframe;
#endif
                        cpu_x86_frstor(env, (void *)buf, 1);
		}
	}

        *peax = ldl(&sc->eax);
	return err;
#if 0
badframe:
	return 1;
#endif
}

long do_sigreturn(CPUX86State *env)
{
    struct sigframe *frame = (struct sigframe *)g2h(env->regs[R_ESP] - 8);
    target_sigset_t target_set;
    sigset_t set;
    int eax, i;

#if defined(DEBUG_SIGNAL)
    fprintf(stderr, "do_sigreturn\n");
#endif
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
    return eax;

badframe:
    force_sig(TARGET_SIGSEGV);
    return 0;
}

long do_rt_sigreturn(CPUX86State *env)
{
	struct rt_sigframe *frame = (struct rt_sigframe *)g2h(env->regs[R_ESP] - 4);
        sigset_t set;
        //	stack_t st;
	int eax;

#if 0
	if (verify_area(VERIFY_READ, frame, sizeof(*frame)))
		goto badframe;
#endif
        target_to_host_sigset(&set, &frame->uc.tuc_sigmask);
        sigprocmask(SIG_SETMASK, &set, NULL);
	
	if (restore_sigcontext(env, &frame->uc.tuc_mcontext, &eax))
		goto badframe;

#if 0
	if (__copy_from_user(&st, &frame->uc.tuc_stack, sizeof(st)))
		goto badframe;
	/* It is more difficult to avoid calling this function than to
	   call it and ignore errors.  */
	do_sigaltstack(&st, NULL, regs->esp);
#endif
	return eax;

badframe:
	force_sig(TARGET_SIGSEGV);
	return 0;
}

#elif defined(TARGET_ARM)

struct target_sigcontext {
	target_ulong trap_no;
	target_ulong error_code;
	target_ulong oldmask;
	target_ulong arm_r0;
	target_ulong arm_r1;
	target_ulong arm_r2;
	target_ulong arm_r3;
	target_ulong arm_r4;
	target_ulong arm_r5;
	target_ulong arm_r6;
	target_ulong arm_r7;
	target_ulong arm_r8;
	target_ulong arm_r9;
	target_ulong arm_r10;
	target_ulong arm_fp;
	target_ulong arm_ip;
	target_ulong arm_sp;
	target_ulong arm_lr;
	target_ulong arm_pc;
	target_ulong arm_cpsr;
	target_ulong fault_address;
};

typedef struct target_sigaltstack {
	target_ulong ss_sp;
	int ss_flags;
	target_ulong ss_size;
} target_stack_t;

struct target_ucontext {
    target_ulong tuc_flags;
    target_ulong tuc_link;
    target_stack_t tuc_stack;
    struct target_sigcontext tuc_mcontext;
    target_sigset_t  tuc_sigmask;	/* mask last for extensibility */
};

struct sigframe
{
    struct target_sigcontext sc;
    target_ulong extramask[TARGET_NSIG_WORDS-1];
    target_ulong retcode;
};

struct rt_sigframe
{
    struct target_siginfo *pinfo;
    void *puc;
    struct target_siginfo info;
    struct target_ucontext uc;
    target_ulong retcode;
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

static const target_ulong retcodes[4] = {
	SWI_SYS_SIGRETURN,	SWI_THUMB_SIGRETURN,
	SWI_SYS_RT_SIGRETURN,	SWI_THUMB_RT_SIGRETURN
};


#define __put_user_error(x,p,e) __put_user(x, p)
#define __get_user_error(x,p,e) __get_user(x, p)

static inline int valid_user_regs(CPUState *regs)
{
    return 1;
}

static int
setup_sigcontext(struct target_sigcontext *sc, /*struct _fpstate *fpstate,*/
		 CPUState *env, unsigned long mask)
{
	int err = 0;

	__put_user_error(env->regs[0], &sc->arm_r0, err);
	__put_user_error(env->regs[1], &sc->arm_r1, err);
	__put_user_error(env->regs[2], &sc->arm_r2, err);
	__put_user_error(env->regs[3], &sc->arm_r3, err);
	__put_user_error(env->regs[4], &sc->arm_r4, err);
	__put_user_error(env->regs[5], &sc->arm_r5, err);
	__put_user_error(env->regs[6], &sc->arm_r6, err);
	__put_user_error(env->regs[7], &sc->arm_r7, err);
	__put_user_error(env->regs[8], &sc->arm_r8, err);
	__put_user_error(env->regs[9], &sc->arm_r9, err);
	__put_user_error(env->regs[10], &sc->arm_r10, err);
	__put_user_error(env->regs[11], &sc->arm_fp, err);
	__put_user_error(env->regs[12], &sc->arm_ip, err);
	__put_user_error(env->regs[13], &sc->arm_sp, err);
	__put_user_error(env->regs[14], &sc->arm_lr, err);
	__put_user_error(env->regs[15], &sc->arm_pc, err);
#ifdef TARGET_CONFIG_CPU_32
	__put_user_error(cpsr_read(env), &sc->arm_cpsr, err);
#endif

	__put_user_error(/* current->thread.trap_no */ 0, &sc->trap_no, err);
	__put_user_error(/* current->thread.error_code */ 0, &sc->error_code, err);
	__put_user_error(/* current->thread.address */ 0, &sc->fault_address, err);
	__put_user_error(mask, &sc->oldmask, err);

	return err;
}

static inline void *
get_sigframe(struct emulated_sigaction *ka, CPUState *regs, int framesize)
{
	unsigned long sp = regs->regs[13];

#if 0
	/*
	 * This is the X/Open sanctioned signal stack switching.
	 */
	if ((ka->sa.sa_flags & SA_ONSTACK) && !sas_ss_flags(sp))
		sp = current->sas_ss_sp + current->sas_ss_size;
#endif
	/*
	 * ATPCS B01 mandates 8-byte alignment
	 */
	return g2h((sp - framesize) & ~7);
}

static int
setup_return(CPUState *env, struct emulated_sigaction *ka,
	     target_ulong *rc, void *frame, int usig)
{
	target_ulong handler = (target_ulong)ka->sa._sa_handler;
	target_ulong retcode;
	int thumb = 0;
#if defined(TARGET_CONFIG_CPU_32)
#if 0
	target_ulong cpsr = env->cpsr;

	/*
	 * Maybe we need to deliver a 32-bit signal to a 26-bit task.
	 */
	if (ka->sa.sa_flags & SA_THIRTYTWO)
		cpsr = (cpsr & ~MODE_MASK) | USR_MODE;

#ifdef CONFIG_ARM_THUMB
	if (elf_hwcap & HWCAP_THUMB) {
		/*
		 * The LSB of the handler determines if we're going to
		 * be using THUMB or ARM mode for this signal handler.
		 */
		thumb = handler & 1;

		if (thumb)
			cpsr |= T_BIT;
		else
			cpsr &= ~T_BIT;
	}
#endif
#endif
#endif /* TARGET_CONFIG_CPU_32 */

	if (ka->sa.sa_flags & TARGET_SA_RESTORER) {
		retcode = (target_ulong)ka->sa.sa_restorer;
	} else {
		unsigned int idx = thumb;

		if (ka->sa.sa_flags & TARGET_SA_SIGINFO)
			idx += 2;

		if (__put_user(retcodes[idx], rc))
			return 1;
#if 0
		flush_icache_range((target_ulong)rc,
				   (target_ulong)(rc + 1));
#endif
		retcode = ((target_ulong)rc) + thumb;
	}

	env->regs[0] = usig;
	env->regs[13] = h2g(frame);
	env->regs[14] = retcode;
	env->regs[15] = handler & (thumb ? ~1 : ~3);

#if 0
#ifdef TARGET_CONFIG_CPU_32
	env->cpsr = cpsr;
#endif
#endif

	return 0;
}

static void setup_frame(int usig, struct emulated_sigaction *ka,
			target_sigset_t *set, CPUState *regs)
{
	struct sigframe *frame = get_sigframe(ka, regs, sizeof(*frame));
	int i, err = 0;

	err |= setup_sigcontext(&frame->sc, /*&frame->fpstate,*/ regs, set->sig[0]);

        for(i = 1; i < TARGET_NSIG_WORDS; i++) {
            if (__put_user(set->sig[i], &frame->extramask[i - 1]))
                return;
	}

	if (err == 0)
            err = setup_return(regs, ka, &frame->retcode, frame, usig);
        //	return err;
}

static void setup_rt_frame(int usig, struct emulated_sigaction *ka, 
                           target_siginfo_t *info,
			   target_sigset_t *set, CPUState *env)
{
	struct rt_sigframe *frame = get_sigframe(ka, env, sizeof(*frame));
	int i, err = 0;

	if (!access_ok(VERIFY_WRITE, frame, sizeof (*frame)))
            return /* 1 */;

	__put_user_error(&frame->info, (target_ulong *)&frame->pinfo, err);
	__put_user_error(&frame->uc, (target_ulong *)&frame->puc, err);
	err |= copy_siginfo_to_user(&frame->info, info);

	/* Clear all the bits of the ucontext we don't use.  */
	memset(&frame->uc, 0, offsetof(struct target_ucontext, tuc_mcontext));

	err |= setup_sigcontext(&frame->uc.tuc_mcontext, /*&frame->fpstate,*/
				env, set->sig[0]);
        for(i = 0; i < TARGET_NSIG_WORDS; i++) {
            if (__put_user(set->sig[i], &frame->uc.tuc_sigmask.sig[i]))
                return;
        }

	if (err == 0)
		err = setup_return(env, ka, &frame->retcode, frame, usig);

	if (err == 0) {
		/*
		 * For realtime signals we must also set the second and third
		 * arguments for the signal handler.
		 *   -- Peter Maydell <pmaydell@chiark.greenend.org.uk> 2000-12-06
		 */
            env->regs[1] = (target_ulong)frame->pinfo;
            env->regs[2] = (target_ulong)frame->puc;
	}

        //	return err;
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
        cpsr_write(env, cpsr, 0xffffffff);
#endif

	err |= !valid_user_regs(env);

	return err;
}

long do_sigreturn(CPUState *env)
{
	struct sigframe *frame;
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

	frame = (struct sigframe *)g2h(env->regs[13]);

#if 0
	if (verify_area(VERIFY_READ, frame, sizeof (*frame)))
		goto badframe;
#endif
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
	return env->regs[0];

badframe:
        force_sig(SIGSEGV /* , current */);
	return 0;
}

long do_rt_sigreturn(CPUState *env)
{
	struct rt_sigframe *frame;
        sigset_t host_set;

	/*
	 * Since we stacked the signal on a 64-bit boundary,
	 * then 'sp' should be word aligned here.  If it's
	 * not, then the user is trying to mess with us.
	 */
	if (env->regs[13] & 7)
		goto badframe;

	frame = (struct rt_sigframe *)env->regs[13];

#if 0
	if (verify_area(VERIFY_READ, frame, sizeof (*frame)))
		goto badframe;
#endif
        target_to_host_sigset(&host_set, &frame->uc.tuc_sigmask);
        sigprocmask(SIG_SETMASK, &host_set, NULL);

	if (restore_sigcontext(env, &frame->uc.tuc_mcontext))
		goto badframe;

#if 0
	/* Send SIGTRAP if we're single-stepping */
	if (ptrace_cancel_bpt(current))
		send_sig(SIGTRAP, current, 1);
#endif
	return env->regs[0];

badframe:
        force_sig(SIGSEGV /* , current */);
	return 0;
}

#elif defined(TARGET_SPARC)

#define __SUNOS_MAXWIN   31

/* This is what SunOS does, so shall I. */
struct target_sigcontext {
        target_ulong sigc_onstack;      /* state to restore */

        target_ulong sigc_mask;         /* sigmask to restore */
        target_ulong sigc_sp;           /* stack pointer */
        target_ulong sigc_pc;           /* program counter */
        target_ulong sigc_npc;          /* next program counter */
        target_ulong sigc_psr;          /* for condition codes etc */
        target_ulong sigc_g1;           /* User uses these two registers */
        target_ulong sigc_o0;           /* within the trampoline code. */

        /* Now comes information regarding the users window set
         * at the time of the signal.
         */
        target_ulong sigc_oswins;       /* outstanding windows */

        /* stack ptrs for each regwin buf */
        char *sigc_spbuf[__SUNOS_MAXWIN];

        /* Windows to restore after signal */
        struct {
                target_ulong locals[8];
                target_ulong ins[8];
        } sigc_wbuf[__SUNOS_MAXWIN];
};
/* A Sparc stack frame */
struct sparc_stackf {
        target_ulong locals[8];
        target_ulong ins[6];
        struct sparc_stackf *fp;
        target_ulong callers_pc;
        char *structptr;
        target_ulong xargs[6];
        target_ulong xxargs[1];
};

typedef struct {
        struct {
                target_ulong psr;
                target_ulong pc;
                target_ulong npc;
                target_ulong y;
                target_ulong u_regs[16]; /* globals and ins */
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
	qemu_siginfo_fpu_t 	*fpu_save;
	target_ulong		insns[2] __attribute__ ((aligned (8)));
	target_ulong		extramask[TARGET_NSIG_WORDS - 1];
	target_ulong		extra_size; /* Should be 0 */
	qemu_siginfo_fpu_t	fpu_state;
};
struct target_rt_signal_frame {
	struct sparc_stackf	ss;
	siginfo_t		info;
	target_ulong		regs[20];
	sigset_t		mask;
	qemu_siginfo_fpu_t 	*fpu_save;
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
#define UREG_I6        6
#define UREG_I7        7
#define UREG_L0	       8
#define UREG_FP        UREG_I6
#define UREG_SP        UREG_O6

static inline void *get_sigframe(struct emulated_sigaction *sa, CPUState *env, unsigned long framesize)
{
	unsigned long sp;

	sp = env->regwptr[UREG_FP];
#if 0

	/* This is the X/Open sanctioned signal stack switching.  */
	if (sa->sa_flags & TARGET_SA_ONSTACK) {
		if (!on_sig_stack(sp) && !((current->sas_ss_sp + current->sas_ss_size) & 7))
			sp = current->sas_ss_sp + current->sas_ss_size;
	}
#endif
	return g2h(sp - framesize);
}

static int
setup___siginfo(__siginfo_t *si, CPUState *env, target_ulong mask)
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

static void setup_frame(int sig, struct emulated_sigaction *ka,
			target_sigset_t *set, CPUState *env)
{
	struct target_signal_frame *sf;
	int sigframe_size, err, i;

	/* 1. Make sure everything is clean */
	//synchronize_user_stack();

        sigframe_size = NF_ALIGNEDSZ;

	sf = (struct target_signal_frame *)
		get_sigframe(ka, env, sigframe_size);

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
	env->regwptr[UREG_FP] = h2g(sf);
	env->regwptr[UREG_I0] = sig;
	env->regwptr[UREG_I1] = h2g(&sf->info);
	env->regwptr[UREG_I2] = h2g(&sf->info);

	/* 4. signal handler */
	env->pc = (unsigned long) ka->sa._sa_handler;
	env->npc = (env->pc + 4);
	/* 5. return to kernel instructions */
	if (ka->sa.sa_restorer)
		env->regwptr[UREG_I7] = (unsigned long)ka->sa.sa_restorer;
	else {
		env->regwptr[UREG_I7] = h2g(&(sf->insns[0]) - 2);

		/* mov __NR_sigreturn, %g1 */
		err |= __put_user(0x821020d8, &sf->insns[0]);

		/* t 0x10 */
		err |= __put_user(0x91d02010, &sf->insns[1]);
		if (err)
			goto sigsegv;

		/* Flush instruction space. */
		//flush_sig_insns(current->mm, (unsigned long) &(sf->insns[0]));
                //		tb_flush(env);
	}
	return;

        //sigill_and_return:
	force_sig(TARGET_SIGILL);
sigsegv:
	//fprintf(stderr, "force_sig\n");
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

        err = __copy_from_user(&env->fpr[0], &fpu->si_float_regs[0],
	                             (sizeof(unsigned long) * 32));
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


static void setup_rt_frame(int sig, struct emulated_sigaction *ka, 
                           target_siginfo_t *info,
			   target_sigset_t *set, CPUState *env)
{
    fprintf(stderr, "setup_rt_frame: not implemented\n");
}

long do_sigreturn(CPUState *env)
{
        struct target_signal_frame *sf;
        uint32_t up_psr, pc, npc;
        target_sigset_t set;
        sigset_t host_set;
        target_ulong fpu_save;
        int err, i;

        sf = (struct target_signal_frame *)g2h(env->regwptr[UREG_FP]);
#if 0
	fprintf(stderr, "sigreturn\n");
	fprintf(stderr, "sf: %x pc %x fp %x sp %x\n", sf, env->pc, env->regwptr[UREG_FP], env->regwptr[UREG_SP]);
#endif
	//cpu_dump_state(env, stderr, fprintf, 0);

        /* 1. Make sure we are not getting garbage from the user */
#if 0
        if (verify_area (VERIFY_READ, sf, sizeof (*sf)))
                goto segv_and_exit;
#endif

        if (((uint) sf) & 3)
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

        err |= __get_user(fpu_save, (target_ulong *)&sf->fpu_save);

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

        return env->regwptr[0];

segv_and_exit:
	force_sig(TARGET_SIGSEGV);
}

long do_rt_sigreturn(CPUState *env)
{
    fprintf(stderr, "do_rt_sigreturn: not implemented\n");
    return -ENOSYS;
}

#elif defined(TARGET_MIPS)

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

    err |= __put_user(regs->PC, &sc->sc_pc);

    #define save_gp_reg(i) do {   					\
        err |= __put_user(regs->gpr[i], &sc->sc_regs[i]);		\
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

    err |= __put_user(regs->HI, &sc->sc_mdhi);
    err |= __put_user(regs->LO, &sc->sc_mdlo);

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

    err |= __get_user(regs->HI, &sc->sc_mdhi);
    err |= __get_user(regs->LO, &sc->sc_mdlo);

    #define restore_gp_reg(i) do {   					\
        err |= __get_user(regs->gpr[i], &sc->sc_regs[i]);		\
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
static inline void *
get_sigframe(struct emulated_sigaction *ka, CPUState *regs, size_t frame_size)
{
    unsigned long sp;

    /* Default to using normal stack */
    sp = regs->gpr[29];

    /*
     * FPU emulator may have it's own trampoline active just
     * above the user stack, 16-bytes before the next lowest
     * 16 byte boundary.  Try to avoid trashing it.
     */
    sp -= 32;

#if 0
    /* This is the X/Open sanctioned signal stack switching.  */
    if ((ka->sa.sa_flags & SA_ONSTACK) && (sas_ss_flags (sp) == 0))
	sp = current->sas_ss_sp + current->sas_ss_size;
#endif

    return g2h((sp - frame_size) & ~7);
}

static void setup_frame(int sig, struct emulated_sigaction * ka, 
   		target_sigset_t *set, CPUState *regs)
{
    struct sigframe *frame;
    int i;

    frame = get_sigframe(ka, regs, sizeof(*frame));
    if (!access_ok(VERIFY_WRITE, frame, sizeof (*frame)))
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
    regs->gpr[ 4] = sig;
    regs->gpr[ 5] = 0;
    regs->gpr[ 6] = h2g(&frame->sf_sc);
    regs->gpr[29] = h2g(frame);
    regs->gpr[31] = h2g(frame->sf_code);
    /* The original kernel code sets CP0_EPC to the handler
    * since it returns to userland using eret
    * we cannot do this here, and we must set PC directly */
    regs->PC = regs->gpr[25] = ka->sa._sa_handler;
    return;

give_sigsegv:
    force_sig(TARGET_SIGSEGV/*, current*/);
    return;	
}

long do_sigreturn(CPUState *regs)
{
   struct sigframe *frame;
   sigset_t blocked;
   target_sigset_t target_set;
   int i;

#if defined(DEBUG_SIGNAL)
   fprintf(stderr, "do_sigreturn\n");
#endif
   frame = (struct sigframe *) regs->gpr[29];
   if (!access_ok(VERIFY_READ, frame, sizeof(*frame)))
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
    
    regs->PC = regs->CP0_EPC;
   /* I am not sure this is right, but it seems to work
    * maybe a problem with nested signals ? */
    regs->CP0_EPC = 0;
    return 0;

badframe:
   force_sig(TARGET_SIGSEGV/*, current*/);
   return 0;	

}

static void setup_rt_frame(int sig, struct emulated_sigaction *ka, 
                           target_siginfo_t *info,
			   target_sigset_t *set, CPUState *env)
{
    fprintf(stderr, "setup_rt_frame: not implemented\n");
}

long do_rt_sigreturn(CPUState *env)
{
    fprintf(stderr, "do_rt_sigreturn: not implemented\n");
    return -ENOSYS;
}

#else

static void setup_frame(int sig, struct emulated_sigaction *ka,
			target_sigset_t *set, CPUState *env)
{
    fprintf(stderr, "setup_frame: not implemented\n");
}

static void setup_rt_frame(int sig, struct emulated_sigaction *ka, 
                           target_siginfo_t *info,
			   target_sigset_t *set, CPUState *env)
{
    fprintf(stderr, "setup_rt_frame: not implemented\n");
}

long do_sigreturn(CPUState *env)
{
    fprintf(stderr, "do_sigreturn: not implemented\n");
    return -ENOSYS;
}

long do_rt_sigreturn(CPUState *env)
{
    fprintf(stderr, "do_rt_sigreturn: not implemented\n");
    return -ENOSYS;
}

#endif

void process_pending_signals(void *cpu_env)
{
    int sig;
    target_ulong handler;
    sigset_t set, old_set;
    target_sigset_t target_old_set;
    struct emulated_sigaction *k;
    struct sigqueue *q;
    
    if (!signal_pending)
        return;

    k = sigact_table;
    for(sig = 1; sig <= TARGET_NSIG; sig++) {
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

    handler = k->sa._sa_handler;
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
        target_to_host_sigset(&set, &k->sa.sa_mask);
        /* SA_NODEFER indicates that the current signal should not be
           blocked during the handler */
        if (!(k->sa.sa_flags & TARGET_SA_NODEFER))
            sigaddset(&set, target_to_host_signal(sig));
        
        /* block signals in the handler using Linux */
        sigprocmask(SIG_BLOCK, &set, &old_set);
        /* save the previous blocked signal state to restore it at the
           end of the signal execution (see do_sigreturn) */
        host_to_target_sigset_internal(&target_old_set, &old_set);

        /* if the CPU is in VM86 mode, we restore the 32 bit values */
#ifdef TARGET_I386
        {
            CPUX86State *env = cpu_env;
            if (env->eflags & VM_MASK)
                save_v86_state(env);
        }
#endif
        /* prepare the stack frame of the virtual CPU */
        if (k->sa.sa_flags & TARGET_SA_SIGINFO)
            setup_rt_frame(sig, k, &q->info, &target_old_set, cpu_env);
        else
            setup_frame(sig, k, &target_old_set, cpu_env);
	if (k->sa.sa_flags & TARGET_SA_RESETHAND)
            k->sa._sa_handler = TARGET_SIG_DFL;
    }
    if (q != &k->info)
        free_sigqueue(q);
}



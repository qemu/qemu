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

#ifdef __ia64__
#undef uc_mcontext
#undef uc_sigmask
#undef uc_stack
#undef uc_link
#endif 

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

/* XXX: do it properly */
static inline int host_to_target_signal(int sig)
{
    return sig;
}

static inline int target_to_host_signal(int sig)
{
    return sig;
}

void host_to_target_sigset(target_sigset_t *d, sigset_t *s)
{
    int i;
    for(i = 0;i < TARGET_NSIG_WORDS; i++) {
        d->sig[i] = tswapl(((unsigned long *)s)[i]);
    }
}

void target_to_host_sigset(sigset_t *d, target_sigset_t *s)
{
    int i;
    for(i = 0;i < TARGET_NSIG_WORDS; i++) {
        ((unsigned long *)d)[i] = tswapl(s->sig[i]);
    }
}

void host_to_target_old_sigset(target_ulong *old_sigset, 
                               const sigset_t *sigset)
{
    *old_sigset = tswap32(*(unsigned long *)sigset & 0xffffffff);
}

void target_to_host_old_sigset(sigset_t *sigset, 
                               const target_ulong *old_sigset)
{
    sigemptyset(sigset);
    *(unsigned long *)sigset = tswapl(*old_sigset);
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

#if defined(DEBUG_SIGNAL)
#ifdef __i386__
static void dump_regs(struct ucontext *uc)
{
    fprintf(stderr, 
            "EAX=%08x EBX=%08x ECX=%08x EDX=%08x\n"
            "ESI=%08x EDI=%08x EBP=%08x ESP=%08x\n"
            "EFL=%08x EIP=%08x\n",
            uc->uc_mcontext.gregs[EAX],
            uc->uc_mcontext.gregs[EBX],
            uc->uc_mcontext.gregs[ECX],
            uc->uc_mcontext.gregs[EDX],
            uc->uc_mcontext.gregs[ESI],
            uc->uc_mcontext.gregs[EDI],
            uc->uc_mcontext.gregs[EBP],
            uc->uc_mcontext.gregs[ESP],
            uc->uc_mcontext.gregs[EFL],
            uc->uc_mcontext.gregs[EIP]);
}
#else
static void dump_regs(struct ucontext *uc)
{
}
#endif

#endif

static void host_signal_handler(int host_signum, siginfo_t *info, 
                                void *puc)
{
    int sig;
    target_siginfo_t tinfo;

    /* the CPU emulator uses some host signals to detect exceptions,
       we we forward to it some signals */
    if (host_signum == SIGSEGV || host_signum == SIGBUS) {
        if (cpu_x86_signal_handler(host_signum, info, puc))
            return;
    }

    /* get target signal number */
    sig = host_to_target_signal(host_signum);
    if (sig < 1 || sig > TARGET_NSIG)
        return;
#if defined(DEBUG_SIGNAL)
    fprintf(stderr, "qemu: got signal %d\n", sig);
    dump_regs(puc);
#endif
    host_to_target_siginfo_noswap(&tinfo, info);
    if (queue_signal(sig, &tinfo) == 1) {
        /* interrupt the virtual CPU as soon as possible */
        cpu_x86_interrupt(global_env);
    }
}

int do_sigaction(int sig, const struct target_sigaction *act,
                 struct target_sigaction *oact)
{
    struct emulated_sigaction *k;

    if (sig < 1 || sig > TARGET_NSIG)
        return -EINVAL;
    k = &sigact_table[sig - 1];
#if defined(DEBUG_SIGNAL) && 0
    fprintf(stderr, "sigaction sig=%d act=0x%08x, oact=0x%08x\n", 
            sig, (int)act, (int)oact);
#endif
    if (oact) {
        oact->_sa_handler = tswapl(k->sa._sa_handler);
        oact->sa_flags = tswapl(k->sa.sa_flags);
        oact->sa_restorer = tswapl(k->sa.sa_restorer);
        oact->sa_mask = k->sa.sa_mask;
    }
    if (act) {
        k->sa._sa_handler = tswapl(act->_sa_handler);
        k->sa.sa_flags = tswapl(act->sa_flags);
        k->sa.sa_restorer = tswapl(act->sa_restorer);
        k->sa.sa_mask = act->sa_mask;
    }
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
        target_ulong	  uc_flags;
	target_ulong      uc_link;
	target_stack_t	  uc_stack;
	struct target_sigcontext uc_mcontext;
	target_sigset_t	  uc_sigmask;	/* mask last for extensibility */
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

#define __put_user(x,ptr)\
({\
    int size = sizeof(*ptr);\
    switch(size) {\
    case 1:\
        stb(ptr, (typeof(*ptr))(x));\
        break;\
    case 2:\
        stw(ptr, (typeof(*ptr))(x));\
        break;\
    case 4:\
        stl(ptr, (typeof(*ptr))(x));\
        break;\
    case 8:\
        stq(ptr, (typeof(*ptr))(x));\
        break;\
    default:\
        abort();\
    }\
    0;\
})

#define get_user(val, ptr) (typeof(*ptr))(*(ptr))


#define __copy_to_user(dst, src, size)\
({\
    memcpy(dst, src, size);\
    0;\
})

static inline int copy_siginfo_to_user(target_siginfo_t *tinfo, 
                                       const target_siginfo_t *info)
{
    tswap_siginfo(tinfo, info);
    return 0;
}

/* XXX: save x87 state */
static int
setup_sigcontext(struct target_sigcontext *sc, struct target_fpstate *fpstate,
		 CPUX86State *env, unsigned long mask)
{
	int err = 0;

	err |= __put_user(env->segs[R_GS], (unsigned int *)&sc->gs);
	err |= __put_user(env->segs[R_FS], (unsigned int *)&sc->fs);
	err |= __put_user(env->segs[R_ES], (unsigned int *)&sc->es);
	err |= __put_user(env->segs[R_DS], (unsigned int *)&sc->ds);
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
	err |= __put_user(env->segs[R_CS], (unsigned int *)&sc->cs);
	err |= __put_user(env->eflags, &sc->eflags);
	err |= __put_user(env->regs[R_ESP], &sc->esp_at_signal);
	err |= __put_user(env->segs[R_SS], (unsigned int *)&sc->ss);
#if 0
	tmp = save_i387(fpstate);
	if (tmp < 0)
	  err = 1;
	else
	  err |= __put_user(tmp ? fpstate : NULL, &sc->fpstate);
#else
        err |= __put_user(0, &sc->fpstate);
#endif
	/* non-iBCS2 extensions.. */
	err |= __put_user(mask, &sc->oldmask);
	err |= __put_user(env->cr2, &sc->cr2);
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
	else if ((regs->xss & 0xffff) != __USER_DS &&
		 !(ka->sa.sa_flags & SA_RESTORER) &&
		 ka->sa.sa_restorer) {
		esp = (unsigned long) ka->sa.sa_restorer;
	}
#endif
	return (void *)((esp - frame_size) & -8ul);
}

static void setup_frame(int sig, struct emulated_sigaction *ka,
			target_sigset_t *set, CPUX86State *env)
{
	struct sigframe *frame;
	int err = 0;

	frame = get_sigframe(ka, env, sizeof(*frame));

#if 0
	if (!access_ok(VERIFY_WRITE, frame, sizeof(*frame)))
		goto give_sigsegv;
#endif
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

	if (TARGET_NSIG_WORDS > 1) {
		err |= __copy_to_user(frame->extramask, &set->sig[1],
				      sizeof(frame->extramask));
	}
	if (err)
		goto give_sigsegv;

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

static void setup_rt_frame(int sig, struct emulated_sigaction *ka, 
                           target_siginfo_t *info,
			   target_sigset_t *set, CPUX86State *env)
{
	struct rt_sigframe *frame;
	int err = 0;

	frame = get_sigframe(ka, env, sizeof(*frame));

#if 0
	if (!access_ok(VERIFY_WRITE, frame, sizeof(*frame)))
		goto give_sigsegv;
#endif

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
	err |= __put_user(0, &frame->uc.uc_flags);
	err |= __put_user(0, &frame->uc.uc_link);
	err |= __put_user(/*current->sas_ss_sp*/ 0, &frame->uc.uc_stack.ss_sp);
	err |= __put_user(/* sas_ss_flags(regs->esp) */ 0,
			  &frame->uc.uc_stack.ss_flags);
	err |= __put_user(/* current->sas_ss_size */ 0, &frame->uc.uc_stack.ss_size);
	err |= setup_sigcontext(&frame->uc.uc_mcontext, &frame->fpstate,
			        env, set->sig[0]);
	err |= __copy_to_user(&frame->uc.uc_sigmask, set, sizeof(*set));
	if (err)
		goto give_sigsegv;

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


        
#define COPY(x)		err |= __get_user(regs->x, &sc->x)

#define COPY_SEG(seg)							\
	{ unsigned short tmp;						\
	  err |= __get_user(tmp, &sc->seg);				\
	  regs->x##seg = tmp; }

#define COPY_SEG_STRICT(seg)						\
	{ unsigned short tmp;						\
	  err |= __get_user(tmp, &sc->seg);				\
	  regs->x##seg = tmp|3; }

#define GET_SEG(seg)							\
	{ unsigned short tmp;						\
	  err |= __get_user(tmp, &sc->seg);				\
	  loadsegment(seg,tmp); }

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

#if 0
	{
		struct _fpstate * buf;
		err |= __get_user(buf, &sc->fpstate);
		if (buf) {
			if (verify_area(VERIFY_READ, buf, sizeof(*buf)))
				goto badframe;
			err |= restore_i387(buf);
		}
	}
#endif
        *peax = ldl(&sc->eax);
	return err;
#if 0
badframe:
	return 1;
#endif
}

long do_sigreturn(CPUX86State *env)
{
    struct sigframe *frame = (struct sigframe *)(env->regs[R_ESP] - 8);
    target_sigset_t target_set;
    sigset_t set;
    int eax, i;

#if defined(DEBUG_SIGNAL)
    fprintf(stderr, "do_sigreturn\n");
#endif
    /* set blocked signals */
    target_set.sig[0] = frame->sc.oldmask;
    for(i = 1; i < TARGET_NSIG_WORDS; i++)
        target_set.sig[i] = frame->extramask[i - 1];

    target_to_host_sigset(&set, &target_set);
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
	struct rt_sigframe *frame = (struct rt_sigframe *)(env->regs[R_ESP] - 4);
	target_sigset_t target_set;
        sigset_t set;
        //	stack_t st;
	int eax;

#if 0
	if (verify_area(VERIFY_READ, frame, sizeof(*frame)))
		goto badframe;
#endif
        memcpy(&target_set, &frame->uc.uc_sigmask, sizeof(target_sigset_t));

        target_to_host_sigset(&set, &target_set);
        sigprocmask(SIG_SETMASK, &set, NULL);
	
	if (restore_sigcontext(env, &frame->uc.uc_mcontext, &eax))
		goto badframe;

#if 0
	if (__copy_from_user(&st, &frame->uc.uc_stack, sizeof(st)))
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
        host_to_target_sigset(&target_old_set, &old_set);

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



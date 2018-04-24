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
#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include <sys/ucontext.h>
#include <sys/resource.h>

#include "qemu.h"
#include "qemu-common.h"
#include "target_signal.h"
#include "trace.h"
#include "signal-common.h"

struct target_sigaltstack target_sigaltstack_used = {
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
       host libpthread signals.  This assumes no one actually uses SIGRTMAX :-/
       To fix this properly we need to do manual signal delivery multiplexed
       over a single host signal.  */
    [__SIGRTMIN] = __SIGRTMAX,
    [__SIGRTMAX] = __SIGRTMIN,
};
static uint8_t target_to_host_signal_table[_NSIG];

int host_to_target_signal(int sig)
{
    if (sig < 0 || sig >= _NSIG)
        return sig;
    return host_to_target_signal_table[sig];
}

int target_to_host_signal(int sig)
{
    if (sig < 0 || sig >= _NSIG)
        return sig;
    return target_to_host_signal_table[sig];
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

void host_to_target_sigset_internal(target_sigset_t *d,
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

void target_to_host_sigset_internal(sigset_t *d,
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

int block_signals(void)
{
    TaskState *ts = (TaskState *)thread_cpu->opaque;
    sigset_t set;

    /* It's OK to block everything including SIGSEGV, because we won't
     * run any further guest code before unblocking signals in
     * process_pending_signals().
     */
    sigfillset(&set);
    sigprocmask(SIG_SETMASK, &set, 0);

    return atomic_xchg(&ts->signal_pending, 1);
}

/* Wrapper for sigprocmask function
 * Emulates a sigprocmask in a safe way for the guest. Note that set and oldset
 * are host signal set, not guest ones. Returns -TARGET_ERESTARTSYS if
 * a signal was already pending and the syscall must be restarted, or
 * 0 on success.
 * If set is NULL, this is guaranteed not to fail.
 */
int do_sigprocmask(int how, const sigset_t *set, sigset_t *oldset)
{
    TaskState *ts = (TaskState *)thread_cpu->opaque;

    if (oldset) {
        *oldset = ts->signal_mask;
    }

    if (set) {
        int i;

        if (block_signals()) {
            return -TARGET_ERESTARTSYS;
        }

        switch (how) {
        case SIG_BLOCK:
            sigorset(&ts->signal_mask, &ts->signal_mask, set);
            break;
        case SIG_UNBLOCK:
            for (i = 1; i <= NSIG; ++i) {
                if (sigismember(set, i)) {
                    sigdelset(&ts->signal_mask, i);
                }
            }
            break;
        case SIG_SETMASK:
            ts->signal_mask = *set;
            break;
        default:
            g_assert_not_reached();
        }

        /* Silently ignore attempts to change blocking status of KILL or STOP */
        sigdelset(&ts->signal_mask, SIGKILL);
        sigdelset(&ts->signal_mask, SIGSTOP);
    }
    return 0;
}

#if !defined(TARGET_OPENRISC) && !defined(TARGET_NIOS2)
/* Just set the guest's signal mask to the specified value; the
 * caller is assumed to have called block_signals() already.
 */
void set_sigmask(const sigset_t *set)
{
    TaskState *ts = (TaskState *)thread_cpu->opaque;

    ts->signal_mask = *set;
}
#endif

/* siginfo conversion */

static inline void host_to_target_siginfo_noswap(target_siginfo_t *tinfo,
                                                 const siginfo_t *info)
{
    int sig = host_to_target_signal(info->si_signo);
    int si_code = info->si_code;
    int si_type;
    tinfo->si_signo = sig;
    tinfo->si_errno = 0;
    tinfo->si_code = info->si_code;

    /* This memset serves two purposes:
     * (1) ensure we don't leak random junk to the guest later
     * (2) placate false positives from gcc about fields
     *     being used uninitialized if it chooses to inline both this
     *     function and tswap_siginfo() into host_to_target_siginfo().
     */
    memset(tinfo->_sifields._pad, 0, sizeof(tinfo->_sifields._pad));

    /* This is awkward, because we have to use a combination of
     * the si_code and si_signo to figure out which of the union's
     * members are valid. (Within the host kernel it is always possible
     * to tell, but the kernel carefully avoids giving userspace the
     * high 16 bits of si_code, so we don't have the information to
     * do this the easy way...) We therefore make our best guess,
     * bearing in mind that a guest can spoof most of the si_codes
     * via rt_sigqueueinfo() if it likes.
     *
     * Once we have made our guess, we record it in the top 16 bits of
     * the si_code, so that tswap_siginfo() later can use it.
     * tswap_siginfo() will strip these top bits out before writing
     * si_code to the guest (sign-extending the lower bits).
     */

    switch (si_code) {
    case SI_USER:
    case SI_TKILL:
    case SI_KERNEL:
        /* Sent via kill(), tkill() or tgkill(), or direct from the kernel.
         * These are the only unspoofable si_code values.
         */
        tinfo->_sifields._kill._pid = info->si_pid;
        tinfo->_sifields._kill._uid = info->si_uid;
        si_type = QEMU_SI_KILL;
        break;
    default:
        /* Everything else is spoofable. Make best guess based on signal */
        switch (sig) {
        case TARGET_SIGCHLD:
            tinfo->_sifields._sigchld._pid = info->si_pid;
            tinfo->_sifields._sigchld._uid = info->si_uid;
            tinfo->_sifields._sigchld._status
                = host_to_target_waitstatus(info->si_status);
            tinfo->_sifields._sigchld._utime = info->si_utime;
            tinfo->_sifields._sigchld._stime = info->si_stime;
            si_type = QEMU_SI_CHLD;
            break;
        case TARGET_SIGIO:
            tinfo->_sifields._sigpoll._band = info->si_band;
            tinfo->_sifields._sigpoll._fd = info->si_fd;
            si_type = QEMU_SI_POLL;
            break;
        default:
            /* Assume a sigqueue()/mq_notify()/rt_sigqueueinfo() source. */
            tinfo->_sifields._rt._pid = info->si_pid;
            tinfo->_sifields._rt._uid = info->si_uid;
            /* XXX: potential problem if 64 bit */
            tinfo->_sifields._rt._sigval.sival_ptr
                = (abi_ulong)(unsigned long)info->si_value.sival_ptr;
            si_type = QEMU_SI_RT;
            break;
        }
        break;
    }

    tinfo->si_code = deposit32(si_code, 16, 16, si_type);
}

void tswap_siginfo(target_siginfo_t *tinfo,
                   const target_siginfo_t *info)
{
    int si_type = extract32(info->si_code, 16, 16);
    int si_code = sextract32(info->si_code, 0, 16);

    __put_user(info->si_signo, &tinfo->si_signo);
    __put_user(info->si_errno, &tinfo->si_errno);
    __put_user(si_code, &tinfo->si_code);

    /* We can use our internal marker of which fields in the structure
     * are valid, rather than duplicating the guesswork of
     * host_to_target_siginfo_noswap() here.
     */
    switch (si_type) {
    case QEMU_SI_KILL:
        __put_user(info->_sifields._kill._pid, &tinfo->_sifields._kill._pid);
        __put_user(info->_sifields._kill._uid, &tinfo->_sifields._kill._uid);
        break;
    case QEMU_SI_TIMER:
        __put_user(info->_sifields._timer._timer1,
                   &tinfo->_sifields._timer._timer1);
        __put_user(info->_sifields._timer._timer2,
                   &tinfo->_sifields._timer._timer2);
        break;
    case QEMU_SI_POLL:
        __put_user(info->_sifields._sigpoll._band,
                   &tinfo->_sifields._sigpoll._band);
        __put_user(info->_sifields._sigpoll._fd,
                   &tinfo->_sifields._sigpoll._fd);
        break;
    case QEMU_SI_FAULT:
        __put_user(info->_sifields._sigfault._addr,
                   &tinfo->_sifields._sigfault._addr);
        break;
    case QEMU_SI_CHLD:
        __put_user(info->_sifields._sigchld._pid,
                   &tinfo->_sifields._sigchld._pid);
        __put_user(info->_sifields._sigchld._uid,
                   &tinfo->_sifields._sigchld._uid);
        __put_user(info->_sifields._sigchld._status,
                   &tinfo->_sifields._sigchld._status);
        __put_user(info->_sifields._sigchld._utime,
                   &tinfo->_sifields._sigchld._utime);
        __put_user(info->_sifields._sigchld._stime,
                   &tinfo->_sifields._sigchld._stime);
        break;
    case QEMU_SI_RT:
        __put_user(info->_sifields._rt._pid, &tinfo->_sifields._rt._pid);
        __put_user(info->_sifields._rt._uid, &tinfo->_sifields._rt._uid);
        __put_user(info->_sifields._rt._sigval.sival_ptr,
                   &tinfo->_sifields._rt._sigval.sival_ptr);
        break;
    default:
        g_assert_not_reached();
    }
}

void host_to_target_siginfo(target_siginfo_t *tinfo, const siginfo_t *info)
{
    target_siginfo_t tgt_tmp;
    host_to_target_siginfo_noswap(&tgt_tmp, info);
    tswap_siginfo(tinfo, &tgt_tmp);
}

/* XXX: we support only POSIX RT signals are used. */
/* XXX: find a solution for 64 bit (additional malloced data is needed) */
void target_to_host_siginfo(siginfo_t *info, const target_siginfo_t *tinfo)
{
    /* This conversion is used only for the rt_sigqueueinfo syscall,
     * and so we know that the _rt fields are the valid ones.
     */
    abi_ulong sival_ptr;

    __get_user(info->si_signo, &tinfo->si_signo);
    __get_user(info->si_errno, &tinfo->si_errno);
    __get_user(info->si_code, &tinfo->si_code);
    __get_user(info->si_pid, &tinfo->_sifields._rt._pid);
    __get_user(info->si_uid, &tinfo->_sifields._rt._uid);
    __get_user(sival_ptr, &tinfo->_sifields._rt._sigval.sival_ptr);
    info->si_value.sival_ptr = (void *)(long)sival_ptr;
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
    TaskState *ts = (TaskState *)thread_cpu->opaque;
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

    /* Set the signal mask from the host mask. */
    sigprocmask(0, 0, &ts->signal_mask);

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

/* Force a synchronously taken signal. The kernel force_sig() function
 * also forces the signal to "not blocked, not ignored", but for QEMU
 * that work is done in process_pending_signals().
 */
void force_sig(int sig)
{
    CPUState *cpu = thread_cpu;
    CPUArchState *env = cpu->env_ptr;
    target_siginfo_t info;

    info.si_signo = sig;
    info.si_errno = 0;
    info.si_code = TARGET_SI_KERNEL;
    info._sifields._kill._pid = 0;
    info._sifields._kill._uid = 0;
    queue_signal(env, info.si_signo, QEMU_SI_KILL, &info);
}

/* Force a SIGSEGV if we couldn't write to memory trying to set
 * up the signal frame. oldsig is the signal we were trying to handle
 * at the point of failure.
 */
#if !defined(TARGET_RISCV)
void force_sigsegv(int oldsig)
{
    if (oldsig == SIGSEGV) {
        /* Make sure we don't try to deliver the signal again; this will
         * end up with handle_pending_signal() calling dump_core_and_abort().
         */
        sigact_table[oldsig - 1]._sa_handler = TARGET_SIG_DFL;
    }
    force_sig(TARGET_SIGSEGV);
}

#endif

/* abort execution with signal */
static void QEMU_NORETURN dump_core_and_abort(int target_sig)
{
    CPUState *cpu = thread_cpu;
    CPUArchState *env = cpu->env_ptr;
    TaskState *ts = (TaskState *)cpu->opaque;
    int host_sig, core_dumped = 0;
    struct sigaction act;

    host_sig = target_to_host_signal(target_sig);
    trace_user_force_sig(env, target_sig, host_sig);
    gdb_signalled(env, target_sig);

    /* dump core if supported by target binary format */
    if (core_dump_signal(target_sig) && (ts->bprm->core_dump != NULL)) {
        stop_all_tasks();
        core_dumped =
            ((*ts->bprm->core_dump)(target_sig, env) == 0);
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
    act.sa_flags = 0;
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
int queue_signal(CPUArchState *env, int sig, int si_type,
                 target_siginfo_t *info)
{
    CPUState *cpu = ENV_GET_CPU(env);
    TaskState *ts = cpu->opaque;

    trace_user_queue_signal(env, sig);

    info->si_code = deposit32(info->si_code, 16, 16, si_type);

    ts->sync_signal.info = *info;
    ts->sync_signal.pending = sig;
    /* signal that a new signal is pending */
    atomic_set(&ts->signal_pending, 1);
    return 1; /* indicates that the signal was queued */
}

#ifndef HAVE_SAFE_SYSCALL
static inline void rewind_if_in_safe_syscall(void *puc)
{
    /* Default version: never rewind */
}
#endif

static void host_signal_handler(int host_signum, siginfo_t *info,
                                void *puc)
{
    CPUArchState *env = thread_cpu->env_ptr;
    CPUState *cpu = ENV_GET_CPU(env);
    TaskState *ts = cpu->opaque;

    int sig;
    target_siginfo_t tinfo;
    ucontext_t *uc = puc;
    struct emulated_sigtable *k;

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
    trace_user_host_signal(env, host_signum, sig);

    rewind_if_in_safe_syscall(puc);

    host_to_target_siginfo_noswap(&tinfo, info);
    k = &ts->sigtab[sig - 1];
    k->info = tinfo;
    k->pending = sig;
    ts->signal_pending = 1;

    /* Block host signals until target signal handler entered. We
     * can't block SIGSEGV or SIGBUS while we're executing guest
     * code in case the guest code provokes one in the window between
     * now and it getting out to the main loop. Signals will be
     * unblocked again in process_pending_signals().
     *
     * WARNING: we cannot use sigfillset() here because the uc_sigmask
     * field is a kernel sigset_t, which is much smaller than the
     * libc sigset_t which sigfillset() operates on. Using sigfillset()
     * would write 0xff bytes off the end of the structure and trash
     * data on the struct.
     * We can't use sizeof(uc->uc_sigmask) either, because the libc
     * headers define the struct field with the wrong (too large) type.
     */
    memset(&uc->uc_sigmask, 0xff, SIGSET_T_SIZE);
    sigdelset(&uc->uc_sigmask, SIGSEGV);
    sigdelset(&uc->uc_sigmask, SIGBUS);

    /* interrupt the virtual CPU as soon as possible */
    cpu_exit(thread_cpu);
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
        size_t minstacksize = TARGET_MINSIGSTKSZ;

#if defined(TARGET_PPC64)
        /* ELF V2 for PPC64 has a 4K minimum stack size for signal handlers */
        struct image_info *image = ((TaskState *)thread_cpu->opaque)->info;
        if (get_ppc64_abi(image) > 1) {
            minstacksize = 4096;
        }
#endif

	ret = -TARGET_EFAULT;
        if (!lock_user_struct(VERIFY_READ, uss, uss_addr, 1)) {
            goto out;
        }
        __get_user(ss.ss_sp, &uss->ss_sp);
        __get_user(ss.ss_size, &uss->ss_size);
        __get_user(ss.ss_flags, &uss->ss_flags);
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
            if (ss.ss_size < minstacksize) {
                goto out;
            }
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

/* do_sigaction() return target values and host errnos */
int do_sigaction(int sig, const struct target_sigaction *act,
                 struct target_sigaction *oact)
{
    struct target_sigaction *k;
    struct sigaction act1;
    int host_sig;
    int ret = 0;

    if (sig < 1 || sig > TARGET_NSIG || sig == TARGET_SIGKILL || sig == TARGET_SIGSTOP) {
        return -TARGET_EINVAL;
    }

    if (block_signals()) {
        return -TARGET_ERESTARTSYS;
    }

    k = &sigact_table[sig - 1];
    if (oact) {
        __put_user(k->_sa_handler, &oact->_sa_handler);
        __put_user(k->sa_flags, &oact->sa_flags);
#ifdef TARGET_ARCH_HAS_SA_RESTORER
        __put_user(k->sa_restorer, &oact->sa_restorer);
#endif
        /* Not swapped.  */
        oact->sa_mask = k->sa_mask;
    }
    if (act) {
        /* FIXME: This is not threadsafe.  */
        __get_user(k->_sa_handler, &act->_sa_handler);
        __get_user(k->sa_flags, &act->sa_flags);
#ifdef TARGET_ARCH_HAS_SA_RESTORER
        __get_user(k->sa_restorer, &act->sa_restorer);
#endif
        /* To be swapped in target_to_host_sigset.  */
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

#if defined(TARGET_PPC)

/* Size of dummy stack frame allocated when calling signal handler.
   See arch/powerpc/include/asm/ptrace.h.  */
#if defined(TARGET_PPC64)
#define SIGNAL_FRAMESIZE 128
#else
#define SIGNAL_FRAMESIZE 64
#endif

/* See arch/powerpc/include/asm/ucontext.h.  Only used for 32-bit PPC;
   on 64-bit PPC, sigcontext and mcontext are one and the same.  */
struct target_mcontext {
    target_ulong mc_gregs[48];
    /* Includes fpscr.  */
    uint64_t mc_fregs[33];
#if defined(TARGET_PPC64)
    /* Pointer to the vector regs */
    target_ulong v_regs;
#else
    target_ulong mc_pad[2];
#endif
    /* We need to handle Altivec and SPE at the same time, which no
       kernel needs to do.  Fortunately, the kernel defines this bit to
       be Altivec-register-large all the time, rather than trying to
       twiddle it based on the specific platform.  */
    union {
        /* SPE vector registers.  One extra for SPEFSCR.  */
        uint32_t spe[33];
        /* Altivec vector registers.  The packing of VSCR and VRSAVE
           varies depending on whether we're PPC64 or not: PPC64 splits
           them apart; PPC32 stuffs them together.
           We also need to account for the VSX registers on PPC64
        */
#if defined(TARGET_PPC64)
#define QEMU_NVRREG (34 + 16)
        /* On ppc64, this mcontext structure is naturally *unaligned*,
         * or rather it is aligned on a 8 bytes boundary but not on
         * a 16 bytes one. This pad fixes it up. This is also why the
         * vector regs are referenced by the v_regs pointer above so
         * any amount of padding can be added here
         */
        target_ulong pad;
#else
        /* On ppc32, we are already aligned to 16 bytes */
#define QEMU_NVRREG 33
#endif
        /* We cannot use ppc_avr_t here as we do *not* want the implied
         * 16-bytes alignment that would result from it. This would have
         * the effect of making the whole struct target_mcontext aligned
         * which breaks the layout of struct target_ucontext on ppc64.
         */
        uint64_t altivec[QEMU_NVRREG][2];
#undef QEMU_NVRREG
    } mc_vregs;
};

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
#if defined(TARGET_PPC64)
    struct target_mcontext mcontext;
#endif
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


struct target_ucontext {
    target_ulong tuc_flags;
    target_ulong tuc_link;    /* ucontext_t __user * */
    struct target_sigaltstack tuc_stack;
#if !defined(TARGET_PPC64)
    int32_t tuc_pad[7];
    target_ulong tuc_regs;    /* struct mcontext __user *
                                points to uc_mcontext field */
#endif
    target_sigset_t tuc_sigmask;
#if defined(TARGET_PPC64)
    target_sigset_t unused[15]; /* Allow for uc_sigmask growth */
    struct target_sigcontext tuc_sigcontext;
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

#if defined(TARGET_PPC64)

#define TARGET_TRAMP_SIZE 6

struct target_rt_sigframe {
    /* sys_rt_sigreturn requires the ucontext be the first field */
    struct target_ucontext uc;
    target_ulong  _unused[2];
    uint32_t trampoline[TARGET_TRAMP_SIZE];
    target_ulong pinfo; /* struct siginfo __user * */
    target_ulong puc; /* void __user * */
    struct target_siginfo info;
    /* 64 bit ABI allows for 288 bytes below sp before decrementing it. */
    char abigap[288];
} __attribute__((aligned(16)));

#else

struct target_rt_sigframe {
    struct target_siginfo info;
    struct target_ucontext uc;
    int32_t abigap[56];
};

#endif

#if defined(TARGET_PPC64)

struct target_func_ptr {
    target_ulong entry;
    target_ulong toc;
};

#endif

/* We use the mc_pad field for the signal return trampoline.  */
#define tramp mc_pad

/* See arch/powerpc/kernel/signal.c.  */
static target_ulong get_sigframe(struct target_sigaction *ka,
                                 CPUPPCState *env,
                                 int frame_size)
{
    target_ulong oldsp;

    oldsp = env->gpr[1];

    if ((ka->sa_flags & TARGET_SA_ONSTACK) &&
            (sas_ss_flags(oldsp) == 0)) {
        oldsp = (target_sigaltstack_used.ss_sp
                 + target_sigaltstack_used.ss_size);
    }

    return (oldsp - frame_size) & ~0xFUL;
}

#if ((defined(TARGET_WORDS_BIGENDIAN) && defined(HOST_WORDS_BIGENDIAN)) || \
     (!defined(HOST_WORDS_BIGENDIAN) && !defined(TARGET_WORDS_BIGENDIAN)))
#define PPC_VEC_HI      0
#define PPC_VEC_LO      1
#else
#define PPC_VEC_HI      1
#define PPC_VEC_LO      0
#endif


static void save_user_regs(CPUPPCState *env, struct target_mcontext *frame)
{
    target_ulong msr = env->msr;
    int i;
    target_ulong ccr = 0;

    /* In general, the kernel attempts to be intelligent about what it
       needs to save for Altivec/FP/SPE registers.  We don't care that
       much, so we just go ahead and save everything.  */

    /* Save general registers.  */
    for (i = 0; i < ARRAY_SIZE(env->gpr); i++) {
        __put_user(env->gpr[i], &frame->mc_gregs[i]);
    }
    __put_user(env->nip, &frame->mc_gregs[TARGET_PT_NIP]);
    __put_user(env->ctr, &frame->mc_gregs[TARGET_PT_CTR]);
    __put_user(env->lr, &frame->mc_gregs[TARGET_PT_LNK]);
    __put_user(env->xer, &frame->mc_gregs[TARGET_PT_XER]);

    for (i = 0; i < ARRAY_SIZE(env->crf); i++) {
        ccr |= env->crf[i] << (32 - ((i + 1) * 4));
    }
    __put_user(ccr, &frame->mc_gregs[TARGET_PT_CCR]);

    /* Save Altivec registers if necessary.  */
    if (env->insns_flags & PPC_ALTIVEC) {
        uint32_t *vrsave;
        for (i = 0; i < ARRAY_SIZE(env->avr); i++) {
            ppc_avr_t *avr = &env->avr[i];
            ppc_avr_t *vreg = (ppc_avr_t *)&frame->mc_vregs.altivec[i];

            __put_user(avr->u64[PPC_VEC_HI], &vreg->u64[0]);
            __put_user(avr->u64[PPC_VEC_LO], &vreg->u64[1]);
        }
        /* Set MSR_VR in the saved MSR value to indicate that
           frame->mc_vregs contains valid data.  */
        msr |= MSR_VR;
#if defined(TARGET_PPC64)
        vrsave = (uint32_t *)&frame->mc_vregs.altivec[33];
        /* 64-bit needs to put a pointer to the vectors in the frame */
        __put_user(h2g(frame->mc_vregs.altivec), &frame->v_regs);
#else
        vrsave = (uint32_t *)&frame->mc_vregs.altivec[32];
#endif
        __put_user((uint32_t)env->spr[SPR_VRSAVE], vrsave);
    }

    /* Save VSX second halves */
    if (env->insns_flags2 & PPC2_VSX) {
        uint64_t *vsregs = (uint64_t *)&frame->mc_vregs.altivec[34];
        for (i = 0; i < ARRAY_SIZE(env->vsr); i++) {
            __put_user(env->vsr[i], &vsregs[i]);
        }
    }

    /* Save floating point registers.  */
    if (env->insns_flags & PPC_FLOAT) {
        for (i = 0; i < ARRAY_SIZE(env->fpr); i++) {
            __put_user(env->fpr[i], &frame->mc_fregs[i]);
        }
        __put_user((uint64_t) env->fpscr, &frame->mc_fregs[32]);
    }

    /* Save SPE registers.  The kernel only saves the high half.  */
    if (env->insns_flags & PPC_SPE) {
#if defined(TARGET_PPC64)
        for (i = 0; i < ARRAY_SIZE(env->gpr); i++) {
            __put_user(env->gpr[i] >> 32, &frame->mc_vregs.spe[i]);
        }
#else
        for (i = 0; i < ARRAY_SIZE(env->gprh); i++) {
            __put_user(env->gprh[i], &frame->mc_vregs.spe[i]);
        }
#endif
        /* Set MSR_SPE in the saved MSR value to indicate that
           frame->mc_vregs contains valid data.  */
        msr |= MSR_SPE;
        __put_user(env->spe_fscr, &frame->mc_vregs.spe[32]);
    }

    /* Store MSR.  */
    __put_user(msr, &frame->mc_gregs[TARGET_PT_MSR]);
}

static void encode_trampoline(int sigret, uint32_t *tramp)
{
    /* Set up the sigreturn trampoline: li r0,sigret; sc.  */
    if (sigret) {
        __put_user(0x38000000 | sigret, &tramp[0]);
        __put_user(0x44000002, &tramp[1]);
    }
}

static void restore_user_regs(CPUPPCState *env,
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
        __get_user(env->gpr[i], &frame->mc_gregs[i]);
    }
    __get_user(env->nip, &frame->mc_gregs[TARGET_PT_NIP]);
    __get_user(env->ctr, &frame->mc_gregs[TARGET_PT_CTR]);
    __get_user(env->lr, &frame->mc_gregs[TARGET_PT_LNK]);
    __get_user(env->xer, &frame->mc_gregs[TARGET_PT_XER]);
    __get_user(ccr, &frame->mc_gregs[TARGET_PT_CCR]);

    for (i = 0; i < ARRAY_SIZE(env->crf); i++) {
        env->crf[i] = (ccr >> (32 - ((i + 1) * 4))) & 0xf;
    }

    if (!sig) {
        env->gpr[2] = save_r2;
    }
    /* Restore MSR.  */
    __get_user(msr, &frame->mc_gregs[TARGET_PT_MSR]);

    /* If doing signal return, restore the previous little-endian mode.  */
    if (sig)
        env->msr = (env->msr & ~(1ull << MSR_LE)) | (msr & (1ull << MSR_LE));

    /* Restore Altivec registers if necessary.  */
    if (env->insns_flags & PPC_ALTIVEC) {
        ppc_avr_t *v_regs;
        uint32_t *vrsave;
#if defined(TARGET_PPC64)
        uint64_t v_addr;
        /* 64-bit needs to recover the pointer to the vectors from the frame */
        __get_user(v_addr, &frame->v_regs);
        v_regs = g2h(v_addr);
#else
        v_regs = (ppc_avr_t *)frame->mc_vregs.altivec;
#endif
        for (i = 0; i < ARRAY_SIZE(env->avr); i++) {
            ppc_avr_t *avr = &env->avr[i];
            ppc_avr_t *vreg = &v_regs[i];

            __get_user(avr->u64[PPC_VEC_HI], &vreg->u64[0]);
            __get_user(avr->u64[PPC_VEC_LO], &vreg->u64[1]);
        }
        /* Set MSR_VEC in the saved MSR value to indicate that
           frame->mc_vregs contains valid data.  */
#if defined(TARGET_PPC64)
        vrsave = (uint32_t *)&v_regs[33];
#else
        vrsave = (uint32_t *)&v_regs[32];
#endif
        __get_user(env->spr[SPR_VRSAVE], vrsave);
    }

    /* Restore VSX second halves */
    if (env->insns_flags2 & PPC2_VSX) {
        uint64_t *vsregs = (uint64_t *)&frame->mc_vregs.altivec[34];
        for (i = 0; i < ARRAY_SIZE(env->vsr); i++) {
            __get_user(env->vsr[i], &vsregs[i]);
        }
    }

    /* Restore floating point registers.  */
    if (env->insns_flags & PPC_FLOAT) {
        uint64_t fpscr;
        for (i = 0; i < ARRAY_SIZE(env->fpr); i++) {
            __get_user(env->fpr[i], &frame->mc_fregs[i]);
        }
        __get_user(fpscr, &frame->mc_fregs[32]);
        env->fpscr = (uint32_t) fpscr;
    }

    /* Save SPE registers.  The kernel only saves the high half.  */
    if (env->insns_flags & PPC_SPE) {
#if defined(TARGET_PPC64)
        for (i = 0; i < ARRAY_SIZE(env->gpr); i++) {
            uint32_t hi;

            __get_user(hi, &frame->mc_vregs.spe[i]);
            env->gpr[i] = ((uint64_t)hi << 32) | ((uint32_t) env->gpr[i]);
        }
#else
        for (i = 0; i < ARRAY_SIZE(env->gprh); i++) {
            __get_user(env->gprh[i], &frame->mc_vregs.spe[i]);
        }
#endif
        __get_user(env->spe_fscr, &frame->mc_vregs.spe[32]);
    }
}

#if !defined(TARGET_PPC64)
static void setup_frame(int sig, struct target_sigaction *ka,
                        target_sigset_t *set, CPUPPCState *env)
{
    struct target_sigframe *frame;
    struct target_sigcontext *sc;
    target_ulong frame_addr, newsp;
    int err = 0;

    frame_addr = get_sigframe(ka, env, sizeof(*frame));
    trace_user_setup_frame(env, frame_addr);
    if (!lock_user_struct(VERIFY_WRITE, frame, frame_addr, 1))
        goto sigsegv;
    sc = &frame->sctx;

    __put_user(ka->_sa_handler, &sc->handler);
    __put_user(set->sig[0], &sc->oldmask);
    __put_user(set->sig[1], &sc->_unused[3]);
    __put_user(h2g(&frame->mctx), &sc->regs);
    __put_user(sig, &sc->signal);

    /* Save user regs.  */
    save_user_regs(env, &frame->mctx);

    /* Construct the trampoline code on the stack. */
    encode_trampoline(TARGET_NR_sigreturn, (uint32_t *)&frame->mctx.tramp);

    /* The kernel checks for the presence of a VDSO here.  We don't
       emulate a vdso, so use a sigreturn system call.  */
    env->lr = (target_ulong) h2g(frame->mctx.tramp);

    /* Turn off all fp exceptions.  */
    env->fpscr = 0;

    /* Create a stack frame for the caller of the handler.  */
    newsp = frame_addr - SIGNAL_FRAMESIZE;
    err |= put_user(env->gpr[1], newsp, target_ulong);

    if (err)
        goto sigsegv;

    /* Set up registers for signal handler.  */
    env->gpr[1] = newsp;
    env->gpr[3] = sig;
    env->gpr[4] = frame_addr + offsetof(struct target_sigframe, sctx);

    env->nip = (target_ulong) ka->_sa_handler;

    /* Signal handlers are entered in big-endian mode.  */
    env->msr &= ~(1ull << MSR_LE);

    unlock_user_struct(frame, frame_addr, 1);
    return;

sigsegv:
    unlock_user_struct(frame, frame_addr, 1);
    force_sigsegv(sig);
}
#endif /* !defined(TARGET_PPC64) */

static void setup_rt_frame(int sig, struct target_sigaction *ka,
                           target_siginfo_t *info,
                           target_sigset_t *set, CPUPPCState *env)
{
    struct target_rt_sigframe *rt_sf;
    uint32_t *trampptr = 0;
    struct target_mcontext *mctx = 0;
    target_ulong rt_sf_addr, newsp = 0;
    int i, err = 0;
#if defined(TARGET_PPC64)
    struct target_sigcontext *sc = 0;
    struct image_info *image = ((TaskState *)thread_cpu->opaque)->info;
#endif

    rt_sf_addr = get_sigframe(ka, env, sizeof(*rt_sf));
    if (!lock_user_struct(VERIFY_WRITE, rt_sf, rt_sf_addr, 1))
        goto sigsegv;

    tswap_siginfo(&rt_sf->info, info);

    __put_user(0, &rt_sf->uc.tuc_flags);
    __put_user(0, &rt_sf->uc.tuc_link);
    __put_user((target_ulong)target_sigaltstack_used.ss_sp,
               &rt_sf->uc.tuc_stack.ss_sp);
    __put_user(sas_ss_flags(env->gpr[1]),
               &rt_sf->uc.tuc_stack.ss_flags);
    __put_user(target_sigaltstack_used.ss_size,
               &rt_sf->uc.tuc_stack.ss_size);
#if !defined(TARGET_PPC64)
    __put_user(h2g (&rt_sf->uc.tuc_mcontext),
               &rt_sf->uc.tuc_regs);
#endif
    for(i = 0; i < TARGET_NSIG_WORDS; i++) {
        __put_user(set->sig[i], &rt_sf->uc.tuc_sigmask.sig[i]);
    }

#if defined(TARGET_PPC64)
    mctx = &rt_sf->uc.tuc_sigcontext.mcontext;
    trampptr = &rt_sf->trampoline[0];

    sc = &rt_sf->uc.tuc_sigcontext;
    __put_user(h2g(mctx), &sc->regs);
    __put_user(sig, &sc->signal);
#else
    mctx = &rt_sf->uc.tuc_mcontext;
    trampptr = (uint32_t *)&rt_sf->uc.tuc_mcontext.tramp;
#endif

    save_user_regs(env, mctx);
    encode_trampoline(TARGET_NR_rt_sigreturn, trampptr);

    /* The kernel checks for the presence of a VDSO here.  We don't
       emulate a vdso, so use a sigreturn system call.  */
    env->lr = (target_ulong) h2g(trampptr);

    /* Turn off all fp exceptions.  */
    env->fpscr = 0;

    /* Create a stack frame for the caller of the handler.  */
    newsp = rt_sf_addr - (SIGNAL_FRAMESIZE + 16);
    err |= put_user(env->gpr[1], newsp, target_ulong);

    if (err)
        goto sigsegv;

    /* Set up registers for signal handler.  */
    env->gpr[1] = newsp;
    env->gpr[3] = (target_ulong) sig;
    env->gpr[4] = (target_ulong) h2g(&rt_sf->info);
    env->gpr[5] = (target_ulong) h2g(&rt_sf->uc);
    env->gpr[6] = (target_ulong) h2g(rt_sf);

#if defined(TARGET_PPC64)
    if (get_ppc64_abi(image) < 2) {
        /* ELFv1 PPC64 function pointers are pointers to OPD entries. */
        struct target_func_ptr *handler =
            (struct target_func_ptr *)g2h(ka->_sa_handler);
        env->nip = tswapl(handler->entry);
        env->gpr[2] = tswapl(handler->toc);
    } else {
        /* ELFv2 PPC64 function pointers are entry points, but R12
         * must also be set */
        env->nip = tswapl((target_ulong) ka->_sa_handler);
        env->gpr[12] = env->nip;
    }
#else
    env->nip = (target_ulong) ka->_sa_handler;
#endif

    /* Signal handlers are entered in big-endian mode.  */
    env->msr &= ~(1ull << MSR_LE);

    unlock_user_struct(rt_sf, rt_sf_addr, 1);
    return;

sigsegv:
    unlock_user_struct(rt_sf, rt_sf_addr, 1);
    force_sigsegv(sig);

}

#if !defined(TARGET_PPC64)
long do_sigreturn(CPUPPCState *env)
{
    struct target_sigcontext *sc = NULL;
    struct target_mcontext *sr = NULL;
    target_ulong sr_addr = 0, sc_addr;
    sigset_t blocked;
    target_sigset_t set;

    sc_addr = env->gpr[1] + SIGNAL_FRAMESIZE;
    if (!lock_user_struct(VERIFY_READ, sc, sc_addr, 1))
        goto sigsegv;

#if defined(TARGET_PPC64)
    set.sig[0] = sc->oldmask + ((uint64_t)(sc->_unused[3]) << 32);
#else
    __get_user(set.sig[0], &sc->oldmask);
    __get_user(set.sig[1], &sc->_unused[3]);
#endif
    target_to_host_sigset_internal(&blocked, &set);
    set_sigmask(&blocked);

    __get_user(sr_addr, &sc->regs);
    if (!lock_user_struct(VERIFY_READ, sr, sr_addr, 1))
        goto sigsegv;
    restore_user_regs(env, sr, 1);

    unlock_user_struct(sr, sr_addr, 1);
    unlock_user_struct(sc, sc_addr, 1);
    return -TARGET_QEMU_ESIGRETURN;

sigsegv:
    unlock_user_struct(sr, sr_addr, 1);
    unlock_user_struct(sc, sc_addr, 1);
    force_sig(TARGET_SIGSEGV);
    return -TARGET_QEMU_ESIGRETURN;
}
#endif /* !defined(TARGET_PPC64) */

/* See arch/powerpc/kernel/signal_32.c.  */
static int do_setcontext(struct target_ucontext *ucp, CPUPPCState *env, int sig)
{
    struct target_mcontext *mcp;
    target_ulong mcp_addr;
    sigset_t blocked;
    target_sigset_t set;

    if (copy_from_user(&set, h2g(ucp) + offsetof(struct target_ucontext, tuc_sigmask),
                       sizeof (set)))
        return 1;

#if defined(TARGET_PPC64)
    mcp_addr = h2g(ucp) +
        offsetof(struct target_ucontext, tuc_sigcontext.mcontext);
#else
    __get_user(mcp_addr, &ucp->tuc_regs);
#endif

    if (!lock_user_struct(VERIFY_READ, mcp, mcp_addr, 1))
        return 1;

    target_to_host_sigset_internal(&blocked, &set);
    set_sigmask(&blocked);
    restore_user_regs(env, mcp, sig);

    unlock_user_struct(mcp, mcp_addr, 1);
    return 0;
}

long do_rt_sigreturn(CPUPPCState *env)
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
    force_sig(TARGET_SIGSEGV);
    return -TARGET_QEMU_ESIGRETURN;
}
#endif

static void handle_pending_signal(CPUArchState *cpu_env, int sig,
                                  struct emulated_sigtable *k)
{
    CPUState *cpu = ENV_GET_CPU(cpu_env);
    abi_ulong handler;
    sigset_t set;
    target_sigset_t target_old_set;
    struct target_sigaction *sa;
    TaskState *ts = cpu->opaque;

    trace_user_handle_signal(cpu_env, sig);
    /* dequeue signal */
    k->pending = 0;

    sig = gdb_handlesig(cpu, sig);
    if (!sig) {
        sa = NULL;
        handler = TARGET_SIG_IGN;
    } else {
        sa = &sigact_table[sig - 1];
        handler = sa->_sa_handler;
    }

    if (do_strace) {
        print_taken_signal(sig, &k->info);
    }

    if (handler == TARGET_SIG_DFL) {
        /* default handler : ignore some signal. The other are job control or fatal */
        if (sig == TARGET_SIGTSTP || sig == TARGET_SIGTTIN || sig == TARGET_SIGTTOU) {
            kill(getpid(),SIGSTOP);
        } else if (sig != TARGET_SIGCHLD &&
                   sig != TARGET_SIGURG &&
                   sig != TARGET_SIGWINCH &&
                   sig != TARGET_SIGCONT) {
            dump_core_and_abort(sig);
        }
    } else if (handler == TARGET_SIG_IGN) {
        /* ignore sig */
    } else if (handler == TARGET_SIG_ERR) {
        dump_core_and_abort(sig);
    } else {
        /* compute the blocked signals during the handler execution */
        sigset_t *blocked_set;

        target_to_host_sigset(&set, &sa->sa_mask);
        /* SA_NODEFER indicates that the current signal should not be
           blocked during the handler */
        if (!(sa->sa_flags & TARGET_SA_NODEFER))
            sigaddset(&set, target_to_host_signal(sig));

        /* save the previous blocked signal state to restore it at the
           end of the signal execution (see do_sigreturn) */
        host_to_target_sigset_internal(&target_old_set, &ts->signal_mask);

        /* block signals in the handler */
        blocked_set = ts->in_sigsuspend ?
            &ts->sigsuspend_mask : &ts->signal_mask;
        sigorset(&ts->signal_mask, blocked_set, &set);
        ts->in_sigsuspend = 0;

        /* if the CPU is in VM86 mode, we restore the 32 bit values */
#if defined(TARGET_I386) && !defined(TARGET_X86_64)
        {
            CPUX86State *env = cpu_env;
            if (env->eflags & VM_MASK)
                save_v86_state(env);
        }
#endif
        /* prepare the stack frame of the virtual CPU */
#if defined(TARGET_ABI_MIPSN32) || defined(TARGET_ABI_MIPSN64) \
        || defined(TARGET_OPENRISC) || defined(TARGET_TILEGX) \
        || defined(TARGET_PPC64) || defined(TARGET_HPPA) \
        || defined(TARGET_NIOS2) || defined(TARGET_X86_64) \
        || defined(TARGET_RISCV) || defined(TARGET_XTENSA)
        /* These targets do not have traditional signals.  */
        setup_rt_frame(sig, sa, &k->info, &target_old_set, cpu_env);
#else
        if (sa->sa_flags & TARGET_SA_SIGINFO)
            setup_rt_frame(sig, sa, &k->info, &target_old_set, cpu_env);
        else
            setup_frame(sig, sa, &target_old_set, cpu_env);
#endif
        if (sa->sa_flags & TARGET_SA_RESETHAND) {
            sa->_sa_handler = TARGET_SIG_DFL;
        }
    }
}

void process_pending_signals(CPUArchState *cpu_env)
{
    CPUState *cpu = ENV_GET_CPU(cpu_env);
    int sig;
    TaskState *ts = cpu->opaque;
    sigset_t set;
    sigset_t *blocked_set;

    while (atomic_read(&ts->signal_pending)) {
        /* FIXME: This is not threadsafe.  */
        sigfillset(&set);
        sigprocmask(SIG_SETMASK, &set, 0);

    restart_scan:
        sig = ts->sync_signal.pending;
        if (sig) {
            /* Synchronous signals are forced,
             * see force_sig_info() and callers in Linux
             * Note that not all of our queue_signal() calls in QEMU correspond
             * to force_sig_info() calls in Linux (some are send_sig_info()).
             * However it seems like a kernel bug to me to allow the process
             * to block a synchronous signal since it could then just end up
             * looping round and round indefinitely.
             */
            if (sigismember(&ts->signal_mask, target_to_host_signal_table[sig])
                || sigact_table[sig - 1]._sa_handler == TARGET_SIG_IGN) {
                sigdelset(&ts->signal_mask, target_to_host_signal_table[sig]);
                sigact_table[sig - 1]._sa_handler = TARGET_SIG_DFL;
            }

            handle_pending_signal(cpu_env, sig, &ts->sync_signal);
        }

        for (sig = 1; sig <= TARGET_NSIG; sig++) {
            blocked_set = ts->in_sigsuspend ?
                &ts->sigsuspend_mask : &ts->signal_mask;

            if (ts->sigtab[sig - 1].pending &&
                (!sigismember(blocked_set,
                              target_to_host_signal_table[sig]))) {
                handle_pending_signal(cpu_env, sig, &ts->sigtab[sig - 1]);
                /* Restart scan from the beginning, as handle_pending_signal
                 * might have resulted in a new synchronous signal (eg SIGSEGV).
                 */
                goto restart_scan;
            }
        }

        /* if no signal is pending, unblock signals and recheck (the act
         * of unblocking might cause us to take another host signal which
         * will set signal_pending again).
         */
        atomic_set(&ts->signal_pending, 0);
        ts->in_sigsuspend = 0;
        set = ts->signal_mask;
        sigdelset(&set, SIGSEGV);
        sigdelset(&set, SIGBUS);
        sigprocmask(SIG_SETMASK, &set, 0);
    }
    ts->in_sigsuspend = 0;
}

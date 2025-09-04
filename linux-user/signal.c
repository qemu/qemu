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
#include "qemu/cutils.h"
#include "gdbstub/user.h"
#include "exec/page-protection.h"
#include "accel/tcg/cpu-ops.h"

#include <sys/ucontext.h>
#include <sys/resource.h>

#include "qemu.h"
#include "user-internals.h"
#include "strace.h"
#include "loader.h"
#include "trace.h"
#include "signal-common.h"
#include "host-signal.h"
#include "user/cpu_loop.h"
#include "user/page-protection.h"
#include "user/safe-syscall.h"
#include "user/signal.h"
#include "tcg/tcg.h"

/* target_siginfo_t must fit in gdbstub's siginfo save area. */
QEMU_BUILD_BUG_ON(sizeof(target_siginfo_t) > MAX_SIGINFO_LENGTH);

static struct target_sigaction sigact_table[TARGET_NSIG];

static void host_signal_handler(int host_signum, siginfo_t *info,
                                void *puc);

/* Fallback addresses into sigtramp page. */
abi_ulong default_sigreturn;
abi_ulong default_rt_sigreturn;
abi_ulong vdso_sigreturn_region_start;
abi_ulong vdso_sigreturn_region_end;

/*
 * System includes define _NSIG as SIGRTMAX + 1, but qemu (like the kernel)
 * defines TARGET_NSIG as TARGET_SIGRTMAX and the first signal is 1.
 * Signal number 0 is reserved for use as kill(pid, 0), to test whether
 * a process exists without sending it a signal.
 */
#ifdef __SIGRTMAX
QEMU_BUILD_BUG_ON(__SIGRTMAX + 1 != _NSIG);
#endif
static uint8_t host_to_target_signal_table[_NSIG] = {
#define MAKE_SIG_ENTRY(sig)     [sig] = TARGET_##sig,
        MAKE_SIGNAL_LIST
#undef MAKE_SIG_ENTRY
};

static uint8_t target_to_host_signal_table[TARGET_NSIG + 1];

/* valid sig is between 1 and _NSIG - 1 */
int host_to_target_signal(int sig)
{
    if (sig < 1) {
        return sig;
    }
    if (sig >= _NSIG) {
        return TARGET_NSIG + 1;
    }
    return host_to_target_signal_table[sig];
}

/* valid sig is between 1 and TARGET_NSIG */
int target_to_host_signal(int sig)
{
    if (sig < 1) {
        return sig;
    }
    if (sig > TARGET_NSIG) {
        return _NSIG;
    }
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
    int host_sig, target_sig;
    target_sigemptyset(d);
    for (host_sig = 1; host_sig < _NSIG; host_sig++) {
        target_sig = host_to_target_signal(host_sig);
        if (target_sig < 1 || target_sig > TARGET_NSIG) {
            continue;
        }
        if (sigismember(s, host_sig)) {
            target_sigaddset(d, target_sig);
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
    int host_sig, target_sig;
    sigemptyset(d);
    for (target_sig = 1; target_sig <= TARGET_NSIG; target_sig++) {
        host_sig = target_to_host_signal(target_sig);
        if (host_sig < 1 || host_sig >= _NSIG) {
            continue;
        }
        if (target_sigismember(s, target_sig)) {
            sigaddset(d, host_sig);
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
    TaskState *ts = get_task_state(thread_cpu);
    sigset_t set;

    /* It's OK to block everything including SIGSEGV, because we won't
     * run any further guest code before unblocking signals in
     * process_pending_signals().
     */
    sigfillset(&set);
    sigprocmask(SIG_SETMASK, &set, 0);

    return qatomic_xchg(&ts->signal_pending, 1);
}

/* Wrapper for sigprocmask function
 * Emulates a sigprocmask in a safe way for the guest. Note that set and oldset
 * are host signal set, not guest ones. Returns -QEMU_ERESTARTSYS if
 * a signal was already pending and the syscall must be restarted, or
 * 0 on success.
 * If set is NULL, this is guaranteed not to fail.
 */
int do_sigprocmask(int how, const sigset_t *set, sigset_t *oldset)
{
    TaskState *ts = get_task_state(thread_cpu);

    if (oldset) {
        *oldset = ts->signal_mask;
    }

    if (set) {
        int i;

        if (block_signals()) {
            return -QEMU_ERESTARTSYS;
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

/* Just set the guest's signal mask to the specified value; the
 * caller is assumed to have called block_signals() already.
 */
void set_sigmask(const sigset_t *set)
{
    TaskState *ts = get_task_state(thread_cpu);

    ts->signal_mask = *set;
}

/* sigaltstack management */

int on_sig_stack(unsigned long sp)
{
    TaskState *ts = get_task_state(thread_cpu);

    return (sp - ts->sigaltstack_used.ss_sp
            < ts->sigaltstack_used.ss_size);
}

int sas_ss_flags(unsigned long sp)
{
    TaskState *ts = get_task_state(thread_cpu);

    return (ts->sigaltstack_used.ss_size == 0 ? SS_DISABLE
            : on_sig_stack(sp) ? SS_ONSTACK : 0);
}

abi_ulong target_sigsp(abi_ulong sp, struct target_sigaction *ka)
{
    /*
     * This is the X/Open sanctioned signal stack switching.
     */
    TaskState *ts = get_task_state(thread_cpu);

    if ((ka->sa_flags & TARGET_SA_ONSTACK) && !sas_ss_flags(sp)) {
        return ts->sigaltstack_used.ss_sp + ts->sigaltstack_used.ss_size;
    }
    return sp;
}

void target_save_altstack(target_stack_t *uss, CPUArchState *env)
{
    TaskState *ts = get_task_state(thread_cpu);

    __put_user(ts->sigaltstack_used.ss_sp, &uss->ss_sp);
    __put_user(sas_ss_flags(get_sp_from_cpustate(env)), &uss->ss_flags);
    __put_user(ts->sigaltstack_used.ss_size, &uss->ss_size);
}

abi_long target_restore_altstack(target_stack_t *uss, CPUArchState *env)
{
    TaskState *ts = get_task_state(thread_cpu);
    size_t minstacksize = TARGET_MINSIGSTKSZ;
    target_stack_t ss;

#if defined(TARGET_PPC64)
    /* ELF V2 for PPC64 has a 4K minimum stack size for signal handlers */
    struct image_info *image = ts->info;
    if (get_ppc64_abi(image) > 1) {
        minstacksize = 4096;
    }
#endif

    __get_user(ss.ss_sp, &uss->ss_sp);
    __get_user(ss.ss_size, &uss->ss_size);
    __get_user(ss.ss_flags, &uss->ss_flags);

    if (on_sig_stack(get_sp_from_cpustate(env))) {
        return -TARGET_EPERM;
    }

    switch (ss.ss_flags) {
    default:
        return -TARGET_EINVAL;

    case TARGET_SS_DISABLE:
        ss.ss_size = 0;
        ss.ss_sp = 0;
        break;

    case TARGET_SS_ONSTACK:
    case 0:
        if (ss.ss_size < minstacksize) {
            return -TARGET_ENOMEM;
        }
        break;
    }

    ts->sigaltstack_used.ss_sp = ss.ss_sp;
    ts->sigaltstack_used.ss_size = ss.ss_size;
    return 0;
}

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
            if (si_code == CLD_EXITED)
                tinfo->_sifields._sigchld._status = info->si_status;
            else
                tinfo->_sifields._sigchld._status
                    = host_to_target_signal(info->si_status & 0x7f)
                        | (info->si_status & ~0x7f);
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

static void tswap_siginfo(target_siginfo_t *tinfo,
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

int host_interrupt_signal;

static void signal_table_init(const char *rtsig_map)
{
    int hsig, tsig, count;

    if (rtsig_map) {
        /*
         * Map host RT signals to target RT signals according to the
         * user-provided specification.
         */
        const char *s = rtsig_map;

        while (true) {
            int i;

            if (qemu_strtoi(s, &s, 10, &tsig) || *s++ != ' ') {
                fprintf(stderr, "Malformed target signal in QEMU_RTSIG_MAP\n");
                exit(EXIT_FAILURE);
            }
            if (qemu_strtoi(s, &s, 10, &hsig) || *s++ != ' ') {
                fprintf(stderr, "Malformed host signal in QEMU_RTSIG_MAP\n");
                exit(EXIT_FAILURE);
            }
            if (qemu_strtoi(s, &s, 10, &count) || (*s && *s != ',')) {
                fprintf(stderr, "Malformed signal count in QEMU_RTSIG_MAP\n");
                exit(EXIT_FAILURE);
            }

            for (i = 0; i < count; i++, tsig++, hsig++) {
                if (tsig < TARGET_SIGRTMIN || tsig > TARGET_NSIG) {
                    fprintf(stderr, "%d is not a target rt signal\n", tsig);
                    exit(EXIT_FAILURE);
                }
                if (hsig < SIGRTMIN || hsig > SIGRTMAX) {
                    fprintf(stderr, "%d is not a host rt signal\n", hsig);
                    exit(EXIT_FAILURE);
                }
                if (host_to_target_signal_table[hsig]) {
                    fprintf(stderr, "%d already maps %d\n",
                            hsig, host_to_target_signal_table[hsig]);
                    exit(EXIT_FAILURE);
                }
                host_to_target_signal_table[hsig] = tsig;
            }

            if (*s) {
                s++;
            } else {
                break;
            }
        }
    } else {
        /*
         * Default host-to-target RT signal mapping.
         *
         * Signals are supported starting from TARGET_SIGRTMIN and going up
         * until we run out of host realtime signals.  Glibc uses the lower 2
         * RT signals and (hopefully) nobody uses the upper ones.
         * This is why SIGRTMIN (34) is generally greater than __SIGRTMIN (32).
         * To fix this properly we would need to do manual signal delivery
         * multiplexed over a single host signal.
         * Attempts for configure "missing" signals via sigaction will be
         * silently ignored.
         *
         * Reserve two signals for internal usage (see below).
         */

        hsig = SIGRTMIN + 2;
        for (tsig = TARGET_SIGRTMIN;
             hsig <= SIGRTMAX && tsig <= TARGET_NSIG;
             hsig++, tsig++) {
            host_to_target_signal_table[hsig] = tsig;
        }
    }

    /*
     * Remap the target SIGABRT, so that we can distinguish host abort
     * from guest abort.  When the guest registers a signal handler or
     * calls raise(SIGABRT), the host will raise SIG_RTn.  If the guest
     * arrives at dump_core_and_abort(), we will map back to host SIGABRT
     * so that the parent (native or emulated) sees the correct signal.
     * Finally, also map host to guest SIGABRT so that the emulated
     * parent sees the correct mapping from wait status.
     */

    host_to_target_signal_table[SIGABRT] = 0;
    for (hsig = SIGRTMIN; hsig <= SIGRTMAX; hsig++) {
        if (!host_to_target_signal_table[hsig]) {
            if (host_interrupt_signal) {
                host_to_target_signal_table[hsig] = TARGET_SIGABRT;
                break;
            } else {
                host_interrupt_signal = hsig;
            }
        }
    }
    if (hsig > SIGRTMAX) {
        fprintf(stderr,
                "No rt signals left for interrupt and SIGABRT mapping\n");
        exit(EXIT_FAILURE);
    }

    /* Invert the mapping that has already been assigned. */
    for (hsig = 1; hsig < _NSIG; hsig++) {
        tsig = host_to_target_signal_table[hsig];
        if (tsig) {
            if (target_to_host_signal_table[tsig]) {
                fprintf(stderr, "%d is already mapped to %d\n",
                        tsig, target_to_host_signal_table[tsig]);
                exit(EXIT_FAILURE);
            }
            target_to_host_signal_table[tsig] = hsig;
        }
    }

    host_to_target_signal_table[SIGABRT] = TARGET_SIGABRT;

    /* Map everything else out-of-bounds. */
    for (hsig = 1; hsig < _NSIG; hsig++) {
        if (host_to_target_signal_table[hsig] == 0) {
            host_to_target_signal_table[hsig] = TARGET_NSIG + 1;
        }
    }
    for (count = 0, tsig = 1; tsig <= TARGET_NSIG; tsig++) {
        if (target_to_host_signal_table[tsig] == 0) {
            target_to_host_signal_table[tsig] = _NSIG;
            count++;
        }
    }

    trace_signal_table_init(count);
}

void signal_init(const char *rtsig_map)
{
    TaskState *ts = get_task_state(thread_cpu);
    struct sigaction act, oact;

    /* initialize signal conversion tables */
    signal_table_init(rtsig_map);

    /* Set the signal mask from the host mask. */
    sigprocmask(0, 0, &ts->signal_mask);

    sigfillset(&act.sa_mask);
    act.sa_flags = SA_SIGINFO;
    act.sa_sigaction = host_signal_handler;

    /*
     * A parent process may configure ignored signals, but all other
     * signals are default.  For any target signals that have no host
     * mapping, set to ignore.  For all core_dump_signal, install our
     * host signal handler so that we may invoke dump_core_and_abort.
     * This includes SIGSEGV and SIGBUS, which are also need our signal
     * handler for paging and exceptions.
     */
    for (int tsig = 1; tsig <= TARGET_NSIG; tsig++) {
        int hsig = target_to_host_signal(tsig);
        abi_ptr thand = TARGET_SIG_IGN;

        if (hsig >= _NSIG) {
            continue;
        }

        /* As we force remap SIGABRT, cannot probe and install in one step. */
        if (tsig == TARGET_SIGABRT) {
            sigaction(SIGABRT, NULL, &oact);
            sigaction(hsig, &act, NULL);
        } else {
            struct sigaction *iact = core_dump_signal(tsig) ? &act : NULL;
            sigaction(hsig, iact, &oact);
        }

        if (oact.sa_sigaction != (void *)SIG_IGN) {
            thand = TARGET_SIG_DFL;
        }
        sigact_table[tsig - 1]._sa_handler = thand;
    }

    sigaction(host_interrupt_signal, &act, NULL);
}

/* Force a synchronously taken signal. The kernel force_sig() function
 * also forces the signal to "not blocked, not ignored", but for QEMU
 * that work is done in process_pending_signals().
 */
void force_sig(int sig)
{
    CPUState *cpu = thread_cpu;
    target_siginfo_t info = {};

    info.si_signo = sig;
    info.si_errno = 0;
    info.si_code = TARGET_SI_KERNEL;
    info._sifields._kill._pid = 0;
    info._sifields._kill._uid = 0;
    queue_signal(cpu_env(cpu), info.si_signo, QEMU_SI_KILL, &info);
}

/*
 * Force a synchronously taken QEMU_SI_FAULT signal. For QEMU the
 * 'force' part is handled in process_pending_signals().
 */
void force_sig_fault(int sig, int code, abi_ulong addr)
{
    CPUState *cpu = thread_cpu;
    target_siginfo_t info = {};

    info.si_signo = sig;
    info.si_errno = 0;
    info.si_code = code;
    info._sifields._sigfault._addr = addr;
    queue_signal(cpu_env(cpu), sig, QEMU_SI_FAULT, &info);
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

void cpu_loop_exit_sigsegv(CPUState *cpu, vaddr addr,
                           MMUAccessType access_type, bool maperr, uintptr_t ra)
{
    const TCGCPUOps *tcg_ops = cpu->cc->tcg_ops;

    if (tcg_ops->record_sigsegv) {
        tcg_ops->record_sigsegv(cpu, addr, access_type, maperr, ra);
    }

    force_sig_fault(TARGET_SIGSEGV,
                    maperr ? TARGET_SEGV_MAPERR : TARGET_SEGV_ACCERR,
                    addr);
    cpu->exception_index = EXCP_INTERRUPT;
    cpu_loop_exit_restore(cpu, ra);
}

void cpu_loop_exit_sigbus(CPUState *cpu, vaddr addr,
                          MMUAccessType access_type, uintptr_t ra)
{
    const TCGCPUOps *tcg_ops = cpu->cc->tcg_ops;

    if (tcg_ops->record_sigbus) {
        tcg_ops->record_sigbus(cpu, addr, access_type, ra);
    }

    force_sig_fault(TARGET_SIGBUS, TARGET_BUS_ADRALN, addr);
    cpu->exception_index = EXCP_INTERRUPT;
    cpu_loop_exit_restore(cpu, ra);
}

/* abort execution with signal */
static G_NORETURN
void die_with_signal(int host_sig)
{
    struct sigaction act = {
        .sa_handler = SIG_DFL,
    };

    /*
     * The proper exit code for dying from an uncaught signal is -<signal>.
     * The kernel doesn't allow exit() or _exit() to pass a negative value.
     * To get the proper exit code we need to actually die from an uncaught
     * signal.  Here the default signal handler is installed, we send
     * the signal and we wait for it to arrive.
     */
    sigfillset(&act.sa_mask);
    sigaction(host_sig, &act, NULL);

    kill(getpid(), host_sig);

    /* Make sure the signal isn't masked (reusing the mask inside of act). */
    sigdelset(&act.sa_mask, host_sig);
    sigsuspend(&act.sa_mask);

    /* unreachable */
    _exit(EXIT_FAILURE);
}

static G_NORETURN
void dump_core_and_abort(CPUArchState *env, int target_sig)
{
    CPUState *cpu = env_cpu(env);
    TaskState *ts = get_task_state(cpu);
    int host_sig, core_dumped = 0;

    /* On exit, undo the remapping of SIGABRT. */
    if (target_sig == TARGET_SIGABRT) {
        host_sig = SIGABRT;
    } else {
        host_sig = target_to_host_signal(target_sig);
    }
    trace_user_dump_core_and_abort(env, target_sig, host_sig);
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

    preexit_cleanup(env, 128 + target_sig);
    die_with_signal(host_sig);
}

/* queue a signal so that it will be send to the virtual CPU as soon
   as possible */
void queue_signal(CPUArchState *env, int sig, int si_type,
                  target_siginfo_t *info)
{
    CPUState *cpu = env_cpu(env);
    TaskState *ts = get_task_state(cpu);

    trace_user_queue_signal(env, sig);

    info->si_code = deposit32(info->si_code, 16, 16, si_type);

    ts->sync_signal.info = *info;
    ts->sync_signal.pending = sig;
    /* signal that a new signal is pending */
    qatomic_set(&ts->signal_pending, 1);
}


/* Adjust the signal context to rewind out of safe-syscall if we're in it */
static inline void rewind_if_in_safe_syscall(void *puc)
{
    host_sigcontext *uc = (host_sigcontext *)puc;
    uintptr_t pcreg = host_signal_pc(uc);

    if (pcreg > (uintptr_t)safe_syscall_start
        && pcreg < (uintptr_t)safe_syscall_end) {
        host_signal_set_pc(uc, (uintptr_t)safe_syscall_start);
    }
}

static G_NORETURN
void die_from_signal(siginfo_t *info)
{
    char sigbuf[4], codebuf[12];
    const char *sig, *code = NULL;

    switch (info->si_signo) {
    case SIGSEGV:
        sig = "SEGV";
        switch (info->si_code) {
        case SEGV_MAPERR:
            code = "MAPERR";
            break;
        case SEGV_ACCERR:
            code = "ACCERR";
            break;
        }
        break;
    case SIGBUS:
        sig = "BUS";
        switch (info->si_code) {
        case BUS_ADRALN:
            code = "ADRALN";
            break;
        case BUS_ADRERR:
            code = "ADRERR";
            break;
        }
        break;
    case SIGILL:
        sig = "ILL";
        switch (info->si_code) {
        case ILL_ILLOPC:
            code = "ILLOPC";
            break;
        case ILL_ILLOPN:
            code = "ILLOPN";
            break;
        case ILL_ILLADR:
            code = "ILLADR";
            break;
        case ILL_PRVOPC:
            code = "PRVOPC";
            break;
        case ILL_PRVREG:
            code = "PRVREG";
            break;
        case ILL_COPROC:
            code = "COPROC";
            break;
        }
        break;
    case SIGFPE:
        sig = "FPE";
        switch (info->si_code) {
        case FPE_INTDIV:
            code = "INTDIV";
            break;
        case FPE_INTOVF:
            code = "INTOVF";
            break;
        }
        break;
    case SIGTRAP:
        sig = "TRAP";
        break;
    default:
        snprintf(sigbuf, sizeof(sigbuf), "%d", info->si_signo);
        sig = sigbuf;
        break;
    }
    if (code == NULL) {
        snprintf(codebuf, sizeof(sigbuf), "%d", info->si_code);
        code = codebuf;
    }

    error_report("QEMU internal SIG%s {code=%s, addr=%p}",
                 sig, code, info->si_addr);
    die_with_signal(info->si_signo);
}

static void host_sigsegv_handler(CPUState *cpu, siginfo_t *info,
                                 host_sigcontext *uc)
{
    uintptr_t host_addr = (uintptr_t)info->si_addr;
    /*
     * Convert forcefully to guest address space: addresses outside
     * reserved_va are still valid to report via SEGV_MAPERR.
     */
    bool is_valid = h2g_valid(host_addr);
    abi_ptr guest_addr = h2g_nocheck(host_addr);
    uintptr_t pc = host_signal_pc(uc);
    bool is_write = host_signal_write(info, uc);
    MMUAccessType access_type = adjust_signal_pc(&pc, is_write);
    bool maperr;

    /* If this was a write to a TB protected page, restart. */
    if (is_write
        && is_valid
        && info->si_code == SEGV_ACCERR
        && handle_sigsegv_accerr_write(cpu, host_signal_mask(uc),
                                       pc, guest_addr)) {
        return;
    }

    /*
     * If the access was not on behalf of the guest, within the executable
     * mapping of the generated code buffer, then it is a host bug.
     */
    if (access_type != MMU_INST_FETCH
        && !in_code_gen_buffer((void *)(pc - tcg_splitwx_diff))) {
        die_from_signal(info);
    }

    maperr = true;
    if (is_valid && info->si_code == SEGV_ACCERR) {
        /*
         * With reserved_va, the whole address space is PROT_NONE,
         * which means that we may get ACCERR when we want MAPERR.
         */
        if (page_get_flags(guest_addr) & PAGE_VALID) {
            maperr = false;
        } else {
            info->si_code = SEGV_MAPERR;
        }
    }

    sigprocmask(SIG_SETMASK, host_signal_mask(uc), NULL);
    cpu_loop_exit_sigsegv(cpu, guest_addr, access_type, maperr, pc);
}

static uintptr_t host_sigbus_handler(CPUState *cpu, siginfo_t *info,
                                host_sigcontext *uc)
{
    uintptr_t pc = host_signal_pc(uc);
    bool is_write = host_signal_write(info, uc);
    MMUAccessType access_type = adjust_signal_pc(&pc, is_write);

    /*
     * If the access was not on behalf of the guest, within the executable
     * mapping of the generated code buffer, then it is a host bug.
     */
    if (!in_code_gen_buffer((void *)(pc - tcg_splitwx_diff))) {
        die_from_signal(info);
    }

    if (info->si_code == BUS_ADRALN) {
        uintptr_t host_addr = (uintptr_t)info->si_addr;
        abi_ptr guest_addr = h2g_nocheck(host_addr);

        sigprocmask(SIG_SETMASK, host_signal_mask(uc), NULL);
        cpu_loop_exit_sigbus(cpu, guest_addr, access_type, pc);
    }
    return pc;
}

static void host_signal_handler(int host_sig, siginfo_t *info, void *puc)
{
    CPUState *cpu = thread_cpu;
    CPUArchState *env = cpu_env(cpu);
    TaskState *ts = get_task_state(cpu);
    target_siginfo_t tinfo;
    host_sigcontext *uc = puc;
    struct emulated_sigtable *k;
    int guest_sig;
    uintptr_t pc = 0;
    bool sync_sig = false;
    void *sigmask;

    if (host_sig == host_interrupt_signal) {
        ts->signal_pending = 1;
        cpu_exit(thread_cpu);
        return;
    }

    /*
     * Non-spoofed SIGSEGV and SIGBUS are synchronous, and need special
     * handling wrt signal blocking and unwinding.  Non-spoofed SIGILL,
     * SIGFPE, SIGTRAP are always host bugs.
     */
    if (info->si_code > 0) {
        switch (host_sig) {
        case SIGSEGV:
            /* Only returns on handle_sigsegv_accerr_write success. */
            host_sigsegv_handler(cpu, info, uc);
            return;
        case SIGBUS:
            pc = host_sigbus_handler(cpu, info, uc);
            sync_sig = true;
            break;
        case SIGILL:
        case SIGFPE:
        case SIGTRAP:
            die_from_signal(info);
        }
    }

    /* get target signal number */
    guest_sig = host_to_target_signal(host_sig);
    if (guest_sig < 1 || guest_sig > TARGET_NSIG) {
        return;
    }
    trace_user_host_signal(env, host_sig, guest_sig);

    host_to_target_siginfo_noswap(&tinfo, info);
    k = &ts->sigtab[guest_sig - 1];
    k->info = tinfo;
    k->pending = guest_sig;
    ts->signal_pending = 1;

    /*
     * For synchronous signals, unwind the cpu state to the faulting
     * insn and then exit back to the main loop so that the signal
     * is delivered immediately.
     */
    if (sync_sig) {
        cpu->exception_index = EXCP_INTERRUPT;
        cpu_loop_exit_restore(cpu, pc);
    }

    rewind_if_in_safe_syscall(puc);

    /*
     * Block host signals until target signal handler entered. We
     * can't block SIGSEGV or SIGBUS while we're executing guest
     * code in case the guest code provokes one in the window between
     * now and it getting out to the main loop. Signals will be
     * unblocked again in process_pending_signals().
     *
     * WARNING: we cannot use sigfillset() here because the sigmask
     * field is a kernel sigset_t, which is much smaller than the
     * libc sigset_t which sigfillset() operates on. Using sigfillset()
     * would write 0xff bytes off the end of the structure and trash
     * data on the struct.
     */
    sigmask = host_signal_mask(uc);
    memset(sigmask, 0xff, SIGSET_T_SIZE);
    sigdelset(sigmask, SIGSEGV);
    sigdelset(sigmask, SIGBUS);

    /* interrupt the virtual CPU as soon as possible */
    cpu_exit(thread_cpu);
}

/* do_sigaltstack() returns target values and errnos. */
/* compare linux/kernel/signal.c:do_sigaltstack() */
abi_long do_sigaltstack(abi_ulong uss_addr, abi_ulong uoss_addr,
                        CPUArchState *env)
{
    target_stack_t oss, *uoss = NULL;
    abi_long ret = -TARGET_EFAULT;

    if (uoss_addr) {
        /* Verify writability now, but do not alter user memory yet. */
        if (!lock_user_struct(VERIFY_WRITE, uoss, uoss_addr, 0)) {
            goto out;
        }
        target_save_altstack(&oss, env);
    }

    if (uss_addr) {
        target_stack_t *uss;

        if (!lock_user_struct(VERIFY_READ, uss, uss_addr, 1)) {
            goto out;
        }
        ret = target_restore_altstack(uss, env);
        if (ret) {
            goto out;
        }
    }

    if (uoss_addr) {
        memcpy(uoss, &oss, sizeof(oss));
        unlock_user_struct(uoss, uoss_addr, 1);
        uoss = NULL;
    }
    ret = 0;

 out:
    if (uoss) {
        unlock_user_struct(uoss, uoss_addr, 0);
    }
    return ret;
}

/* do_sigaction() return target values and host errnos */
int do_sigaction(int sig, const struct target_sigaction *act,
                 struct target_sigaction *oact, abi_ulong ka_restorer)
{
    struct target_sigaction *k;
    int host_sig;
    int ret = 0;

    trace_signal_do_sigaction_guest(sig, TARGET_NSIG);

    if (sig < 1 || sig > TARGET_NSIG) {
        return -TARGET_EINVAL;
    }

    if (act && (sig == TARGET_SIGKILL || sig == TARGET_SIGSTOP)) {
        return -TARGET_EINVAL;
    }

    if (block_signals()) {
        return -QEMU_ERESTARTSYS;
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
        __get_user(k->_sa_handler, &act->_sa_handler);
        __get_user(k->sa_flags, &act->sa_flags);
#ifdef TARGET_ARCH_HAS_SA_RESTORER
        __get_user(k->sa_restorer, &act->sa_restorer);
#endif
#ifdef TARGET_ARCH_HAS_KA_RESTORER
        k->ka_restorer = ka_restorer;
#endif
        /* To be swapped in target_to_host_sigset.  */
        k->sa_mask = act->sa_mask;

        /* we update the host linux signal state */
        host_sig = target_to_host_signal(sig);
        trace_signal_do_sigaction_host(host_sig, TARGET_NSIG);
        if (host_sig > SIGRTMAX) {
            /* we don't have enough host signals to map all target signals */
            qemu_log_mask(LOG_UNIMP, "Unsupported target signal #%d, ignored\n",
                          sig);
            /*
             * we don't return an error here because some programs try to
             * register an handler for all possible rt signals even if they
             * don't need it.
             * An error here can abort them whereas there can be no problem
             * to not have the signal available later.
             * This is the case for golang,
             *   See https://github.com/golang/go/issues/33746
             * So we silently ignore the error.
             */
            return 0;
        }
        if (host_sig != SIGSEGV && host_sig != SIGBUS) {
            struct sigaction act1;

            sigfillset(&act1.sa_mask);
            act1.sa_flags = SA_SIGINFO;
            if (k->_sa_handler == TARGET_SIG_IGN) {
                /*
                 * It is important to update the host kernel signal ignore
                 * state to avoid getting unexpected interrupted syscalls.
                 */
                act1.sa_sigaction = (void *)SIG_IGN;
            } else if (k->_sa_handler == TARGET_SIG_DFL) {
                if (core_dump_signal(sig)) {
                    act1.sa_sigaction = host_signal_handler;
                } else {
                    act1.sa_sigaction = (void *)SIG_DFL;
                }
            } else {
                act1.sa_sigaction = host_signal_handler;
                if (k->sa_flags & TARGET_SA_RESTART) {
                    act1.sa_flags |= SA_RESTART;
                }
            }
            ret = sigaction(host_sig, &act1, NULL);
        }
    }
    return ret;
}

static void handle_pending_signal(CPUArchState *cpu_env, int sig,
                                  struct emulated_sigtable *k)
{
    CPUState *cpu = env_cpu(cpu_env);
    abi_ulong handler;
    sigset_t set;
    target_siginfo_t unswapped;
    target_sigset_t target_old_set;
    struct target_sigaction *sa;
    TaskState *ts = get_task_state(cpu);

    trace_user_handle_signal(cpu_env, sig);
    /* dequeue signal */
    k->pending = 0;

    /*
     * Writes out siginfo values byteswapped, accordingly to the target.
     * It also cleans the si_type from si_code making it correct for
     * the target.  We must hold on to the original unswapped copy for
     * strace below, because si_type is still required there.
     */
    if (unlikely(qemu_loglevel_mask(LOG_STRACE))) {
        unswapped = k->info;
    }
    tswap_siginfo(&k->info, &k->info);

    sig = gdb_handlesig(cpu, sig, NULL, &k->info, sizeof(k->info));
    if (!sig) {
        sa = NULL;
        handler = TARGET_SIG_IGN;
    } else {
        sa = &sigact_table[sig - 1];
        handler = sa->_sa_handler;
    }

    if (unlikely(qemu_loglevel_mask(LOG_STRACE))) {
        print_taken_signal(sig, &unswapped);
    }

    if (handler == TARGET_SIG_DFL) {
        /* default handler : ignore some signal. The other are job control or fatal */
        if (sig == TARGET_SIGTSTP || sig == TARGET_SIGTTIN || sig == TARGET_SIGTTOU) {
            kill(getpid(),SIGSTOP);
        } else if (sig != TARGET_SIGCHLD &&
                   sig != TARGET_SIGURG &&
                   sig != TARGET_SIGWINCH &&
                   sig != TARGET_SIGCONT) {
            dump_core_and_abort(cpu_env, sig);
        }
    } else if (handler == TARGET_SIG_IGN) {
        /* ignore sig */
    } else if (handler == TARGET_SIG_ERR) {
        dump_core_and_abort(cpu_env, sig);
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
#if defined(TARGET_ARCH_HAS_SETUP_FRAME)
        if (sa->sa_flags & TARGET_SA_SIGINFO) {
            setup_rt_frame(sig, sa, &k->info, &target_old_set, cpu_env);
        } else {
            setup_frame(sig, sa, &target_old_set, cpu_env);
        }
#else
        /* These targets do not have traditional signals.  */
        setup_rt_frame(sig, sa, &k->info, &target_old_set, cpu_env);
#endif
        if (sa->sa_flags & TARGET_SA_RESETHAND) {
            sa->_sa_handler = TARGET_SIG_DFL;
        }
    }
}

void process_pending_signals(CPUArchState *cpu_env)
{
    CPUState *cpu = env_cpu(cpu_env);
    int sig;
    TaskState *ts = get_task_state(cpu);
    sigset_t set;
    sigset_t *blocked_set;

    while (qatomic_read(&ts->signal_pending)) {
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
        qatomic_set(&ts->signal_pending, 0);
        ts->in_sigsuspend = 0;
        set = ts->signal_mask;
        sigdelset(&set, SIGSEGV);
        sigdelset(&set, SIGBUS);
        sigprocmask(SIG_SETMASK, &set, 0);
    }
    ts->in_sigsuspend = 0;
}

int process_sigsuspend_mask(sigset_t **pset, target_ulong sigset,
                            target_ulong sigsize)
{
    TaskState *ts = get_task_state(thread_cpu);
    sigset_t *host_set = &ts->sigsuspend_mask;
    target_sigset_t *target_sigset;

    if (sigsize != sizeof(*target_sigset)) {
        /* Like the kernel, we enforce correct size sigsets */
        return -TARGET_EINVAL;
    }

    target_sigset = lock_user(VERIFY_READ, sigset, sigsize, 1);
    if (!target_sigset) {
        return -TARGET_EFAULT;
    }
    target_to_host_sigset(host_set, target_sigset);
    unlock_user(target_sigset, sigset, 0);

    *pset = host_set;
    return 0;
}

/*
 *  Emulation of BSD signals
 *
 *  Copyright (c) 2003 - 2008 Fabrice Bellard
 *  Copyright (c) 2013 Stacey Son
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
#include "qemu.h"
#include "signal-common.h"
#include "trace.h"
#include "hw/core/tcg-cpu-ops.h"
#include "host-signal.h"

/*
 * Stubbed out routines until we merge signal support from bsd-user
 * fork.
 */

static struct target_sigaction sigact_table[TARGET_NSIG];
static void host_signal_handler(int host_sig, siginfo_t *info, void *puc);

/*
 * The BSD ABIs use the same singal numbers across all the CPU architectures, so
 * (unlike Linux) these functions are just the identity mapping. This might not
 * be true for XyzBSD running on AbcBSD, which doesn't currently work.
 */
int host_to_target_signal(int sig)
{
    return sig;
}

int target_to_host_signal(int sig)
{
    return sig;
}

/* Adjust the signal context to rewind out of safe-syscall if we're in it */
static inline void rewind_if_in_safe_syscall(void *puc)
{
    ucontext_t *uc = (ucontext_t *)puc;
    uintptr_t pcreg = host_signal_pc(uc);

    if (pcreg > (uintptr_t)safe_syscall_start
        && pcreg < (uintptr_t)safe_syscall_end) {
        host_signal_set_pc(uc, (uintptr_t)safe_syscall_start);
    }
}

static bool has_trapno(int tsig)
{
    return tsig == TARGET_SIGILL ||
        tsig == TARGET_SIGFPE ||
        tsig == TARGET_SIGSEGV ||
        tsig == TARGET_SIGBUS ||
        tsig == TARGET_SIGTRAP;
}

/* Siginfo conversion. */

/*
 * Populate tinfo w/o swapping based on guessing which fields are valid.
 */
static inline void host_to_target_siginfo_noswap(target_siginfo_t *tinfo,
        const siginfo_t *info)
{
    int sig = host_to_target_signal(info->si_signo);
    int si_code = info->si_code;
    int si_type;

    /*
     * Make sure we that the variable portion of the target siginfo is zeroed
     * out so we don't leak anything into that.
     */
    memset(&tinfo->_reason, 0, sizeof(tinfo->_reason));

    /*
     * This is awkward, because we have to use a combination of the si_code and
     * si_signo to figure out which of the union's members are valid.o We
     * therefore make our best guess.
     *
     * Once we have made our guess, we record it in the top 16 bits of
     * the si_code, so that tswap_siginfo() later can use it.
     * tswap_siginfo() will strip these top bits out before writing
     * si_code to the guest (sign-extending the lower bits).
     */
    tinfo->si_signo = sig;
    tinfo->si_errno = info->si_errno;
    tinfo->si_code = info->si_code;
    tinfo->si_pid = info->si_pid;
    tinfo->si_uid = info->si_uid;
    tinfo->si_status = info->si_status;
    tinfo->si_addr = (abi_ulong)(unsigned long)info->si_addr;
    /*
     * si_value is opaque to kernel. On all FreeBSD platforms,
     * sizeof(sival_ptr) >= sizeof(sival_int) so the following
     * always will copy the larger element.
     */
    tinfo->si_value.sival_ptr =
        (abi_ulong)(unsigned long)info->si_value.sival_ptr;

    switch (si_code) {
        /*
         * All the SI_xxx codes that are defined here are global to
         * all the signals (they have values that none of the other,
         * more specific signal info will set).
         */
    case SI_USER:
    case SI_LWP:
    case SI_KERNEL:
    case SI_QUEUE:
    case SI_ASYNCIO:
        /*
         * Only the fixed parts are valid (though FreeBSD doesn't always
         * set all the fields to non-zero values.
         */
        si_type = QEMU_SI_NOINFO;
        break;
    case SI_TIMER:
        tinfo->_reason._timer._timerid = info->_reason._timer._timerid;
        tinfo->_reason._timer._overrun = info->_reason._timer._overrun;
        si_type = QEMU_SI_TIMER;
        break;
    case SI_MESGQ:
        tinfo->_reason._mesgq._mqd = info->_reason._mesgq._mqd;
        si_type = QEMU_SI_MESGQ;
        break;
    default:
        /*
         * We have to go based on the signal number now to figure out
         * what's valid.
         */
        if (has_trapno(sig)) {
            tinfo->_reason._fault._trapno = info->_reason._fault._trapno;
            si_type = QEMU_SI_FAULT;
        }
#ifdef TARGET_SIGPOLL
        /*
         * FreeBSD never had SIGPOLL, but emulates it for Linux so there's
         * a chance it may popup in the future.
         */
        if (sig == TARGET_SIGPOLL) {
            tinfo->_reason._poll._band = info->_reason._poll._band;
            si_type = QEMU_SI_POLL;
        }
#endif
        /*
         * Unsure that this can actually be generated, and our support for
         * capsicum is somewhere between weak and non-existant, but if we get
         * one, then we know what to save.
         */
        if (sig == TARGET_SIGTRAP) {
            tinfo->_reason._capsicum._syscall =
                info->_reason._capsicum._syscall;
            si_type = QEMU_SI_CAPSICUM;
        }
        break;
    }
    tinfo->si_code = deposit32(si_code, 24, 8, si_type);
}

/*
 * Queue a signal so that it will be send to the virtual CPU as soon as
 * possible.
 */
void queue_signal(CPUArchState *env, int sig, int si_type,
                  target_siginfo_t *info)
{
    qemu_log_mask(LOG_UNIMP, "No signal queueing, dropping signal %d\n", sig);
}

static int fatal_signal(int sig)
{

    switch (sig) {
    case TARGET_SIGCHLD:
    case TARGET_SIGURG:
    case TARGET_SIGWINCH:
    case TARGET_SIGINFO:
        /* Ignored by default. */
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

/*
 * Force a synchronously taken QEMU_SI_FAULT signal. For QEMU the
 * 'force' part is handled in process_pending_signals().
 */
void force_sig_fault(int sig, int code, abi_ulong addr)
{
    CPUState *cpu = thread_cpu;
    CPUArchState *env = cpu->env_ptr;
    target_siginfo_t info = {};

    info.si_signo = sig;
    info.si_errno = 0;
    info.si_code = code;
    info.si_addr = addr;
    queue_signal(env, sig, QEMU_SI_FAULT, &info);
}

static void host_signal_handler(int host_sig, siginfo_t *info, void *puc)
{
    CPUArchState *env = thread_cpu->env_ptr;
    CPUState *cpu = env_cpu(env);
    TaskState *ts = cpu->opaque;
    target_siginfo_t tinfo;
    ucontext_t *uc = puc;
    struct emulated_sigtable *k;
    int guest_sig;
    uintptr_t pc = 0;
    bool sync_sig = false;

    /*
     * Non-spoofed SIGSEGV and SIGBUS are synchronous, and need special
     * handling wrt signal blocking and unwinding.
     */
    if ((host_sig == SIGSEGV || host_sig == SIGBUS) && info->si_code > 0) {
        MMUAccessType access_type;
        uintptr_t host_addr;
        abi_ptr guest_addr;
        bool is_write;

        host_addr = (uintptr_t)info->si_addr;

        /*
         * Convert forcefully to guest address space: addresses outside
         * reserved_va are still valid to report via SEGV_MAPERR.
         */
        guest_addr = h2g_nocheck(host_addr);

        pc = host_signal_pc(uc);
        is_write = host_signal_write(info, uc);
        access_type = adjust_signal_pc(&pc, is_write);

        if (host_sig == SIGSEGV) {
            bool maperr = true;

            if (info->si_code == SEGV_ACCERR && h2g_valid(host_addr)) {
                /* If this was a write to a TB protected page, restart. */
                if (is_write &&
                    handle_sigsegv_accerr_write(cpu, &uc->uc_sigmask,
                                                pc, guest_addr)) {
                    return;
                }

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

            sigprocmask(SIG_SETMASK, &uc->uc_sigmask, NULL);
            cpu_loop_exit_sigsegv(cpu, guest_addr, access_type, maperr, pc);
        } else {
            sigprocmask(SIG_SETMASK, &uc->uc_sigmask, NULL);
            if (info->si_code == BUS_ADRALN) {
                cpu_loop_exit_sigbus(cpu, guest_addr, access_type, pc);
            }
        }

        sync_sig = true;
    }

    /* Get the target signal number. */
    guest_sig = host_to_target_signal(host_sig);
    if (guest_sig < 1 || guest_sig > TARGET_NSIG) {
        return;
    }
    trace_user_host_signal(cpu, host_sig, guest_sig);

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
     */
    sigfillset(&uc->uc_sigmask);
    sigdelset(&uc->uc_sigmask, SIGSEGV);
    sigdelset(&uc->uc_sigmask, SIGBUS);

    /* Interrupt the virtual CPU as soon as possible. */
    cpu_exit(thread_cpu);
}

void signal_init(void)
{
    TaskState *ts = (TaskState *)thread_cpu->opaque;
    struct sigaction act;
    struct sigaction oact;
    int i;
    int host_sig;

    /* Set the signal mask from the host mask. */
    sigprocmask(0, 0, &ts->signal_mask);

    sigfillset(&act.sa_mask);
    act.sa_sigaction = host_signal_handler;
    act.sa_flags = SA_SIGINFO;

    for (i = 1; i <= TARGET_NSIG; i++) {
#ifdef CONFIG_GPROF
        if (i == TARGET_SIGPROF) {
            continue;
        }
#endif
        host_sig = target_to_host_signal(i);
        sigaction(host_sig, NULL, &oact);
        if (oact.sa_sigaction == (void *)SIG_IGN) {
            sigact_table[i - 1]._sa_handler = TARGET_SIG_IGN;
        } else if (oact.sa_sigaction == (void *)SIG_DFL) {
            sigact_table[i - 1]._sa_handler = TARGET_SIG_DFL;
        }
        /*
         * If there's already a handler installed then something has
         * gone horribly wrong, so don't even try to handle that case.
         * Install some handlers for our own use.  We need at least
         * SIGSEGV and SIGBUS, to detect exceptions.  We can not just
         * trap all signals because it affects syscall interrupt
         * behavior.  But do trap all default-fatal signals.
         */
        if (fatal_signal(i)) {
            sigaction(host_sig, &act, NULL);
        }
    }
}

void process_pending_signals(CPUArchState *cpu_env)
{
}

void cpu_loop_exit_sigsegv(CPUState *cpu, target_ulong addr,
                           MMUAccessType access_type, bool maperr, uintptr_t ra)
{
    const struct TCGCPUOps *tcg_ops = CPU_GET_CLASS(cpu)->tcg_ops;

    if (tcg_ops->record_sigsegv) {
        tcg_ops->record_sigsegv(cpu, addr, access_type, maperr, ra);
    }

    force_sig_fault(TARGET_SIGSEGV,
                    maperr ? TARGET_SEGV_MAPERR : TARGET_SEGV_ACCERR,
                    addr);
    cpu->exception_index = EXCP_INTERRUPT;
    cpu_loop_exit_restore(cpu, ra);
}

void cpu_loop_exit_sigbus(CPUState *cpu, target_ulong addr,
                          MMUAccessType access_type, uintptr_t ra)
{
    const struct TCGCPUOps *tcg_ops = CPU_GET_CLASS(cpu)->tcg_ops;

    if (tcg_ops->record_sigbus) {
        tcg_ops->record_sigbus(cpu, addr, access_type, ra);
    }

    force_sig_fault(TARGET_SIGBUS, TARGET_BUS_ADRALN, addr);
    cpu->exception_index = EXCP_INTERRUPT;
    cpu_loop_exit_restore(cpu, ra);
}

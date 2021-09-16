/*
 * safe-syscall.h: prototypes for linux-user signal-race-safe syscalls
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

#ifndef LINUX_USER_SAFE_SYSCALL_H
#define LINUX_USER_SAFE_SYSCALL_H

/**
 * safe_syscall:
 * @int number: number of system call to make
 * ...: arguments to the system call
 *
 * Call a system call if guest signal not pending.
 * This has the same API as the libc syscall() function, except that it
 * may return -1 with errno == TARGET_ERESTARTSYS if a signal was pending.
 *
 * Returns: the system call result, or -1 with an error code in errno
 * (Errnos are host errnos; we rely on TARGET_ERESTARTSYS not clashing
 * with any of the host errno values.)
 */

/*
 * A guide to using safe_syscall() to handle interactions between guest
 * syscalls and guest signals:
 *
 * Guest syscalls come in two flavours:
 *
 * (1) Non-interruptible syscalls
 *
 * These are guest syscalls that never get interrupted by signals and
 * so never return EINTR. They can be implemented straightforwardly in
 * QEMU: just make sure that if the implementation code has to make any
 * blocking calls that those calls are retried if they return EINTR.
 * It's also OK to implement these with safe_syscall, though it will be
 * a little less efficient if a signal is delivered at the 'wrong' moment.
 *
 * Some non-interruptible syscalls need to be handled using block_signals()
 * to block signals for the duration of the syscall. This mainly applies
 * to code which needs to modify the data structures used by the
 * host_signal_handler() function and the functions it calls, including
 * all syscalls which change the thread's signal mask.
 *
 * (2) Interruptible syscalls
 *
 * These are guest syscalls that can be interrupted by signals and
 * for which we need to either return EINTR or arrange for the guest
 * syscall to be restarted. This category includes both syscalls which
 * always restart (and in the kernel return -ERESTARTNOINTR), ones
 * which only restart if there is no handler (kernel returns -ERESTARTNOHAND
 * or -ERESTART_RESTARTBLOCK), and the most common kind which restart
 * if the handler was registered with SA_RESTART (kernel returns
 * -ERESTARTSYS). System calls which are only interruptible in some
 * situations (like 'open') also need to be handled this way.
 *
 * Here it is important that the host syscall is made
 * via this safe_syscall() function, and *not* via the host libc.
 * If the host libc is used then the implementation will appear to work
 * most of the time, but there will be a race condition where a
 * signal could arrive just before we make the host syscall inside libc,
 * and then then guest syscall will not correctly be interrupted.
 * Instead the implementation of the guest syscall can use the safe_syscall
 * function but otherwise just return the result or errno in the usual
 * way; the main loop code will take care of restarting the syscall
 * if appropriate.
 *
 * (If the implementation needs to make multiple host syscalls this is
 * OK; any which might really block must be via safe_syscall(); for those
 * which are only technically blocking (ie which we know in practice won't
 * stay in the host kernel indefinitely) it's OK to use libc if necessary.
 * You must be able to cope with backing out correctly if some safe_syscall
 * you make in the implementation returns either -TARGET_ERESTARTSYS or
 * EINTR though.)
 *
 * block_signals() cannot be used for interruptible syscalls.
 *
 *
 * How and why the safe_syscall implementation works:
 *
 * The basic setup is that we make the host syscall via a known
 * section of host native assembly. If a signal occurs, our signal
 * handler checks the interrupted host PC against the addresse of that
 * known section. If the PC is before or at the address of the syscall
 * instruction then we change the PC to point at a "return
 * -TARGET_ERESTARTSYS" code path instead, and then exit the signal handler
 * (causing the safe_syscall() call to immediately return that value).
 * Then in the main.c loop if we see this magic return value we adjust
 * the guest PC to wind it back to before the system call, and invoke
 * the guest signal handler as usual.
 *
 * This winding-back will happen in two cases:
 * (1) signal came in just before we took the host syscall (a race);
 *   in this case we'll take the guest signal and have another go
 *   at the syscall afterwards, and this is indistinguishable for the
 *   guest from the timing having been different such that the guest
 *   signal really did win the race
 * (2) signal came in while the host syscall was blocking, and the
 *   host kernel decided the syscall should be restarted;
 *   in this case we want to restart the guest syscall also, and so
 *   rewinding is the right thing. (Note that "restart" semantics mean
 *   "first call the signal handler, then reattempt the syscall".)
 * The other situation to consider is when a signal came in while the
 * host syscall was blocking, and the host kernel decided that the syscall
 * should not be restarted; in this case QEMU's host signal handler will
 * be invoked with the PC pointing just after the syscall instruction,
 * with registers indicating an EINTR return; the special code in the
 * handler will not kick in, and we will return EINTR to the guest as
 * we should.
 *
 * Notice that we can leave the host kernel to make the decision for
 * us about whether to do a restart of the syscall or not; we do not
 * need to check SA_RESTART flags in QEMU or distinguish the various
 * kinds of restartability.
 */
#ifdef HAVE_SAFE_SYSCALL
/* The core part of this function is implemented in assembly */
extern long safe_syscall_base(int *pending, long number, ...);

#define safe_syscall(...)                                               \
    ({                                                                  \
        long ret_;                                                      \
        int *psp_ = &((TaskState *)thread_cpu->opaque)->signal_pending; \
        ret_ = safe_syscall_base(psp_, __VA_ARGS__);                    \
        if (is_error(ret_)) {                                           \
            errno = -ret_;                                              \
            ret_ = -1;                                                  \
        }                                                               \
        ret_;                                                           \
    })

#else

/*
 * Fallback for architectures which don't yet provide a safe-syscall assembly
 * fragment; note that this is racy!
 * This should go away when all host architectures have been updated.
 */
#define safe_syscall syscall

#endif

#endif

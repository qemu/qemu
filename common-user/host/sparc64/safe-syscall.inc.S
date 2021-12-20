/*
 * safe-syscall.inc.S : host-specific assembly fragment
 * to handle signals occurring at the same time as system calls.
 * This is intended to be included by common-user/safe-syscall.S
 *
 * Written by Richard Henderson <richard.henderson@linaro.org>
 * Copyright (C) 2021 Linaro, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

        .text
        .balign 4

        .register %g2, #scratch
        .register %g3, #scratch

        .global safe_syscall_base
        .global safe_syscall_start
        .global safe_syscall_end
        .type   safe_syscall_base, @function
        .type   safe_syscall_start, @function
        .type   safe_syscall_end, @function

#define STACK_BIAS  2047
#define PARAM(N)    STACK_BIAS + N*8

        /*
         * This is the entry point for making a system call. The calling
         * convention here is that of a C varargs function with the
         * first argument an 'int *' to the signal_pending flag, the
         * second one the system call number (as a 'long'), and all further
         * arguments being syscall arguments (also 'long').
         */
safe_syscall_base:
        .cfi_startproc
        /*
         * The syscall calling convention isn't the same as the C one:
         * we enter with o0 == &signal_pending
         *               o1 == syscall number
         *               o2 ... o5, (stack) == syscall arguments
         *               and return the result in x0
         * and the syscall instruction needs
         *               g1 == syscall number
         *               o0 ... o5 == syscall arguments
         *               and returns the result in o0
         * Shuffle everything around appropriately.
         */
        mov     %o0, %g2                /* signal_pending pointer */
        mov     %o1, %g1                /* syscall number */
        mov     %o2, %o0                /* syscall arguments */
        mov     %o3, %o1
        mov     %o4, %o2
        mov     %o5, %o3
        ldx     [%sp + PARAM(6)], %o4
        ldx     [%sp + PARAM(7)], %o5

        /*
         * This next sequence of code works in conjunction with the
         * rewind_if_safe_syscall_function(). If a signal is taken
         * and the interrupted PC is anywhere between 'safe_syscall_start'
         * and 'safe_syscall_end' then we rewind it to 'safe_syscall_start'.
         * The code sequence must therefore be able to cope with this, and
         * the syscall instruction must be the final one in the sequence.
         */
safe_syscall_start:
        /* if signal_pending is non-zero, don't do the call */
        lduw    [%g2], %g3
        brnz,pn %g3, 2f
         nop
        ta      0x6d
safe_syscall_end:
        /* code path for having successfully executed the syscall */
        bcs,pn  %xcc, 1f
         nop
        ret
         nop

        /* code path when we didn't execute the syscall */
2:      set     QEMU_ERESTARTSYS, %o0

        /* code path setting errno */
1:      mov     %o7, %g1
        call    safe_syscall_set_errno_tail
         mov    %g1, %o7

        .cfi_endproc
        .size   safe_syscall_base, .-safe_syscall_base

/*
 * safe-syscall.inc.S : host-specific assembly fragment
 * to handle signals occurring at the same time as system calls.
 * This is intended to be included by common-user/safe-syscall.S
 *
 * Written by Richard Henderson <rth@twiddle.net>
 * Copyright (C) 2016 Red Hat, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

        .global safe_syscall_base
        .global safe_syscall_start
        .global safe_syscall_end
        .type   safe_syscall_base, @function

        /* This is the entry point for making a system call. The calling
         * convention here is that of a C varargs function with the
         * first argument an 'int *' to the signal_pending flag, the
         * second one the system call number (as a 'long'), and all further
         * arguments being syscall arguments (also 'long').
         */
safe_syscall_base:
        .cfi_startproc
        stmg    %r6,%r15,48(%r15)       /* save all call-saved registers */
        .cfi_offset %r15,-40
        .cfi_offset %r14,-48
        .cfi_offset %r13,-56
        .cfi_offset %r12,-64
        .cfi_offset %r11,-72
        .cfi_offset %r10,-80
        .cfi_offset %r9,-88
        .cfi_offset %r8,-96
        .cfi_offset %r7,-104
        .cfi_offset %r6,-112
        lgr     %r1,%r15
        lg      %r0,8(%r15)             /* load eos */
        aghi    %r15,-160
        .cfi_adjust_cfa_offset 160
        stg     %r1,0(%r15)             /* store back chain */
        stg     %r0,8(%r15)             /* store eos */

        /*
         * The syscall calling convention isn't the same as the C one:
         * we enter with r2 == &signal_pending
         *               r3 == syscall number
         *               r4, r5, r6, (stack) == syscall arguments
         *               and return the result in r2
         * and the syscall instruction needs
         *               r1 == syscall number
         *               r2 ... r7 == syscall arguments
         *               and returns the result in r2
         * Shuffle everything around appropriately.
         */
        lgr     %r8,%r2                 /* signal_pending pointer */
        lgr     %r1,%r3                 /* syscall number */
        lgr     %r2,%r4                 /* syscall args */
        lgr     %r3,%r5
        lgr     %r4,%r6
        lmg     %r5,%r7,320(%r15)

        /* This next sequence of code works in conjunction with the
         * rewind_if_safe_syscall_function(). If a signal is taken
         * and the interrupted PC is anywhere between 'safe_syscall_start'
         * and 'safe_syscall_end' then we rewind it to 'safe_syscall_start'.
         * The code sequence must therefore be able to cope with this, and
         * the syscall instruction must be the final one in the sequence.
         */
safe_syscall_start:
        /* if signal_pending is non-zero, don't do the call */
        icm     %r0,15,0(%r8)
        jne     2f
        svc     0
safe_syscall_end:

        /* code path for having successfully executed the syscall */
        lg      %r15,0(%r15)            /* load back chain */
        .cfi_remember_state
        .cfi_adjust_cfa_offset -160
        lmg     %r6,%r15,48(%r15)       /* load saved registers */

        lghi    %r0, -4095              /* check for syscall error */
        clgr    %r2, %r0
        blr     %r14                    /* return on success */
        lcr     %r2, %r2                /* create positive errno */
        jg      safe_syscall_set_errno_tail
        .cfi_restore_state

        /* code path when we didn't execute the syscall */
2:      lg      %r15,0(%r15)            /* load back chain */
        .cfi_adjust_cfa_offset -160
        lmg     %r6,%r15,48(%r15)       /* load saved registers */
        lghi    %r2, QEMU_ERESTARTSYS
        jg      safe_syscall_set_errno_tail

        .cfi_endproc
        .size   safe_syscall_base, .-safe_syscall_base

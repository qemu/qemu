/*
 * safe-syscall.inc.S : host-specific assembly fragment
 * to handle signals occurring at the same time as system calls.
 * This is intended to be included by common-user/safe-syscall.S
 *
 * Ported to LoongArch by WANG Xuerui <git@xen0n.name>
 *
 * Based on safe-syscall.inc.S code for RISC-V,
 * originally written by Richard Henderson <rth@twiddle.net>
 * Copyright (C) 2018 Linaro, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

        .global safe_syscall_base
        .global safe_syscall_start
        .global safe_syscall_end
        .type   safe_syscall_base, @function
        .type   safe_syscall_start, @function
        .type   safe_syscall_end, @function

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
         * The syscall calling convention is nearly the same as C:
         * we enter with a0 == &signal_pending
         *               a1 == syscall number
         *               a2 ... a7 == syscall arguments
         *               and return the result in a0
         * and the syscall instruction needs
         *               a7 == syscall number
         *               a0 ... a5 == syscall arguments
         *               and returns the result in a0
         * Shuffle everything around appropriately.
         */
        move    $t0, $a0        /* signal_pending pointer */
        move    $t1, $a1        /* syscall number */
        move    $a0, $a2        /* syscall arguments */
        move    $a1, $a3
        move    $a2, $a4
        move    $a3, $a5
        move    $a4, $a6
        move    $a5, $a7
        move    $a7, $t1

        /*
         * We need to preserve the signal_pending pointer but t0 is
         * clobbered by syscalls on LoongArch, so we need to move it
         * somewhere else, ideally both preserved across syscalls and
         * clobbered by procedure calls so we don't have to allocate a
         * stack frame; a6 is just the register we want here.
         */
        move    $a6, $t0

        /*
         * This next sequence of code works in conjunction with the
         * rewind_if_safe_syscall_function(). If a signal is taken
         * and the interrupted PC is anywhere between 'safe_syscall_start'
         * and 'safe_syscall_end' then we rewind it to 'safe_syscall_start'.
         * The code sequence must therefore be able to cope with this, and
         * the syscall instruction must be the final one in the sequence.
         */
safe_syscall_start:
        /* If signal_pending is non-zero, don't do the call */
        ld.w    $t1, $a6, 0
        bnez    $t1, 2f
        syscall 0
safe_syscall_end:
        /* code path for having successfully executed the syscall */
        li.w    $t2, -4096
        bgtu    $a0, $t2, 0f
        jr      $ra

        /* code path setting errno */
0:      sub.d   $a0, $zero, $a0
        b       safe_syscall_set_errno_tail

        /* code path when we didn't execute the syscall */
2:      li.w    $a0, QEMU_ERESTARTSYS
        b       safe_syscall_set_errno_tail
        .cfi_endproc
        .size   safe_syscall_base, .-safe_syscall_base

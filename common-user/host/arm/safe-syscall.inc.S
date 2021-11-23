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
        .type   safe_syscall_base, %function

        .cfi_sections   .debug_frame

        .text
        .syntax unified
        .arm
        .align 2

        /* This is the entry point for making a system call. The calling
         * convention here is that of a C varargs function with the
         * first argument an 'int *' to the signal_pending flag, the
         * second one the system call number (as a 'long'), and all further
         * arguments being syscall arguments (also 'long').
         */
safe_syscall_base:
        .fnstart
        .cfi_startproc
        mov     r12, sp                 /* save entry stack */
        push    { r4, r5, r6, r7, r8, lr }
        .save   { r4, r5, r6, r7, r8, lr }
        .cfi_adjust_cfa_offset 24
        .cfi_rel_offset r4, 0
        .cfi_rel_offset r5, 4
        .cfi_rel_offset r6, 8
        .cfi_rel_offset r7, 12
        .cfi_rel_offset r8, 16
        .cfi_rel_offset lr, 20

        /* The syscall calling convention isn't the same as the C one:
         * we enter with r0 == &signal_pending
         *               r1 == syscall number
         *               r2, r3, [sp+0] ... [sp+12] == syscall arguments
         *               and return the result in r0
         * and the syscall instruction needs
         *               r7 == syscall number
         *               r0 ... r6 == syscall arguments
         *               and returns the result in r0
         * Shuffle everything around appropriately.
         * Note the 16 bytes that we pushed to save registers.
         */
        mov     r8, r0                  /* copy signal_pending */
        mov     r7, r1                  /* syscall number */
        mov     r0, r2                  /* syscall args */
        mov     r1, r3
        ldm     r12, { r2, r3, r4, r5, r6 }

        /* This next sequence of code works in conjunction with the
         * rewind_if_safe_syscall_function(). If a signal is taken
         * and the interrupted PC is anywhere between 'safe_syscall_start'
         * and 'safe_syscall_end' then we rewind it to 'safe_syscall_start'.
         * The code sequence must therefore be able to cope with this, and
         * the syscall instruction must be the final one in the sequence.
         */
safe_syscall_start:
        /* if signal_pending is non-zero, don't do the call */
        ldr     r12, [r8]               /* signal_pending */
        tst     r12, r12
        bne     2f
        swi     0
safe_syscall_end:

        /* code path for having successfully executed the syscall */
#if defined(__linux__)
        /* Linux kernel returns (small) negative errno. */
        cmp     r0, #-4096
        neghi   r0, r0
        bhi     1f
#elif defined(__FreeBSD__)
        /* FreeBSD kernel returns positive errno and C bit set. */
        bcs     1f
#else
#error "unsupported os"
#endif
        pop     { r4, r5, r6, r7, r8, pc }

        /* code path when we didn't execute the syscall */
2:      mov     r0, #QEMU_ERESTARTSYS

        /* code path setting errno */
1:      pop     { r4, r5, r6, r7, r8, lr }
        .cfi_adjust_cfa_offset -24
        .cfi_restore r4
        .cfi_restore r5
        .cfi_restore r6
        .cfi_restore r7
        .cfi_restore r8
        .cfi_restore lr
        b       safe_syscall_set_errno_tail

        .fnend
        .cfi_endproc
        .size   safe_syscall_base, .-safe_syscall_base

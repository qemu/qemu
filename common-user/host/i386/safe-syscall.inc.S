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
        push    %ebp
        .cfi_adjust_cfa_offset 4
        .cfi_rel_offset ebp, 0
        push    %esi
        .cfi_adjust_cfa_offset 4
        .cfi_rel_offset esi, 0
        push    %edi
        .cfi_adjust_cfa_offset 4
        .cfi_rel_offset edi, 0
        push    %ebx
        .cfi_adjust_cfa_offset 4
        .cfi_rel_offset ebx, 0

        /* The syscall calling convention isn't the same as the C one:
         * we enter with 0(%esp) == return address
         *               4(%esp) == &signal_pending
         *               8(%esp) == syscall number
         *               12(%esp) ... 32(%esp) == syscall arguments
         *               and return the result in eax
         * and the syscall instruction needs
         *               eax == syscall number
         *               ebx, ecx, edx, esi, edi, ebp == syscall arguments
         *               and returns the result in eax
         * Shuffle everything around appropriately.
         * Note the 16 bytes that we pushed to save registers.
         */
        mov     12+16(%esp), %ebx       /* the syscall arguments */
        mov     16+16(%esp), %ecx
        mov     20+16(%esp), %edx
        mov     24+16(%esp), %esi
        mov     28+16(%esp), %edi
        mov     32+16(%esp), %ebp

        /* This next sequence of code works in conjunction with the
         * rewind_if_safe_syscall_function(). If a signal is taken
         * and the interrupted PC is anywhere between 'safe_syscall_start'
         * and 'safe_syscall_end' then we rewind it to 'safe_syscall_start'.
         * The code sequence must therefore be able to cope with this, and
         * the syscall instruction must be the final one in the sequence.
         */
safe_syscall_start:
        /* if signal_pending is non-zero, don't do the call */
        mov     4+16(%esp), %eax        /* signal_pending */
        cmpl    $0, (%eax)
        jnz     2f
        mov     8+16(%esp), %eax        /* syscall number */
        int     $0x80
safe_syscall_end:

        /* code path for having successfully executed the syscall */
#if defined(__linux__)
        /* Linux kernel returns (small) negative errno. */
        cmp     $-4095, %eax
        jae     0f
#elif defined(__FreeBSD__)
        /* FreeBSD kernel returns positive errno and C bit set. */
        jc      1f
#else
#error "unsupported os"
#endif
        pop     %ebx
        .cfi_remember_state
        .cfi_adjust_cfa_offset -4
        .cfi_restore ebx
        pop     %edi
        .cfi_adjust_cfa_offset -4
        .cfi_restore edi
        pop     %esi
        .cfi_adjust_cfa_offset -4
        .cfi_restore esi
        pop     %ebp
        .cfi_adjust_cfa_offset -4
        .cfi_restore ebp
        ret
        .cfi_restore_state

#if defined(__linux__)
0:      neg     %eax
        jmp     1f
#endif

        /* code path when we didn't execute the syscall */
2:      mov     $QEMU_ERESTARTSYS, %eax

        /* code path setting errno */
1:      pop     %ebx
        .cfi_adjust_cfa_offset -4
        .cfi_restore ebx
        pop     %edi
        .cfi_adjust_cfa_offset -4
        .cfi_restore edi
        pop     %esi
        .cfi_adjust_cfa_offset -4
        .cfi_restore esi
        pop     %ebp
        .cfi_adjust_cfa_offset -4
        .cfi_restore ebp
        mov     %eax, 4(%esp)
        jmp     safe_syscall_set_errno_tail

        .cfi_endproc
        .size   safe_syscall_base, .-safe_syscall_base

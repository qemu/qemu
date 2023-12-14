/*
 * safe-syscall.inc.S : host-specific assembly fragment
 * to handle signals occurring at the same time as system calls.
 * This is intended to be included by common-user/safe-syscall.S
 *
 * Copyright (C) 2022 Linaro, Ltd.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

/*
 * Standardize on the _CALL_FOO symbols used by GCC:
 * Apple XCode does not define _CALL_DARWIN.
 * Clang defines _CALL_ELF (64-bit) but not _CALL_SYSV (32-bit).
 */
#if !defined(_CALL_SYSV) && \
    !defined(_CALL_DARWIN) && \
    !defined(_CALL_AIX) && \
    !defined(_CALL_ELF)
# if defined(__APPLE__)
#  define _CALL_DARWIN
# elif defined(__ELF__) && TCG_TARGET_REG_BITS == 32
#  define _CALL_SYSV
# else
#  error "Unknown ABI"
# endif
#endif 

#ifndef _CALL_SYSV
# error "Unsupported ABI"
#endif


        .global safe_syscall_base
        .global safe_syscall_start
        .global safe_syscall_end
        .type   safe_syscall_base, @function

        .text

        /*
         * This is the entry point for making a system call. The calling
         * convention here is that of a C varargs function with the
         * first argument an 'int *' to the signal_pending flag, the
         * second one the system call number (as a 'long'), and all further
         * arguments being syscall arguments (also 'long').
         */
safe_syscall_base:
        .cfi_startproc
        stwu    1, -8(1)
        .cfi_def_cfa_offset 8
        stw     30, 4(1)
        .cfi_offset 30, -4

        /*
         * We enter with r3 == &signal_pending
         *               r4 == syscall number
         *               r5 ... r10 == syscall arguments
         *               and return the result in r3
         * and the syscall instruction needs
         *               r0 == syscall number
         *               r3 ... r8 == syscall arguments
         *               and returns the result in r3
         * Shuffle everything around appropriately.
         */
        mr      30, 3           /* signal_pending */
        mr      0, 4            /* syscall number */
        mr      3, 5            /* syscall arguments */
        mr      4, 6
        mr      5, 7
        mr      6, 8
        mr      7, 9
        mr      8, 10

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
        lwz     12, 0(30)
        cmpwi   0, 12, 0
        bne-    2f
        sc
safe_syscall_end:
        /* code path when we did execute the syscall */
        lwz     30, 4(1)        /* restore r30 */
        addi    1, 1, 8         /* restore stack */
        .cfi_restore 30
        .cfi_def_cfa_offset 0
        bnslr+                  /* return on success */
        b       safe_syscall_set_errno_tail

        /* code path when we didn't execute the syscall */
2:      lwz     30, 4(1)
        addi    1, 1, 8
        addi    3, 0, QEMU_ERESTARTSYS
        b       safe_syscall_set_errno_tail

        .cfi_endproc

        .size   safe_syscall_base, .-safe_syscall_base

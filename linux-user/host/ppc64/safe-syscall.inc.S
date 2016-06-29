/*
 * safe-syscall.inc.S : host-specific assembly fragment
 * to handle signals occurring at the same time as system calls.
 * This is intended to be included by linux-user/safe-syscall.S
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
	.type	safe_syscall_base, @function

	.text

	/* This is the entry point for making a system call. The calling
	 * convention here is that of a C varargs function with the
	 * first argument an 'int *' to the signal_pending flag, the
	 * second one the system call number (as a 'long'), and all further
	 * arguments being syscall arguments (also 'long').
	 * We return a long which is the syscall's return value, which
	 * may be negative-errno on failure. Conversion to the
	 * -1-and-errno-set convention is done by the calling wrapper.
	 */
#if _CALL_ELF == 2
safe_syscall_base:
	.cfi_startproc
	.localentry safe_syscall_base,0
#else
	.section ".opd","aw"
	.align	3
safe_syscall_base:
	.quad	.L.safe_syscall_base,.TOC.@tocbase,0
	.previous
.L.safe_syscall_base:
	.cfi_startproc
#endif
	/* We enter with r3 == *signal_pending
	 *               r4 == syscall number
	 *               r5 ... r10 == syscall arguments
	 *               and return the result in r3
	 * and the syscall instruction needs
	 *               r0 == syscall number
	 *               r3 ... r8 == syscall arguments
	 *               and returns the result in r3
	 * Shuffle everything around appropriately.
	 */
	mr	11, 3	/* signal_pending */
	mr	0, 4	/* syscall number */
	mr	3, 5	/* syscall arguments */
	mr	4, 6
	mr	5, 7
	mr	6, 8
	mr	7, 9
	mr	8, 10

	/* This next sequence of code works in conjunction with the
	 * rewind_if_safe_syscall_function(). If a signal is taken
	 * and the interrupted PC is anywhere between 'safe_syscall_start'
	 * and 'safe_syscall_end' then we rewind it to 'safe_syscall_start'.
	 * The code sequence must therefore be able to cope with this, and
	 * the syscall instruction must be the final one in the sequence.
	 */
safe_syscall_start:
	/* if signal_pending is non-zero, don't do the call */
	lwz	12, 0(11)
	cmpwi	0, 12, 0
	bne-	0f
	sc
safe_syscall_end:
	/* code path when we did execute the syscall */
	bnslr+

	/* syscall failed; return negative errno */
	neg	3, 3
	blr

	/* code path when we didn't execute the syscall */
0:	addi	3, 0, -TARGET_ERESTARTSYS
	blr
	.cfi_endproc

#if _CALL_ELF == 2
	.size	safe_syscall_base, .-safe_syscall_base
#else
	.size	safe_syscall_base, .-.L.safe_syscall_base
	.size	.L.safe_syscall_base, .-.L.safe_syscall_base
#endif

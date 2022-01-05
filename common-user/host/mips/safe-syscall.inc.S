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

#include "sys/regdef.h"
#include "sys/asm.h"

        .text
        .set    nomips16
        .set    reorder

        .global safe_syscall_start
        .global safe_syscall_end
        .type   safe_syscall_start, @function
        .type   safe_syscall_end, @function

        /*
         * This is the entry point for making a system call. The calling
         * convention here is that of a C varargs function with the
         * first argument an 'int *' to the signal_pending flag, the
         * second one the system call number (as a 'long'), and all further
         * arguments being syscall arguments (also 'long').
         */

#if _MIPS_SIM == _ABIO32
/* 8 * 4 = 32 for outgoing parameters; 1 * 4 for s0 save; 1 * 4 for align. */
#define FRAME    40
#define OFS_S0   32
#else
/* 1 * 8 for s0 save; 1 * 8 for align. */
#define FRAME    16
#define OFS_S0   0
#endif


NESTED(safe_syscall_base, FRAME, ra)
        .cfi_startproc
        PTR_ADDIU sp, sp, -FRAME
        .cfi_adjust_cfa_offset FRAME
        REG_S   s0, OFS_S0(sp)
        .cfi_rel_offset s0, OFS_S0
#if _MIPS_SIM == _ABIO32
        /*
         * The syscall calling convention is nearly the same as C:
         * we enter with a0 == &signal_pending
         *               a1 == syscall number
         *               a2, a3, stack == syscall arguments
         *               and return the result in a0
         * and the syscall instruction needs
         *               v0 == syscall number
         *               a0 ... a3, stack == syscall arguments
         *               and returns the result in v0
         * Shuffle everything around appropriately.
         */
        move    s0, a0          /* signal_pending pointer */
        move    v0, a1          /* syscall number */
        move    a0, a2          /* syscall arguments */
        move    a1, a3
        lw      a2, FRAME+16(sp)
        lw      a3, FRAME+20(sp)
        lw      t4, FRAME+24(sp)
        lw      t5, FRAME+28(sp)
        lw      t6, FRAME+32(sp)
        lw      t7, FRAME+40(sp)
        sw      t4, 16(sp)
        sw      t5, 20(sp)
        sw      t6, 24(sp)
        sw      t7, 28(sp)
#else
        /*
         * The syscall calling convention is nearly the same as C:
         * we enter with a0 == &signal_pending
         *               a1 == syscall number
         *               a2 ... a7 == syscall arguments
         *               and return the result in a0
         * and the syscall instruction needs
         *               v0 == syscall number
         *               a0 ... a5 == syscall arguments
         *               and returns the result in v0
         * Shuffle everything around appropriately.
         */
        move    s0, a0          /* signal_pending pointer */
        move    v0, a1          /* syscall number */
        move    a0, a2          /* syscall arguments */
        move    a1, a3
        move    a2, a4
        move    a3, a5
        move    a4, a6
        move    a5, a7
#endif

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
        lw      t1, 0(s0)
        bnez    t1, 2f
        syscall
safe_syscall_end:

        /* code path for having successfully executed the syscall */
        REG_L   s0, OFS_S0(sp)
        PTR_ADDIU sp, sp, FRAME
        .cfi_remember_state
        .cfi_adjust_cfa_offset -FRAME
        .cfi_restore s0
        bnez    a3, 1f
        jr      ra
        .cfi_restore_state

        /* code path when we didn't execute the syscall */
2:      REG_L   s0, OFS_S0(sp)
        PTR_ADDIU sp, sp, FRAME
        .cfi_adjust_cfa_offset -FRAME
        .cfi_restore s0
        li      v0, QEMU_ERESTARTSYS

        /* code path setting errno */
        /*
         * We didn't setup GP on entry, optimistic of the syscall success.
         * We must do so now to load the address of the helper, as required
         * by the ABI, into t9.
         *
         * Note that SETUP_GPX and SETUP_GPX64 are themselves conditional,
         * so we can simply let the one that's not empty succeed.
         */
1:      USE_ALT_CP(t0)
        SETUP_GPX(t1)
        SETUP_GPX64(t0, t1)
        move    a0, v0
        PTR_LA  t9, safe_syscall_set_errno_tail
        jr      t9

        .cfi_endproc
END(safe_syscall_base)

/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

    .text
valid_tgt:
    .word 0x7f80c000 /* nop_rb */
    jumpr r2

invalid_tgt:
    nop
    jumpr r2

valid_call_tgt:
    .word 0x7f80c000 /* nop_rb */
    jumpr r31

invalid_call_tgt:
    nop
    jumpr r31

.macro set_pred reg, id
    r4 = \id
    r3 = xor(\reg, r4)
    p0 = !cmp.eq(r3, #0)
.endm

.macro set_pred_in_packet reg, id
    r4 = \id
    r3 = xor(\reg, r4)
    .word 0x75034010 /* { p0 = !cmp.eq(r3, #0) */
.endm

/*
 * r0: nonzero for a valid target, zero for an invalid target
 * r1: test case number
 */
.global run_test_case
run_test_case:
    r5 = r31
    p1 = cmp.eq(r0, #1)
    if (p1) r0 = #valid_tgt
    if (!p1) r0 = #invalid_tgt
    r2 = #return

    set_pred r1, #0
    .word 0x53e0c000 /* if (!p0) jumpr_rb:nt r0 */

    set_pred r1, #1
    .word 0x53e0d000 /* if (!p0) jumpr_rb:t r0 */

    set_pred_in_packet r1, #2
    .word 0x53e0c800 /* if (!p0.new) jumpr_rb:nt r0 } */

    set_pred_in_packet r1, #3
    .word 0x53e0d800 /* if (!p0.new) jumpr_rb:t r0 } */

    p0 = cmp.eq(r1, #4)
    .word 0x53c0c000 /* if (p0) jumpr_rb:nt r0 */

    p0 = cmp.eq(r1, #5)
    .word 0x53c0d000 /* if (p0) jumpr_rb:t r0 */

    .word 0x750140c0 /* { p0 = cmp.eq(r1, #6) */
    .word 0x53c0c800 /* if (p0.new) jumpr_rb:nt r0 } */

    .word 0x750140e0 /* { p0 = cmp.eq(r1, #7) */
    .word 0x53c0d800 /* if (p0.new) jumpr_rb:t r0 } */

    p0 = cmp.eq(r1, #9)
    if (p0) jump callr_case
    p0 = cmp.eq(r1, #10)
    if (p0) jump callrh_case
    p0 = cmp.eq(r1, #11)
    if (p0) jump callrt_case
    p0 = cmp.eq(r1, #12)
    if (p0) jump callrf_case

    .word 0x5200c000 /* jumpr_rb r0 */

callr_case:
    if (p1) r0 = #valid_call_tgt
    if (!p1) r0 = #invalid_call_tgt
    .word 0x5020c000 /* callr_rb r0 */
    jump return

callrh_case:
    if (p1) r0 = #valid_call_tgt
    if (!p1) r0 = #invalid_call_tgt
    .word 0x5040c000 /* callrh_rb r0 */
    jump return

callrt_case:
    if (p1) r0 = #valid_call_tgt
    if (!p1) r0 = #invalid_call_tgt
    p0 = cmp.eq(r0, r0)
    .word 0x5180c000 /* if (p0) callr_rb r0 */
    jump return

callrf_case:
    if (p1) r0 = #valid_call_tgt
    if (!p1) r0 = #invalid_call_tgt
    p0 = cmp.eq(r0, #0)
    .word 0x51a0c000 /* if (!p0) callr_rb r0 */
    jump return

return:
    jumpr r5

/* jumpr_rb r31 is a return, not a restricted branch. */
.global run_jumpr_rb_lr_test
run_jumpr_rb_lr_test:
    .word 0x521fc000 /* jumpr_rb r31 */

/* A target marker and restricted branch in the same packet. */
.global run_jumpr_rb_nop_rb_test
run_jumpr_rb_nop_rb_test:
    r0 = #1f
    .word 0x7f804000 /* { nop_rb */
    .word 0x5200c000 /* jumpr_rb r0 } */
1:
    nop

/* A trap0(#0xdb) landing pad: the syscall must not leave CFI armed. */
.global run_trap0_target_test
run_trap0_target_test:
    r5 = r31
    r6 = #172 /* __NR_getpid */
    r0 = #1f
    .word 0x5200c000 /* jumpr_rb r0 */
1:
    trap0(#0xdb)
    nop
    jumpr r5

/*
 * callr_rb r31 remains restricted: it must branch to a landing pad even
 * though it overwrites r31 with its return address.
 */
.global run_callr_rb_lr_test
run_callr_rb_lr_test:
    r2 = r31
    p0 = cmp.eq(r0, #1)
    if (p0) r31 = #valid_callr_rb_lr_tgt
    if (!p0) r31 = #invalid_callr_rb_lr_tgt
    .word 0x503fc000 /* callr_rb r31 */
    jumpr r2

valid_callr_rb_lr_tgt:
    .word 0x7f80c000 /* nop_rb */
    jumpr r31

invalid_callr_rb_lr_tgt:
    nop
    jumpr r31

/*
 * Enter each restricted allocframe instruction with CFI active.  The
 * following ordinary instructions show that the restricted target reset it.
 */
.global run_allocframe_test
run_allocframe_test:
    r0 = #allocframe_rb_reg
    r2 = #allocframe_rb_duplex
    .word 0x5200c000 /* jumpr_rb r0 */

allocframe_rb_reg:
    .word 0xa09de000 /* allocframe_rb(r29, #0):raw */
    deallocframe
    nop
    jumpr r2

allocframe_rb_duplex:
    .word 0x70003e00 /* r0 = r0; allocframe_rb(#0) */
    deallocframe
    nop
    jumpr r31

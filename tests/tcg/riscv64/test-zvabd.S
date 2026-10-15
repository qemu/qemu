/*
 * Test the Zvabd vector absolute-difference instructions and the vabs.v
 * pseudoinstruction.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

	.option arch, +v
	.option norvc

	.text

	.global _start
_start:
	/* Enable the vector unit (mstatus.VS = Initial). */
	li	t0, 1 << 9
	csrs	mstatus, t0

	/* Route synchronous traps to trap_handler (mtvec direct mode). */
	la	t0, trap_handler
	csrw	mtvec, t0

	/* Run each test function. */
	call	test_vabs_v
	bnez	a0, _exit
	call	test_vabs_v_mask
	bnez	a0, _exit

	call	test_vabd_vv
	bnez	a0, _exit
	call	test_vabd_vv_mask
	bnez	a0, _exit

	call	test_vabd_vx
	bnez	a0, _exit
	call	test_vabd_vx_mask
	bnez	a0, _exit

	call	test_vabdu_vv
	bnez	a0, _exit
	call	test_vabdu_vv_mask
	bnez	a0, _exit

	call	test_vabdu_vx
	bnez	a0, _exit
	call	test_vabdu_vx_mask
	bnez	a0, _exit

	call	test_vwabda_vv
	bnez	a0, _exit
	call	test_vwabda_vv_mask
	bnez	a0, _exit

	call	test_vwabda_vx
	bnez	a0, _exit
	call	test_vwabda_vx_mask
	bnez	a0, _exit

	call	test_vwabdau_vv
	bnez	a0, _exit
	call	test_vwabdau_vv_mask
	bnez	a0, _exit

	call	test_vwabdau_vx
	bnez	a0, _exit
	call	test_vwabdau_vx_mask
	bnez	a0, _exit

	j	_exit

test_vabs_v:
	vsetivli zero, 2, e8, m1, ta, ma

	/* Load .arr_neg array in v1 */
	la     t0,.arr_neg
	vle8.v v1,0(t0)

	/* vabs.v v2, v1 (vabd.vx v2, v1, x0) raw opcode */
	.insn r 0x57, 0x6, 0x2b, x2, x0, x1

	/* Load .arr_pos in v3 */
	la      t1,.arr_pos
	vle8.v  v3,0(t1)

	/* Compare v2 and v3 into v0 */
	vmsne.vv  v0,v2,v3
	vcpop.m a0,v0
	snez    a0,a0

	ret

test_vabs_v_mask:
	vsetivli zero, 2, e8, m1, ta, mu

	/* Load .arr_neg array in v1 */
	la      t0, .arr_neg
	vle8.v  v1, 0(t0)

	/* Zero v2 using .arr_zero */
	la     t0,.arr_zero
	vle8.v v2,0(t0)

	/* Load .v0_mask array in v0 */
	la     t1,.v0_mask
	vle8.v v0,0(t1)

	/* vabs.v v2, v1, v0.t raw opcode */
	.insn r 0x57, 0x6, 0x2a, x2, x0, x1

	/* Load .arr_masked in v3 */
	la      t0,.arr_masked
	vle8.v  v3,0(t0)

	/* Compare v2 and v3 into v0 */
	vmsne.vv  v0,v2,v3
	vcpop.m a0,v0
	snez    a0,a0

	ret

test_vabd_vv:
	/*
	 * Test signed vector-vector absolute difference:
	 * abs({-5, -7} - {5, 7}) = {10, 14}.
	 */
	vsetivli zero, 2, e8, m1, ta, mu

	/* Load the two signed source vectors into v1 and v3. */
	la      t0, .arr_neg
	vle8.v  v1, 0(t0)
	la      t0, .arr_pos
	vle8.v  v3, 0(t0)

	/* Execute vabd.vv v2, v1, v3. */
	.insn r 0x57, 0x2, 0x2b, x2, x3, x1

	/* Load the expected result into v3. */
	la      t0, .expect_vabd_vv
	vle8.v  v3, 0(t0)

	/* Compare v2 and v3 into v0 */
	vmsne.vv  v0, v2, v3
	vcpop.m a0, v0
	snez    a0, a0

	ret

test_vabd_vv_mask:
	/*
	 * Test masked vabd.vv.  Element 0 remains 9 and element 1 becomes
	 * abs(-7 - 7) = 14.
	 */
	vsetivli zero, 2, e8, m1, ta, mu

	/* Load the two signed source vectors into v1 and v3. */
	la      t0, .arr_neg
	vle8.v  v1, 0(t0)
	la      t0, .arr_pos
	vle8.v  v3, 0(t0)

	/* Load original value into v2 */
	la      t0, .arr_init
	vle8.v  v2, 0(t0)

	/* Load mask value into v0 */
	la      t0, .v0_mask
	vle8.v  v0, 0(t0)

	.insn r 0x57, 0x2, 0x2a, x2, x3, x1

	/* Load the expected result into v3. */
	la      t0, .expect_vabd_vv_masked
	vle8.v  v3, 0(t0)

	vmsne.vv  v0, v2, v3
	vcpop.m a0, v0
	snez    a0, a0
	ret

test_vabd_vx:
	/*
	 * Test signed vector-scalar absolute difference:
	 * abs({-5, -7} - 3) = {8, 10}.
	 */
	vsetivli zero, 2, e8, m1, ta, mu
	la      t0, .arr_neg
	vle8.v  v1, 0(t0)
	li      a2, 3

	/* Execute vabd.vx v2, v1, a2. */
	.insn r 0x57, 0x6, 0x2b, x2, x12, x1

	/* Load the expected result into v3. */
	la      t0, .expect_vabd_vx
	vle8.v  v3, 0(t0)

	vmsne.vv  v0, v2, v3
	vcpop.m a0, v0
	snez    a0, a0
	ret

test_vabd_vx_mask:
	/*
	 * Test masked vabd.vx.  Element 0 remains 9 and element 1 becomes
	 * abs(-7 - 3) = 10.
	 */
	vsetivli zero, 2, e8, m1, ta, mu

	la      t0, .arr_neg
	vle8.v  v1, 0(t0)
	li      a2, 3

	la      t0, .arr_init
	vle8.v  v2, 0(t0)

	la      t0, .v0_mask
	vle8.v  v0, 0(t0)

	.insn r 0x57, 0x6, 0x2a, x2, x12, x1

	la      t0, .expect_vabd_vx_masked
	vle8.v  v3, 0(t0)

	vmsne.vv  v0, v2, v3
	vcpop.m a0, v0
	snez    a0, a0
	ret

test_vabdu_vv:
	/*
	 * Test unsigned vector-vector absolute difference:
	 * abs({0, 255} - {255, 0}) = {255, 255}.
	 */
	vsetivli zero, 2, e8, m1, ta, mu

	/* Load the two unsigned source vectors into v1 and v3. */
	la      t0, .arr_unsigned_lhs
	vle8.v  v1, 0(t0)
	la      t0, .arr_unsigned_rhs
	vle8.v  v3, 0(t0)

	/* Execute vabdu.vv v2, v1, v3. */
	.insn r 0x57, 0x2, 0x2d, x2, x3, x1

	/* Load the expected result into v3. */
	la      t0, .expect_vabdu_vv
	vle8.v  v3, 0(t0)

	vmsne.vv  v0, v2, v3
	vcpop.m a0, v0
	snez    a0, a0
	ret

test_vabdu_vv_mask:
	/*
	 * Test masked vabdu.vv.  Element 0 remains 9 and element 1 becomes
	 * abs(255 - 0) = 255.
	 */
	vsetivli zero, 2, e8, m1, ta, mu

	la      t0, .arr_unsigned_lhs
	vle8.v  v1, 0(t0)
	la      t0, .arr_unsigned_rhs
	vle8.v  v3, 0(t0)

	la      t0, .arr_init
	vle8.v  v2, 0(t0)

	la      t0, .v0_mask
	vle8.v  v0, 0(t0)

	.insn r 0x57, 0x2, 0x2c, x2, x3, x1

	la      t0, .expect_vabdu_vv_masked
	vle8.v  v3, 0(t0)

	vmsne.vv  v0, v2, v3
	vcpop.m a0, v0
	snez    a0, a0
	ret

test_vabdu_vx:
	/*
	 * Test unsigned vector-scalar absolute difference:
	 * abs({0, 255} - 128) = {128, 127}.
	 */
	vsetivli zero, 2, e8, m1, ta, mu

	la      t0, .arr_unsigned_lhs
	vle8.v  v1, 0(t0)
	li      a2, 128

	/* Execute vabdu.vx v2, v1, a2. */
	.insn r 0x57, 0x6, 0x2d, x2, x12, x1

	la      t0, .expect_vabdu_vx
	vle8.v  v3, 0(t0)

	vmsne.vv  v0, v2, v3
	vcpop.m a0, v0
	snez    a0, a0
	ret

test_vabdu_vx_mask:
	/*
	 * Test masked vabdu.vx.  Element 0 remains 9 and element 1 becomes
	 * abs(255 - 128) = 127.
	 */
	vsetivli zero, 2, e8, m1, ta, mu

	la      t0, .arr_unsigned_lhs
	vle8.v  v1, 0(t0)
	li      a2, 128

	la      t0, .arr_init
	vle8.v  v2, 0(t0)

	la      t0, .v0_mask
	vle8.v  v0, 0(t0)

	.insn r 0x57, 0x6, 0x2c, x2, x12, x1

	la      t0, .expect_vabdu_vx_masked
	vle8.v  v3, 0(t0)

	vmsne.vv  v0, v2, v3
	vcpop.m a0, v0
	snez    a0, a0
	ret

test_vwabda_vv:
	/*
	 * Test signed widening vector-vector absolute difference and
	 * accumulation. Start with v4 = {1000, -1000}, and the expected
	 * widened result is {1000, -1000} + abs({-5, -7} - {5, 7}) =
	 * {1010, -986}.
	 */
	/* Load the 16-bit accumulator before selecting 8-bit operands. */
	vsetivli zero, 2, e16, m2, ta, mu
	la      t0, .arr_signed_acc
	vle16.v v4, 0(t0)

	vsetivli zero, 2, e8, m1, ta, mu
	la      t0, .arr_neg
	vle8.v  v1, 0(t0)
	la      t0, .arr_pos
	vle8.v  v2, 0(t0)

	/* Execute vwabda.vv v4, v1, v2. */
	.insn r 0x57, 0x0, 0x7b, x4, x2, x1

	/* Select the widened EEW and compare v4 with the expected result. */
	vsetivli zero, 2, e16, m2, ta, mu
	la      t0, .expect_vwabda_vv
	vle16.v v6, 0(t0)

	vmsne.vv  v0, v4, v6
	vcpop.m a0, v0
	snez    a0, a0
	ret

test_vwabda_vv_mask:
	/*
	 * Test masked vwabda.vv.
	 * Start with v4 = {1000, -1000}, v0 = {0, 1}, and the expected widened
	 * result is {1000, -1000} + abs({-5, -7} - {5, 7}) = {1000, -986}.
	 */
	vsetivli zero, 2, e16, m2, ta, mu
	la      t0, .arr_signed_acc
	vle16.v v4, 0(t0)

	vsetivli zero, 2, e8, m1, ta, mu
	la      t0, .arr_neg
	vle8.v  v1, 0(t0)
	la      t0, .arr_pos
	vle8.v  v2, 0(t0)

	la      t0, .v0_mask
	vle8.v  v0, 0(t0)

	.insn r 0x57, 0x0, 0x7a, x4, x2, x1

	vsetivli zero, 2, e16, m2, ta, mu
	la      t0, .expect_vwabda_vv_masked

	vle16.v v6, 0(t0)
	vmsne.vv  v0, v4, v6
	vcpop.m a0, v0
	snez    a0, a0
	ret

test_vwabda_vx:
	/*
	 * Test signed widening vector-scalar absolute difference and
	 * accumulation. With scalar 3, v4 becomes {1000 + 8, -1000 + 10}.
	 */
	/* Load the 16-bit accumulator before selecting 8-bit operands. */
	vsetivli zero, 2, e16, m2, ta, mu
	la      t0, .arr_signed_acc
	vle16.v v4, 0(t0)

	vsetivli zero, 2, e8, m1, ta, mu
	la      t0, .arr_neg
	vle8.v  v1, 0(t0)
	li      a2, 3

	/* Execute vwabda.vx v4, v1, a2. */
	.insn r 0x57, 0x4, 0x7b, x4, x12, x1

	/* Select the widened EEW and compare v4 with the expected result. */
	vsetivli zero, 2, e16, m2, ta, mu
	la      t0, .expect_vwabda_vx
	vle16.v v6, 0(t0)

	vmsne.vv  v0, v4, v6
	vcpop.m a0, v0
	snez    a0, a0
	ret

test_vwabda_vx_mask:
	/*
	 * Test masked vwabda.vx.  Element 0 remains 1000 while element 1
	 * accumulates 10 and becomes -990.
	 */
	vsetivli zero, 2, e16, m2, ta, mu

	la      t0, .arr_signed_acc
	vle16.v v4, 0(t0)

	vsetivli zero, 2, e8, m1, ta, mu
	la      t0, .arr_neg
	vle8.v  v1, 0(t0)
	li      a2, 3

	la      t0, .v0_mask
	vle8.v  v0, 0(t0)

	.insn r 0x57, 0x4, 0x7a, x4, x12, x1

	vsetivli zero, 2, e16, m2, ta, mu
	la      t0, .expect_vwabda_vx_masked
	vle16.v v6, 0(t0)

	vmsne.vv  v0, v4, v6
	vcpop.m a0, v0
	snez    a0, a0
	ret

test_vwabdau_vv:
	/*
	 * Test unsigned widening vector-vector absolute difference and
	 * accumulation. Start with v4 = {1000, 2000}, adding {255, 255}
	 * produces {1255, 2255}.
	 */
	/* Load the unsigned 16-bit accumulator and 8-bit source vectors. */
	vsetivli zero, 2, e16, m2, ta, mu

	la      t0, .arr_unsigned_acc
	vle16.v v4, 0(t0)
	vsetivli zero, 2, e8, m1, ta, mu

	la      t0, .arr_unsigned_lhs
	vle8.v  v1, 0(t0)
	la      t0, .arr_unsigned_rhs
	vle8.v  v2, 0(t0)

	/* Execute vwabdau.vv v4, v1, v2. */
	.insn r 0x57, 0x0, 0x7d, x4, x2, x1

	/* Select the widened EEW and compare v4 with the expected result. */
	vsetivli zero, 2, e16, m2, ta, mu
	la      t0, .expect_vwabdau_vv
	vle16.v v6, 0(t0)

	vmsne.vv  v0, v4, v6
	vcpop.m a0, v0
	snez    a0, a0
	ret

test_vwabdau_vv_mask:
	/*
	 * Test masked vwabdau.vv.  Element 0 remains 1000 while element 1
	 * accumulates 255 and becomes 2255.
	 */
	vsetivli zero, 2, e16, m2, ta, mu

	la      t0, .arr_unsigned_acc
	vle16.v v4, 0(t0)

	vsetivli zero, 2, e8, m1, ta, mu
	la      t0, .arr_unsigned_lhs
	vle8.v  v1, 0(t0)
	la      t0, .arr_unsigned_rhs
	vle8.v  v2, 0(t0)

	la      t0, .v0_mask
	vle8.v  v0, 0(t0)

	.insn r 0x57, 0x0, 0x7c, x4, x2, x1

	vsetivli zero, 2, e16, m2, ta, mu
	la      t0, .expect_vwabdau_vv_masked
	vle16.v v6, 0(t0)

	vmsne.vv  v0, v4, v6
	vcpop.m a0, v0
	snez    a0, a0
	ret

test_vwabdau_vx:
	/*
	 * Test unsigned widening vector-scalar absolute difference and
	 * accumulation. With scalar 128, v4 becomes {1000 + 128, 2000 + 127}.
	 */
	/* Load the unsigned 16-bit accumulator and 8-bit source vector. */
	vsetivli zero, 2, e16, m2, ta, mu

	la      t0, .arr_unsigned_acc
	vle16.v v4, 0(t0)

	vsetivli zero, 2, e8, m1, ta, mu
	la      t0, .arr_unsigned_lhs
	vle8.v  v1, 0(t0)
	li      a2, 128

	/* Execute vwabdau.vx v4, v1, a2. */
	.insn r 0x57, 0x4, 0x7d, x4, x12, x1

	/* Select the widened EEW and compare v4 with the expected result. */
	vsetivli zero, 2, e16, m2, ta, mu
	la      t0, .expect_vwabdau_vx
	vle16.v v6, 0(t0)

	vmsne.vv  v0, v4, v6
	vcpop.m a0, v0
	snez    a0, a0
	ret

test_vwabdau_vx_mask:
	/*
	 * Test masked vwabdau.vx.  Element 0 remains 1000 while element 1
	 * accumulates 127 and becomes 2127.
	 */
	vsetivli zero, 2, e16, m2, ta, mu

	la      t0, .arr_unsigned_acc
	vle16.v v4, 0(t0)

	vsetivli zero, 2, e8, m1, ta, mu
	la      t0, .arr_unsigned_lhs
	vle8.v  v1, 0(t0)
	li      a2, 128

	la      t0, .v0_mask
	vle8.v  v0, 0(t0)

	.insn r 0x57, 0x4, 0x7c, x4, x12, x1

	vsetivli zero, 2, e16, m2, ta, mu
	la      t0, .expect_vwabdau_vx_masked
	vle16.v v6, 0(t0)

	vmsne.vv  v0, v4, v6
	vcpop.m a0, v0
	snez    a0, a0
	ret


/* Exit through the semihosting SYS_EXIT_EXTENDED call with a0 as the code. */
_exit:
	la	a1, semiargs
	li	t0, 0x20026	/* ADP_Stopped_ApplicationExit */
	sd	t0, 0(a1)
	sd	a0, 8(a1)
	li	a0, 0x20	/* TARGET_SYS_EXIT_EXTENDED */
	.balign	16
	slli	zero, zero, 0x1f
	ebreak
	srai	zero, zero, 0x7
	j	.

	.balign	4
trap_handler:
	csrr	t4, mcause
	la	t5, trap_mcause
	sd	t4, 0(t5)
	csrr	t4, mtval
	la	t5, trap_mtval
	sd	t4, 0(t5)
	li	a0, 1
	j	_exit

	.data
	.balign	8
semiargs:
	.space	16
trap_mcause:
	.space	8
trap_mtval:
	.space	8
.arr_neg:
	.byte -5, -7
.arr_pos:
	.byte 5, 7
.arr_zero:
	.byte 0, 0
.arr_init:
	.byte 9, 9
.v0_mask:
	.byte 0b10
.arr_masked:
	.byte 0, 7

/* Unsigned input vectors used by vabdu and vwabdau. */
.arr_unsigned_lhs:
	.byte 0, 255
.arr_unsigned_rhs:
	.byte 255, 0
	.balign 2

/* Nonzero accumulators verify the add part of widening operations. */
.arr_signed_acc:
	.half 1000, -1000
.arr_unsigned_acc:
	.half 1000, 2000

/* Expected results for the unmasked and masked forms. */
.expect_vabd_vv:
	.byte 10, 14
.expect_vabd_vv_masked:
	.byte 9, 14
.expect_vabd_vx:
	.byte 8, 10
.expect_vabd_vx_masked:
	.byte 9, 10
.expect_vabdu_vv:
	.byte 255, 255
.expect_vabdu_vv_masked:
	.byte 9, 255
.expect_vabdu_vx:
	.byte 128, 127
.expect_vabdu_vx_masked:
	.byte 9, 127
.expect_vwabda_vv:
	.half 1010, -986
.expect_vwabda_vv_masked:
	.half 1000, -986
.expect_vwabda_vx:
	.half 1008, -990
.expect_vwabda_vx_masked:
	.half 1000, -990
.expect_vwabdau_vv:
	.half 1255, 2255
.expect_vwabdau_vv_masked:
	.half 1000, 2255
.expect_vwabdau_vx:
	.half 1128, 2127
.expect_vwabdau_vx_masked:
	.half 1000, 2127

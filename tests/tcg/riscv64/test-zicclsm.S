/*
 * Test the Zicclsm extension (misaligned load/store support).
 *
 * This single source is assembled twice:
 *   - test-zicclsm      : run on a CPU with zicclsm=true. Every misaligned
 *                         scalar integer, floating-point and vector
 *                         load/store must complete WITHOUT raising a trap.
 *   - test-zicclsm-off  : built with -DZICCLSM_DISABLED and run on a CPU with
 *                         zicclsm=false. Every misaligned access must raise a
 *                         misaligned load/store exception, with the correct
 *                         mcause and mtval.
 *
 * Zicclsm governs all regular scalar loads/stores (integer and F/D/Zfh
 * floating-point) as well as vector element loads/stores.  Floating-point
 * loads/stores (flh/flw/fld, fsh/fsw/fsd) are therefore exercised here.
 *
 * Atomic (A/Zacas/...) accesses are intentionally excluded: they always
 * require natural alignment regardless of Zicclsm.  Likewise cm.push/cm.pop
 * (Zcmp) are excluded, as they are not regular loads/stores.
 *
 * Register conventions (persist across the whole test; the trap handler only
 * clobbers t0-t4):
 *   s1 = expected mcause for the pending misaligned access
 *   s2 = expected mtval (the misaligned address)
 *   s3 = trap counter (incremented by the handler)
 *   s4 = base address of the aligned data buffer
 *   s5 = snapshot of s3 taken before an access, used to check the delta
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

	.option	norelax
	.option	norvc

/* RISC-V exception causes (see target/riscv/cpu_bits.h). */
#define CAUSE_LOAD_MISALIGNED	0x4
#define CAUSE_STORE_MISALIGNED	0x6

/*
 * EXPECT sets up the expectation for the access that immediately follows and
 * snapshots the trap counter.
 *   \cause = expected mcause if the access traps
 *   \off   = byte offset from the buffer base; also the expected mtval
 */
	.macro	EXPECT cause, off
	li	s1, \cause
	addi	s2, s4, \off
	mv	s5, s3
	.endm

/*
 * CHECK validates the outcome of the preceding access.
 *   - When Zicclsm is disabled, exactly one trap must have fired.
 *   - When Zicclsm is enabled, no trap must have fired.
 */
	.macro	CHECK
#ifdef ZICCLSM_DISABLED
	addi	s5, s5, 1
	bne	s3, s5, fail
#else
	bne	s3, s5, fail
#endif
	.endm

/* In the enabled case, also verify the value returned by scalar loads. */
	.macro	CHECK_VALUE value
#ifndef ZICCLSM_DISABLED
	li	t0, \value
	bne	a2, t0, fail
#endif
	.endm

/* Verify bytes written by an enabled scalar store. */
	.macro	CHECK_BYTE off, value
#ifndef ZICCLSM_DISABLED
	lbu	t0, \off(s4)
	li	t1, \value
	bne	t0, t1, fail
#endif
	.endm

	.text
	.global	_start
_start:
	/* Install the trap handler. */
	lla	t0, trap
	csrw	mtvec, t0

	/* Enable the FP (FS) and Vector (VS) unit state so F/D/V instructions
	 * do not trap as illegal. 0x6600 = FS[14:13]=11 | VS[10:9]=11. */
	li	t0, 0x6600
	csrs	mstatus, t0

	/* Initialise persistent state. */
	li	s3, 0			/* trap counter */
	lla	s4, buf			/* aligned buffer base */

	/*
	 * ---- Scalar integer loads ----
	 * lh/lhu need 2-byte alignment; lw/lwu 4-byte; ld 8-byte.
	 */
	EXPECT	CAUSE_LOAD_MISALIGNED, 1
	lh	a2, 1(s4)
	CHECK
	CHECK_VALUE 0x2211
	EXPECT	CAUSE_LOAD_MISALIGNED, 1
	lhu	a2, 1(s4)
	CHECK
	CHECK_VALUE 0x2211

	EXPECT	CAUSE_LOAD_MISALIGNED, 1
	lw	a2, 1(s4)
	CHECK
	CHECK_VALUE 0x44332211
	EXPECT	CAUSE_LOAD_MISALIGNED, 3
	lw	a2, 3(s4)
	CHECK
	CHECK_VALUE 0x66554433
	EXPECT	CAUSE_LOAD_MISALIGNED, 1
	lwu	a2, 1(s4)
	CHECK
	CHECK_VALUE 0x44332211

	EXPECT	CAUSE_LOAD_MISALIGNED, 1
	ld	a2, 1(s4)
	CHECK
	CHECK_VALUE 0x8877665544332211
	EXPECT	CAUSE_LOAD_MISALIGNED, 3
	ld	a2, 3(s4)
	CHECK
	CHECK_VALUE 0xaa99887766554433
	EXPECT	CAUSE_LOAD_MISALIGNED, 7
	ld	a2, 7(s4)
	CHECK
	CHECK_VALUE 0xeeddccbbaa998877

	/*
	 * ---- Scalar integer stores ----
	 */
	li	t6, 0x1122334455667788
	EXPECT	CAUSE_STORE_MISALIGNED, 1
	sh	t6, 1(s4)
	CHECK
	CHECK_BYTE 1, 0x88
	CHECK_BYTE 2, 0x77

	EXPECT	CAUSE_STORE_MISALIGNED, 1
	sw	t6, 1(s4)
	CHECK
	CHECK_BYTE 1, 0x88
	CHECK_BYTE 2, 0x77
	CHECK_BYTE 3, 0x66
	CHECK_BYTE 4, 0x55
	EXPECT	CAUSE_STORE_MISALIGNED, 3
	sw	t6, 3(s4)
	CHECK
	CHECK_BYTE 3, 0x88
	CHECK_BYTE 4, 0x77
	CHECK_BYTE 5, 0x66
	CHECK_BYTE 6, 0x55

	EXPECT	CAUSE_STORE_MISALIGNED, 1
	sd	t6, 1(s4)
	CHECK
	CHECK_BYTE 1, 0x88
	CHECK_BYTE 2, 0x77
	CHECK_BYTE 3, 0x66
	CHECK_BYTE 4, 0x55
	CHECK_BYTE 5, 0x44
	CHECK_BYTE 6, 0x33
	CHECK_BYTE 7, 0x22
	CHECK_BYTE 8, 0x11
	EXPECT	CAUSE_STORE_MISALIGNED, 7
	sd	t6, 7(s4)
	CHECK
	CHECK_BYTE 7, 0x88
	CHECK_BYTE 8, 0x77
	CHECK_BYTE 9, 0x66
	CHECK_BYTE 10, 0x55
	CHECK_BYTE 11, 0x44
	CHECK_BYTE 12, 0x33
	CHECK_BYTE 13, 0x22
	CHECK_BYTE 14, 0x11

	/*
	 * ---- Floating-point loads ----
	 * flh needs 2-byte alignment; flw 4-byte; fld 8-byte.  Their alignment
	 * is governed by Zicclsm just like the scalar integer forms.
	 */
	EXPECT	CAUSE_LOAD_MISALIGNED, 1
	flh	fa0, 1(s4)
	CHECK

	EXPECT	CAUSE_LOAD_MISALIGNED, 1
	flw	fa0, 1(s4)
	CHECK
	EXPECT	CAUSE_LOAD_MISALIGNED, 3
	flw	fa0, 3(s4)
	CHECK

	EXPECT	CAUSE_LOAD_MISALIGNED, 1
	fld	fa0, 1(s4)
	CHECK
	EXPECT	CAUSE_LOAD_MISALIGNED, 7
	fld	fa0, 7(s4)
	CHECK

	/*
	 * ---- Floating-point stores ----
	 */
	EXPECT	CAUSE_STORE_MISALIGNED, 1
	fsh	fa0, 1(s4)
	CHECK

	EXPECT	CAUSE_STORE_MISALIGNED, 1
	fsw	fa0, 1(s4)
	CHECK
	EXPECT	CAUSE_STORE_MISALIGNED, 3
	fsw	fa0, 3(s4)
	CHECK

	EXPECT	CAUSE_STORE_MISALIGNED, 1
	fsd	fa0, 1(s4)
	CHECK
	EXPECT	CAUSE_STORE_MISALIGNED, 7
	fsd	fa0, 7(s4)
	CHECK

	/*
	 * ---- Vector unit-stride loads / stores ----
	 * A base address that is not aligned to the element size (SEW) is
	 * misaligned for the first element access.
	 */
	vsetvli	t1, x0, e16, m1, ta, ma
	EXPECT	CAUSE_LOAD_MISALIGNED, 1
	addi	a0, s4, 1
	vle16.v	v0, (a0)
	CHECK
	EXPECT	CAUSE_STORE_MISALIGNED, 1
	addi	a0, s4, 1
	vse16.v	v0, (a0)
	CHECK

	vsetvli	t1, x0, e32, m1, ta, ma
	EXPECT	CAUSE_LOAD_MISALIGNED, 1
	addi	a0, s4, 1
	vle32.v	v0, (a0)
	CHECK
	EXPECT	CAUSE_STORE_MISALIGNED, 3
	addi	a0, s4, 3
	vse32.v	v0, (a0)
	CHECK

	vsetvli	t1, x0, e64, m1, ta, ma
	EXPECT	CAUSE_LOAD_MISALIGNED, 1
	addi	a0, s4, 1
	vle64.v	v0, (a0)
	CHECK
	EXPECT	CAUSE_STORE_MISALIGNED, 7
	addi	a0, s4, 7
	vse64.v	v0, (a0)
	CHECK

	/*
	 * ---- Vector strided loads / stores ----
	 */
	vsetvli	t1, x0, e32, m1, ta, ma
	li	a1, 8			/* stride in bytes */
	EXPECT	CAUSE_LOAD_MISALIGNED, 1
	addi	a0, s4, 1
	vlse32.v	v0, (a0), a1
	CHECK
	EXPECT	CAUSE_STORE_MISALIGNED, 1
	addi	a0, s4, 1
	vsse32.v	v0, (a0), a1
	CHECK

	/* ---- Vector indexed loads / stores ---- */
	/*
	 * Zero indices keep the first element at the deliberately misaligned base.
	 */
	vmv.v.i	v1, 0
	EXPECT	CAUSE_LOAD_MISALIGNED, 1
	addi	a0, s4, 1
	vluxei32.v	v0, (a0), v1
	CHECK
	EXPECT	CAUSE_STORE_MISALIGNED, 1
	addi	a0, s4, 1
	vsuxei32.v	v0, (a0), v1
	CHECK

	/* ---- Vector segmented loads / stores ---- */
	EXPECT	CAUSE_LOAD_MISALIGNED, 1
	addi	a0, s4, 1
	vlseg2e32.v	v0, (a0)
	CHECK
	EXPECT	CAUSE_STORE_MISALIGNED, 1
	addi	a0, s4, 1
	vsseg2e32.v	v0, (a0)
	CHECK

	/*
	 * ---- Vector whole-register load ----
	 * Only the whole-register *load* forms carry an element width
	 * (vl1re32.v => EEW=32), so only they enforce alignment when Zicclsm is
	 * off.  The whole-register store form (vs1r.v) is defined as EEW=8
	 * (byte granular) and therefore never faults on misalignment, so it is
	 * not exercised here.
	 */
	EXPECT	CAUSE_LOAD_MISALIGNED, 1
	addi	a0, s4, 1
	vl1re32.v	v1, (a0)
	CHECK

	/* Success. */
	li	a0, 0
	j	_exit

	/*
	 * Trap handler: validate mcause and mtval against the expectation, bump
	 * the trap counter, then skip past the faulting instruction.  The
	 * instruction length is decoded from its low two bits (0b11 => 4 bytes,
	 * otherwise a 2-byte compressed instruction).
	 */
	.balign	4
trap:
	csrr	t0, mcause
	bne	t0, s1, fail
	csrr	t1, mtval
	bne	t1, s2, fail
	addi	s3, s3, 1

	csrr	t0, mepc
	lhu	t2, 0(t0)
	andi	t3, t2, 3
	li	t4, 3
	bne	t3, t4, 1f
	addi	t0, t0, 4		/* 32-bit instruction */
	j	2f
1:
	addi	t0, t0, 2		/* 16-bit compressed instruction */
2:
	csrw	mepc, t0
	mret

fail:
	li	a0, 1
_exit:
	lla	a1, semiargs
	li	t0, 0x20026		/* ADP_Stopped_ApplicationExit */
	sd	t0, 0(a1)
	sd	a0, 8(a1)
	li	a0, 0x20		/* TARGET_SYS_EXIT_EXTENDED */
	.balign	16
	slli	zero, zero, 0x1f
	ebreak
	srai	zero, zero, 0x7
	j	.

	.data
	.balign	16
semiargs:
	.space	16
	.balign	64
buf:
	.byte	0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77
	.byte	0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff
	.space	240

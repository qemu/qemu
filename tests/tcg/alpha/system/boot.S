/*
 * Minimal Alpha system boot code.
 *
 * Copyright Linaro Ltd 2019
 */

	.set	noat
	.set	nomacro
	.arch	ev6
	.text

.macro load_pci_io reg
	/* For typhoon, this is
	 *   0xfffffc0000000000  -- kseg identity map
	 * +      0x10000000000  -- typhoon pio base
	 * +        0x1fc000000  -- typhoon pchip0 pci base
	 * = 0xfffffd01fc000000
	 */
	ldah	\reg, -3		/* ff..fd0000 */
	lda	\reg, 0x1fc(\reg)	/* ff..fd01fc */
	sll	\reg, 24, \reg
.endm

#define com1Rbr 0x3f8
#define com1Thr 0x3f8
#define com1Ier 0x3f9
#define com1Iir 0x3fa
#define com1Lcr 0x3fb
#define com1Mcr 0x3fc
#define com1Lsr 0x3fd
#define com1Msr 0x3fe
#define com1Scr 0x3ff
#define com1Dll 0x3f8
#define com1Dlm 0x3f9

#define PAL_halt    0
#define PAL_wrent  52
#define PAL_wrkgp  55

	.text
	.p2align 4
	.globl	_start
	.ent	_start
_start:
	br	$gp, .+4
	ldah	$gp, 0($gp)		!gpdisp!1
	lda	$gp, 0($gp)		!gpdisp!1

	ldah	$sp, $stack_end($gp)	!gprelhigh
	lda	$sp, $stack_end($sp)	!gprellow

	/* Install kernel gp for exception handlers.  */
	mov	$gp, $16
	call_pal PAL_wrkgp

	/* Install exception handlers.  */
	ldah	$16, entInt($gp)	!gprelhigh
	lda	$16, entInt($16)	!gprellow
	lda	$17, 0
	call_pal PAL_wrent

	ldah	$16, entArith($gp)	!gprelhigh
	lda	$16, entArith($16)	!gprellow
	lda	$17, 1
	call_pal PAL_wrent

	ldah	$16, entMM($gp)		!gprelhigh
	lda	$16, entMM($16)		!gprellow
	lda	$17, 2
	call_pal PAL_wrent

	ldah	$16, entIF($gp)		!gprelhigh
	lda	$16, entIF($16)		!gprellow
	lda	$17, 3
	call_pal PAL_wrent

	ldah	$16, entUna($gp)	!gprelhigh
	lda	$16, entUna($16)	!gprellow
	lda	$17, 4
	call_pal PAL_wrent

	ldah	$16, entSys($gp)	!gprelhigh
	lda	$16, entSys($16)	!gprellow
	lda	$17, 5
	call_pal PAL_wrent

	/*
	 * Initialize COM1.
	 */
	load_pci_io $1
	lda	$2, 0x87		/* outb(0x87, com1Lcr); */
	stb	$2, com1Lcr($1)
	stb	$31, com1Dlm($1)	/* outb(0, com1Dlm); */
	lda	$2, 3			/* baudconst 3 => 56000 */
	stb	$2, com1Dll($1)		/* outb(baudconst, com1Dll); */
	lda	$2, 0x07
	stb	$2, com1Lcr($1)		/* outb(0x07, com1Lcr) */
	lda	$2, 0x0f
	stb	$2, com1Mcr($1)		/* outb(0x0f, com1Mcr) */

	bsr	$26, main		!samegp

	/* fall through to _exit */
	.end	_start

	.globl	_exit
	.ent	_exit
_exit:
	.frame	$sp, 0, $26, 0
	.prologue 0

	/* We cannot return an error code.  */
	call_pal PAL_halt
	.end	_exit

/*
 * We have received an exception that we don't handle.  Log and exit.
 */
	.ent	log_exit
log_exit:
entInt:
entArith:
entMM:
entIF:
entUna:
entSys:
	ldah	$16, $errormsg($gp)	!gprelhigh
	lda	$16, $errormsg($16)	!gprellow
	bsr	$26, __sys_outs		!samegp
	bsr	$26, _exit		!samegp
	.end	log_exit

	.section .rodata
$errormsg:
	.string "Terminated by exception.\n"
	.previous

	/*
	 * Helper Functions
	 */

	/* Output a single character to serial port */
	.global __sys_outc
	.ent	__sys_outc
__sys_outc:
	.frame	$sp, 0, $26, 0
	.prologue 0

	load_pci_io $1

	/* 
	 * while ((inb(com1Lsr) & 0x20) == 0)
	 *       continue;
	 */
1:	ldbu	$0, com1Lsr($1)
	and	$0, 0x20, $0
	beq	$0, 1b

	/* outb(c, com1Thr); */
	stb	$16, com1Thr($1)
	ret
	.end	__sys_outc

	/* Output a nul-terminated string to serial port */
	.global	__sys_outs
	.ent	__sys_outs
__sys_outs:
	.frame	$sp, 0, $26, 0
	.prologue 0

	load_pci_io $1

	ldbu	$2, 0($16)
	beq	$2, 9f

	/* 
	 * while ((inb(com1Lsr) & 0x20) == 0)
	 *       continue;
	 */
1:	ldbu	$0, com1Lsr($1)
	and	$0, 0x20, $0
	beq	$0, 1b

	/* outb(c, com1Thr); */
	stb	$2, com1Thr($1)

	addq	$16, 1, $16
	ldbu	$2, 0($16)
	bne	$2, 1b

9:	ret
	.end	__sys_outs

/*
 * Division routines that are normally in libc.
 *
 * These do not follow the C calling convention.  Arguments are in $24+$25,
 * the result is in $27.  Register $28 may be clobbered; everything else
 * must be saved.
 *
 * We store the remainder in $28, so that we can share code.
 *
 * We do not signal divide by zero.
 */

/*
 * Unsigned 64-bit division.
 */

	.globl	__divqu
	.ent	__divqu
__divqu:
	.frame	$sp, 48, $23
	subq	$sp, 48, $sp
	stq	$0, 0($sp)
	stq	$1, 8($sp)
	stq	$2, 16($sp)
	stq	$3, 24($sp)
	stq	$4, 32($sp)
	.prologue 0

#define mask     $0
#define divisor  $1
#define compare  $2
#define tmp1     $3
#define tmp2     $4
#define quotient $27
#define modulus  $28

	mov	$24, modulus
	mov	$25, divisor
	mov	$31, quotient
	mov	1, mask
	beq	$25, 9f

	/* Shift left until divisor >= modulus.  */
1:	cmpult	divisor, modulus, compare
	blt	divisor, 2f
	addq	divisor, divisor, divisor
	addq	mask, mask, mask
	bne	compare, 1b

2:	addq	quotient, mask, tmp2
	srl	mask, 1, mask
	cmpule	divisor, modulus, compare
	subq	modulus, divisor, tmp1
	cmovne	compare, tmp2, quotient
	srl	divisor, 1, divisor
	cmovne	compare, tmp1, modulus
	bne	mask, 2b

9:	ldq	$0, 0($sp)
	ldq	$1, 8($sp)
	ldq	$2, 16($sp)
	ldq	$3, 24($sp)
	ldq	$4, 32($sp)
	addq	$sp, 48, $sp
	ret	$31, ($23), 1

#undef mask
#undef divisor
#undef compare
#undef tmp1
#undef tmp2
#undef quotient
#undef modulus

	.end	__divqu

/*
 * Unsigned 64-bit remainder.
 * Note that __divqu above leaves the result in $28.
 */

	.globl	__remqu
	.ent	__remqu
__remqu:
	.frame	$sp, 16, $23
	subq	$sp, 16, $sp
	stq	$23, 0($sp)
	.prologue 0

	bsr	$23, __divqu

	ldq	$23, 0($sp)
	mov	$28, $27
	addq	$sp, 16, $sp
	ret	$31, ($23), 1
	.end	__remqu

/*
 * Signed 64-bit division.
 */

	.globl	__divqs
	.ent	__divqs
__divqs:
	.prologue 0

	/* Common case: both arguments are positive.  */
	bis	$24, $25, $28
	bge	$28, __divqu

	/* At least one argument is negative.  */
	subq	$sp, 32, $sp
	stq	$23, 0($sp)
	stq	$24, 8($sp)
	stq	$25, 16($sp)

	/* Compute absolute values.  */
	subq	$31, $24, $28
	cmovlt	$24, $28, $24
	subq	$31, $25, $28
	cmovlt	$25, $28, $25

	bsr	$23, __divqu

	ldq	$24, 8($sp)
	ldq	$25, 16($sp)

	/* -a / b = a / -b = -(a / b) */
	subq	$31, $27, $23
	xor	$24, $25, $28
	cmovlt	$28, $23, $27

	ldq	$23, 0($sp)
	addq	$sp, 32, $sp
	ret	$31, ($23), 1
	.end	__divqs

/*
 * Signed 64-bit remainder.
 */

	.globl	__remqs
	.ent	__remqs
__remqs:
	.prologue 0

	/* Common case: both arguments are positive.  */
	bis	$24, $25, $28
	bge	$28, __remqu

	/* At least one argument is negative.  */
	subq	$sp, 32, $sp
	stq	$23, 0($sp)
	stq	$24, 8($sp)
	stq	$25, 16($sp)

	/* Compute absolute values.  */
	subq	$31, $24, $28
	cmovlt	$24, $28, $24
	subq	$31, $25, $28
	cmovlt	$25, $28, $25

	bsr	$23, __divqu

	ldq	$23, 0($sp)
	ldq	$24, 8($sp)
	ldq	$25, 16($sp)

	/* -a % b = -(a % b); a % -b = a % b.  */
	subq	$31, $28, $27
	cmovge	$24, $28, $27

	addq	$sp, 32, $sp
	ret	$31, ($23), 1
	.end	__remqs

/*
 * Unsigned 32-bit division.
 */

	.globl	__divlu
	.ent	__divlu
__divlu:
	.frame	$sp, 32, $23
	subq	$sp, 32, $sp
	stq	$23, 0($sp)
	stq	$24, 8($sp)
	stq	$25, 16($sp)
	.prologue 0

	/* Zero extend and use the 64-bit routine.  */
	zap	$24, 0xf0, $24
	zap	$25, 0xf0, $25
	bsr	$23, __divqu

	addl	$27, 0, $27
	ldq	$23, 0($sp)
	ldq	$24, 8($sp)
	ldq	$25, 16($sp)
	addq	$sp, 32, $sp
	ret	$31, ($23), 1
	.end	__divlu

/*
 * Unsigned 32-bit remainder.
 */

	.globl	__remlu
	.ent	__remlu
__remlu:
	.frame	$sp, 32, $23
	subq	$sp, 32, $sp
	stq	$23, 0($sp)
	stq	$24, 8($sp)
	stq	$25, 16($sp)
	.prologue 0

	/* Zero extend and use the 64-bit routine.  */
	zap	$24, 0xf0, $24
	zap	$25, 0xf0, $25
	bsr	$23, __divqu

	/* Recall that the remainder is returned in $28.  */
	addl	$28, 0, $27
	ldq	$23, 0($sp)
	ldq	$24, 8($sp)
	ldq	$25, 16($sp)
	addq	$sp, 32, $sp
	ret	$31, ($23), 1
	.end	__remlu

/*
 * Signed 32-bit division.
 */

	.globl	__divls
	.ent	__divls
__divls:
	.frame	$sp, 32, $23
	subq	$sp, 32, $sp
	stq	$23, 0($sp)
	stq	$24, 8($sp)
	stq	$25, 16($sp)
	.prologue 0

	/* Sign extend.  */
	addl	$24, 0, $24
	addl	$25, 0, $25

	/* Compute absolute values.  */
	subq	$31, $24, $28
	cmovlt	$24, $28, $24
	subq	$31, $25, $28
	cmovlt	$25, $28, $25

	bsr	$23, __divqu

	ldq	$24, 8($sp)
	ldq	$25, 16($sp)

	/* Negate the unsigned result, if necessary.  */
	xor	$24, $25, $28
	subl	$31, $27, $23
	addl	$27, 0, $27
	addl	$28, 0, $28
	cmovlt	$28, $23, $27

	ldq	$23, 0($sp)
	addq	$sp, 32, $sp
	ret	$31, ($23), 1
	.end	__divls

/*
 * Signed 32-bit remainder.
 */

	.globl	__remls
	.ent	__remls
__remls:
	.frame	$sp, 32, $23
	subq	$sp, 32, $sp
	stq	$23, 0($sp)
	stq	$24, 8($sp)
	stq	$25, 16($sp)
	.prologue 0

	/* Sign extend.  */
	addl	$24, 0, $24
	addl	$25, 0, $25

	/* Compute absolute values.  */
	subq	$31, $24, $28
	cmovlt	$24, $28, $24
	subq	$31, $25, $28
	cmovlt	$25, $28, $25

	bsr	$23, __divqu

	ldq	$23, 0($sp)
	ldq	$24, 8($sp)
	ldq	$25, 16($sp)

	/* Negate the unsigned result, if necessary.  */
	subl	$31, $28, $27
	addl	$28, 0, $28
	cmovge	$24, $28, $27

	addq	$sp, 32, $sp
	ret	$31, ($23), 1
	.end	__remls

	.data
	.p2align 4
stack:
	.skip	65536
$stack_end:
	.type	stack,@object
	.size	stack, . - stack

/*
 * Minimal Nios2 system boot code.
 *
 * Copyright Linaro Ltd 2022
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "semicall.h"

        .text
	.set	noat

_start:
	/* Linker script defines stack at end of ram. */
	movia	sp, __stack

	/* Install trampoline to _fast_tlb_miss at hardcoded vector. */
	movia	r4, 0xc0000100
	movia	r5, _ftm_tramp
	movi	r6, .L__ftm_end - _ftm_tramp
	call	memcpy

	/* Zero the bss to satisfy C. */
	movia	r4, __bss_start
	movia	r6, __bss_end
	sub	r6, r6, r4
	movi	r5, 0
	call	memset

	/* Test! */
	call	main

	/* Exit with main's return value. */
	movi	r4, HOSTED_EXIT
	mov	r5, r2
	semihosting_call

	.globl	_start
	.type	_start, @function
	.size	_start, . - _start

_ftm_tramp:
	movia	et, _fast_tlb_miss
	jmp	et
.L__ftm_end:

	.type	_ftm_tramp, @function
	.size	_ftm_tramp, . - _ftm_tramp

#define dst	r4
#define src	r5
#define len	r6

memcpy:
	/* Store return value right away, per API */
	mov	r2, dst

	/* Check for both dst and src aligned. */
	or	at, dst, src
	andi	at, at, 3
	bne	at, zero, .L_mc_test1

	/* Copy blocks of 8. */

	movi	at, 8
	bltu	len, at, .L_mc_test4

.L_mc_loop8:
	ldw	r8, 0(src)
	ldw	r9, 4(src)
	addi	src, src, 8
	addi	dst, dst, 8
	subi	len, len, 8
	stw	r8, -8(dst)
	stw	r9, -4(dst)
	bgeu	len, at, .L_mc_loop8

	/* Copy final aligned block of 4. */

.L_mc_test4:
	movi	at, 4
	bltu	len, at, .L_mc_test1

	ldw	r8, 0(src)
	addi	src, src, 4
	addi	dst, dst, 4
	subi	len, len, 4
	stw	r8, -4(dst)

	/* Copy single bytes to finish. */

.L_mc_test1:
	beq	len, zero, .L_mc_done

.L_mc_loop1:
	ldb	r8, 0(src)
	addi	src, src, 1
	addi	dst, dst, 1
	subi	len, len, 1
	stb	r8, -1(dst)
	bne	len, zero, .L_mc_loop1

.L_mc_done:
	ret

#undef dst
#undef src
#undef len

	.global	memcpy
	.type	memcpy, @function
	.size	memcpy, . - memcpy

#define dst	r4
#define val	r5
#define len	r6

memset:
	/* Store return value right away, per API */
	mov	r2, dst

	/* Check for small blocks; fall back to bytewise. */
	movi	r3, 8
	bltu	len, r3, .L_ms_test1

	/* Replicate the byte across the word. */
	andi	val, val, 0xff
	slli	at, val, 8
	or	val, val, at
	slli	at, val, 16
	or	val, val, at

	/* Check for destination alignment; realign if needed. */
	andi	at, dst, 3
	bne	at, zero, .L_ms_align

	/* Set blocks of 8. */

.L_ms_loop8:
	stw	val, 0(dst)
	stw	val, 4(dst)
	addi	dst, dst, 8
	subi	len, len, 8
	bgeu	len, r3, .L_ms_loop8

	/* Set final aligned block of 4. */

.L_ms_test4:
	movi	at, 4
	bltu	len, at, .L_ms_test1

	stw	r8, 0(dst)
	addi	dst, dst, 4
	subi	len, len, 4
	stw	r8, -4(dst)

	/* Set single bytes to finish. */

.L_ms_test1:
	beq	len, zero, .L_ms_done

.L_ms_loop1:
	stb	r8, 0(dst)
	addi	dst, dst, 1
	subi	len, len, 1
	bne	len, zero, .L_ms_loop1

.L_ms_done:
	ret

	/* Realign for a large block, len >= 8. */
.L_ms_align:
	andi	at, dst, 1
	beq	at, zero, 2f

	stb	val, 0(dst)
	addi	dst, dst, 1
	subi	len, len, 1

2:	andi	at, dst, 2
	beq	at, zero, 4f

	sth	val, 0(dst)
	addi	dst, dst, 2
	subi	len, len, 2

4:	bgeu	len, r3, .L_ms_loop8
	br	.L_ms_test4

#undef dst
#undef val
#undef len

	.global	memset
	.type	memset, @function
	.size	memset, . - memset

/*
 * void __sys_outc(char c);
 */
__sys_outc:
	subi	sp, sp, 16
	stb	r4, 0(sp)	/* buffer[0] = c */
	movi	at, 1
	stw	at, 4(sp)	/* STDOUT_FILENO */
	stw	sp, 8(sp)	/* buffer */
	stw	at, 12(sp)	/* len */

	movi	r4, HOSTED_WRITE
	addi	r5, sp, 4
	semihosting_call

	addi	sp, sp, 16
	ret

	.global	__sys_outc
	.type	__sys_outc, @function
	.size	__sys_outc, . - __sys_outc

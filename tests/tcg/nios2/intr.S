/*
 * Minimal Nios2 system boot code -- exit on interrupt.
 *
 * Copyright Linaro Ltd 2022
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "semicall.h"

        .section .text.intr, "ax"
	.global	_interrupt
	.type	_interrupt, @function

_interrupt:
	rdctl	r5, exception		/* extract exception.CAUSE */
	srli	r5, r5, 2
	movi	r4, HOSTED_EXIT
	semihosting_call

	.size	_interrupt, . - _interrupt

        .text
	.global	_fast_tlb_miss
	.type	_fast_tlb_miss, @function

_fast_tlb_miss:
	movi	r5, 32
	movi	r4, HOSTED_EXIT
	semihosting_call

	.size	_fast_tlb_miss, . - _fast_tlb_miss

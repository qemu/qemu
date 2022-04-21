/*
 * Regression test for TCG indirect global lowering.
 *
 * Copyright Linaro Ltd 2022
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "semicall.h"

	.text
	.set noat
	.align	2
	.globl	main
	.type	main, @function

main:
	/* Initialize r0 in shadow register set 1. */
	movhi	at, 1			/* PRS=1, CRS=0, RSIE=0, PIE=0 */
	wrctl	status, at
	wrprs	zero, zero

	/* Change current register set to 1. */
	movi	at, 1 << 10		/* PRS=0, CRS=1, RSIE=0, PIE=0 */
	wrctl	estatus, at
	movia	ea, 1f
	eret

	/* Load address for callr, then end TB. */
1:	movia	at, 3f
	br	2f

	/* Test case! TCG abort on indirect lowering across brcond. */
2:	callr	at

	/* exit(0) */
3:	movi	r4, HOSTED_EXIT
	movi	r5, 0
	semihosting_call

	.size	main, . - main

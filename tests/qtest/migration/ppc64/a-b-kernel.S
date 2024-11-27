#
# Copyright (c) 2024 IBM, Inc
#
# This work is licensed under the terms of the GNU GPL, version 2 or later.
# See the COPYING file in the top-level directory.

#include "../migration-test.h"

.section .text

.macro print ch
	li	%r3,PPC_H_PUT_TERM_CHAR
	li	%r4,0
	li	%r5,1
	li	%r6,\ch
	sldi	%r6,%r6,56
	sc	1
.endm

        .globl  _start
_start:
. = 0x100
	/*
	 * Enter 64-bit mode. Not necessary because the test uses 32-bit
	 * addresses, but those constants could easily be changed and break
	 * in 32-bit mode.
	 */
	mfmsr	%r9
	li	%r10,-1
	rldimi	%r9,%r10,63,0
	mtmsrd	%r9

        /*
	 * Set up test memory region. Non-volatiles are used because the
	 * hcall can clobber regs.
	 * r20 - start address
	 * r21 - number of pages
	 */
	lis	%r20,PPC_TEST_MEM_START@h
	ori	%r20,%r20,PPC_TEST_MEM_START@l
	lis	%r9,PPC_TEST_MEM_END@h
	ori	%r9,%r9,PPC_TEST_MEM_END@l
	subf	%r21,%r20,%r9
	li	%r10,TEST_MEM_PAGE_SIZE
	divd	%r21,%r21,%r10

	print	'A'

	li	%r3,0
	mr	%r9,%r20
	mtctr	%r21
1:	stb	%r3,0(%r9)
	addi	%r9,%r9,TEST_MEM_PAGE_SIZE
	bdnz	1b

loop:
	mr	%r9,%r20
	mtctr	%r21
1:	lbz	%r3,0(%r9)
	addi	%r3,%r3,1
	stb	%r3,0(%r9)
	addi	%r9,%r9,TEST_MEM_PAGE_SIZE
	bdnz	1b

	print	'B'
	b	loop

/*
 *  Copyright(c) 2024-2025 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "crt0.inc"
	.equ DEFAULT_HEAP_SIZE, 0x4000000 /* 64MB */
	.equ DEFAULT_STACK_SIZE, 0x100000 /* 1MB */

	.section .start, "ax", @progbits
	.subsection 0
	.org 0

	.global _start
	.type _start, @function
        .p2align 5
_start:
	jump hexagon_start_init
	jump hexagon_start_main
	.size _start, . - _start

/*----------------------------------------------------------------------------*/

	.global hexagon_pre_main
        .type hexagon_pre_main, @function

hexagon_pre_main:
	/* Mark first stack frame. */
	fp = #0

        ReadFrom heapBase, r4

        AddrOf DEFAULT_HEAP_SIZE
        r5 = r0

        r5 = add (r4, r5)               /* Calculate aligned heap top. */
        r5 = add (r5, #15)
        r5 = and (r5, #-16)
        WriteTo heapLimit, r5

        /* Set up stack. */
        AddrOf DEFAULT_STACK_SIZE
        r7 = r0

        r6 = add (r5, r7)               /* Assume stack after heap. */
        r6 = and (r6, #-16)

        WriteTo stackBase, r6

        ReadFrom stackBase, r6

        r7 = sub (r6, r7)               /* Desired stack size. */
        r7 = add (r7, #15)
        r7 = and (r7, #-16)
        WriteTo stackLimit, r7

        /* Set stack up. */
        ReadFrom stackBase, r0
        sp = and (r0, #-16)             /* Align top of stack. */

        /* Zero up BSS. */
        AddrOf __bss_start, r0
        AddrOf _end, r2
        AddrOf memset, r28		/* bzero () is deprecated. */
        { r1 = #0
          r2 = sub (r2, r0)
          callr r28 }
        .size hexagon_pre_main, . - hexagon_pre_main

/*----------------------------------------------------------------------------*/

        .global hexagon_start_main
        .type hexagon_start_main, @function
hexagon_start_main:
        AddrOf _start_main, r28
        callr r28
        /*Stop all threads to terminate execution */
        r0 = #0x3f
        stop (r0)
        .size hexagon_start_main, . - hexagon_start_main

/*----------------------------------------------------------------------------*/

	.data
        .global heapBase
        .global heapLimit
        .global stackBase
        .global stackLimit
	.global setHeapAngelCallParams

.HeapParams:
heapBase:
        .word end                       /* Provided by the linker script. */
heapLimit:
        .word end + (DEFAULT_HEAP_SIZE & -16)
stackBase:
        .word 0
stackLimit:
        .word end + ((DEFAULT_HEAP_SIZE + 15) & -16)

setHeapAngelCallParams:
        .word .HeapParams

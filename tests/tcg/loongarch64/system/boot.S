/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Minimal LoongArch system boot code.
 *
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 */

#include "regdef.h"

	.global _start
	.align 16
_start:
	la.local t0, stack_end
	move sp, t0
	bl main

	.type _start 2
	.size _start, .-_start

	.global _exit
	.align 16
_exit:
2:      /* QEMU ACPI poweroff */
	li.w  t0, 0x34
	li.w  t1, 0x100e001c
	st.b  t0, t1, 0

	idle  0
	bl    2b

	.type _exit 2
	.size _exit, .-_exit

	.global __sys_outc
__sys_outc:
	li.d t1, 1000000
loop:
	lu12i.w	t2, 0x1fe00
	ori	t0, t2, 0x1e5
	ld.bu	t0, t0, 0
	andi	t0, t0, 0x20
	ext.w.b	t0, t0
	bnez	t0, in
	addi.w	t1, t1, -1
	bnez	t1, loop
in:
	ext.w.b	a0, a0
	lu12i.w	t0, 0x1fe00
	ori	t0, t0, 0x1e0
	st.b	a0, t0, 0
	jirl	$r0, ra, 0

	.data
	.align 4
stack:
	.space	65536
stack_end:

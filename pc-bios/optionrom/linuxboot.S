/*
 * Linux Boot Option ROM
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Copyright Novell Inc, 2009
 *   Authors: Alexander Graf <agraf@suse.de>
 *
 * Based on code in hw/pc.c.
 */

#include "optionrom.h"

#define BOOT_ROM_PRODUCT "Linux loader"

BOOT_ROM_START

run_linuxboot:

	cli
	cld

	jmp		copy_kernel
boot_kernel:

	read_fw		FW_CFG_SETUP_ADDR

	mov		%eax, %ebx
	shr		$4, %ebx

	/* All segments contain real_addr */
	mov		%bx, %ds
	mov		%bx, %es
	mov		%bx, %fs
	mov		%bx, %gs
	mov		%bx, %ss

	/* CX = CS we want to jump to */
	add		$0x20, %bx
	mov		%bx, %cx

	/* SP = cmdline_addr-real_addr-16 */
	read_fw		FW_CFG_CMDLINE_ADDR
	mov		%eax, %ebx
	read_fw		FW_CFG_SETUP_ADDR
	sub		%eax, %ebx
	sub		$16, %ebx
	mov		%ebx, %esp

	/* Build indirect lret descriptor */
	pushw		%cx		/* CS */
	xor		%ax, %ax
	pushw		%ax		/* IP = 0 */

	/* Clear registers */
	xor		%eax, %eax
	xor		%ebx, %ebx
	xor		%ecx, %ecx
	xor		%edx, %edx
	xor		%edi, %edi
	xor		%ebp, %ebp

	/* Jump to Linux */
	lret


copy_kernel:

	/* We need to load the kernel into memory we can't access in 16 bit
	   mode, so let's get into 32 bit mode, write the kernel and jump
	   back again. */

	/* Reserve space on the stack for our GDT descriptor. */
	mov		%esp, %ebp
	sub		$16, %esp

	/* Now create the GDT descriptor */
	movw		$((3 * 8) - 1), -16(%bp)
	mov		%cs, %eax
	movzwl		%ax, %eax
	shl		$4, %eax
	addl		$gdt, %eax
	movl		%eax, -14(%bp)

	/* And load the GDT */
	data32 lgdt	-16(%bp)
	mov		%ebp, %esp

	/* Get us to protected mode now */
	mov		$1, %eax
	mov		%eax, %cr0

	/* So we can set ES to a 32-bit segment */
	mov		$0x10, %eax
	mov		%eax, %es

	/* We're now running in 16-bit CS, but 32-bit ES! */

	/* Load kernel and initrd */
	read_fw_blob_addr32(FW_CFG_KERNEL)
	read_fw_blob_addr32(FW_CFG_INITRD)
	read_fw_blob_addr32(FW_CFG_CMDLINE)
	read_fw_blob_addr32(FW_CFG_SETUP)

	/* And now jump into Linux! */
	mov		$0, %eax
	mov		%eax, %cr0

	/* ES = CS */
	mov		%cs, %ax
	mov		%ax, %es

	jmp		boot_kernel

/* Variables */

.align 4, 0
gdt:
	/* 0x00 */
.byte	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00

	/* 0x08: code segment (base=0, limit=0xfffff, type=32bit code exec/read, DPL=0, 4k) */
.byte	0xff, 0xff, 0x00, 0x00, 0x00, 0x9a, 0xcf, 0x00

	/* 0x10: data segment (base=0, limit=0xfffff, type=32bit data read/write, DPL=0, 4k) */
.byte	0xff, 0xff, 0x00, 0x00, 0x00, 0x92, 0xcf, 0x00

BOOT_ROM_END

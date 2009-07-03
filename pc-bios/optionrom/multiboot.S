/*
 * Multiboot Option ROM
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
 * along with this program; if not, write to the Free Software
 * Foundation, 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * Copyright Novell Inc, 2009
 *   Authors: Alexander Graf <agraf@suse.de>
 */

#define NO_QEMU_PROTOS
#include "../../hw/fw_cfg.h"

#define BIOS_CFG_IOPORT_CFG	0x510
#define BIOS_CFG_IOPORT_DATA	0x511

#define MULTIBOOT_MAGIC		0x2badb002

/* Read a variable from the fw_cfg device.
   Clobbers:	%edx
   Out:		%eax */
.macro read_fw VAR
	mov		$\VAR, %ax
	mov		$BIOS_CFG_IOPORT_CFG, %dx
	outw		%ax, (%dx)
	mov		$BIOS_CFG_IOPORT_DATA, %dx
	inb		(%dx), %al
	shl		$8, %eax
	inb		(%dx), %al
	shl		$8, %eax
	inb		(%dx), %al
	shl		$8, %eax
	inb		(%dx), %al
	bswap		%eax
.endm

.code16
.text
	.global 	_start
_start:
	.short		0xaa55
	.byte		1 /* (_end - _start) / 512 */
	push		%eax
	push		%ds

	/* setup ds so we can access the IVT */
	xor		%ax, %ax
	mov		%ax, %ds

	/* save old int 19 */
	mov		(0x19*4), %eax
	mov		%eax, %cs:old_int19

	/* install our int 19 handler */
	movw		$int19_handler, (0x19*4)
	mov		%cs, (0x19*4+2)

	pop		%ds
	pop		%eax
	lret

int19_handler:
	/* DS = CS */
	movw		%cs, %ax
	movw		%ax, %ds

	/* fall through */

run_multiboot:

	cli
	cld

	mov		%cs, %eax
	shl		$0x4, %eax

	/* fix the gdt descriptor to be PC relative */
	mov		(gdt_desc+2), %ebx
	add		%eax, %ebx
	mov		%ebx, (gdt_desc+2)

	/* fix the prot mode indirect jump to be PC relative */
	mov		(prot_jump), %ebx
	add		%eax, %ebx
	mov		%ebx, (prot_jump)

	/* FS = bootinfo_struct */
	read_fw		FW_CFG_INITRD_ADDR
	shr		$4, %eax
	mov		%ax, %fs

	/* ES = mmap_addr */
	read_fw		FW_CFG_INITRD_SIZE
	shr		$4, %eax
	mov		%ax, %es

	/* Initialize multiboot mmap structs using int 0x15(e820) */
	xor		%ebx, %ebx
	/* mmap start after first size */
	movl		$4, %edi

mmap_loop:
	/* entry size (mmap struct) & max buffer size (int15) */
	movl		$20, %ecx
	/* store entry size */
	movl		%ecx, %es:-4(%edi)
	/* e820 */
	movl		$0x0000e820, %eax
	/* 'SMAP' magic */
	movl		$0x534d4150, %edx
	int		$0x15

mmap_check_entry:
	/* last entry? then we're done */
	jb		mmap_done
	and		%bx, %bx
	jz		mmap_done
	/* valid entry, so let's loop on */

mmap_store_entry:
	/* %ax = entry_number * 24 */
	mov		$24, %ax
	mul		%bx
	mov		%ax, %di
	movw		%di, %fs:0x2c
	/* %di = 4 + (entry_number * 24) */
	add		$4, %di
	jmp		mmap_loop

mmap_done:
real_to_prot:
	/* Load the GDT before going into protected mode */
lgdt:
	data32 lgdt	%cs:gdt_desc

	/* get us to protected mode now */
	movl		$1, %eax
	movl		%eax, %cr0

	/* the LJMP sets CS for us and gets us to 32-bit */
ljmp:
	data32 ljmp	*%cs:prot_jump

prot_mode:
.code32

	/* initialize all other segments */
	movl		$0x10, %eax
	movl		%eax, %ss
	movl		%eax, %ds
	movl		%eax, %es
	movl		%eax, %fs
	movl		%eax, %gs

	/* Jump off to the kernel */
	read_fw		FW_CFG_KERNEL_ADDR
	mov		%eax, %ecx

	/* EBX contains a pointer to the bootinfo struct */
	read_fw		FW_CFG_INITRD_ADDR
	movl		%eax, %ebx

	/* EAX has to contain the magic */
	movl		$MULTIBOOT_MAGIC, %eax
ljmp2:
	jmp		*%ecx

/* Variables */
.align 4, 0
old_int19:	.long 0

prot_jump:	.long prot_mode
		.short 8

.align 4, 0
gdt:
	/* 0x00 */
.byte	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00

	/* 0x08: code segment (base=0, limit=0xfffff, type=32bit code exec/read, DPL=0, 4k) */
.byte	0xff, 0xff, 0x00, 0x00, 0x00, 0x9a, 0xcf, 0x00

	/* 0x10: data segment (base=0, limit=0xfffff, type=32bit data read/write, DPL=0, 4k) */
.byte	0xff, 0xff, 0x00, 0x00, 0x00, 0x92, 0xcf, 0x00

	/* 0x18: code segment (base=0, limit=0x0ffff, type=16bit code exec/read/conf, DPL=0, 1b) */
.byte	0xff, 0xff, 0x00, 0x00, 0x00, 0x9e, 0x00, 0x00

	/* 0x20: data segment (base=0, limit=0x0ffff, type=16bit data read/write, DPL=0, 1b) */
.byte	0xff, 0xff, 0x00, 0x00, 0x00, 0x92, 0x00, 0x00

gdt_desc:
.short	(5 * 8) - 1
.long	gdt

.align 512, 0
_end:


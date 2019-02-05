/*
 * PVH Option ROM
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
 * Copyright (c) 2019 Red Hat Inc.
 *   Authors: Stefano Garzarella <sgarzare@redhat.com>
 */

#include "optionrom.h"

#define BOOT_ROM_PRODUCT "PVH loader"

#define GS_PROT_JUMP		0
#define GS_GDT_DESC		6

#ifdef OPTION_ROM_START
#undef OPTION_ROM_START
#endif
#ifdef OPTION_ROM_END
#undef OPTION_ROM_END
#endif

/*
 * Redefine OPTION_ROM_START and OPTION_ROM_END, because this rom is produced
 * linking multiple objects.
 * signrom.py will add padding.
 */
#define OPTION_ROM_START                                \
    .code16;						\
    .text;						\
	.global 	_start;				\
    _start:;						\
	.short		0xaa55;				\
	.byte		3; /* desired size in 512 units */

#define OPTION_ROM_END					\
    _end:

BOOT_ROM_START

run_pvhboot:

	cli
	cld

	mov		%cs, %eax
	shl		$0x4, %eax

	/* set up a long jump descriptor that is PC relative */

	/* move stack memory to %gs */
	mov		%ss, %ecx
	shl		$0x4, %ecx
	mov		%esp, %ebx
	add		%ebx, %ecx
	sub		$0x20, %ecx
	sub		$0x30, %esp
	shr		$0x4, %ecx
	mov		%cx, %gs

	/* now push the indirect jump descriptor there */
	mov		(prot_jump), %ebx
	add		%eax, %ebx
	movl		%ebx, %gs:GS_PROT_JUMP
	mov		$8, %bx
	movw		%bx, %gs:GS_PROT_JUMP + 4

	/* fix the gdt descriptor to be PC relative */
	movw		(gdt_desc), %bx
	movw		%bx, %gs:GS_GDT_DESC
	movl		(gdt_desc+2), %ebx
	add		%eax, %ebx
	movl		%ebx, %gs:GS_GDT_DESC + 2

	/* initialize HVM memmap table using int 0x15(e820) */

	/* ES = pvh_e820 struct */
	mov 		$pvh_e820, %eax
	shr		$4, %eax
	mov		%ax, %es

	/* start storing memmap table at %es:8 (pvh_e820.table) */
	mov 		$8,%edi
	xor		%ebx, %ebx
	jmp 		memmap_loop

memmap_loop_check:
	/* pvh_e820 can contains up to 128 entries */
	cmp 		$128, %ebx
	je 		memmap_done

memmap_loop:
	/* entry size (hvm_memmap_table_entry) & max buffer size (int15) */
	movl		$24, %ecx
	/* e820 */
	movl		$0x0000e820, %eax
	/* 'SMAP' magic */
	movl		$0x534d4150, %edx
	/* store counter value at %es:0 (pvh_e820.entries) */
	movl 		%ebx, %es:0

	int		$0x15
	/* error or last entry already done? */
	jb		memmap_err

	/* %edi += entry size (hvm_memmap_table_entry) */
	add		$24, %edi

	/* continuation value 0 means last entry */
	test		%ebx, %ebx
	jnz		memmap_loop_check

	/* increase pvh_e820.entries to save the last entry */
	movl 		%es:0, %ebx
	inc 		%ebx

memmap_done:
	movl 		%ebx, %es:0

memmap_err:

	/* load the GDT before going into protected mode */
lgdt:
	data32 lgdt	%gs:GS_GDT_DESC

	/* get us to protected mode now */
	movl		$1, %eax
	movl		%eax, %cr0

	/* the LJMP sets CS for us and gets us to 32-bit */
ljmp:
	data32 ljmp	*%gs:GS_PROT_JUMP

prot_mode:
.code32

	/* initialize all other segments */
	movl		$0x10, %eax
	movl		%eax, %ss
	movl		%eax, %ds
	movl		%eax, %es
	movl		%eax, %fs
	movl		%eax, %gs

	jmp pvh_load_kernel

/* Variables */
.align 4, 0
prot_jump:	.long prot_mode
		.short 8

.align 4, 0
gdt:
	/* 0x00 */
.byte	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00

	/*
	 * 0x08: code segment
	 * (base=0, limit=0xfffff, type=32bit code exec/read, DPL=0, 4k)
	 */
.byte	0xff, 0xff, 0x00, 0x00, 0x00, 0x9a, 0xcf, 0x00

	/*
	 * 0x10: data segment
	 * (base=0, limit=0xfffff, type=32bit data read/write, DPL=0, 4k)
	 */
.byte	0xff, 0xff, 0x00, 0x00, 0x00, 0x92, 0xcf, 0x00

	/*
	 * 0x18: code segment
	 * (base=0, limit=0x0ffff, type=16bit code exec/read/conf, DPL=0, 1b)
	 */
.byte	0xff, 0xff, 0x00, 0x00, 0x00, 0x9e, 0x00, 0x00

	/*
	 * 0x20: data segment
	 * (base=0, limit=0x0ffff, type=16bit data read/write, DPL=0, 1b)
	 */
.byte	0xff, 0xff, 0x00, 0x00, 0x00, 0x92, 0x00, 0x00

gdt_desc:
.short	(5 * 8) - 1
.long	gdt

BOOT_ROM_END

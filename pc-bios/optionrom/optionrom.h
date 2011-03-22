/*
 * Common Option ROM Functions
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
 */


#define NO_QEMU_PROTOS
#include "../../hw/fw_cfg.h"

#define BIOS_CFG_IOPORT_CFG	0x510
#define BIOS_CFG_IOPORT_DATA	0x511

/* Break the translation block flow so -d cpu shows us values */
#define DEBUG_HERE \
	jmp		1f;				\
	1:
	
/*
 * Read a variable from the fw_cfg device.
 * Clobbers:	%edx
 * Out:		%eax
 */
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

#define read_fw_blob_pre(var)				\
	read_fw		var ## _ADDR;			\
	mov		%eax, %edi;			\
	read_fw		var ## _SIZE;			\
	mov		%eax, %ecx;			\
	mov		$var ## _DATA, %ax;		\
	mov		$BIOS_CFG_IOPORT_CFG, %edx;	\
	outw		%ax, (%dx);			\
	mov		$BIOS_CFG_IOPORT_DATA, %dx;	\
	cld

/*
 * Read a blob from the fw_cfg device.
 * Requires _ADDR, _SIZE and _DATA values for the parameter.
 *
 * Clobbers:	%eax, %edx, %es, %ecx, %edi
 */
#define read_fw_blob(var)				\
	read_fw_blob_pre(var);				\
	/* old as(1) doesn't like this insn so emit the bytes instead: \
	rep insb	(%dx), %es:(%edi);		\
	*/						\
	.dc.b		0xf3,0x6c

/*
 * Read a blob from the fw_cfg device in forced addr32 mode.
 * Requires _ADDR, _SIZE and _DATA values for the parameter.
 *
 * Clobbers:	%eax, %edx, %es, %ecx, %edi
 */
#define read_fw_blob_addr32(var)				\
	read_fw_blob_pre(var);				\
	/* old as(1) doesn't like this insn so emit the bytes instead: \
	addr32 rep insb	(%dx), %es:(%edi);		\
	*/						\
	.dc.b		0x67,0xf3,0x6c

#define OPTION_ROM_START					\
    .code16;						\
    .text;						\
	.global 	_start;				\
    _start:;						\
	.short		0xaa55;				\
	.byte		(_end - _start) / 512;

#define BOOT_ROM_START					\
	OPTION_ROM_START				\
	lret;						\
	.org 		0x18;				\
	.short		0;				\
	.short		_pnph;				\
    _pnph:						\
	.ascii		"$PnP";				\
	.byte		0x01;				\
	.byte		( _pnph_len / 16 );		\
	.short		0x0000;				\
	.byte		0x00;				\
	.byte		0x00;				\
	.long		0x00000000;			\
	.short		_manufacturer;			\
	.short		_product;			\
	.long		0x00000000;			\
	.short		0x0000;				\
	.short		0x0000;				\
	.short		_bev;				\
	.short		0x0000;				\
	.short		0x0000;				\
	.equ		_pnph_len, . - _pnph;		\
    _bev:;						\
	/* DS = CS */					\
	movw		%cs, %ax;			\
	movw		%ax, %ds;

#define OPTION_ROM_END					\
    .align 512, 0;					\
    _end:

#define BOOT_ROM_END					\
    _manufacturer:;					\
	.asciz "QEMU";					\
    _product:;						\
	.asciz BOOT_ROM_PRODUCT;			\
	OPTION_ROM_END


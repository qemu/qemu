/*
 * QEMU Boot sector to launch a preloaded Linux kernel
 * Copyright (c) 2004 Fabrice Bellard
 */

#define LOAD_SEG 0x9000
        
.code16	
.text
	.globl	_start

_start:
        cli
        cld
        mov $LOAD_SEG, %ax
        mov %ax, %ds
        mov %ax, %es
        mov %ax, %fs
        mov %ax, %gs
        mov %ax, %ss
        mov $0x8ffe, %sp
        ljmp $LOAD_SEG + 0x20, $0

1:              
        .fill 510 - (1b - _start), 1, 0

        /* boot sector signature */
        .byte 0x55
        .byte 0xaa

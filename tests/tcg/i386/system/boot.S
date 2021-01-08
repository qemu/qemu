/*
 * i386 boot code, based on  qemu-bmibug.
 *
 * Copyright 2019 Doug Gale
 * Copyright 2019 Linaro
 *
 * This work is licensed under the terms of the GNU GPL, version 3 or later.
 * See the COPYING file in the top-level directory.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

        .section .head

        /* Multi-boot header */
multiboot_st:
        .int 0x1BADB002
        .int 0x10000
        .int -(0x10000+0x1BADB002)
        // Load address
        .int __load_st
        .int __load_st
        .int __load_en
        .int __bss_en
        .int _start
        // mode
        .int 0
        // width
        .int 0
        // height
        .int 0
        // depth
        .int 0

        .code32
        .section .text

        /* Kernel Entry Point */
.global _start
_start:
        // Setup stack ASAP
        mov $stack_end,%esp

        // Load GDT ASAP
        lgdt gdtr
        ljmp $0x8,$.Lloadcs
.Lloadcs:
        mov $0x10,%eax
        mov %eax,%ds
        mov %eax,%es
        mov %eax,%fs
        mov %eax,%gs
        mov %eax,%ss

        // Fixup the IDT to the ridiculous i386 layout
        xor %ebx,%ebx
.Lnextidt:
        mov idt_00(,%ebx,8),%eax
        shr $16,%eax
        movw $0x8,idt_00+2(,%ebx,8)
        movw $0x8E00,idt_00+4(,%ebx,8)
        movw %ax,idt_00+6(,%ebx,8)
        add $1,%ebx
        cmp $32,%ebx
        jl .Lnextidt

        // Load IDTR
        push $idt_00
        push $((32 * 8 - 1) << 16)
        lidt 2(%esp)
        add $8,%esp

        /*
         * Don't worry about stack frame, assume everthing
         * is garbage when we return, we won't need it.
         */
        call main

_exit:	/* output any non-zero result in eax to isa-debug-exit device */
        test %al, %al
        jz 1f
        out %ax, $0xf4

1:      /* QEMU ACPI poweroff */
        mov $0x604,%edx
        mov $0x2000,%eax
        out %ax,%dx
        hlt
        jmp 1b

        /*
         * Helper Functions
         */

        /* Output a single character to serial port */
        .global __sys_outc
__sys_outc:
        pushl %ebp
        movl %esp, %ebp
        out %al,$0xE9
        movl %ebp, %esp
        popl %ebp
        ret


        /* Interrupt Descriptor Table */

        .section .data
        .align 16

idt_00: .int 0, 0
idt_01: .int 0, 0
idt_02: .int 0, 0
idt_03: .int 0, 0
idt_04: .int 0, 0
idt_05: .int 0, 0
idt_06: .int 0, 0 /* intr_6_opcode, Invalid Opcode */
idt_07: .int 0, 0
idt_08: .int 0, 0
idt_09: .int 0, 0
idt_0A: .int 0, 0
idt_0B: .int 0, 0
idt_0C: .int 0, 0
idt_0D: .int 0, 0
idt_0E: .int 0, 0
idt_0F: .int 0, 0
idt_10: .int 0, 0
idt_11: .int 0, 0
idt_12: .int 0, 0
idt_13: .int 0, 0
idt_14: .int 0, 0
idt_15: .int 0, 0
idt_16: .int 0, 0
idt_17: .int 0, 0
idt_18: .int 0, 0
idt_19: .int 0, 0
idt_1A: .int 0, 0
idt_1B: .int 0, 0
idt_1C: .int 0, 0
idt_1D: .int 0, 0
idt_1E: .int 0, 0
idt_1F: .int 0, 0

gdt:
        .short 0
gdtr:
        .short gdt_en - gdt - 1
        .int gdt

        // Code
        .short 0xFFFF
        .short 0
        .byte 0
        .byte 0x9b
        .byte 0xCF
        .byte 0

        // Data
        .short 0xFFFF
        .short 0
        .byte 0
        .byte 0x93
        .byte 0xCF
        .byte 0

gdt_en:

        .section .bss
        .align 16

stack: .space 65536
stack_end:

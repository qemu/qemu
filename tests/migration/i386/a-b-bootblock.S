# x86 bootblock used in migration test
#  repeatedly increments the first byte of each page in a 100MB
#  range.
#  Outputs an initial 'A' on serial followed by repeated 'B's
#
# Copyright (c) 2016 Red Hat, Inc. and/or its affiliates
# This work is licensed under the terms of the GNU GPL, version 2 or later.
# See the COPYING file in the top-level directory.
#
# Author: dgilbert@redhat.com


.code16
.org 0x7c00
        .file   "fill.s"
        .text
        .globl  start
        .type   start, @function
start:             # at 0x7c00 ?
        cli
        lgdt gdtdesc
        mov $1,%eax
        mov %eax,%cr0  # Protected mode enable
        data32 ljmp $8,$0x7c20

.org 0x7c20
.code32
        # A20 enable - not sure I actually need this
        inb $0x92,%al
        or  $2,%al
        outb %al, $0x92

        # set up DS for the whole of RAM (needed on KVM)
        mov $16,%eax
        mov %eax,%ds

        mov $65,%ax
        mov $0x3f8,%dx
        outb %al,%dx

        # bl keeps a counter so we limit the output speed
        mov $0, %bl
mainloop:
        # Start from 1MB
        mov $(1024*1024),%eax
innerloop:
        incb (%eax)
        add $4096,%eax
        cmp $(100*1024*1024),%eax
        jl innerloop

        inc %bl
        andb $0x3f,%bl
        jnz mainloop

        mov $66,%ax
        mov $0x3f8,%dx
        outb %al,%dx

        jmp mainloop

        # GDT magic from old (GPLv2)  Grub startup.S
        .p2align        2       /* force 4-byte alignment */
gdt:
        .word   0, 0
        .byte   0, 0, 0, 0

        /* -- code segment --
         * base = 0x00000000, limit = 0xFFFFF (4 KiB Granularity), present
         * type = 32bit code execute/read, DPL = 0
         */
        .word   0xFFFF, 0
        .byte   0, 0x9A, 0xCF, 0

        /* -- data segment --
         * base = 0x00000000, limit 0xFFFFF (4 KiB Granularity), present
         * type = 32 bit data read/write, DPL = 0
         */
        .word   0xFFFF, 0
        .byte   0, 0x92, 0xCF, 0

gdtdesc:
        .word   0x27                    /* limit */
        .long   gdt                     /* addr */

/* I'm a bootable disk */
.org 0x7dfe
        .byte 0x55
        .byte 0xAA

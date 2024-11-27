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

#include "migration-test.h"

#define ACPI_ENABLE         0xf1
#define ACPI_PORT_SMI_CMD   0xb2
#define ACPI_PM_BASE        0x600
#define PM1A_CNT_OFFSET     4

#define ACPI_SCI_ENABLE     0x0001
#define ACPI_SLEEP_TYPE     0x0400
#define ACPI_SLEEP_ENABLE   0x2000
#define SLEEP (ACPI_SCI_ENABLE + ACPI_SLEEP_TYPE + ACPI_SLEEP_ENABLE)

#define LOW_ADDR            X86_TEST_MEM_START
#define HIGH_ADDR           X86_TEST_MEM_END

/* Save the suspended status at an address that is not written in the loop. */
#define suspended           (X86_TEST_MEM_START + 4)

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

# Start from 1MB
.set TEST_MEM_START, X86_TEST_MEM_START
.set TEST_MEM_END, X86_TEST_MEM_END

        mov $65,%ax
        mov $0x3f8,%dx
        outb %al,%dx

        # bl keeps a counter so we limit the output speed
        mov $0, %bl

pre_zero:
        mov $TEST_MEM_START,%eax
do_zero:
        movb $0, (%eax)
        add $4096,%eax
        cmp $TEST_MEM_END,%eax
        jl do_zero

mainloop:
        mov $TEST_MEM_START,%eax
innerloop:
        incb (%eax)
        add $4096,%eax
        cmp $TEST_MEM_END,%eax
        jl innerloop

        inc %bl
        andb $0x3f,%bl
        jnz mainloop

        mov $66,%ax
        mov $0x3f8,%dx
        outb %al,%dx

        # should this test suspend?
        mov (suspend_me),%eax
        cmp $0,%eax
        je mainloop

        # are we waking after suspend?  do not suspend again.
        mov $suspended,%eax
        mov (%eax),%eax
        cmp $1,%eax
        je mainloop

        # enable acpi
        mov $ACPI_ENABLE,%al
        outb %al,$ACPI_PORT_SMI_CMD

        # suspend to ram
        mov $suspended,%eax
        movl $1,(%eax)
        mov $SLEEP,%ax
        mov $(ACPI_PM_BASE + PM1A_CNT_OFFSET),%dx
        outw %ax,%dx
        # not reached.  The wakeup causes reset and restart at 0x7c00, and we
        # do not save and restore registers as a real kernel would do.


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

        /* test launcher can poke a 1 here to exercise suspend */
suspend_me:
        .int  0

/* I'm a bootable disk */
.org 0x7dfe
        .byte 0x55
        .byte 0xAA

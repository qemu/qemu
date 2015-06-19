/*
 * First stage boot loader for virtio devices. The compiled output goes
 * into the pc-bios directory of qemu.
 *
 * Copyright (c) 2013 Alexander Graf <agraf@suse.de>
 * Copyright 2013 IBM Corp.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

        .globl _start
_start:

larl	%r15, stack + 0x8000    /* Set up stack */
larl    %r6, boot_value
stg     %r7, 0(%r6)     /* save the boot_value before any function calls */
j	main                    /* And call C */

/*
 * void disabled_wait(void)
 *
 * stops the current guest cpu.
 */
	.globl disabled_wait
disabled_wait:
        larl %r1,disabled_wait_psw
        lpswe   0(%r1)


/*
 * void consume_sclp_int(void)
 *
 * eats one sclp interrupt
 */
        .globl consume_sclp_int
consume_sclp_int:
        /* enable service interrupts in cr0 */
        stctg 0,0,0(15)
        oi 6(15), 0x2
        lctlg 0,0,0(15)
        /* prepare external call handler */
        larl %r1, external_new_code
        stg %r1, 0x1b8
        larl %r1, external_new_mask
        mvc 0x1b0(8),0(%r1)
        /* load enabled wait PSW */
        larl %r1, enabled_wait_psw
        lpswe 0(%r1)

external_new_code:
        /* disable service interrupts in cr0 */
        stctg 0,0,0(15)
        ni 6(15), 0xfd
        lctlg 0,0,0(15)
        br 14

        .align  8
disabled_wait_psw:
        .quad   0x0002000180000000,0x0000000000000000
enabled_wait_psw:
        .quad   0x0302000180000000,0x0000000000000000
external_new_mask:
        .quad   0x0000000180000000

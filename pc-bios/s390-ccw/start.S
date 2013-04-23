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

        .align  8
disabled_wait_psw:
        .quad   0x0002000180000000,0x0000000000000000

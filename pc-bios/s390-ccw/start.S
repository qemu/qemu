/*
 * First stage boot loader for virtio devices. The compiled output goes
 * into the pc-bios directory of qemu.
 *
 * Copyright (c) 2013 Alexander Graf <agraf@suse.de>
 * Copyright IBM Corp. 2013, 2017
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

        .globl _start
_start:

	larl   %r15, stack + 0x8000	/* Set up stack */

	/* clear bss */
	larl %r2, __bss_start
	larl %r3, _end
	slgr %r3, %r2		/* get sizeof bss */
	ltgr	%r3,%r3 	/* bss emtpy? */
	jz	done
	aghi	%r3,-1
	srlg	%r4,%r3,8	/* how many 256 byte chunks? */
	ltgr	%r4,%r4
	lgr	%r1,%r2
	jz	remainder
loop:
	xc	0(256,%r1),0(%r1)
	la	%r1,256(%r1)
	brctg	%r4,loop
remainder:
	larl	%r2,memsetxc
	ex	%r3,0(%r2)
done:
	j      main		/* And call C */

memsetxc:
	xc	0(1,%r1),0(%r1)


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
        stctg   %c0,%c0,0(%r15)
        oi      6(%r15),0x2
        lctlg   %c0,%c0,0(%r15)
        /* prepare external call handler */
        larl %r1, external_new_code
        stg %r1, 0x1b8
        larl %r1, external_new_mask
        mvc 0x1b0(8),0(%r1)
        /* load enabled wait PSW */
        larl %r1, enabled_wait_psw
        lpswe 0(%r1)

/*
 * void consume_io_int(void)
 *
 * eats one I/O interrupt
 */
        .globl consume_io_int
consume_io_int:
        /* enable I/O interrupts in cr6 */
        stctg %c6,%c6,0(%r15)
        oi    4(%r15), 0xff
        lctlg %c6,%c6,0(%r15)
        /* prepare i/o call handler */
        larl  %r1, io_new_code
        stg   %r1, 0x1f8
        larl  %r1, io_new_mask
        mvc   0x1f0(8),0(%r1)
        /* load enabled wait PSW */
        larl  %r1, enabled_wait_psw
        lpswe 0(%r1)

external_new_code:
        /* disable service interrupts in cr0 */
        stctg   %c0,%c0,0(%r15)
        ni      6(%r15),0xfd
        lctlg   %c0,%c0,0(%r15)
        br      %r14

io_new_code:
        /* disable I/O interrupts in cr6 */
        stctg %c6,%c6,0(%r15)
        ni    4(%r15), 0x00
        lctlg %c6,%c6,0(%r15)
        br    %r14

        .align  8
disabled_wait_psw:
        .quad   0x0002000180000000,0x0000000000000000
enabled_wait_psw:
        .quad   0x0302000180000000,0x0000000000000000
external_new_mask:
        .quad   0x0000000180000000
io_new_mask:
        .quad   0x0000000180000000

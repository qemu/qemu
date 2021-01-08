/*
 * Minimal AArch64 system boot code.
 *
 * Copyright Linaro Ltd 2019
 *
 * Loosely based on the newlib/libgloss setup stubs. Using semihosting
 * for serial output and exit functions.
 */

/*
 * Semihosting interface on ARM AArch64
 * See "Semihosting for AArch32 and AArch64 Relase 2.0" by ARM
 * w0 - semihosting call number
 * x1 - semihosting parameter
 */
#define semihosting_call hlt 0xf000
#define SYS_WRITEC	0x03	/* character to debug channel */
#define SYS_WRITE0	0x04	/* string to debug channel */
#define SYS_EXIT	0x18

	.align	12

	.macro	ventry	label
	.align	7
	b	\label
	.endm

vector_table:
	/* Current EL with SP0.	 */
	ventry	curr_sp0_sync		/* Synchronous	*/
	ventry	curr_sp0_irq		/* Irq/vIRQ  */
	ventry	curr_sp0_fiq		/* Fiq/vFIQ  */
	ventry	curr_sp0_serror		/* SError/VSError  */

	/* Current EL with SPx.	 */
	ventry	curr_spx_sync		/* Synchronous	*/
	ventry	curr_spx_irq		/* IRQ/vIRQ  */
	ventry	curr_spx_fiq		/* FIQ/vFIQ  */
	ventry	curr_spx_serror		/* SError/VSError  */

	/* Lower EL using AArch64.  */
	ventry	lower_a64_sync		/* Synchronous	*/
	ventry	lower_a64_irq		/* IRQ/vIRQ  */
	ventry	lower_a64_fiq		/* FIQ/vFIQ  */
	ventry	lower_a64_serror	/* SError/VSError  */

	/* Lower EL using AArch32.  */
	ventry	lower_a32_sync		/* Synchronous	*/
	ventry	lower_a32_irq		/* IRQ/vIRQ  */
	ventry	lower_a32_fiq		/* FIQ/vFIQ  */
	ventry	lower_a32_serror	/* SError/VSError  */

	.text
	.align 4

	/* Common vector handling for now */
curr_sp0_sync:
curr_sp0_irq:
curr_sp0_fiq:
curr_sp0_serror:
curr_spx_sync:
curr_spx_irq:
curr_spx_fiq:
curr_spx_serror:
lower_a64_sync:
lower_a64_irq:
lower_a64_fiq:
lower_a64_serror:
lower_a32_sync:
lower_a32_irq:
lower_a32_fiq:
lower_a32_serror:
	mov	x0, SYS_WRITE0
	adr	x1, .error
	semihosting_call
	mov	x0, SYS_EXIT
	mov	x1, 1
	semihosting_call
	/* never returns */

	.section .rodata
.error:
	.string "Terminated by exception.\n"

	.text
	.align 4
	.global __start
__start:
	/* Installs a table of exception vectors to catch and handle all
	   exceptions by terminating the process with a diagnostic.  */
	adr	x0, vector_table
	msr	vbar_el1, x0

	/* Page table setup (identity mapping). */
	adrp	x0, ttb
	add	x0, x0, :lo12:ttb
	msr	ttbr0_el1, x0

	/*
	 * Setup a flat address mapping page-tables. Stage one simply
	 * maps RAM to the first Gb. The stage2 tables have two 2mb
	 * translation block entries covering a series of adjacent
	 * 4k pages.
	*/

	/* Stage 1 entry: indexed by IA[38:30] */
	adr	x1, .				/* phys address */
	bic	x1, x1, #(1 << 30) - 1		/* 1GB alignment*/
	add	x2, x0, x1, lsr #(30 - 3)	/* offset in l1 page table */

	/* point to stage 2 table [47:12] */
	adrp	x0, ttb_stage2
	orr 	x1, x0, #3 			/* ptr to stage 2 */
	str	x1, [x2]

	/* Stage 2 entries: indexed by IA[29:21] */
	ldr	x5, =(((1 << 9) - 1) << 21)

	/* First block: .text/RO/execute enabled */
	adr	x1, .				/* phys address */
	bic	x1, x1, #(1 << 21) - 1		/* 2mb block alignment	*/
	and	x4, x1, x5			/* IA[29:21] */
	add	x2, x0, x4, lsr #(21 - 3)	/* offset in l2 page table */
	ldr	x3, =0x401			/* attr(AF, block) */
	orr	x1, x1, x3
	str	x1, [x2]			/* 1st 2mb (.text & rodata) */

	/* Second block: .data/RW/no execute */
	adrp	x1, .data
	add	x1, x1, :lo12:.data
	bic	x1, x1, #(1 << 21) - 1		/* 2mb block alignment */
	and	x4, x1, x5			/* IA[29:21] */
	add	x2, x0, x4, lsr #(21 - 3)	/* offset in l2 page table */
	ldr	x3, =(3 << 53) | 0x401		/* attr(AF, NX, block) */
	orr	x1, x1, x3
	str	x1, [x2]			/* 2nd 2mb (.data & .bss)*/

	/* Setup/enable the MMU.  */

	/*
	 * TCR_EL1 - Translation Control Registers
	 *
	 * IPS[34:32] = 40-bit PA, 1TB
	 * TG0[14:15] = b00 => 4kb granuale
	 * ORGN0[11:10] = Outer: Normal, WB Read-Alloc No Write-Alloc Cacheable
	 * IRGN0[9:8] = Inner: Normal, WB Read-Alloc No Write-Alloc Cacheable
	 * T0SZ[5:0]  = 2^(64 - 25)
	 *
	 * The size of T0SZ controls what the initial lookup level. It
	 * would be nice to start at level 2 but unfortunatly for a
	 * flat-mapping on the virt machine we need to handle IA's
	 * with at least 1gb range to see RAM. So we start with a
	 * level 1 lookup.
	 */
	ldr	x0, = (2 << 32) | 25 | (3 << 10) | (3 << 8)
	msr	tcr_el1, x0

	mov	x0, #0xee			/* Inner/outer cacheable WB */
	msr	mair_el1, x0
	isb

	/*
	 * SCTLR_EL1 - System Control Register
	 *
	 * WXN[19] = 0 = no effect, Write does not imply XN (execute never)
	 * I[12] = Instruction cachability control
	 * SA[3] = SP alignment check
	 * C[2] = Data cachability control
	 * M[0] = 1, enable stage 1 address translation for EL0/1
	 */
	mrs	x0, sctlr_el1
	ldr	x1, =0x100d			/* bits I(12) SA(3) C(2) M(0) */
	bic	x0, x0, #(1 << 1)		/* clear bit A(1) */
	bic	x0, x0, #(1 << 19)		/* clear WXN */
	orr	x0, x0, x1			/* set bits */

	dsb	sy
	msr	sctlr_el1, x0
	isb

	/*
	 * Enable FP registers. The standard C pre-amble will be
	 * saving these and A-profile compilers will use AdvSIMD
	 * registers unless we tell it not to.
	*/
	mrs	x0, cpacr_el1
	orr	x0, x0, #(3 << 20)
	msr	cpacr_el1, x0

	/* Setup some stack space and enter the test code.
	 * Assume everthing except the return value is garbage when we
	 * return, we won't need it.
	 */
	adrp	x0, stack_end
	add	x0, x0, :lo12:stack_end
	mov	sp, x0
	bl	main

	/* pass return value to sys exit */
_exit:
	mov    x1, x0
	ldr    x0, =0x20026 /* ADP_Stopped_ApplicationExit */
	stp    x0, x1, [sp, #-16]!
	mov    x1, sp
	mov    x0, SYS_EXIT
	semihosting_call
	/* never returns */

	/*
	 * Helper Functions
	*/

	/* Output a single character to serial port */
	.global __sys_outc
__sys_outc:
	stp x0, x1, [sp, #-16]!
	/* pass address of c on stack */
	mov x1, sp
	mov x0, SYS_WRITEC
	semihosting_call
	ldp x0, x1, [sp], #16
	ret

	.data
	.align	12

	/* Translation table
	 * @4k granuale: 9 bit lookup, 512 entries
	*/
ttb:
	.space	4096, 0

	.align	12
ttb_stage2:
	.space	4096, 0

	.align	12
stack:
	.space 65536, 0
stack_end:

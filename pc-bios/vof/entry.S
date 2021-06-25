#define LOAD32(rn, name)    \
	lis     rn,name##@h;    \
	ori     rn,rn,name##@l

#define ENTRY(func_name)    \
	.text;                  \
	.align  2;              \
	.globl  .func_name;     \
	.func_name:             \
	.globl  func_name;      \
	func_name:

#define KVMPPC_HCALL_BASE       0xf000
#define KVMPPC_H_RTAS           (KVMPPC_HCALL_BASE + 0x0)
#define KVMPPC_H_VOF_CLIENT     (KVMPPC_HCALL_BASE + 0x5)

	. = 0x100 /* Do exactly as SLOF does */

ENTRY(_start)
	LOAD32(2, __toc_start)
	b entry_c

ENTRY(_prom_entry)
	LOAD32(2, __toc_start)
	stwu    %r1,-112(%r1)
	stw     %r31,104(%r1)
	mflr    %r31
	bl prom_entry
	nop
	mtlr    %r31
	lwz     %r31,104(%r1)
	addi    %r1,%r1,112
	blr

ENTRY(ci_entry)
	mr	4,3
	LOAD32(3,KVMPPC_H_VOF_CLIENT)
	sc	1
	blr

/* This is the actual RTAS blob copied to the OS at instantiate-rtas */
ENTRY(hv_rtas)
	mr      %r4,%r3
	LOAD32(3,KVMPPC_H_RTAS)
	sc	1
	blr
	.globl hv_rtas_size
hv_rtas_size:
	.long . - hv_rtas;

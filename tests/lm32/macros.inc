
.macro test_name name
	.data
tn_\name:
	.asciz "\name"
	.text
	mvhi r13, hi(tn_\name)
	ori r13, r13, lo(tn_\name)
	sw (r12+8), r13
.endm

.macro load reg val
	mvhi \reg, hi(\val)
	ori \reg, \reg, lo(\val)
.endm

.macro tc_pass
	mvi r13, 0
	sw (r12+4), r13
.endm

.macro tc_fail
	mvi r13, 1
	sw (r12+4), r13
.endm

.macro check_r3 val
	mvhi r13, hi(\val)
	ori r13, r13, lo(\val)
	be r3, r13, 1f
	tc_fail
	bi 2f
1:
	tc_pass
2:
.endm

.macro check_mem adr val
	mvhi r13, hi(\adr)
	ori r13, r13, lo(\adr)
	mvhi r14, hi(\val)
	ori r14, r14, lo(\val)
	lw r13, (r13+0)
	be r13, r14, 1f
	tc_fail
	bi 2f
1:
	tc_pass
2:
.endm

.macro check_excp excp
	andi r13, r25, \excp
	bne r13, r0, 1f
	tc_fail
	bi 2f
1:
	tc_pass
2:
.endm

.macro start
	.global _main
	.text
_main:
	mvhi r12, hi(0xffff0000)      # base address of test block
	ori r12, r12, lo(0xffff0000)
.endm

.macro end
	sw (r12+0), r0
1:
	bi 1b
.endm

# base +
#  0  ctrl
#  4  pass/fail
#  8  ptr to test name


.equ MAX_TESTNAME_LEN, 32
.macro test_name name
	.data
tn_\name:
	.ascii "\name"
	.space MAX_TESTNAME_LEN - (. - tn_\name), ' '
	.text
	.global \name
\name:
	addi sp, sp, -12
	sw (sp+4), r1
	sw (sp+8), r2
	sw (sp+12), r3
	mvi r1, 1
	mvhi r2, hi(tn_\name)
	ori r2, r2, lo(tn_\name)
	mvi r3, MAX_TESTNAME_LEN
	calli _write
	lw r3, (sp+12)
	lw r2, (sp+8)
	lw r1, (sp+4)
	addi sp, sp, 12
.endm

.macro load reg val
	mvhi \reg, hi(\val)
	ori \reg, \reg, lo(\val)
.endm

.macro tc_pass
	calli _tc_pass
.endm

.macro tc_fail
	addi r12, r12, 1
	calli _tc_fail
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
	mvi r12, 0
.endm

.macro end
	mv r1, r12
	calli _exit
.endm

# base +
#  0  ctrl
#  4  pass/fail
#  8  ptr to test name

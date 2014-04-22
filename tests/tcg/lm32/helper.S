.text
.global _start, _write, _exit
.global _tc_fail, _tc_pass

_write:
	addi sp, sp, -4
	sw (sp+4), r8
	mvi r8, 5
	scall
	lw r8, (sp+4)
	addi sp, sp, 4
	ret

_exit:
	mvi r8, 1
	scall
1:
	bi 1b

_tc_pass:
.data
1:
	.ascii "OK\n"
2:
.text
	addi sp, sp, -16
	sw (sp+4), ra
	sw (sp+8), r1
	sw (sp+12), r2
	sw (sp+16), r3
	mvi r1, 1
	mvhi r2, hi(1b)
	ori r2, r2, lo(1b)
	mvi r3, (2b - 1b)
	calli _write
	lw r3, (sp+16)
	lw r2, (sp+12)
	lw r1, (sp+8)
	lw ra, (sp+4)
	addi sp, sp, 16
	ret

_tc_fail:
.data
1:
	.ascii "FAILED\n"
2:
.text
	addi sp, sp, -16
	sw (sp+4), ra
	sw (sp+8), r1
	sw (sp+12), r2
	sw (sp+16), r3
	sw (sp+4), ra
	mvi r1, 1
	mvhi r2, hi(1b)
	ori r2, r2, lo(1b)
	mvi r3, (2b - 1b)
	calli _write
	lw r3, (sp+16)
	lw r2, (sp+12)
	lw r1, (sp+8)
	lw ra, (sp+4)
	addi sp, sp, 16
	ret

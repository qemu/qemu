.include "macros.inc"

start

test_name LW_1
load r1 data
lw r3, (r1+0)
check_r3 0x7e7f7071

test_name LW_2
lw r3, (r1+4)
check_r3 0x72737475

test_name LW_3
lw r3, (r1+-4)
check_r3 0x7a7b7c7d

test_name LW_4
load r3 data
lw r3, (r3+0)
check_r3 0x7e7f7071

end

.data
	.align 4
	.byte 0x7a, 0x7b, 0x7c, 0x7d
data:
	.byte 0x7e, 0x7f, 0x70, 0x71
	.byte 0x72, 0x73, 0x74, 0x75

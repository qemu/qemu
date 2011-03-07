.include "macros.inc"

start

test_name LBU_1
load r1 data
lbu r3, (r1+0)
check_r3 0x7e

test_name LBU_2
lbu r3, (r1+1)
check_r3 0x7f

test_name LBU_3
lbu r3, (r1+-1)
check_r3 0x7d

test_name LBU_4
load r1 data_msb
lbu r3, (r1+0)
check_r3 0xfe

test_name LBU_5
lbu r3, (r1+1)
check_r3 0xff

test_name LBU_6
lbu r3, (r1+-1)
check_r3 0xfd

test_name LBU_7
load r3 data
lbu r3, (r3+0)
check_r3 0x7e

end

.data
	.align 4
	.byte 0x7a, 0x7b, 0x7c, 0x7d
data:
	.byte 0x7e, 0x7f, 0x70, 0x71
	.byte 0xfa, 0xfb, 0xfc, 0xfd
data_msb:
	.byte 0xfe, 0xff, 0xf0, 0xf1

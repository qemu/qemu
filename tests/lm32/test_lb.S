.include "macros.inc"

start

test_name LB_1
load r1 data
lb r3, (r1+0)
check_r3 0x7e

test_name LB_2
lb r3, (r1+1)
check_r3 0x7f

test_name LB_3
lb r3, (r1+-1)
check_r3 0x7d

test_name LB_4
load r1 data_msb
lb r3, (r1+0)
check_r3 0xfffffffe

test_name LB_5
lb r3, (r1+1)
check_r3 0xffffffff

test_name LB_6
lb r3, (r1+-1)
check_r3 0xfffffffd

test_name LB_7
load r3 data
lb r3, (r3+0)
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

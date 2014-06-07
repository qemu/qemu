.include "macros.inc"

start

test_name LHU_1
load r1 data
lhu r3, (r1+0)
check_r3 0x7e7f

test_name LHU_2
load r1 data
lhu r3, (r1+2)
check_r3 0x7071

test_name LHU_3
load r1 data
lhu r3, (r1+-2)
check_r3 0x7c7d

test_name LHU_4
load r1 data_msb
lhu r3, (r1+0)
check_r3 0xfeff

test_name LHU_5
load r1 data_msb
lhu r3, (r1+2)
check_r3 0xf0f1

test_name LHU_6
load r1 data_msb
lhu r3, (r1+-2)
check_r3 0xfcfd

test_name LHU_7
load r3 data
lhu r3, (r3+0)
check_r3 0x7e7f

end

.data
	.align 4
	.byte 0x7a, 0x7b, 0x7c, 0x7d
data:
	.byte 0x7e, 0x7f, 0x70, 0x71
	.byte 0xfa, 0xfb, 0xfc, 0xfd
data_msb:
	.byte 0xfe, 0xff, 0xf0, 0xf1

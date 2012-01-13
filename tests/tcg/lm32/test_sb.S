.include "macros.inc"

start

test_name SB_1
load r1 data
load r2 0xf0f1f2aa
sb (r1+0), r2
check_mem data 0xaa000000

test_name SB_2
load r2 0xf0f1f2bb
sb (r1+1), r2
check_mem data 0xaabb0000

test_name SB_3
load r2 0xf0f1f2cc
sb (r1+-1), r2
check_mem data0 0x000000cc

end

.data
	.align 4
data0:
	.byte 0, 0, 0, 0
data:
	.byte 0, 0, 0, 0
data1:
	.byte 0, 0, 0, 0

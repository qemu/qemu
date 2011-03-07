.include "macros.inc"

start

test_name SW_1
load r1 data
load r2 0xaabbccdd
sw (r1+0), r2
check_mem data 0xaabbccdd

test_name SW_2
load r2 0x00112233
sw (r1+4), r2
check_mem data1 0x00112233

test_name SW_3
load r2 0x44556677
sw (r1+-4), r2
check_mem data0 0x44556677

test_name SW_4
sw (r1+0), r1
lw r3, (r1+0)
check_r3 data

end

.data
	.align 4
data0:
	.byte 0, 0, 0, 0
data:
	.byte 0, 0, 0, 0
data1:
	.byte 0, 0, 0, 0

.include "macros.inc"

start

test_name SH_1
load r1 data
load r2 0xf0f1aaaa
sh (r1+0), r2
check_mem data 0xaaaa0000

test_name SH_2
load r1 data
load r2 0xf0f1bbbb
sh (r1+2), r2
check_mem data 0xaaaabbbb

test_name SH_3
load r1 data
load r2 0xf0f1cccc
sh (r1+-2), r2
check_mem data0 0x0000cccc

end

.data
	.align 4
data0:
	.byte 0, 0, 0, 0
data:
	.byte 0, 0, 0, 0
data1:
	.byte 0, 0, 0, 0

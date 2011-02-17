.include "macros.inc"

start

test_name XNOR_1
mvi r1, 0
mvi r2, 0
xnor r3, r1, r2
check_r3 0xffffffff

test_name XNOR_2
mvi r1, 0
mvi r2, 1
xnor r3, r1, r2
check_r3 0xfffffffe

test_name XNOR_3
mvi r1, 1
mvi r2, 1
xnor r3, r1, r2
check_r3 0xffffffff

test_name XNOR_4
mvi r1, 1
mvi r2, 0
xnor r3, r1, r2
check_r3 0xfffffffe

test_name XNOR_5
load r1 0xaa55aa55
load r2 0x55aa55aa
xnor r3, r1, r2
check_r3 0

test_name XNOR_6
load r1 0xaa550000
load r2 0x0000aa55
xnor r3, r1, r2
check_r3 0x55aa55aa

test_name XNOR_7
load r1 0xaa55aa55
xnor r3, r1, r1
check_r3 0xffffffff

test_name XNOR_8
load r3 0xaa55aa55
xnor r3, r3, r3
check_r3 0xffffffff

end

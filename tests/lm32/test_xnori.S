.include "macros.inc"

start

test_name XNORI_1
mvi r1, 0
xnori r3, r1, 0
check_r3 0xffffffff

test_name XNORI_2
mvi r1, 0
xnori r3, r1, 1
check_r3 0xfffffffe

test_name XNORI_3
mvi r1, 1
xnori r3, r1, 1
check_r3 0xffffffff

test_name XNORI_4
mvi r1, 1
xnori r3, r1, 0
check_r3 0xfffffffe

test_name XNORI_5
load r1 0xaa55aa55
xnori r3, r1, 0x5555
check_r3 0x55aa00ff

test_name XNORI_6
load r3 0xaa55aa55
xnori r3, r3, 0x5555
check_r3 0x55aa00ff

end

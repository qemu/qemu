.include "macros.inc"

start

test_name OR_1
mvi r1, 0
mvi r2, 0
or r3, r1, r2
check_r3 0

test_name OR_2
mvi r1, 0
mvi r2, 1
or r3, r1, r2
check_r3 1

test_name OR_3
mvi r1, 1
mvi r2, 1
or r3, r1, r2
check_r3 1

test_name OR_4
mvi r1, 1
mvi r2, 0
or r3, r1, r2
check_r3 1

test_name OR_5
load r1 0xaa55aa55
load r2 0x55aa55aa
or r3, r1, r2
check_r3 0xffffffff

test_name OR_6
load r1 0xaa550000
load r2 0x0000aa55
or r3, r1, r2
check_r3 0xaa55aa55

test_name OR_7
load r1 0xaa55aa55
or r3, r1, r1
check_r3 0xaa55aa55

test_name OR_8
load r3 0xaa55aa55
or r3, r3, r3
check_r3 0xaa55aa55

end

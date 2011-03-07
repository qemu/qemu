.include "macros.inc"

start

test_name AND_1
mvi r1, 0
mvi r2, 0
and r3, r1, r2
check_r3 0

test_name AND_2
mvi r1, 0
mvi r2, 1
and r3, r1, r2
check_r3 0

test_name AND_3
mvi r1, 1
mvi r2, 1
and r3, r1, r2
check_r3 1

test_name AND_4
mvi r3, 7
and r3, r3, r3
check_r3 7

test_name AND_5
mvi r1, 7
and r3, r1, r1
check_r3 7

test_name AND_6
mvi r1, 7
mvi r3, 0
and r3, r1, r3
check_r3 0

test_name AND_7
load r1 0xaa55aa55
load r2 0x55aa55aa
and r3, r1, r2
check_r3 0

end

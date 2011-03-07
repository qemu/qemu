.include "macros.inc"

start

test_name SL_1
mvi r1, 1
mvi r2, 0
sl r3, r1, r2
check_r3 1

test_name SL_2
mvi r1, 0
mvi r2, 1
sl r3, r1, r2
check_r3 0

test_name SL_3
mvi r1, 1
mvi r2, 31
sl r3, r1, r2
check_r3 0x80000000

test_name SL_4
mvi r1, 16
mvi r2, 31
sl r3, r1, r2
check_r3 0

test_name SL_5
mvi r1, 1
mvi r2, 34
sl r3, r1, r2
check_r3 4

test_name SL_6
mvi r1, 2
sl r3, r1, r1
check_r3 8

test_name SL_7
mvi r3, 2
sl r3, r3, r3
check_r3 8

end

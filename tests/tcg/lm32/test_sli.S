.include "macros.inc"

start

test_name SLI_1
mvi r1, 1
sli r3, r1, 0
check_r3 1

test_name SLI_2
mvi r1, 0
sli r3, r1, 1
check_r3 0

test_name SLI_3
mvi r1, 1
sli r3, r1, 31
check_r3 0x80000000

test_name SLI_4
mvi r1, 16
sli r3, r1, 31
check_r3 0

test_name SLI_7
mvi r3, 2
sli r3, r3, 2
check_r3 8

end

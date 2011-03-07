.include "macros.inc"

start

test_name SR_1
mvi r1, 1
mvi r2, 0
sr r3, r1, r2
check_r3 1

test_name SR_2
mvi r1, 0
mvi r2, 1
sr r3, r1, r2
check_r3 0

test_name SR_3
load r1 0x40000000
mvi r2, 30
sr r3, r1, r2
check_r3 1

test_name SR_4
load r1 0x40000000
mvi r2, 31
sr r3, r1, r2
check_r3 0

test_name SR_5
mvi r1, 16
mvi r2, 34
sr r3, r1, r2
check_r3 4

test_name SR_6
mvi r1, 2
sr r3, r1, r1
check_r3 0

test_name SR_7
mvi r3, 2
sr r3, r3, r3
check_r3 0

test_name SR_8
mvi r1, 0xfffffff0
mvi r2, 2
sr r3, r1, r2
check_r3 0xfffffffc

test_name SR_9
mvi r1, 0xfffffff0
mvi r2, 4
sr r3, r1, r2
check_r3 0xffffffff

end

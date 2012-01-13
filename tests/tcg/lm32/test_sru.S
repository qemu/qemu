.include "macros.inc"

start

test_name SRU_1
mvi r1, 1
mvi r2, 0
sru r3, r1, r2
check_r3 1

test_name SRU_2
mvi r1, 0
mvi r2, 1
sru r3, r1, r2
check_r3 0

test_name SRU_3
load r1 0x40000000
mvi r2, 30
sru r3, r1, r2
check_r3 1

test_name SRU_4
load r1 0x40000000
mvi r2, 31
sru r3, r1, r2
check_r3 0

test_name SRU_5
mvi r1, 16
mvi r2, 34
sru r3, r1, r2
check_r3 4

test_name SRU_6
mvi r1, 2
sru r3, r1, r1
check_r3 0

test_name SRU_7
mvi r3, 2
sru r3, r3, r3
check_r3 0

test_name SRU_8
mvi r1, 0xfffffff0
mvi r2, 2
sru r3, r1, r2
check_r3 0x3ffffffc

test_name SRU_9
mvi r1, 0xfffffff0
mvi r2, 4
sru r3, r1, r2
check_r3 0x0fffffff

end

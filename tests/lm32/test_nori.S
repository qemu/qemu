.include "macros.inc"

start

test_name NORI_1
mvi r1, 0
nori r3, r1, 0
check_r3 0xffffffff

test_name NORI_2
mvi r1, 0
nori r3, r1, 1
check_r3 0xfffffffe

test_name NORI_3
mvi r1, 1
nori r3, r1, 1
check_r3 0xfffffffe

test_name NORI_4
mvi r1, 1
nori r3, r1, 0
check_r3 0xfffffffe

test_name NORI_5
load r1 0xaa55aa55
nori r3, r1, 0x55aa
check_r3 0x55aa0000

test_name NORI_6
load r3 0xaa55aa55
nori r3, r3, 0x55aa
check_r3 0x55aa0000

end

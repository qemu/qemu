.include "macros.inc"

start

test_name XOR_1
mvi r1, 0
mvi r2, 0
xor r3, r1, r2
check_r3 0

test_name XOR_2
mvi r1, 0
mvi r2, 1
xor r3, r1, r2
check_r3 1

test_name XOR_3
mvi r1, 1
mvi r2, 1
xor r3, r1, r2
check_r3 0

test_name XOR_4
mvi r1, 1
mvi r2, 0
xor r3, r1, r2
check_r3 1

test_name XOR_5
load r1 0xaa55aa55
load r2 0x55aa55aa
xor r3, r1, r2
check_r3 0xffffffff

test_name XOR_6
load r1 0xaa550000
load r2 0x0000aa55
xor r3, r1, r2
check_r3 0xaa55aa55

test_name XOR_7
load r1 0xaa55aa55
xor r3, r1, r1
check_r3 0

test_name XOR_8
load r3 0xaa55aa55
xor r3, r3, r3
check_r3 0

end

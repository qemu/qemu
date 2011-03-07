.include "macros.inc"

start

test_name MUL_1
mvi r1, 0
mvi r2, 0
mul r3, r1, r2
check_r3 0

test_name MUL_2
mvi r1, 1
mvi r2, 0
mul r3, r1, r2
check_r3 0

test_name MUL_3
mvi r1, 0
mvi r2, 1
mul r3, r1, r2
check_r3 0

test_name MUL_4
mvi r1, 1
mvi r2, 1
mul r3, r1, r2
check_r3 1

test_name MUL_5
mvi r1, 2
mvi r2, -1
mul r3, r1, r2
check_r3 -2

test_name MUL_6
mvi r1, -2
mvi r2, -1
mul r3, r1, r2
check_r3 2

test_name MUL_7
mvi r1, 0x1234
mvi r2, 0x789
mul r3, r1, r2
check_r3 0x8929d4

test_name MUL_8
mvi r3, 4
mul r3, r3, r3
check_r3 16

test_name MUL_9
mvi r2, 2
mvi r3, 4
mul r3, r3, r2
check_r3 8

test_name MUL_10
load r1 0x12345678
load r2 0x7bcdef12
mul r3, r1, r2
check_r3 0xa801c70

test_name MUL_11
load r1 0x12345678
load r2 0xabcdef12
mul r3, r1, r2
check_r3 0x8a801c70

end

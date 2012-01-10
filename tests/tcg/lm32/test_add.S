.include "macros.inc"

start

test_name ADD_1
mvi r1, 0
mvi r2, 0
add r3, r1, r2
check_r3 0

test_name ADD_2
mvi r1, 0
mvi r2, 1
add r3, r1, r2
check_r3 1

test_name ADD_3
mvi r1, 1
mvi r2, 0
add r3, r1, r2
check_r3 1

test_name ADD_4
mvi r1, 1
mvi r2, -1
add r3, r1, r2
check_r3 0

test_name ADD_5
mvi r1, -1
mvi r2, 1
add r3, r1, r2
check_r3 0

test_name ADD_6
mvi r1, -1
mvi r2, 0
add r3, r1, r2
check_r3 -1

test_name ADD_7
mvi r1, 0
mvi r2, -1
add r3, r1, r2
check_r3 -1

test_name ADD_8
mvi r3, 2
add r3, r3, r3
check_r3 4

test_name ADD_9
mvi r1, 4
mvi r3, 2
add r3, r1, r3
check_r3 6

test_name ADD_10
mvi r1, 4
mvi r3, 2
add r3, r3, r1
check_r3 6

test_name ADD_11
mvi r1, 4
add r3, r1, r1
check_r3 8

test_name ADD_12
load r1 0x12345678
load r2 0xabcdef97
add r3, r1, r2
check_r3 0xbe02460f

end

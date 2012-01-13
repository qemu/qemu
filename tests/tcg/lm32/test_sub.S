.include "macros.inc"

start

test_name SUB_1
mvi r1, 0
mvi r2, 0
sub r3, r1, r2
check_r3 0

test_name SUB_2
mvi r1, 0
mvi r2, 1
sub r3, r1, r2
check_r3 -1

test_name SUB_3
mvi r1, 1
mvi r2, 0
sub r3, r1, r2
check_r3 1

test_name SUB_4
mvi r1, 1
mvi r2, -1
sub r3, r1, r2
check_r3 2

test_name SUB_5
mvi r1, -1
mvi r2, 1
sub r3, r1, r2
check_r3 -2

test_name SUB_6
mvi r1, -1
mvi r2, 0
sub r3, r1, r2
check_r3 -1

test_name SUB_7
mvi r1, 0
mvi r2, -1
sub r3, r1, r2
check_r3 1

test_name SUB_8
mvi r3, 2
sub r3, r3, r3
check_r3 0

test_name SUB_9
mvi r1, 4
mvi r3, 2
sub r3, r1, r3
check_r3 2

test_name SUB_10
mvi r1, 4
mvi r3, 2
sub r3, r3, r1
check_r3 -2

test_name SUB_11
mvi r1, 4
sub r3, r1, r1
check_r3 0

test_name SUB_12
load r1 0x12345678
load r2 0xabcdef97
sub r3, r1, r2
check_r3 0x666666e1

end

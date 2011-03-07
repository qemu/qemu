.include "macros.inc"

start

test_name DIVU_1
mvi r1, 0
mvi r2, 1
divu r3, r1, r2
check_r3 0

test_name DIVU_2
mvi r1, 1
mvi r2, 1
divu r3, r1, r2
check_r3 1

test_name DIVU_3
mvi r1, 0
mvi r2, 0
divu r3, r1, r2
check_excp 16

test_name DIVU_4
load r1 0xabcdef12
load r2 0x12345
divu r3, r1, r2
check_r3 0x9700

end

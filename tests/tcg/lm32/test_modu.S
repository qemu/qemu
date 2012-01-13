.include "macros.inc"

start

test_name MODU_1
mvi r1, 0
mvi r2, 1
modu r3, r1, r2
check_r3 0

test_name MODU_2
mvi r1, 1
mvi r2, 1
modu r3, r1, r2
check_r3 0

test_name MODU_3
mvi r1, 3
mvi r2, 2
modu r3, r1, r2
check_r3 1

test_name MODU_4
mvi r1, 0
mvi r2, 0
modu r3, r1, r2
check_excp 16

test_name MODU_5
load r1 0xabcdef12
load r2 0x12345
modu r3, r1, r2
check_r3 0x3c12

end

.include "macros.inc"

start

test_name ORI_1
mvi r1, 0
ori r3, r1, 0
check_r3 0

test_name ORI_2
mvi r1, 0
ori r3, r1, 1
check_r3 1

test_name ORI_3
mvi r1, 1
ori r3, r1, 1
check_r3 1

test_name ORI_4
mvi r1, 1
ori r3, r1, 0
check_r3 1

test_name ORI_5
load r1 0xaa55aa55
ori r3, r1, 0x55aa
check_r3 0xaa55ffff

test_name ORI_6
load r3 0xaa55aa55
ori r3, r3, 0x55aa
check_r3 0xaa55ffff

end

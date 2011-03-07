.include "macros.inc"

start

test_name ANDI_1
mvi r1, 0
andi r3, r1, 0
check_r3 0

test_name ANDI_2
mvi r1, 1
andi r3, r1, 1
check_r3 1

test_name ANDI_3
load r1 0x000f0000
andi r3, r1, 1
check_r3 0

test_name ANDI_4
load r1 0xffffffff
andi r3, r1, 0xffff
check_r3 0xffff

test_name ANDI_5
load r1 0xffffffff
andi r3, r1, 0
check_r3 0

test_name ANDI_6
load r3 0xffff55aa
andi r3, r3, 0xaaaa
check_r3 0x000000aa

end

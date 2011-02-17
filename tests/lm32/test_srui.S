.include "macros.inc"

start

test_name SRUI_1
mvi r1, 1
srui r3, r1, 0
check_r3 1

test_name SRUI_2
mvi r1, 0
srui r3, r1, 1
check_r3 0

test_name SRUI_3
load r1 0x40000000
srui r3, r1, 30
check_r3 1

test_name SRUI_4
load r1 0x40000000
srui r3, r1, 31
check_r3 0

test_name SRUI_5
mvi r3, 2
srui r3, r3, 2
check_r3 0

test_name SRUI_6
mvi r1, 0xfffffff0
srui r3, r1, 2
check_r3 0x3ffffffc

test_name SRUI_7
mvi r1, 0xfffffff0
srui r3, r1, 4
check_r3 0x0fffffff

end

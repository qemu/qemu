.include "macros.inc"

start

test_name CMPGEUI_1
mvi r1, 0
cmpgeui r3, r1, 0
check_r3 1

test_name CMPGEUI_2
mvi r1, 0
cmpgeui r3, r1, 1
check_r3 0

test_name CMPGEUI_3
mvi r1, 1
cmpgeui r3, r1, 0
check_r3 1

test_name CMPGEUI_4
mvi r1, 1
cmpgeui r3, r1, 1
check_r3 1

test_name CMPGEUI_5
mvi r1, 0
cmpgeui r3, r1, 0xffff
check_r3 0

test_name CMPGEUI_6
mvi r1, -1
cmpgeui r3, r1, 0
check_r3 1

test_name CMPGEUI_7
mvi r1, -1
cmpgeui r3, r1, 0xffff
check_r3 1

test_name CMPGEUI_8
mvi r3, 0
cmpgeui r3, r3, 1
check_r3 0

test_name CMPGEUI_9
mvi r3, 1
cmpgeui r3, r3, 0
check_r3 1

test_name CMPGEUI_10
mvi r3, 0
cmpgeui r3, r3, 0
check_r3 1

test_name CMPGEUI_11
mvi r1, 0
cmpgeui r3, r1, 0x8000
check_r3 0

test_name CMPGEUI_12
mvi r1, -1
cmpgeui r3, r1, 0x8000
check_r3 1

test_name CMPGEUI_13
ori r1, r0, 0x8000
cmpgeui r3, r1, 0x8000
check_r3 1

end

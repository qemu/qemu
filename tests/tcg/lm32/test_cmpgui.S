.include "macros.inc"

start

test_name CMPGUI_1
mvi r1, 0
cmpgui r3, r1, 0
check_r3 0

test_name CMPGUI_2
mvi r1, 0
cmpgui r3, r1, 1
check_r3 0

test_name CMPGUI_3
mvi r1, 1
cmpgui r3, r1, 0
check_r3 1

test_name CMPGUI_4
mvi r1, 1
cmpgui r3, r1, 1
check_r3 0

test_name CMPGUI_5
mvi r1, 0
cmpgui r3, r1, 0xffff
check_r3 0

test_name CMPGUI_6
mvi r1, -1
cmpgui r3, r1, 0
check_r3 1

test_name CMPGUI_7
mvi r1, -1
cmpgui r3, r1, 0xffff
check_r3 0

test_name CMPGUI_8
mvi r3, 0
cmpgui r3, r3, 1
check_r3 0

test_name CMPGUI_9
mvi r3, 1
cmpgui r3, r3, 0
check_r3 1

test_name CMPGUI_10
mvi r3, 0
cmpgui r3, r3, 0
check_r3 0

end

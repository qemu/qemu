.include "macros.inc"

start

test_name CMPG_1
mvi r1, 0
mvi r2, 0
cmpg r3, r1, r2
check_r3 0

test_name CMPG_2
mvi r1, 0
mvi r2, 1
cmpg r3, r1, r2
check_r3 0

test_name CMPG_3
mvi r1, 1
mvi r2, 0
cmpg r3, r1, r2
check_r3 1

test_name CMPG_4
mvi r1, 1
mvi r2, 1
cmpg r3, r1, r2
check_r3 0

test_name CMPG_5
mvi r1, 0
mvi r2, -1
cmpg r3, r1, r2
check_r3 1

test_name CMPG_6
mvi r1, -1
mvi r2, 0
cmpg r3, r1, r2
check_r3 0

test_name CMPG_7
mvi r1, -1
mvi r2, -1
cmpg r3, r1, r2
check_r3 0

test_name CMPG_8
mvi r3, 0
mvi r2, 1
cmpg r3, r3, r2
check_r3 0

test_name CMPG_9
mvi r3, 1
mvi r2, 0
cmpg r3, r3, r2
check_r3 1

test_name CMPG_10
mvi r3, 0
cmpg r3, r3, r3
check_r3 0

end

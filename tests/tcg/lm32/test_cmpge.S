.include "macros.inc"

start

test_name CMPGE_1
mvi r1, 0
mvi r2, 0
cmpge r3, r1, r2
check_r3 1

test_name CMPGE_2
mvi r1, 0
mvi r2, 1
cmpge r3, r1, r2
check_r3 0

test_name CMPGE_3
mvi r1, 1
mvi r2, 0
cmpge r3, r1, r2
check_r3 1

test_name CMPGE_4
mvi r1, 1
mvi r2, 1
cmpge r3, r1, r2
check_r3 1

test_name CMPGE_5
mvi r1, 0
mvi r2, -1
cmpge r3, r1, r2
check_r3 1

test_name CMPGE_6
mvi r1, -1
mvi r2, 0
cmpge r3, r1, r2
check_r3 0

test_name CMPGE_7
mvi r1, -1
mvi r2, -1
cmpge r3, r1, r2
check_r3 1

test_name CMPGE_8
mvi r3, 0
mvi r2, 1
cmpge r3, r3, r2
check_r3 0

test_name CMPGE_9
mvi r3, 1
mvi r2, 0
cmpge r3, r3, r2
check_r3 1

test_name CMPGE_10
mvi r3, 0
cmpge r3, r3, r3
check_r3 1

end

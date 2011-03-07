.include "macros.inc"

start

test_name CMPGU_1
mvi r1, 0
mvi r2, 0
cmpgu r3, r1, r2
check_r3 0

test_name CMPGU_2
mvi r1, 0
mvi r2, 1
cmpgu r3, r1, r2
check_r3 0

test_name CMPGU_3
mvi r1, 1
mvi r2, 0
cmpgu r3, r1, r2
check_r3 1

test_name CMPGU_4
mvi r1, 1
mvi r2, 1
cmpgu r3, r1, r2
check_r3 0

test_name CMPGU_5
mvi r1, 0
mvi r2, -1
cmpgu r3, r1, r2
check_r3 0

test_name CMPGU_6
mvi r1, -1
mvi r2, 0
cmpgu r3, r1, r2
check_r3 1

test_name CMPGU_7
mvi r1, -1
mvi r2, -1
cmpgu r3, r1, r2
check_r3 0

test_name CMPGU_8
mvi r3, 0
mvi r2, 1
cmpgu r3, r3, r2
check_r3 0

test_name CMPGU_9
mvi r3, 1
mvi r2, 0
cmpgu r3, r3, r2
check_r3 1

test_name CMPGU_10
mvi r3, 0
cmpgu r3, r3, r3
check_r3 0

end

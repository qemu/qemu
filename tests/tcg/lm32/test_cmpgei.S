.include "macros.inc"

start

test_name CMPGEI_1
mvi r1, 0
cmpgei r3, r1, 0
check_r3 1

test_name CMPGEI_2
mvi r1, 0
cmpgei r3, r1, 1
check_r3 0

test_name CMPGEI_3
mvi r1, 1
cmpgei r3, r1, 0
check_r3 1

test_name CMPGEI_4
mvi r1, 1
cmpgei r3, r1, 1
check_r3 1

test_name CMPGEI_5
mvi r1, 0
cmpgei r3, r1, -1
check_r3 1

test_name CMPGEI_6
mvi r1, -1
cmpgei r3, r1, 0
check_r3 0

test_name CMPGEI_7
mvi r1, -1
cmpgei r3, r1, -1
check_r3 1

test_name CMPGEI_8
mvi r3, 0
cmpgei r3, r3, 1
check_r3 0

test_name CMPGEI_9
mvi r3, 1
cmpgei r3, r3, 0
check_r3 1

test_name CMPGEI_10
mvi r3, 0
cmpgei r3, r3, 0
check_r3 1

test_name CMPGEI_11
mvi r1, 0
cmpgei r3, r1, -32768
check_r3 1

test_name CMPGEI_12
mvi r1, -1
cmpgei r3, r1, -32768
check_r3 1

test_name CMPGEI_13
mvi r1, -32768
cmpgei r3, r1, -32768
check_r3 1

end

.include "macros.inc"

start

test_name CMPEI_1
mvi r1, 0
cmpei r3, r1, 0
check_r3 1

test_name CMPEI_2
mvi r1, 0
cmpei r3, r1, 1
check_r3 0

test_name CMPEI_3
mvi r1, 1
cmpei r3, r1, 0
check_r3 0

test_name CMPEI_4
load r1 0xffffffff
cmpei r3, r1, -1
check_r3 1

test_name CMPEI_5
mvi r3, 0
cmpei r3, r3, 0
check_r3 1

test_name CMPEI_6
mvi r3, 0
cmpei r3, r3, 1
check_r3 0

end

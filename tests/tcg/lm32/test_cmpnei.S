.include "macros.inc"

start

test_name CMPNEI_1
mvi r1, 0
cmpnei r3, r1, 0
check_r3 0

test_name CMPNEI_2
mvi r1, 0
cmpnei r3, r1, 1
check_r3 1

test_name CMPNEI_3
mvi r1, 1
cmpnei r3, r1, 0
check_r3 1

test_name CMPNEI_4
load r1 0xffffffff
cmpnei r3, r1, -1
check_r3 0

test_name CMPNEI_5
mvi r3, 0
cmpnei r3, r3, 0
check_r3 0

test_name CMPNEI_6
mvi r3, 0
cmpnei r3, r3, 1
check_r3 1

end

.include "macros.inc"

start

test_name CMPGI_1
mvi r1, 0
cmpgi r3, r1, 0
check_r3 0

test_name CMPGI_2
mvi r1, 0
cmpgi r3, r1, 1
check_r3 0

test_name CMPGI_3
mvi r1, 1
cmpgi r3, r1, 0
check_r3 1

test_name CMPGI_4
mvi r1, 1
cmpgi r3, r1, 1
check_r3 0

test_name CMPGI_5
mvi r1, 0
cmpgi r3, r1, -1
check_r3 1

test_name CMPGI_6
mvi r1, -1
cmpgi r3, r1, 0
check_r3 0

test_name CMPGI_7
mvi r1, -1
cmpgi r3, r1, -1
check_r3 0

test_name CMPGI_8
mvi r3, 0
cmpgi r3, r3, 1
check_r3 0

test_name CMPGI_9
mvi r3, 1
cmpgi r3, r3, 0
check_r3 1

test_name CMPGI_10
mvi r3, 0
cmpgi r3, r3, 0
check_r3 0

test_name CMPGI_11
mvi r1, 0
cmpgi r3, r1, -32768
check_r3 1

test_name CMPGI_12
mvi r1, -1
cmpgi r3, r1, -32768
check_r3 1

test_name CMPGI_13
mvi r1, -32768
cmpgi r3, r1, -32768
check_r3 0

end

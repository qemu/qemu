.include "macros.inc"

start

test_name CMPE_1
mvi r1, 0
mvi r2, 0
cmpe r3, r1, r2
check_r3 1

test_name CMPE_2
mvi r1, 0
mvi r2, 1
cmpe r3, r1, r2
check_r3 0

test_name CMPE_3
mvi r1, 1
mvi r2, 0
cmpe r3, r1, r2
check_r3 0

test_name CMPE_4
mvi r3, 0
mvi r2, 1
cmpe r3, r3, r2
check_r3 0

test_name CMPE_5
mvi r3, 0
mvi r2, 0
cmpe r3, r3, r2
check_r3 1

test_name CMPE_6
mvi r3, 0
cmpe r3, r3, r3
check_r3 1

end

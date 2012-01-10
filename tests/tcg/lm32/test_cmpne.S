.include "macros.inc"

start

test_name CMPNE_1
mvi r1, 0
mvi r2, 0
cmpne r3, r1, r2
check_r3 0

test_name CMPNE_2
mvi r1, 0
mvi r2, 1
cmpne r3, r1, r2
check_r3 1

test_name CMPNE_3
mvi r1, 1
mvi r2, 0
cmpne r3, r1, r2
check_r3 1

test_name CMPNE_4
mvi r3, 0
mvi r2, 1
cmpne r3, r3, r2
check_r3 1

test_name CMPNE_5
mvi r3, 0
mvi r2, 0
cmpne r3, r3, r2
check_r3 0

test_name CMPNE_6
mvi r3, 0
cmpne r3, r3, r3
check_r3 0

end

.include "macros.inc"

start

test_name CMPGEU_1
mvi r1, 0
mvi r2, 0
cmpgeu r3, r1, r2
check_r3 1

test_name CMPGEU_2
mvi r1, 0
mvi r2, 1
cmpgeu r3, r1, r2
check_r3 0

test_name CMPGEU_3
mvi r1, 1
mvi r2, 0
cmpgeu r3, r1, r2
check_r3 1

test_name CMPGEU_4
mvi r1, 1
mvi r2, 1
cmpgeu r3, r1, r2
check_r3 1

test_name CMPGEU_5
mvi r1, 0
mvi r2, -1
cmpgeu r3, r1, r2
check_r3 0

test_name CMPGEU_6
mvi r1, -1
mvi r2, 0
cmpgeu r3, r1, r2
check_r3 1

test_name CMPGEU_7
mvi r1, -1
mvi r2, -1
cmpgeu r3, r1, r2
check_r3 1

test_name CMPGEU_8
mvi r3, 0
mvi r2, 1
cmpgeu r3, r3, r2
check_r3 0

test_name CMPGEU_9
mvi r3, 1
mvi r2, 0
cmpgeu r3, r3, r2
check_r3 1

test_name CMPGEU_10
mvi r3, 0
cmpgeu r3, r3, r3
check_r3 1

end

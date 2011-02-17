.include "macros.inc"

start

test_name MULI_1
mvi r1, 0
muli r3, r1, 0
check_r3 0

test_name MULI_2
mvi r1, 1
muli r3, r1, 0
check_r3 0

test_name MULI_3
mvi r1, 0
muli r3, r1, 1
check_r3 0

test_name MULI_4
mvi r1, 1
muli r3, r1, 1
check_r3 1

test_name MULI_5
mvi r1, 2
muli r3, r1, -1
check_r3 -2

test_name MULI_6
mvi r1, -2
muli r3, r1, -1
check_r3 2

test_name MULI_7
mvi r1, 0x1234
muli r3, r1, 0x789
check_r3 0x8929d4

test_name MULI_8
mvi r3, 4
muli r3, r3, 4
check_r3 16

end

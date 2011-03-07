.include "macros.inc"

start

test_name ADDI_1
mvi r1, 0
addi r3, r1, 0
check_r3 0

test_name ADDI_2
mvi r1, 0
addi r3, r1, 1
check_r3 1

test_name ADDI_3
mvi r1, 1
addi r3, r1, 0
check_r3 1

test_name ADDI_4
mvi r1, 1
addi r3, r1, -1
check_r3 0

test_name ADDI_5
mvi r1, -1
addi r3, r1, 1
check_r3 0

test_name ADDI_6
mvi r1, -1
addi r3, r1, 0
check_r3 -1

test_name ADDI_7
mvi r1, 0
addi r3, r1, -1
check_r3 -1

test_name ADDI_8
mvi r3, 4
addi r3, r3, 4
check_r3 8

test_name ADDI_9
mvi r3, 4
addi r3, r3, -4
check_r3 0

test_name ADDI_10
mvi r3, 4
addi r3, r3, -5
check_r3 -1

end


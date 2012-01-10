.include "macros.inc"

start

test_name XORI_1
mvi r1, 0
xori r3, r1, 0
check_r3 0

test_name XORI_2
mvi r1, 0
xori r3, r1, 1
check_r3 1

test_name XORI_3
mvi r1, 1
xori r3, r1, 1
check_r3 0

test_name XORI_4
mvi r1, 1
xori r3, r1, 0
check_r3 1

test_name XORI_5
load r1 0xaa55aa55
xori r3, r1, 0x5555
check_r3 0xaa55ff00

test_name XORI_6
load r3 0xaa55aa55
xori r3, r3, 0x5555
check_r3 0xaa55ff00

end

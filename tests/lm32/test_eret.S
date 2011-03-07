.include "macros.inc"

start

test_name ERET_1
mvi r1, 2
wcsr IE, r1
load ea mark
eret
tc_fail
bi 1f

mark:
tc_pass

1:
test_name ERET_2
rcsr r3, IE
check_r3 3

test_name ERET_3
mvi r1, 0
wcsr IE, r1
load ea mark2
eret
tc_fail
bi 1f

mark2:
tc_pass

1:
test_name ERET_4
rcsr r3, IE
check_r3 0

end


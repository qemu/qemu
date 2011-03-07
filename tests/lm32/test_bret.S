.include "macros.inc"

start

test_name BRET_1
mvi r1, 4
wcsr IE, r1
load ba mark
bret
tc_fail
bi 1f

mark:
tc_pass

1:
test_name BRET_2
rcsr r3, IE
check_r3 5

test_name BRET_3
mvi r1, 0
wcsr IE, r1
load ba mark2
bret
tc_fail
bi 1f

mark2:
tc_pass

1:
test_name BRET_4
rcsr r3, IE
check_r3 0

end


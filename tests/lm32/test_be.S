.include "macros.inc"

start

test_name BE_1
mvi r1, 0
mvi r2, 0
be r1, r2, 1f
tc_fail
bi 2f
1:
tc_pass
2:

test_name BE_2
mvi r1, 1
mvi r2, 0
be r1, r2, 1f
tc_pass
bi 2f
1:
tc_fail
2:

test_name BE_3
mvi r1, 0
mvi r2, 1
be r1, r2, 1f
tc_pass
bi 2f
1:
tc_fail
2:

bi 2f
1:
tc_pass
bi 3f
2:
test_name BE_4
mvi r1, 1
mvi r2, 1
be r1, r2, 1b
tc_fail
3:

end


.include "macros.inc"

start

test_name BNE_1
mvi r1, 0
mvi r2, 0
bne r1, r2, 1f
tc_pass
bi 2f
1:
tc_fail
2:

test_name BNE_2
mvi r1, 1
mvi r2, 0
bne r1, r2, 1f
tc_fail
bi 2f
1:
tc_pass
2:

test_name BNE_3
mvi r1, 0
mvi r2, 1
bne r1, r2, 1f
tc_fail
bi 2f
1:
tc_pass
2:

bi 2f
1:
tc_fail
bi 3f
2:
test_name BNE_4
mvi r1, 1
mvi r2, 1
bne r1, r2, 1b
tc_pass
3:

end


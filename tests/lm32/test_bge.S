.include "macros.inc"

start

test_name BGE_1
mvi r1, 0
mvi r2, 0
bge r1, r2, 1f
tc_fail
bi 2f
1:
tc_pass
2:

test_name BGE_2
mvi r1, 1
mvi r2, 0
bge r1, r2, 1f
tc_fail
bi 2f
1:
tc_pass
2:

test_name BGE_3
mvi r1, 0
mvi r2, 1
bge r1, r2, 1f
tc_pass
bi 2f
1:
tc_fail
2:

test_name BGE_4
mvi r1, 0
mvi r2, -1
bge r1, r2, 1f
tc_fail
bi 2f
1:
tc_pass
2:

test_name BGE_5
mvi r1, -1
mvi r2, 0
bge r1, r2, 1f
tc_pass
bi 2f
1:
tc_fail
2:

test_name BGE_6
mvi r1, -1
mvi r2, -1
bge r1, r2, 1f
tc_fail
bi 2f
1:
tc_pass
2:

bi 2f
1:
tc_pass
bi 3f
2:
test_name BGE_7
mvi r1, 1
mvi r2, 0
bge r1, r2, 1b
tc_fail
3:

end


.include "macros.inc"

start

test_name BGU_1
mvi r1, 0
mvi r2, 0
bgu r1, r2, 1f
tc_pass
bi 2f
1:
tc_fail
2:

test_name BGU_2
mvi r1, 1
mvi r2, 0
bgu r1, r2, 1f
tc_fail
bi 2f
1:
tc_pass
2:

test_name BGU_3
mvi r1, 0
mvi r2, 1
bgu r1, r2, 1f
tc_pass
bi 2f
1:
tc_fail
2:

test_name BGU_4
mvi r1, 0
mvi r2, -1
bgu r1, r2, 1f
tc_pass
bi 2f
1:
tc_fail
2:

test_name BGU_5
mvi r1, -1
mvi r2, 0
bgu r1, r2, 1f
tc_fail
bi 2f
1:
tc_pass
2:

test_name BGU_6
mvi r1, -1
mvi r2, -1
bgu r1, r2, 1f
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
test_name BGU_7
mvi r1, 1
mvi r2, 0
bgu r1, r2, 1b
tc_fail
3:

end


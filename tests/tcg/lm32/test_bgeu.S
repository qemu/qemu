.include "macros.inc"

start

test_name BGEU_1
mvi r1, 0
mvi r2, 0
bgeu r1, r2, 1f
tc_fail
bi 2f
1:
tc_pass
2:

test_name BGEU_2
mvi r1, 1
mvi r2, 0
bgeu r1, r2, 1f
tc_fail
bi 2f
1:
tc_pass
2:

test_name BGEU_3
mvi r1, 0
mvi r2, 1
bgeu r1, r2, 1f
tc_pass
bi 2f
1:
tc_fail
2:

test_name BGEU_4
mvi r1, 0
mvi r2, -1
bgeu r1, r2, 1f
tc_pass
bi 2f
1:
tc_fail
2:

test_name BGEU_5
mvi r1, -1
mvi r2, 0
bgeu r1, r2, 1f
tc_fail
bi 2f
1:
tc_pass
2:

test_name BGEU_6
mvi r1, -1
mvi r2, -1
bgeu r1, r2, 1f
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
test_name BGEU_7
mvi r1, 1
mvi r2, 0
bgeu r1, r2, 1b
tc_fail
3:

end


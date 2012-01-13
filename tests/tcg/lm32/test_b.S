.include "macros.inc"

start

test_name B_1
load r1 jump
b r1
tc_fail
end

jump:
tc_pass
end

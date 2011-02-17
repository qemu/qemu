.include "macros.inc"

start

test_name CALL_1
load r1 mark
call r1
return:

tc_fail
end

mark:
mv r3, ra
check_r3 return
end

.include "macros.inc"

start

test_name CALLI_1
calli mark
return:

tc_fail
end

mark:
mv r3, ra
check_r3 return
end

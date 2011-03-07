.include "macros.inc"

start

test_name RET_1
load ra mark
ret

tc_fail
end

mark:
tc_pass
end

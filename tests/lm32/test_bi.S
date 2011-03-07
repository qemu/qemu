.include "macros.inc"

start

test_name BI_1
bi jump
tc_fail
end

jump_back:
tc_pass
end

jump:
tc_pass

test_name BI_2
bi jump_back
tc_fail

end



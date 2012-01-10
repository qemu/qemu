.include "macros.inc"

start

test_name BREAK_1
mvi r1, 1
wcsr IE, r1
insn:
break
check_excp 1

test_name BREAK_2
mv r3, ba
check_r3 insn

test_name BREAK_3
rcsr r3, IE
check_r3 4

end

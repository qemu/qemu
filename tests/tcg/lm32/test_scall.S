.include "macros.inc"

start

test_name SCALL_1
mvi r1, 1
wcsr IE, r1
# we are running in a semi hosted environment
# therefore we have to set r8 to some unused system
# call
mvi r8, 0
insn:
scall
check_excp 64

test_name SCALL_2
mv r3, ea
check_r3 insn

test_name SCALL_3
rcsr r3, IE
check_r3 2

end

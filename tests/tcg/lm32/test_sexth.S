.include "macros.inc"

start

test_name SEXTH_1
mvi r1, 0
sexth r3, r1
check_r3 0

test_name SEXTH_2
load r1 0x7fff
sexth r3, r1
check_r3 0x00007fff

test_name SEXTH_3
load r1 0x8000
sexth r3, r1
check_r3 0xffff8000

end

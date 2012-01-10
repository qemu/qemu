.include "macros.inc"

start

test_name SEXTB_1
mvi r1, 0
sextb r3, r1
check_r3 0

test_name SEXTB_2
mvi r1, 0x7f
sextb r3, r1
check_r3 0x0000007f

test_name SEXTB_3
mvi r1, 0x80
sextb r3, r1
check_r3 0xffffff80

end

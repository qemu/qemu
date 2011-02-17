.include "macros.inc"

start

test_name ORHI_1
mvi r1, 0
orhi r3, r1, 0
check_r3 0

test_name ORHI_2
mvi r1, 0
orhi r3, r1, 1
check_r3 0x00010000

test_name ORHI_3
load r1 0x00010000
orhi r3, r1, 1
check_r3 0x00010000

test_name ORHI_4
mvi r1, 1
orhi r3, r1, 0
check_r3 1

test_name ORHI_5
load r1 0xaa55aa55
orhi r3, r1, 0x55aa
check_r3 0xffffaa55

test_name ORHI_6
load r3 0xaa55aa55
orhi r3, r3, 0x55aa
check_r3 0xffffaa55

end

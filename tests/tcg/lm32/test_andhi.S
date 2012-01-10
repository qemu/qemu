.include "macros.inc"

start

test_name ANDHI_1
mvi r1, 0
andhi r3, r1, 0
check_r3 0

test_name ANDHI_2
mvi r1, 1
andhi r3, r1, 1
check_r3 0

test_name ANDHI_3
load r1 0x000f0000
andhi r3, r1, 1
check_r3 0x00010000

test_name ANDHI_4
load r1 0xffffffff
andhi r3, r1, 0xffff
check_r3 0xffff0000

test_name ANDHI_5
load r1 0xffffffff
andhi r3, r1, 0
check_r3 0

test_name ANDHI_6
load r3 0x55aaffff
andhi r3, r3, 0xaaaa
check_r3 0x00aa0000

end

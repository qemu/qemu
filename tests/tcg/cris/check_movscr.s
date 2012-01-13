# mach: crisv3 crisv8 crisv10 crisv32
# output: 42\nffffff85\n7685\nffff8765\n0\n

; Move constant byte, word, dword to register.  Check that sign-extension
; is performed.

 .include "testutils.inc"
 start
 moveq -1,r3
 movs.b 0x42,r3
 checkr3 42

 movs.b 0x85,r3
 test_move_cc 1 0 0 0
 checkr3 ffffff85

 movs.w 0x7685,r3
 test_move_cc 0 0 0 0
 checkr3 7685

 movs.w 0x8765,r3
 test_move_cc 1 0 0 0
 checkr3 ffff8765

 movs.w 0,r3
 test_move_cc 0 1 0 0
 checkr3 0

 quit

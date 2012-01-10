# mach: crisv3 crisv8 crisv10 crisv32
# output: ffffff42\n94\nffff4321\n9234\n76543210\n76540000\n

; Move constant byte, word, dword to register.  Check that no extension is
; performed, that only part of the register is set.

 .include "testutils.inc"
 startnostack
 moveq -1,r3
 move.b 0x42,r3
 test_move_cc 0 0 0 0
 checkr3 ffffff42

 moveq 0,r3
 move.b 0x94,r3
 test_move_cc 1 0 0 0
 checkr3 94

 moveq -1,r3
 move.w 0x4321,r3
 test_move_cc 0 0 0 0
 checkr3 ffff4321

 moveq 0,r3
 move.w 0x9234,r3
 test_move_cc 1 0 0 0
 checkr3 9234

 move.d 0x76543210,r3
 test_move_cc 0 0 0 0
 checkr3 76543210

 move.w 0,r3
 test_move_cc 0 1 0 0
 checkr3 76540000

 quit

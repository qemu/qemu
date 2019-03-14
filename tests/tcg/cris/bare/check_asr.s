# mach: crisv0 crisv3 crisv8 crisv10 crisv32
# output: ffffffff\n1\nffffffff\nffffffff\n5a67f\nffffffff\nffffffff\nffffffff\nf699fc67\nffffffff\n1\nffffffff\nffffffff\n5a67f\nda67ffff\nda67ffff\nda67ffff\nda67fc67\nffffffff\nffffffff\n1\nffffffff\nffffffff\n5a670007\nda67f1ff\nda67f1ff\nda67f1ff\nda67f1e7\nffffffff\nffffffff\n1\nffffffff\nffffffff\nffffffff\n5a67f1ff\n5a67f1f9\n0\n5a670000\n

 .include "testutils.inc"
 start
 moveq -1,r3
 asrq 0,r3
 test_move_cc 1 0 0 0
 checkr3 ffffffff

 moveq 2,r3
 asrq 1,r3
 test_move_cc 0 0 0 0
 checkr3 1

 moveq -1,r3
 asrq 31,r3
 test_move_cc 1 0 0 0
 checkr3 ffffffff

 moveq -1,r3
 asrq 15,r3
 test_move_cc 1 0 0 0
 checkr3 ffffffff

 move.d 0x5a67f19f,r3
 asrq 12,r3
 test_move_cc 0 0 0 0
 checkr3 5a67f

 move.d 0xda67f19f,r3
 move.d 31,r4
 asr.d r4,r3
 test_move_cc 1 0 0 0
 checkr3 ffffffff

 move.d 0xda67f19f,r3
 move.d 32,r4
 asr.d r4,r3
 test_move_cc 1 0 0 0
 checkr3 ffffffff

 move.d 0xda67f19f,r3
 move.d 33,r4
 asr.d r4,r3
 test_move_cc 1 0 0 0
 checkr3 ffffffff

 move.d 0xda67f19f,r3
 move.d 66,r4
 asr.d r4,r3
 test_move_cc 1 0 0 0
 checkr3 f699fc67

 moveq -1,r3
 moveq 0,r4
 asr.d r4,r3
 test_move_cc 1 0 0 0
 checkr3 ffffffff

 moveq 2,r3
 moveq 1,r4
 asr.d r4,r3
 test_move_cc 0 0 0 0
 checkr3 1

 moveq -1,r3
 moveq 31,r4
 asr.d r4,r3
 test_move_cc 1 0 0 0
 checkr3 ffffffff

 moveq -1,r3
 moveq 15,r4
 asr.d r4,r3
 test_move_cc 1 0 0 0
 checkr3 ffffffff

 move.d 0x5a67f19f,r3
 moveq 12,r4
 asr.d r4,r3
 test_move_cc 0 0 0 0
 checkr3 5a67f

 move.d 0xda67f19f,r3
 move.d 31,r4
 asr.w r4,r3
 test_move_cc 1 0 0 0
 checkr3 da67ffff

 move.d 0xda67f19f,r3
 move.d 32,r4
 asr.w r4,r3
 test_move_cc 1 0 0 0
 checkr3 da67ffff

 move.d 0xda67f19f,r3
 move.d 33,r4
 asr.w r4,r3
 test_move_cc 1 0 0 0
 checkr3 da67ffff

 move.d 0xda67f19f,r3
 move.d 66,r4
 asr.w r4,r3
 test_move_cc 1 0 0 0
 checkr3 da67fc67

 moveq -1,r3
 moveq 0,r4
 asr.w r4,r3
 test_move_cc 1 0 0 0
 checkr3 ffffffff

 moveq -1,r3
 moveq 1,r4
 asr.w r4,r3
 test_move_cc 1 0 0 0
 checkr3 ffffffff

 moveq 2,r3
 moveq 1,r4
 asr.w r4,r3
 test_move_cc 0 0 0 0
 checkr3 1

 moveq -1,r3
 moveq 31,r4
 asr.w r4,r3
 test_move_cc 1 0 0 0
 checkr3 ffffffff

 moveq -1,r3
 moveq 15,r4
 asr.w r4,r3
 test_move_cc 1 0 0 0
 checkr3 ffffffff

 move.d 0x5a67719f,r3
 moveq 12,r4
 asr.w r4,r3
 test_move_cc 0 0 0 0
 checkr3 5a670007

 move.d 0xda67f19f,r3
 move.d 31,r4
 asr.b r4,r3
 test_move_cc 1 0 0 0
 checkr3 da67f1ff

 move.d 0xda67f19f,r3
 move.d 32,r4
 asr.b r4,r3
 test_move_cc 1 0 0 0
 checkr3 da67f1ff

 move.d 0xda67f19f,r3
 move.d 33,r4
 asr.b r4,r3
 test_move_cc 1 0 0 0
 checkr3 da67f1ff

 move.d 0xda67f19f,r3
 move.d 66,r4
 asr.b r4,r3
 test_move_cc 1 0 0 0
 checkr3 da67f1e7

 moveq -1,r3
 moveq 0,r4
 asr.b r4,r3
 test_move_cc 1 0 0 0
 checkr3 ffffffff

 moveq -1,r3
 moveq 1,r4
 asr.b r4,r3
 test_move_cc 1 0 0 0
 checkr3 ffffffff

 moveq 2,r3
 moveq 1,r4
 asr.b r4,r3
 test_move_cc 0 0 0 0
 checkr3 1

 moveq -1,r3
 moveq 31,r4
 asr.b r4,r3
 test_move_cc 1 0 0 0
 checkr3 ffffffff

 moveq -1,r3
 moveq 15,r4
 asr.b r4,r3
 test_move_cc 1 0 0 0
 checkr3 ffffffff

 moveq -1,r3
 moveq 7,r4
 asr.b r4,r3
 test_move_cc 1 0 0 0
 checkr3 ffffffff

; FIXME: was wrong.
 move.d 0x5a67f19f,r3
 moveq 12,r4
 asr.b r4,r3
 test_move_cc 1 0 0 0
 checkr3 5a67f1ff

; FIXME: was wrong.
 move.d 0x5a67f19f,r3
 moveq 4,r4
 asr.b r4,r3
 test_move_cc 1 0 0 0
 checkr3 5a67f1f9

 move.d 0x5a67f19f,r3
 asrq 31,r3
 test_move_cc 0 1 0 0
 checkr3 0

 move.d 0x5a67419f,r3
 moveq 16,r4
 asr.w r4,r3
 test_move_cc 0 1 0 0
 checkr3 5a670000

 quit

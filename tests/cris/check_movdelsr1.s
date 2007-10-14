# mach: crisv0 crisv3 crisv8 crisv10 crisv32
# output: aa117acd\n
# output: eeaabb42\n

; Bug with move to special register in delay slot, due to
; special flush-insn-cache simulator use.  Ordinary move worked;
; special register caused branch to fail.

 .include "testutils.inc"
 start
 move -1,srp

 move.d 0xaa117acd,r1
 moveq 3,r9
 cmpq 1,r9
 bhi 0f
 move.d r1,r3

 fail
0:
 checkr3 aa117acd

 move.d 0xeeaabb42,r1
 moveq 3,r9
 cmpq 1,r9
 bhi 0f
 move r1,srp

 fail
0:
 move srp,r3
 checkr3 eeaabb42
 quit

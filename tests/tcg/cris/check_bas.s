# mach: crisv32
# output: 0\n0\n0\nfb349abc\n0\n12124243\n0\n0\neab5baad\n0\nefb37832\n

 .include "testutils.inc"
 start
x:
 setf zncv
 bsr 0f
 nop
0:
 test_cc 1 1 1 1
 move srp,r3
 sub.d 0b,r3
 checkr3 0

 bas 1f,mof
 moveq 0,r0
6:
 nop
 quit

2:
 move srp,r3
 sub.d 3f,r3
 checkr3 0
 move srp,r4
 subq 4,r4
 move.d [r4],r3
 checkr3 fb349abc

 basc 4f,mof
 nop
 .dword 0x12124243
7:
 nop
 quit

8:
 move mof,r3
 sub.d 7f,r3
 checkr3 0

 move mof,r4
 subq 4,r4
 move.d [r4],r3
 checkr3 eab5baad

 jasc 9f,mof
 nop
 .dword 0xefb37832
0:
 quit

 quit
9:
 move mof,r3
 sub.d 0b,r3
 checkr3 0

 move mof,r4
 subq 4,r4
 move.d [r4],r3
 checkr3 efb37832

 quit

4:
 move mof,r3
 sub.d 7b,r3
 checkr3 0
 move mof,r4
 subq 4,r4
 move.d [r4],r3
 checkr3 12124243
 basc 5f,bz
 moveq 0,r3
 .dword 0x7634aeba
 quit

 .space 32770,0
1:
 move mof,r3
 sub.d 6b,r3
 checkr3 0

 bsrc 2b
 nop
 .dword 0xfb349abc
3:

 quit

5:
 move mof,r3
 sub.d 7b,r3
 checkr3 0
 move.d 8b,r6
 jasc r6,mof
 nop
 .dword 0xeab5baad
7:
 quit

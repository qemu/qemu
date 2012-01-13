# mach: crisv3 crisv8 crisv10 crisv32
# output: 0\n0\n0\n0\n0\n0\n

# Test that jsr Rn and jsr [PC+] work.

 .include "testutils.inc"
 start
x:
 move.d 0f,r6
 setf nzvc
 jsr r6
 .if 1; ..asm.arch.cris.v32
 nop
 .endif
0:
 test_move_cc 1 1 1 1
 move srp,r3
 sub.d 0b,r3
 checkr3 0

 move.d 1f,r0
 setf nzvc
 jsr r0
 .if 1 ; ..asm.arch.cris.v32
 moveq 0,r0
 .endif
6:
 nop
 quit

2:
 test_move_cc 0 0 0 0
 move srp,r3
 sub.d 3f,r3
 checkr3 0
 jsr 4f
 .if 1 ; ..asm.arch.cris.v32
 nop
 .endif
7:
 nop
 quit

8:
 move srp,r3
 sub.d 7b,r3
 checkr3 0
 quit

4:
 move srp,r3
 sub.d 7b,r3
 checkr3 0
 move.d 5f,r3
 jump r3
 .if 1; ..asm.arch.cris.v32
 moveq 0,r3
 .endif
 quit

 .space 32770,0
1:
 test_move_cc 1 1 1 1
 move srp,r3
 sub.d 6b,r3
 checkr3 0

 clearf cznv
 jsr 2b
 .if 1; ..asm.arch.cris.v32
 nop
 .endif
3:

 quit

5:
 move srp,r3
 sub.d 7b,r3
 checkr3 0
 jump 8b
 .if 1 ; ..asm.arch.cris.v32
 nop
 .endif
 quit

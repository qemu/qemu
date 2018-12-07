# mach: crisv3 crisv8 crisv10 crisv32
# output: ffffff00\n

; A bug resulting in a non-effectual clear.b discovered running the GCC
; testsuite; jump actually wrote to p0.

 .include "testutils.inc"

 start
 jump 1f
 nop
 .p2align 8
1:
 move.d y,r4

 .if 0 ;0 == ..asm.arch.cris.v32
; There was a bug causing this insn to set special register p0
; (byte-clear) to 8 (low 8 bits of location after insn).
 jump [r4+]
 .endif

1:
 move.d 0f,r4

; The corresponding bug would cause this insn too, to set p0.
 jump r4
 nop
 quit
0:
 moveq -1,r3
 clear.b r3
 checkr3 ffffff00
 quit

y:
 .dword 1b

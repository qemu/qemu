# mach: crisv32
# output: fffffffe\n
# output: fffffffe\n

; Check basic integral-write semantics regarding flags.

 .include "testutils.inc"
 start

 move.d 0, $r3	
; A write that works.  Check that flags are set correspondingly.
 move.d d,r4
 ;; store to bring it into the tlb with the right prot bits
 move.d r3,[r4]
 moveq -2,r5
 setf c
 clearf p
 move.d [r4],r3
 ax
 move.d r5,[r4]
 move.d [r4],r3

 bcc 0f
 nop
 fail

0:
 checkr3 fffffffe

; A write that fails; check flags too.
 move.d d,r4
 moveq 23,r5
 setf p
 clearf c
 move.d [r4],r3
 ax
 move.d r5,[r4]
 move.d [r4],r3

 bcs 0f
 nop
 fail

0:
 checkr3 fffffffe
 quit

 .data
d:
 .dword 42424242

# mach: crisv3 crisv8 crisv10 crisv32
# output: ffffff00\nffff0000\n0\nffffff00\nffff0000\n0\nffffff00\nffff0000\n0\nbb113344\n664433aa\ncc557788\nabcde012\nabcde000\n77880000\n0\n

# Test generic "move Ps,[]" and "move [],Pd" insns; the ones with
# functionality common to all models.

 .include "testutils.inc"
 start

 .data
filler:
 .byte 0xaa
 .word 0x4433
 .dword 0x55778866
 .byte 0xcc

 .text
; Test that writing to zero-registers is a nop
 .if 0
 ; We used to just ignore the writes, but now an error is emitted.  We
 ; keep the test-code but disabled, in case we need to change this again.
 move 0xaa,p0
 move 0x4433,p4
 move 0x55774433,p8
 .endif

 moveq -1,r3
 setf zcvn
 clear.b r3
 test_cc 1 1 1 1
 checkr3 ffffff00

 moveq -1,r3
 clearf zcvn
 clear.w r3
 test_cc 0 0 0 0
 checkr3 ffff0000

 moveq -1,r3
 clear.d r3
 checkr3 0

; "Write" using ordinary memory references too.
 .if 0 ; See ".if 0" above.
 move.d filler,r6
 move [r6],p0
 move [r6],p4
 move [r6],p8
 .endif

# ffffff00\nffff0000\n0\nffffff00\nffff0000\n0\nbb113344\n664433aa\ncc557788\nabcde012\nabcde000\n77880000\n0\n

 moveq -1,r3
 clear.b r3
 checkr3 ffffff00

 moveq -1,r3
 clear.w r3
 checkr3 ffff0000

 moveq -1,r3
 clear.d r3
 checkr3 0

; And postincremented.
 .if 0 ; See ".if 0" above.
 move [r6+],p0
 move [r6+],p4
 move [r6+],p8
 .endif

# ffffff00\nffff0000\n0\nbb113344\n664433aa\ncc557788\nabcde012\nabcde000\n77880000\n0\n

 moveq -1,r3
 clear.b r3
 checkr3 ffffff00

 moveq -1,r3
 clear.w r3
 checkr3 ffff0000

 moveq -1,r3
 clear.d r3
 checkr3 0

; Now see that we can write to the registers too.
# bb113344\n664433aa\ncc557788\nabcde012\nabcde000\n77880000\n0\n
; [PC+]
 move.d filler,r9
 move 0xbb113344,srp
 move srp,r3
 checkr3 bb113344

; [R+]
 move [r9+],srp
 move srp,r3
 checkr3 664433aa

; [R]
 move [r9],srp
 move srp,r3
 checkr3 cc557788

; And check writing to memory, clear and srp.

 move.d filler,r9
 move 0xabcde012,srp
 setf zcvn
 move srp,[r9+]
 test_cc 1 1 1 1
 subq 4,r9
 move.d [r9],r3
 checkr3 abcde012

 clearf zcvn
 clear.b [r9]
 test_cc 0 0 0 0
 move.d [r9],r3
 checkr3 abcde000

 addq 2,r9
 clear.w [r9+]
 subq 2,r9
 move.d [r9],r3
 checkr3 77880000

 clear.d [r9]
 move.d [r9],r3
 checkr3 0

 quit

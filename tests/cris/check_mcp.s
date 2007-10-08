# mach: crisv32
# output: fffffffe\n1\n1ffff\nfffffffe\ncc463bdc\n4c463bdc\n0\n

 .include "testutils.inc"
 start

; Set R, clear C.
 move 0x100,ccs
 moveq -5,r3
 move 2,mof
 mcp mof,r3
 test_cc 1 0 0 0
 checkr3 fffffffe

 moveq 2,r3
 move -1,srp
 mcp srp,r3
 test_cc 0 0 0 0
 checkr3 1

 move 0xffff,srp
 move srp,r3
 mcp srp,r3
 test_cc 0 0 0 0
 checkr3 1ffff

 move -1,mof
 move mof,r3
 mcp mof,r3
 test_cc 1 0 0 0
 checkr3 fffffffe

 move 0x5432f789,mof
 move.d 0x78134452,r3
 mcp mof,r3
 test_cc 1 0 1 0
 checkr3 cc463bdc

 move 0x80000000,srp
 mcp srp,r3
 test_cc 0 0 1 0
 checkr3 4c463bdc

 move 0xb3b9c423,srp
 mcp srp,r3
 test_cc 0 1 0 0
 checkr3 0

 quit

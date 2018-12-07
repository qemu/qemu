# mach: crisv32
# output: 4455aa77\n4455aa77\nee19ccff\nff22\n4455aa77\nff224455\n55aa77ff\n

 .include "testutils.inc"
 .data
x:
 .dword 0x55aa77ff
 .dword 0xccff2244
 .dword 0x88ccee19

 start
 setf cv
 moveq -1,r0
 move.d x-32768,r5
 move.d 32769,r6
 addi r6.b,r5,acr
 test_cc 0 0 1 1
 move.d [acr],r3
 checkr3 4455aa77

 addu.w 32771,r5
 setf znvc
 moveq -1,r8
 addi r8.w,r5,acr
 test_cc 1 1 1 1
 move.d [acr],r3
 checkr3 4455aa77

 moveq 5,r10
 clearf znvc
 addi r10.b,acr,acr
 test_cc 0 0 0 0
 move.d [acr],r3
 checkr3 ee19ccff

 subq 1,r5
 move.d r5,r8
 subq 1,r8
 moveq 1,r9
 addi r9.d,r8,acr
 test_cc 0 0 0 0
 movu.w [acr],r3
 checkr3 ff22

 moveq -2,r11
 addi r11.w,acr,acr
 move.d [acr],r3
 checkr3 4455aa77

 moveq 5,r9
 addi r9.d,acr,acr
 subq 18,acr
 move.d [acr],r3
 checkr3 ff224455

 move.d -76789888/4,r12
 addi r12.d,r5,acr
 add.d 76789886,acr
 move.d [acr],r3
 checkr3 55aa77ff

 quit

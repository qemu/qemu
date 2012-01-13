@ Checks whether iwMMXt is functional.
.code	32
.globl	main

main:
ldr	r0, =data0
ldr	r1, =data1
ldr	r2, =data2
#ifndef FPA
wldrd	wr0, [r0, #0]
wldrd	wr1, [r0, #8]
wldrd	wr2, [r1, #0]
wldrd	wr3, [r1, #8]
wsubb	wr2, wr2, wr0
wsubb	wr3, wr3, wr1
wldrd	wr0, [r2, #0]
wldrd	wr1, [r2, #8]
waddb	wr0, wr0, wr2
waddb	wr1, wr1, wr3
wstrd	wr0, [r2, #0]
wstrd	wr1, [r2, #8]
#else
ldfe	f0, [r0, #0]
ldfe	f1, [r0, #8]
ldfe	f2, [r1, #0]
ldfe	f3, [r1, #8]
adfdp	f2, f2, f0
adfdp	f3, f3, f1
ldfe	f0, [r2, #0]
ldfe	f1, [r2, #8]
adfd	f0, f0, f2
adfd	f1, f1, f3
stfe	f0, [r2, #0]
stfe	f1, [r2, #8]
#endif
mov	r0, #1
mov	r1, r2
mov	r2, #0x11
swi	#0x900004
mov	r0, #0
swi	#0x900001

.data
data0:
.string	"aaaabbbbccccdddd"
data1:
.string	"bbbbccccddddeeee"
data2:
.string	"hvLLWs\x1fsdrs9\x1fNJ-\n"

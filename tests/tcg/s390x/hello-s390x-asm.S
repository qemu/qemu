/*
 * Hello, World! in assembly.
 */

.globl _start
_start:

/* puts("Hello, World!"); */
lghi %r2,1
larl %r3,foo
lgrl %r4,foo_len
svc 4

/* exit(0); */
xgr %r2,%r2
svc 1

.align 2
foo: .asciz "Hello, World!\n"
foo_end:
.align 8
foo_len: .quad foo_end-foo

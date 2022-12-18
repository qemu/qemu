/*
 * Purpose: test function calls and duplex instructions.
 * The string "Hello there, I'm a test string!" with the first letter replaced
 * with a capital L should be printed out.
 */

#define SYS_write 64
#define FD_STDOUT 1

    .text
    .globl    test
test:
    {
        jumpr r31
        memb(r0+#0) = #76
    }
.Lfunc_end0:
.Ltmp0:
    .size    test, .Ltmp0-test

    .globl    _start
_start:
    {
        r0 = ##dummy_buffer
        allocframe(#0)
        call test
    }
    {
        call write
    }
    {
        deallocframe
        jump pass
    }
.Lfunc_end1:
.Ltmp1:
    .size    _start, .Ltmp1-_start

write:
    {
        r6 = #SYS_write
        r0 = #FD_STDOUT
        r1 = ##dummy_buffer
        r2 = #33
    }
    {
        trap0(#1)
    }
    {
        jumpr r31
    }

.Lfunc_end2:
.Ltmp2:
    .size    write, .Ltmp2-write

    .type    dummy_buffer,@object
    .data
    .globl    dummy_buffer
    .p2align    3
dummy_buffer:
    .string    "Hello there, I'm a test string!\n"
    .space 223
    .size    dummy_buffer, 256

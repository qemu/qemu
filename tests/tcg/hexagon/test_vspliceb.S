/*
 * Purpose: test example, verify the soundness of the vspliceb operation
 * the operation is a binary splice of two 64bit operators.
 *
 *  vspliceb(0xffffffffffffffff,0x0000000000000000,5) = 0x000000ffffffffff.
 */
    .text
    .globl _start

_start:
    {
        r0 = #-1
        r1 = #-1
    }
    {
        r2 = #0
        r3 = #0
    }
    {
        r5:4 = vspliceb(r1:0, r3:2, #5)
    }
    {
        p0 = cmp.eq(r4, #-1); if (p0.new) jump:t test2
        jump fail
    }

test2:
    {
        p0 = cmp.eq(r5, #255); if (p0.new) jump:t pass
        jump fail
    }

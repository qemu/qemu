/*
 * Purpose: test example, verify the soundness of the cl[01] operations.
 *
 * The number 0x000001aa has 23 leading zeroes
 * they become 55 when considered as 64 bit register
 * and it has 1 trailing zero.
 */
    .text
    .globl _start

_start:
    {
        r0 = #426
        r1 = #0
    }
    {
        r2 = cl0(r0)
    }
    {
        p0 = cmp.eq(r2, #23); if (p0.new) jump:t test2
        jump fail
    }

test2:
    {
        r2 = cl0(r1:0)
    }
    {
        p0 = cmp.eq(r2, #55); if (p0.new) jump:t test3
        jump fail
    }

test3:
    {
        r2 = ct0(r0)
    }
    {
        p0 = cmp.eq(r2, #1); if (p0.new) jump:t pass
        jump fail
    }

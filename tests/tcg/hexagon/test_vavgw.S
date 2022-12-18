/*
 * Purpose: test example, verify the soundness of the vavgw operation.
 *
 * 0x00030001 averaged with 0x00010003 results 0x00020002.
 */

    .text
    .globl _start

_start:
    {
        r0 = #3
        r1 = #1
    }
    {
        r2 = #1
        r3 = #3
    }
    {
        r1:0 = vavgw(r1:0, r3:2):crnd
    }
    {
        p0 = cmp.eq(r0, #2); if (p0.new) jump:t test2
        jump fail
    }

test2:
    {
        p0 = cmp.eq(r1, #2); if (p0.new) jump:t pass
        jump fail
    }

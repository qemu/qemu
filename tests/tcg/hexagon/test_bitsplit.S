/* Purpose: test example, verify the soundness of the bitsplit operation */

    .text
    .globl _start

_start:
    {
        r1 = #187
    }
    {
        r3:2 = bitsplit(r1, #3)
    }
    {
        p0 = cmp.eq(r2, #3); if (p0.new) jump:t test2
        jump fail
    }

test2:
    {
        p0 = cmp.eq(r3, #23); if (p0.new) jump:t pass
        jump fail
    }

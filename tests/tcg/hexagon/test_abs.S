/* Purpose: test example, verify the soundness of the abs operation */

    .text
    .globl _start

_start:
    {
        r1 = #-2
        r2 = #2
    }
    {
        r3 = abs(r1)
    }
    {
        p0 = cmp.eq(r3, r2); if (p0.new) jump:t pass
        jump fail
    }

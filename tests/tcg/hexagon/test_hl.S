/* Purpose: test example, verify the soundness of the high/low assignment */

    .text
    .globl _start

_start:
    {
        r0.H = #42
    }
    {
        r0.L = #69
    }
    {
        p0 = cmp.eq(r0, #2752581); if (p0.new) jump:t pass
        jump fail
    }

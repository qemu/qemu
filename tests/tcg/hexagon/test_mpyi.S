/* Purpose: test a simple multiplication operation */

    .text
    .globl _start

_start:
    {
        r1 = #4
        r2 = #6
    }
    {
        r3 = mpyi(r1, r2)
    }
    {
        p0 = cmp.eq(r3, #24); if (p0.new) jump:t pass
        jump fail
    }

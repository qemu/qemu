/* Purpose: test the soundness of the vlsrw operation */

    .text
    .globl _start

_start:
    {
        r0 = #0x00000001
        r1 = #0x00000001
    }
    {
        r1:0 = vlsrw(r1:0, #1)
    }
    {
        r0 = add(r0, r1)
    }
    {
        p0 = cmp.eq(r0, #0); if (p0.new) jump:t pass
        jump fail
    }

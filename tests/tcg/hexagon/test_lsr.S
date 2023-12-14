/* Purpose: test the soundness of the lsr operation */

    .text
    .globl _start

_start:
    {
        r0 = #-56984
        r1 = #2147483647
    }
    {
        r2 = #0x19
    }
    {
        r0 &= lsr(r1, r2)
    }
    {
        p0  =  cmp.eq(r0, #0x28); if (p0.new) jump:t test2
        jump fail
    }

test2:
    {
        r0 = #0x0000000a
        r1 = #0x00000000
    }
    {
        r2 = #-1
    }
    {
        r1:0 = lsl(r1:0, r2)
    }
    {
        p0  =  cmp.eq(r0, #0x5); if (p0.new) jump:t pass
        jump fail
    }

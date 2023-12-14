/*
 * Purpose: test example, verify the soundness of the vpmpyh operator.
 *
 * 0x01020304 vector polynomial multiplied with 0x04030201 results
 * 0x000400060b060b04.
 */

    .text
    .globl _start

_start:
    {
        r0 = #16909060
        r1 = #67305985
    }
    {
        r1:0 = vpmpyh(r0, r1)
    }
    {
        p0 = cmp.eq(r0, #184945412); if (p0.new) jump:t test2
        jump fail
    }

test2:
    {
        p0 = cmp.eq(r1, #262150); if (p0.new) jump:t pass
        jump fail
    }

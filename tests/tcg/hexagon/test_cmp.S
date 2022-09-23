/* Purpose: test a signed and unsigned comparison */

    .text
    .globl _start

_start:
    {
        jump signed
    }

    .globl signed
signed:
    {
        r0 = #-2
        r1 = #0
    }
    {
        p0 = cmp.lt(r0, r1); if (p0.new) jump:t unsigned
        jump fail
    }

    .globl unsigned
unsigned:
    {
        r0 = #-2
        r1 = #0
    }
    {
        p0 = cmp.gtu(r0, r1); if (p0.new) jump:t pass
        jump fail
    }

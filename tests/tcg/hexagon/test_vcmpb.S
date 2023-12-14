/*
 * Purpose: test example, verify the soundness of the vector compare bytes
 * operation.
 *
 * Vector byte comparison between 0x1234567887654321 and 0x1234567800000000
 * should result in 0b11110000 in binary, or 0xf0 in hex.
 */

    .text
    .globl _start

_start:
    {
        r0 = #0x87654321
        r1 = #0x12345678
    }
    {
        r2 = #0x00000000
        r3 = #0x12345678
    }
    {
        p2 = vcmpb.eq(r1:0, r3:2)
    }
    {
        r4 = p2
    }
    {
        p0 = cmp.eq(r4, #0xf0); if (p0.new) jump:t pass
        jump fail
    }

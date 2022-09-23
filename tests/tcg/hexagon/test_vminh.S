/*
 * Purpose: test example, verify the soundness of the vrmaxh operation.
 *
 * The minimum between  0x0002000300010005 and 0x0003000200020007 is
 * 0x0003000300020007.
 *
 *      input:  r1 = 0x00010003 r0 = 0x00010005 r3 = 0x00030002 r2 = 0x00020007
 *     output:  r1 = 0x00010002 r0 = 0x00010005
 */

    .text
    .globl _start

_start:
    {
        r0 = #65541
        r1 = #65539
    }
    {
        r2 = #131079
        r3 = #196610
    }
    {
        r1:0 = vminh(r1:0, r3:2)
    }
    {
        p0 = cmp.eq(r0, #65541); if (p0.new) jump:t test2
        jump fail
    }

test2:
    {
        p0 = cmp.eq(r1, #65538); if (p0.new) jump:t pass
        jump fail
    }

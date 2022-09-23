/*
 * Purpose: test example, verify the soundness of the vrmaxh operation.
 *
 * The maximum between  0x0002000300010005 and 0x0003000200020007 is
 * 0x0003000300020007.
 *
 *      input:  r1 = 0x00010003 r0 = 0x00010005 r3 = 0x00030002 r2 = 0x00020007
 *     output:  r1 = 0x00030003 r0 = 0x00020007
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
        r1:0 = vmaxh(r1:0, r3:2)
    }
    {
        p0 = cmp.eq(r0, #131079); if (p0.new) jump:t test2
        jump fail
    }

test2:
    {
        p0 = cmp.eq(r1, #196611); if (p0.new) jump:t pass
        jump fail
    }

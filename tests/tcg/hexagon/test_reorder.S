/*
 * Purpose: demonstrate handling of .new uses appearing before the associated
 * definition.
 * Here we perform a jump that skips the code resetting R2 from 0xDEADBEEF to 0,
 * only if P0.new is true, but P0 is assigned to 1 (R4) in the next instruction
 * in the packet.
 */

    .text
    .globl _start

_start:
    {
        r2 = #-559038737
    }
    {
        r4 = #1
    }
    {
        if (p0.new) jump:nt skip
        p0 = r4;
    }

fallthrough:
    {
        r2 = #0
    }

skip:
    {
        p0 = cmp.eq(r2, #-559038737); if (p0.new) jump:t pass
        jump fail
    }

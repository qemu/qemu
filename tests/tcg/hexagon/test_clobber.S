/*
 * Purpose: demonstrate the successful operation of the register save mechanism,
 * in which the caller saves the registers that will be clobbered, and restores
 * them after the call.
 */

    .text
    .globl _start

_start:
    allocframe(#8)
    {
        r16 = #47
        r17 = #155
    }
    memd(sp+#0) = r17:16
    {
        r16 = #255
        r17 = #42
    }
    {
        deallocframe
        r17:16 = memd(sp+#0)
    }
    {
        p0 = cmp.eq(r16, #47)
        p0 = cmp.eq(r17, #155); if (p0.new) jump:t pass
        jump fail
    }

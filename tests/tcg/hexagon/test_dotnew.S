/* Purpose: test the .new operator while performing memory stores. */

    .text
    .globl _start

_start:
    {
        allocframe(#16)
    }
    {
        r0 = #1
        memw(sp+#0) = r0.new
    }
    {
        r1 = #2
        memw(sp+#4) = r1.new
    }
    {
        r2 = #3
        memw(sp+#8) = r2.new
    }
    {
        r0 = memw(sp+#8)
    }
    {
        r1 = memw(sp+#4)
    }
    {
        r2 = memw(sp+#0)
    }
    {
        r3 = mpyi(r1, r2)
    }
    {
        deallocframe
        p0 = cmp.eq(r3, #2); if (p0.new) jump:t pass
        jump fail
    }

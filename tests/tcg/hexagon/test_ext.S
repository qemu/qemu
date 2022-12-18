/* Purpose: test immediate extender instructions. */

    .text
    .globl _start

_start:
    {
        r2 = ##-559038737
    }
    {
        p0 = cmp.eq(r2, ##-559038737); if (p0.new) jump:t pass
        jump fail
    }

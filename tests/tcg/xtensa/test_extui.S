#include "macros.inc"

test_suite extui

.macro test_extui v, shiftimm, maskimm
    .if     \shiftimm + \maskimm <= 32
    movi    a2, \v
    extui   a3, a2, \shiftimm, \maskimm
    movi    a4, ((\v) >> (\shiftimm)) & ((1 << (\maskimm)) - 1)
    assert  eq, a3, a4
    .endif
.endm

test extui
    .set    shiftimm, 0
    .rept   32
    .set    maskimm, 1
    .rept   16
    test_extui 0xc8df1370, shiftimm, maskimm
    .set    maskimm, maskimm + 1
    .endr
    .set    shiftimm, shiftimm + 1
    .endr
test_end

test_suite_end

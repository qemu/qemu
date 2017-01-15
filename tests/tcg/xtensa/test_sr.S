#include "macros.inc"

test_suite sr

.macro  sr_op sym, op_sym, op_byte, sr
    .if \sym
    \op_sym a4, \sr
    .else
    .byte 0x40, \sr, \op_byte
    .endif
.endm

.macro 	test_sr_op sym, mask, op, op_byte, sr
    movi    a4, 0
    .if (\mask)
    set_vector kernel, 0
    sr_op   \sym, \op, \op_byte, \sr
    .else
    set_vector kernel, 2f
1:
    sr_op   \sym, \op, \op_byte, \sr
    test_fail
2:
    reset_ps
    rsr     a2, exccause
    assert  eqi, a2, 0
    rsr     a2, epc1
    movi    a3, 1b
    assert  eq, a2, a3
    .endif
.endm

.macro  test_sr_mask sr, sym, mask
test \sr
    test_sr_op \sym, \mask & 1, rsr, 0x03, \sr
    test_sr_op \sym, \mask & 2, wsr, 0x13, \sr
    test_sr_op \sym, \mask & 4, xsr, 0x61, \sr
test_end
.endm

.macro  test_sr sr, conf
    test_sr_mask    \sr, \conf, 7
.endm

test_sr acchi, 1
test_sr acclo, 1
test_sr /*memctl*/97, 0
test_sr_mask /*atomctl*/99, 0, 0
test_sr_mask /*br*/4, 0, 0
test_sr_mask /*cacheattr*/98, 0, 0
test_sr ccompare0, 1
test_sr ccount, 1
test_sr cpenable, 1
test_sr dbreaka0, 1
test_sr dbreakc0, 1
test_sr_mask debugcause, 1, 1
test_sr depc, 1
test_sr dtlbcfg, 1
test_sr epc1, 1
test_sr epc2, 1
test_sr eps2, 1
test_sr exccause, 1
test_sr excsave1, 1
test_sr excsave2, 1
test_sr excvaddr, 1
test_sr ibreaka0, 1
test_sr ibreakenable, 1
test_sr icount, 1
test_sr icountlevel, 1
test_sr_mask /*intclear*/227, 0, 2
test_sr_mask /*interrupt*/226, 0, 3
test_sr intenable, 1
test_sr itlbcfg, 1
test_sr lbeg, 1
test_sr lcount, 1
test_sr lend, 1
test_sr litbase, 1
test_sr m0, 1
test_sr misc0, 1
test_sr_mask /*prefctl*/40, 0, 0
test_sr_mask /*prid*/235, 0, 1
test_sr ps, 1
test_sr ptevaddr, 1
test_sr rasid, 1
test_sr sar, 1
test_sr scompare1, 1
test_sr vecbase, 1
test_sr windowbase, 1
test_sr windowstart, 1

test_suite_end

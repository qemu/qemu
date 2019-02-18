#include "macros.inc"

test_suite sr

#if XCHAL_HAVE_BE
#define LOW__SR 0x04
#define HI_RSR 0x30
#define HI_WSR 0x31
#define HI_XSR 0x16
#else
#define LOW__SR 0x40
#define HI_RSR 0x03
#define HI_WSR 0x13
#define HI_XSR 0x61
#endif

.macro  sr_op sym, op_sym, op_byte, sr
    .if \sym
    \op_sym a4, \sr
    .else
    .byte LOW__SR, \sr, \op_byte
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
    test_sr_op \sym, \mask & 1, rsr, HI_RSR, \sr
    test_sr_op \sym, \mask & 2, wsr, HI_WSR, \sr
    test_sr_op \sym, \mask & 4, xsr, HI_XSR, \sr
test_end
.endm

.macro  test_sr sr, conf
    test_sr_mask    \sr, \conf, 7
.endm

#if XCHAL_HAVE_MAC16
test_sr acchi, 1
test_sr acclo, 1
#else
test_sr_mask /*acchi*/17, 0, 0
test_sr_mask /*acclo*/16, 0, 0
#endif

#if XCHAL_HAVE_S32C1I && XCHAL_HW_VERSION >= 230000
test_sr atomctl, 1
#else
test_sr_mask /*atomctl*/99, 0, 0
#endif

#if XCHAL_HAVE_BOOLEANS
test_sr br, 1
#else
test_sr_mask /*br*/4, 0, 0
#endif

test_sr_mask /*cacheattr*/98, 0, 0

#if XCHAL_HAVE_CCOUNT
test_sr ccompare0, 1
test_sr ccount, 1
#else
test_sr_mask /*ccompare0*/240, 0, 0
test_sr_mask /*ccount*/234, 0, 0
#endif

#if XCHAL_HAVE_CP
test_sr cpenable, 1
#else
test_sr_mask /*cpenable*/224, 0, 0
#endif

#if XCHAL_HAVE_DEBUG
#if XCHAL_NUM_DBREAK
test_sr dbreaka0, 1
test_sr dbreakc0, 1
#endif
test_sr_mask debugcause, 1, 1
#else
test_sr_mask /*dbreaka0*/144, 0, 0
test_sr_mask /*dbreakc0*/160, 0, 0
test_sr_mask /*debugcause*/233, 0, 0
#endif

test_sr depc, 1

#if XCHAL_HAVE_PTP_MMU
test_sr dtlbcfg, 1
#else
test_sr_mask /*dtlbcfg*/92, 0, 0
#endif

test_sr epc1, 1

#if XCHAL_NUM_INTLEVELS > 1
test_sr epc2, 1
test_sr eps2, 1
#else
test_sr_mask /*epc2*/178, 0, 0
test_sr_mask /*eps2*/194, 0, 0
#endif

test_sr exccause, 1
test_sr excsave1, 1

#if XCHAL_NUM_INTLEVELS > 1
test_sr excsave2, 1
#else
test_sr_mask /*excsave2*/210, 0, 0
#endif

test_sr excvaddr, 1

#if XCHAL_HAVE_DEBUG
#if XCHAL_NUM_IBREAK
test_sr ibreaka0, 1
test_sr ibreakenable, 1
#endif
test_sr icount, 1
test_sr icountlevel, 1
#else
test_sr_mask /*ibreaka0*/128, 0, 0
test_sr_mask /*ibreakenable*/96, 0, 0
test_sr_mask /*icount*/236, 0, 0
test_sr_mask /*icountlevel*/237, 0, 0
#endif

test_sr_mask /*intclear*/227, 0, 2
test_sr_mask /*interrupt*/226, 0, 3
test_sr intenable, 1

#if XCHAL_HAVE_PTP_MMU
test_sr itlbcfg, 1
#else
test_sr_mask /*itlbcfg*/91, 0, 0
#endif

#if XCHAL_HAVE_LOOPS
test_sr lbeg, 1
test_sr lcount, 1
test_sr lend, 1
#else
test_sr_mask /*lbeg*/0, 0, 0
test_sr_mask /*lcount*/2, 0, 0
test_sr_mask /*lend*/1, 0, 0
#endif

#if XCHAL_HAVE_ABSOLUTE_LITERALS
test_sr litbase, 1
#else
test_sr_mask /*litbase*/5, 0, 0
#endif

#if XCHAL_HAVE_MAC16
test_sr m0, 1
#else
test_sr_mask /*m0*/32, 0, 0
#endif

#if XCHAL_HW_VERSION >= 250000
test_sr_mask /*memctl*/97, 0, 7
#else
test_sr_mask /*memctl*/97, 0, 0
#endif

#if XCHAL_NUM_MISC_REGS
test_sr misc0, 1
#else
test_sr_mask /*misc0*/244, 0, 0
#endif

#if XCHAL_HAVE_PREFETCH
test_sr prefctl, 1
#else
test_sr_mask /*prefctl*/40, 0, 0
#endif

#if XCHAL_HAVE_PRID
test_sr_mask /*prid*/235, 0, 1
#else
test_sr_mask /*prid*/235, 0, 0
#endif

test_sr ps, 1

#if XCHAL_HAVE_PTP_MMU
test_sr ptevaddr, 1
test_sr rasid, 1
#else
test_sr_mask /*ptevaddr*/83, 0, 0
test_sr_mask /*rasid*/90, 0, 0
#endif

test_sr sar, 1

#if XCHAL_HAVE_S32C1I
test_sr scompare1, 1
#else
test_sr_mask /*scompare1*/12, 0, 0
#endif

#if XCHAL_HAVE_VECBASE
test_sr vecbase, 1
#else
test_sr_mask /*vecbase*/231, 0, 0
#endif

#if XCHAL_HAVE_WINDOWED
test_sr windowbase, 1
test_sr windowstart, 1
#else
test_sr_mask /*windowbase*/72, 0, 0
test_sr_mask /*windowstart*/73, 0, 0
#endif

test_suite_end

#include "macros.inc"

test_suite fp0_arith

#if XCHAL_HAVE_FP

.macro movfp fr, v
    movi    a2, \v
    wfr     \fr, a2
.endm

.macro check_res fr, r
    rfr     a2, \fr
    dump    a2
    movi    a3, \r
    assert  eq, a2, a3
    rur     a2, fsr
    assert  eqi, a2, 0
.endm

.macro test_op2_rm op, fr0, fr1, fr2, v0, v1, r
    movi    a2, 0
    wur     a2, fsr
    movfp   \fr0, \v0
    movfp   \fr1, \v1
    \op     \fr2, \fr0, \fr1
    check_res \fr2, \r
.endm

.macro test_op3_rm op, fr0, fr1, fr2, fr3, v0, v1, v2, r
    movi    a2, 0
    wur     a2, fsr
    movfp   \fr0, \v0
    movfp   \fr1, \v1
    movfp   \fr2, \v2
    \op     \fr0, \fr1, \fr2
    check_res \fr3, \r
.endm

.macro test_op2_ex op, fr0, fr1, fr2, v0, v1, rm, r
    movi    a2, \rm
    wur     a2, fcr
    test_op2_rm \op, \fr0, \fr1, \fr2, \v0, \v1, \r
    movi    a2, (\rm) | 0x7c
    wur     a2, fcr
    test_op2_rm \op, \fr0, \fr1, \fr2, \v0, \v1, \r
.endm

.macro test_op3_ex op, fr0, fr1, fr2, fr3, v0, v1, v2, rm, r
    movi    a2, \rm
    wur     a2, fcr
    test_op3_rm \op, \fr0, \fr1, \fr2, \fr3, \v0, \v1, \v2, \r
    movi    a2, (\rm) | 0x7c
    wur     a2, fcr
    test_op3_rm \op, \fr0, \fr1, \fr2, \fr3, \v0, \v1, \v2, \r
.endm

.macro test_op2 op, fr0, fr1, fr2, v0, v1, r0, r1, r2, r3
    test_op2_ex \op, \fr0, \fr1, \fr2, \v0, \v1, 0, \r0
    test_op2_ex \op, \fr0, \fr1, \fr2, \v0, \v1, 1, \r1
    test_op2_ex \op, \fr0, \fr1, \fr2, \v0, \v1, 2, \r2
    test_op2_ex \op, \fr0, \fr1, \fr2, \v0, \v1, 3, \r3
.endm

.macro test_op3 op, fr0, fr1, fr2, fr3, v0, v1, v2, r0, r1, r2, r3
    test_op3_ex \op, \fr0, \fr1, \fr2, \fr3, \v0, \v1, \v2, 0, \r0
    test_op3_ex \op, \fr0, \fr1, \fr2, \fr3, \v0, \v1, \v2, 1, \r1
    test_op3_ex \op, \fr0, \fr1, \fr2, \fr3, \v0, \v1, \v2, 2, \r2
    test_op3_ex \op, \fr0, \fr1, \fr2, \fr3, \v0, \v1, \v2, 3, \r3
.endm

.macro test_op2_cpe op
    set_vector  kernel, 2f
    movi    a2, 0
    wsr     a2, cpenable
1:
    \op     f2, f0, f1
    test_fail
2:
    rsr     a2, excvaddr
    movi    a3, 1b
    assert  eq, a2, a3
    rsr     a2, exccause
    movi    a3, 32
    assert  eq, a2, a3

    set_vector  kernel, 0
    movi    a2, 1
    wsr     a2, cpenable
.endm

test add_s
    movi    a2, 1
    wsr     a2, cpenable

    test_op2 add.s, f0, f1, f2, 0x3fc00000, 0x34400000, \
        0x3fc00002, 0x3fc00001, 0x3fc00002, 0x3fc00001
    test_op2 add.s, f3, f4, f5, 0x3fc00000, 0x34a00000, \
        0x3fc00002, 0x3fc00002, 0x3fc00003, 0x3fc00002

    /* MAX_FLOAT + MAX_FLOAT = +inf/MAX_FLOAT  */
    test_op2 add.s, f6, f7, f8, 0x7f7fffff, 0x7f7fffff, \
        0x7f800000, 0x7f7fffff, 0x7f800000, 0x7f7fffff
test_end

test add_s_inf
    /* 1 + +inf = +inf  */
    test_op2 add.s, f6, f7, f8, 0x3fc00000, 0x7f800000, \
        0x7f800000, 0x7f800000, 0x7f800000, 0x7f800000

    /* +inf + -inf = default NaN */
    test_op2 add.s, f0, f1, f2, 0x7f800000, 0xff800000, \
        0x7fc00000, 0x7fc00000, 0x7fc00000, 0x7fc00000
test_end

test add_s_nan
    /* 1 + NaN = NaN  */
    test_op2 add.s, f9, f10, f11, 0x3fc00000, 0x7fc00001, \
        0x7fc00001, 0x7fc00001, 0x7fc00001, 0x7fc00001
    test_op2 add.s, f12, f13, f14, 0x3fc00000, 0x7f800001, \
        0x7f800001, 0x7f800001, 0x7f800001, 0x7f800001

    /* NaN1 + NaN2 = NaN1 */
    test_op2 add.s, f15, f0, f1, 0x7f800001, 0x7fbfffff, \
        0x7f800001, 0x7f800001, 0x7f800001, 0x7f800001
    test_op2 add.s, f2, f3, f4, 0x7fbfffff, 0x7f800001, \
        0x7fbfffff, 0x7fbfffff, 0x7fbfffff, 0x7fbfffff
    test_op2 add.s, f5, f6, f7, 0x7fc00001, 0x7fbfffff, \
        0x7fc00001, 0x7fc00001, 0x7fc00001, 0x7fc00001
    test_op2 add.s, f8, f9, f10, 0x7fbfffff, 0x7fc00001, \
        0x7fbfffff, 0x7fbfffff, 0x7fbfffff, 0x7fbfffff
test_end

test sub_s
    test_op2 sub.s, f0, f1, f0, 0x3f800001, 0x33800000, \
        0x3f800000, 0x3f800000, 0x3f800001, 0x3f800000
    test_op2 sub.s, f0, f1, f1, 0x3f800002, 0x33800000, \
        0x3f800002, 0x3f800001, 0x3f800002, 0x3f800001

    /* norm - norm = denorm */
    test_op2 sub.s, f6, f7, f8, 0x00800001, 0x00800000, \
        0x00000001, 0x00000001, 0x00000001, 0x00000001
test_end

test mul_s
    test_op2 mul.s, f0, f1, f2, 0x3f800001, 0x3f800001, \
        0x3f800002, 0x3f800002, 0x3f800003, 0x3f800002

    /* MAX_FLOAT/2 * MAX_FLOAT/2 = +inf/MAX_FLOAT  */
    test_op2 mul.s, f6, f7, f8, 0x7f000000, 0x7f000000, \
        0x7f800000, 0x7f7fffff, 0x7f800000, 0x7f7fffff
    /* min norm * min norm = 0/denorm */
    test_op2 mul.s, f6, f7, f8, 0x00800001, 0x00800000, \
        0x00000000, 0x00000000, 0x00000001, 0x00000000

    /* inf * 0 = default NaN */
    test_op2 mul.s, f6, f7, f8, 0x7f800000, 0x00000000, \
        0x7fc00000, 0x7fc00000, 0x7fc00000, 0x7fc00000
test_end

test madd_s
    test_op3 madd.s, f0, f1, f2, f0, 0, 0x3f800001, 0x3f800001, \
        0x3f800002, 0x3f800002, 0x3f800003, 0x3f800002
test_end

test msub_s
    test_op3 msub.s, f0, f1, f2, f0, 0x3f800000, 0x3f800001, 0x3f800001, \
        0xb4800000, 0xb4800000, 0xb4800000, 0xb4800001
test_end

#endif

test_suite_end

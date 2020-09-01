#include "macros.inc"
#include "fpu.h"

test_suite fp0_conv

#if XCHAL_HAVE_FP

.macro movfp fr, v
    movi    a2, \v
    wfr     \fr, a2
.endm

.macro test_ftoi_ex op, r0, fr0, v, c, r, sr
    movi    a2, 0
    wur     a2, fsr
    movfp   \fr0, \v
    \op     \r0, \fr0, \c
    dump    \r0
    movi    a3, \r
    assert  eq, \r0, a3
    rur     a2, fsr
#if DFPU
    movi    a3, \sr
    assert  eq, a2, a3
#else
    assert  eqi, a2, 0
#endif
.endm

.macro test_ftoi op, r0, fr0, v, c, r, sr
    movi    a2, 0
    wur     a2, fcr
    test_ftoi_ex \op, \r0, \fr0, \v, \c, \r, \sr
    movi    a2, 0x7c
    wur     a2, fcr
    test_ftoi_ex \op, \r0, \fr0, \v, \c, \r, \sr
.endm


.macro test_itof_ex op, fr0, ar0, v, c, r, sr
    movi    a2, 0
    wur     a2, fsr
    movi    \ar0, \v
    \op     \fr0, \ar0, \c

    rfr     a2, \fr0
    dump    a2
    movi    a3, \r
    assert  eq, a2, a3
    rur     a2, fsr
#if DFPU
    movi    a3, \sr
    assert  eq, a2, a3
#else
    assert  eqi, a2, 0
#endif
.endm

.macro test_itof_rm op, fr0, ar0, v, c, rm, r, sr
    movi    a2, \rm
    wur     a2, fcr
    test_itof_ex \op, \fr0, \ar0, \v, \c, \r, \sr
    movi    a2, (\rm) | 0x7c
    wur     a2, fcr
    test_itof_ex \op, \fr0, \ar0, \v, \c, \r, \sr
.endm

.macro test_itof op, fr0, ar0, v, c, r0, r1, r2, r3, sr
    test_itof_rm \op, \fr0, \ar0, \v, \c, 0, \r0, \sr
    test_itof_rm \op, \fr0, \ar0, \v, \c, 1, \r1, \sr
    test_itof_rm \op, \fr0, \ar0, \v, \c, 2, \r2, \sr
    test_itof_rm \op, \fr0, \ar0, \v, \c, 3, \r3, \sr
.endm

test round_s
    movi    a2, 1
    wsr     a2, cpenable

    /* NaN */
    test_ftoi round.s, a2, f0, 0xffc00001, 0, 0x7fffffff, FSR_V
    test_ftoi round.s, a2, f0, 0xff800001, 0, 0x7fffffff, FSR_V

    /* -inf */
    test_ftoi round.s, a2, f0, 0xff800000, 0, 0x80000000, FSR_V

    /* negative overflow */
    test_ftoi round.s, a2, f0, 0xceffffff, 1, 0x80000000, FSR_V
    test_ftoi round.s, a2, f0, 0xcf000000, 0, 0x80000000, FSR__
    test_ftoi round.s, a2, f0, 0xceffffff, 0, 0x80000080, FSR__

    /* negative */
    test_ftoi round.s, a2, f0, 0xbfa00000, 1, -2, FSR_I  /* -1.25 * 2 */
    test_ftoi round.s, a2, f0, 0xbfc00000, 0, -2, FSR_I  /* -1.5 */
    test_ftoi round.s, a2, f0, 0xbf800000, 1, -2, FSR__  /* -1 * 2 */
    test_ftoi round.s, a2, f0, 0xbf800000, 0, -1, FSR__  /* -1 */
    test_ftoi round.s, a2, f0, 0xbf400000, 0, -1, FSR_I  /* -0.75 */
    test_ftoi round.s, a2, f0, 0xbf000000, 0,  0, FSR_I  /* -0.5 */

    /* positive */
    test_ftoi round.s, a2, f0, 0x3f000000, 0, 0, FSR_I /* 0.5 */
    test_ftoi round.s, a2, f0, 0x3f400000, 0, 1, FSR_I /* 0.75 */
    test_ftoi round.s, a2, f0, 0x3f800000, 0, 1, FSR__ /* 1 */
    test_ftoi round.s, a2, f0, 0x3f800000, 1, 2, FSR__ /* 1 * 2 */
    test_ftoi round.s, a2, f0, 0x3fc00000, 0, 2, FSR_I /* 1.5 */
    test_ftoi round.s, a2, f0, 0x3fa00000, 1, 2, FSR_I /* 1.25 * 2 */

    /* positive overflow */
    test_ftoi round.s, a2, f0, 0x4effffff, 0, 0x7fffff80, FSR__
    test_ftoi round.s, a2, f0, 0x4f000000, 0, 0x7fffffff, FSR_V
    test_ftoi round.s, a2, f0, 0x4effffff, 1, 0x7fffffff, FSR_V

    /* +inf */
    test_ftoi round.s, a2, f0, 0x7f800000, 0, 0x7fffffff, FSR_V

    /* NaN */
    test_ftoi round.s, a2, f0, 0x7f800001, 0, 0x7fffffff, FSR_V
    test_ftoi round.s, a2, f0, 0x7fc00000, 0, 0x7fffffff, FSR_V
test_end

test trunc_s
    /* NaN */
    test_ftoi trunc.s, a2, f0, 0xffc00001, 0, 0x7fffffff, FSR_V
    test_ftoi trunc.s, a2, f0, 0xff800001, 0, 0x7fffffff, FSR_V

    /* -inf */
    test_ftoi trunc.s, a2, f0, 0xff800000, 0, 0x80000000, FSR_V

    /* negative overflow */
    test_ftoi trunc.s, a2, f0, 0xceffffff, 1, 0x80000000, FSR_V
    test_ftoi trunc.s, a2, f0, 0xcf000000, 0, 0x80000000, FSR__
    test_ftoi trunc.s, a2, f0, 0xceffffff, 0, 0x80000080, FSR__

    /* negative */
    test_ftoi trunc.s, a2, f0, 0xbfa00000, 1, -2, FSR_I /* -1.25 * 2 */
    test_ftoi trunc.s, a2, f0, 0xbfc00000, 0, -1, FSR_I /* -1.5 */
    test_ftoi trunc.s, a2, f0, 0xbf800000, 1, -2, FSR__ /* -1 * 2 */
    test_ftoi trunc.s, a2, f0, 0xbf800000, 0, -1, FSR__ /* -1 */
    test_ftoi trunc.s, a2, f0, 0xbf400000, 0,  0, FSR_I /* -0.75 */
    test_ftoi trunc.s, a2, f0, 0xbf000000, 0,  0, FSR_I /* -0.5 */

    /* positive */
    test_ftoi trunc.s, a2, f0, 0x3f000000, 0, 0, FSR_I /* 0.5 */
    test_ftoi trunc.s, a2, f0, 0x3f400000, 0, 0, FSR_I /* 0.75 */
    test_ftoi trunc.s, a2, f0, 0x3f800000, 0, 1, FSR__ /* 1 */
    test_ftoi trunc.s, a2, f0, 0x3f800000, 1, 2, FSR__ /* 1 * 2 */
    test_ftoi trunc.s, a2, f0, 0x3fc00000, 0, 1, FSR_I /* 1.5 */
    test_ftoi trunc.s, a2, f0, 0x3fa00000, 1, 2, FSR_I /* 1.25 * 2 */

    /* positive overflow */
    test_ftoi trunc.s, a2, f0, 0x4effffff, 0, 0x7fffff80, FSR__
    test_ftoi trunc.s, a2, f0, 0x4f000000, 0, 0x7fffffff, FSR_V
    test_ftoi trunc.s, a2, f0, 0x4effffff, 1, 0x7fffffff, FSR_V

    /* +inf */
    test_ftoi trunc.s, a2, f0, 0x7f800000, 0, 0x7fffffff, FSR_V

    /* NaN */
    test_ftoi trunc.s, a2, f0, 0x7f800001, 0, 0x7fffffff, FSR_V
    test_ftoi trunc.s, a2, f0, 0x7fc00000, 0, 0x7fffffff, FSR_V
test_end

test floor_s
    /* NaN */
    test_ftoi floor.s, a2, f0, 0xffc00001, 0, 0x7fffffff, FSR_V
    test_ftoi floor.s, a2, f0, 0xff800001, 0, 0x7fffffff, FSR_V

    /* -inf */
    test_ftoi floor.s, a2, f0, 0xff800000, 0, 0x80000000, FSR_V

    /* negative overflow */
    test_ftoi floor.s, a2, f0, 0xceffffff, 1, 0x80000000, FSR_V
    test_ftoi floor.s, a2, f0, 0xcf000000, 0, 0x80000000, FSR__
    test_ftoi floor.s, a2, f0, 0xceffffff, 0, 0x80000080, FSR__

    /* negative */
    test_ftoi floor.s, a2, f0, 0xbfa00000, 1, -3, FSR_I /* -1.25 * 2 */
    test_ftoi floor.s, a2, f0, 0xbfc00000, 0, -2, FSR_I /* -1.5 */
    test_ftoi floor.s, a2, f0, 0xbf800000, 1, -2, FSR__ /* -1 * 2 */
    test_ftoi floor.s, a2, f0, 0xbf800000, 0, -1, FSR__ /* -1 */
    test_ftoi floor.s, a2, f0, 0xbf400000, 0, -1, FSR_I /* -0.75 */
    test_ftoi floor.s, a2, f0, 0xbf000000, 0, -1, FSR_I /* -0.5 */

    /* positive */
    test_ftoi floor.s, a2, f0, 0x3f000000, 0, 0, FSR_I /* 0.5 */
    test_ftoi floor.s, a2, f0, 0x3f400000, 0, 0, FSR_I /* 0.75 */
    test_ftoi floor.s, a2, f0, 0x3f800000, 0, 1, FSR__ /* 1 */
    test_ftoi floor.s, a2, f0, 0x3f800000, 1, 2, FSR__ /* 1 * 2 */
    test_ftoi floor.s, a2, f0, 0x3fc00000, 0, 1, FSR_I /* 1.5 */
    test_ftoi floor.s, a2, f0, 0x3fa00000, 1, 2, FSR_I /* 1.25 * 2 */

    /* positive overflow */
    test_ftoi floor.s, a2, f0, 0x4effffff, 0, 0x7fffff80, FSR__
    test_ftoi floor.s, a2, f0, 0x4f000000, 0, 0x7fffffff, FSR_V
    test_ftoi floor.s, a2, f0, 0x4effffff, 1, 0x7fffffff, FSR_V

    /* +inf */
    test_ftoi floor.s, a2, f0, 0x7f800000, 0, 0x7fffffff, FSR_V

    /* NaN */
    test_ftoi floor.s, a2, f0, 0x7f800001, 0, 0x7fffffff, FSR_V
    test_ftoi floor.s, a2, f0, 0x7fc00000, 0, 0x7fffffff, FSR_V
test_end

test ceil_s
    /* NaN */
    test_ftoi ceil.s, a2, f0, 0xffc00001, 0, 0x7fffffff, FSR_V
    test_ftoi ceil.s, a2, f0, 0xff800001, 0, 0x7fffffff, FSR_V

    /* -inf */
    test_ftoi ceil.s, a2, f0, 0xff800000, 0, 0x80000000, FSR_V

    /* negative overflow */
    test_ftoi ceil.s, a2, f0, 0xceffffff, 1, 0x80000000, FSR_V
    test_ftoi ceil.s, a2, f0, 0xcf000000, 0, 0x80000000, FSR__
    test_ftoi ceil.s, a2, f0, 0xceffffff, 0, 0x80000080, FSR__

    /* negative */
    test_ftoi ceil.s, a2, f0, 0xbfa00000, 1, -2, FSR_I /* -1.25 * 2 */
    test_ftoi ceil.s, a2, f0, 0xbfc00000, 0, -1, FSR_I /* -1.5 */
    test_ftoi ceil.s, a2, f0, 0xbf800000, 1, -2, FSR__ /* -1 * 2 */
    test_ftoi ceil.s, a2, f0, 0xbf800000, 0, -1, FSR__ /* -1 */
    test_ftoi ceil.s, a2, f0, 0xbf400000, 0,  0, FSR_I /* -0.75 */
    test_ftoi ceil.s, a2, f0, 0xbf000000, 0,  0, FSR_I /* -0.5 */

    /* positive */
    test_ftoi ceil.s, a2, f0, 0x3f000000, 0, 1, FSR_I /* 0.5 */
    test_ftoi ceil.s, a2, f0, 0x3f400000, 0, 1, FSR_I /* 0.75 */
    test_ftoi ceil.s, a2, f0, 0x3f800000, 0, 1, FSR__ /* 1 */
    test_ftoi ceil.s, a2, f0, 0x3f800000, 1, 2, FSR__ /* 1 * 2 */
    test_ftoi ceil.s, a2, f0, 0x3fc00000, 0, 2, FSR_I /* 1.5 */
    test_ftoi ceil.s, a2, f0, 0x3fa00000, 1, 3, FSR_I /* 1.25 * 2 */

    /* positive overflow */
    test_ftoi ceil.s, a2, f0, 0x4effffff, 0, 0x7fffff80, FSR__
    test_ftoi ceil.s, a2, f0, 0x4f000000, 0, 0x7fffffff, FSR_V
    test_ftoi ceil.s, a2, f0, 0x4effffff, 1, 0x7fffffff, FSR_V

    /* +inf */
    test_ftoi ceil.s, a2, f0, 0x7f800000, 0, 0x7fffffff, FSR_V

    /* NaN */
    test_ftoi ceil.s, a2, f0, 0x7f800001, 0, 0x7fffffff, FSR_V
    test_ftoi ceil.s, a2, f0, 0x7fc00000, 0, 0x7fffffff, FSR_V
test_end

test utrunc_s
    /* NaN */
    test_ftoi utrunc.s, a2, f0, 0xffc00001, 0, 0xffffffff, FSR_V
    test_ftoi utrunc.s, a2, f0, 0xff800001, 0, 0xffffffff, FSR_V

    /* -inf */
    test_ftoi utrunc.s, a2, f0, 0xff800000, 0, 0x80000000, FSR_V

    /* negative overflow */
    test_ftoi utrunc.s, a2, f0, 0xceffffff, 1, 0x80000000, FSR_V
    test_ftoi utrunc.s, a2, f0, 0xcf000000, 0, 0x80000000, FSR_V
    test_ftoi utrunc.s, a2, f0, 0xceffffff, 0, 0x80000080, FSR_V

    /* negative */
    test_ftoi utrunc.s, a2, f0, 0xbfa00000, 1, -2, FSR_V /* -1.25 * 2 */
    test_ftoi utrunc.s, a2, f0, 0xbfc00000, 0, -1, FSR_V /* -1.5 */
    test_ftoi utrunc.s, a2, f0, 0xbf800000, 1, -2, FSR_V /* -1 * 2 */
    test_ftoi utrunc.s, a2, f0, 0xbf800000, 0, -1, FSR_V /* -1 */
    test_ftoi utrunc.s, a2, f0, 0xbf400000, 0,  0, FSR_I /* -0.75 */
    test_ftoi utrunc.s, a2, f0, 0xbf000000, 0,  0, FSR_I /* -0.5 */

    /* positive */
    test_ftoi utrunc.s, a2, f0, 0x3f000000, 0, 0, FSR_I /* 0.5 */
    test_ftoi utrunc.s, a2, f0, 0x3f400000, 0, 0, FSR_I /* 0.75 */
    test_ftoi utrunc.s, a2, f0, 0x3f800000, 0, 1, FSR__ /* 1 */
    test_ftoi utrunc.s, a2, f0, 0x3f800000, 1, 2, FSR__ /* 1 * 2 */
    test_ftoi utrunc.s, a2, f0, 0x3fc00000, 0, 1, FSR_I /* 1.5 */
    test_ftoi utrunc.s, a2, f0, 0x3fa00000, 1, 2, FSR_I /* 1.25 * 2 */

    /* positive overflow */
    test_ftoi utrunc.s, a2, f0, 0x4effffff, 0, 0x7fffff80, FSR__
    test_ftoi utrunc.s, a2, f0, 0x4f000000, 0, 0x80000000, FSR__
    test_ftoi utrunc.s, a2, f0, 0x4effffff, 1, 0xffffff00, FSR__
    test_ftoi utrunc.s, a2, f0, 0x4f800000, 1, 0xffffffff, FSR_V

    /* +inf */
    test_ftoi utrunc.s, a2, f0, 0x7f800000, 0, 0xffffffff, FSR_V

    /* NaN */
    test_ftoi utrunc.s, a2, f0, 0x7f800001, 0, 0xffffffff, FSR_V
    test_ftoi utrunc.s, a2, f0, 0x7fc00000, 0, 0xffffffff, FSR_V
test_end

test float_s
    test_itof float.s, f0, a2, -1, 0, \
        0xbf800000, 0xbf800000, 0xbf800000, 0xbf800000, FSR__
    test_itof float.s, f0, a2, 0, 0, 0, 0, 0, 0, FSR__
    test_itof float.s, f0, a2, 1, 1, \
        0x3f000000, 0x3f000000, 0x3f000000, 0x3f000000, FSR__
    test_itof float.s, f0, a2, 1, 0, \
        0x3f800000, 0x3f800000, 0x3f800000, 0x3f800000, FSR__
    test_itof float.s, f0, a2, 0x7fffffff, 0, \
        0x4f000000, 0x4effffff, 0x4f000000, 0x4effffff, FSR_I
test_end

test ufloat_s
    test_itof ufloat.s, f0, a2, 0, 0, 0, 0, 0, 0, FSR__
    test_itof ufloat.s, f0, a2, 1, 1, \
        0x3f000000, 0x3f000000, 0x3f000000, 0x3f000000, FSR__
    test_itof ufloat.s, f0, a2, 1, 0, \
        0x3f800000, 0x3f800000, 0x3f800000, 0x3f800000, FSR__
    test_itof ufloat.s, f0, a2, 0x7fffffff, 0, \
        0x4f000000, 0x4effffff, 0x4f000000, 0x4effffff, FSR_I
    test_itof ufloat.s, f0, a2, 0xffffffff, 0, \
        0x4f800000, 0x4f7fffff, 0x4f800000, 0x4f7fffff, FSR_I
test_end

#endif

test_suite_end

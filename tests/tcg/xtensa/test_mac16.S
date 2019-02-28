#include "macros.inc"

test_suite mac16

#if XCHAL_HAVE_MAC16

#define ext16(v) (((v) & 0xffff) | (((v) & 0x8000) * 0x1ffffffe))
#define mul16(a, b) ((ext16(a) * ext16(b)))

.macro assert_acc_value v
    rsr     a4, ACCLO
    movi    a5, (\v) & 0xffffffff
    assert  eq, a4, a5
    rsr     a4, ACCHI
    movi    a5, (\v) >> 32
    sext    a5, a5, 7
    assert  eq, a4, a5
.endm

.macro init_reg sr, reg, val
    .if (\sr)
    movi    a4, \val
    wsr     a4, \reg
    .else
    movi    \reg, \val
    .endif
.endm

.macro test_mulxx mulop, comb, s, t, a, b
    init_reg \comb & 2, \s, \a
    init_reg \comb & 1, \t, \b

    \mulop\().ll \s, \t
    assert_acc_value mul16(\a, \b)

    \mulop\().lh \s, \t
    assert_acc_value mul16(\a, (\b >> 16))

    \mulop\().hl \s, \t
    assert_acc_value mul16((\a >> 16), \b)

    \mulop\().hh \s, \t
    assert_acc_value mul16((\a >> 16), (\b >> 16))
.endm

test mul_aa
    test_mulxx mul.aa, 0, a2, a3, 0xf7315a5a, 0xa5a5137f
test_end

test mul_ad
    test_mulxx mul.ad, 1, a2, m2, 0xf7315a5a, 0xa5a5137f
test_end

test mul_da
    test_mulxx mul.da, 2, m1, a3, 0xf7315a5a, 0xa5a5137f
test_end

test mul_dd
    test_mulxx mul.dd, 3, m0, m3, 0xf7315a5a, 0xa5a5137f
test_end


.macro init_acc iv
    movi    a4, (\iv) & 0xffffffff
    wsr     a4, ACCLO
    movi    a4, (\iv) >> 32
    wsr     a4, ACCHI
.endm

.macro test_mulxxx mulop, comb, s, t, a, b, iv, op
    init_reg \comb & 2, \s, \a
    init_reg \comb & 1, \t, \b

    init_acc \iv
    \mulop\().ll \s, \t
    assert_acc_value (\iv \op mul16(\a, \b))

    init_acc \iv
    \mulop\().lh \s, \t
    assert_acc_value (\iv \op mul16(\a, (\b >> 16)))

    init_acc \iv
    \mulop\().hl \s, \t
    assert_acc_value (\iv \op mul16((\a >> 16), \b))

    init_acc \iv
    \mulop\().hh \s, \t
    assert_acc_value (\iv \op mul16((\a >> 16), (\b >> 16)))
.endm


test mula_aa
    test_mulxxx mula.aa, 0, a2, a3, 0xf7315a5a, 0xa5a5137f, 0xfff73155aa, +
test_end

test mula_ad
    test_mulxxx mula.ad, 1, a2, m2, 0xf7315a5a, 0xa5a5137f, 0xfff73155aa, +
test_end

test mula_da
    test_mulxxx mula.da, 2, m1, a3, 0xf7315a5a, 0xa5a5137f, 0x0ff73155aa, +
test_end

test mula_dd
    test_mulxxx mula.dd, 3, m0, m3, 0xf7315a5a, 0xa5a5137f, 0x0ff73155aa, +
test_end


test muls_aa
    test_mulxxx muls.aa, 0, a2, a3, 0xf7315a5a, 0xa5a5137f, 0x0ff73155aa, -
test_end

test muls_ad
    test_mulxxx muls.ad, 1, a2, m2, 0xf7315a5a, 0xa5a5137f, 0x0ff73155aa, -
test_end

test muls_da
    test_mulxxx muls.da, 2, m1, a3, 0xf7315a5a, 0xa5a5137f, 0xfff73155aa, -
test_end

test muls_dd
    test_mulxxx muls.dd, 3, m0, m3, 0xf7315a5a, 0xa5a5137f, 0xfff73155aa, -
test_end

test ldinc
    movi    a2, 1f - 4
    ldinc   m0, a2
    movi    a3, 1f
    assert  eq, a2, a3
    rsr     a3, m0
    movi    a4, 0x55aa137f
    assert  eq, a3, a4
    ldinc   m1, a2
    movi    a3, 1f + 4
    assert  eq, a2, a3
    rsr     a3, m1
    movi    a4, 0x12345678
    assert  eq, a3, a4

.data
1:  .word 0x55aa137f, 0x12345678, 0x137fa5a5
.text
test_end

test lddec
    movi    a2, 1f
    lddec   m2, a2
    movi    a3, 1f - 4
    assert  eq, a2, a3
    rsr     a3, m2
    movi    a4, 0x12345678
    assert  eq, a3, a4
    lddec   m3, a2
    movi    a3, 1f - 8
    assert  eq, a2, a3
    rsr     a3, m3
    movi    a4, 0x55aa137f
    assert  eq, a3, a4
.data
    .word 0x55aa137f, 0x12345678
1:
.text
test_end


.macro test_mulxxx_ld mulop, ldop, comb, w, x, s, t, a, b, iv, op
    init_reg \comb & 2, \s, \a
    init_reg \comb & 1, \t, \b

    init_acc \iv
    \mulop\().ll.\ldop \w, \x, \s, \t
    assert_acc_value (\iv \op mul16(\a, \b))

    init_acc \iv
    \mulop\().lh.\ldop \w, \x, \s, \t
    assert_acc_value (\iv \op mul16(\a, (\b >> 16)))

    init_acc \iv
    \mulop\().hl.\ldop \w, \x, \s, \t
    assert_acc_value (\iv \op mul16((\a >> 16), \b))

    init_acc \iv
    \mulop\().hh.\ldop \w, \x, \s, \t
    assert_acc_value (\iv \op mul16((\a >> 16), (\b >> 16)))
.endm

test mula_da_ldinc
    movi    a2, 1f - 4
    test_mulxxx_ld mula.da, ldinc, 2, m1, a2, m1, a3, \
        0xf7315a5a, 0xa5a5137f, 0x0ff73155aa, +
    movi    a3, 1f + 12
    assert  eq, a2, a3
    rsr     a2, m1
    movi    a3, 0x12345678
    assert  eq, a2, a3
.data
1:  .word 0xf7315a5a, 0xf7315a5a, 0xf7315a5a, 0x12345678
.text
test_end

test mula_dd_ldinc
    movi    a2, 1f - 4
    test_mulxxx_ld mula.dd, ldinc, 3, m2, a2, m1, m2, \
        0xf7315a5a, 0xa5a5137f, 0x0ff73155aa, +
    movi    a3, 1f + 12
    assert  eq, a2, a3
    rsr     a2, m2
    movi    a3, 0x12345678
    assert  eq, a2, a3
.data
1:  .word 0xa5a5137f, 0xa5a5137f, 0xa5a5137f, 0x12345678
.text
test_end

test mula_da_lddec
    movi    a2, 1f
    test_mulxxx_ld mula.da, lddec, 2, m1, a2, m1, a3, \
        0xf7315a5a, 0xa5a5137f, 0x0ff73155aa, +
    movi    a3, 1f - 16
    assert  eq, a2, a3
    rsr     a2, m1
    movi    a3, 0x12345678
    assert  eq, a2, a3
.data
    .word 0x12345678, 0xf7315a5a, 0xf7315a5a, 0xf7315a5a
1:
.text
test_end

test mula_dd_lddec
    movi    a2, 1f
    test_mulxxx_ld mula.dd, lddec, 3, m2, a2, m1, m2, \
        0xf7315a5a, 0xa5a5137f, 0x0ff73155aa, +
    movi    a3, 1f - 16
    assert  eq, a2, a3
    rsr     a2, m2
    movi    a3, 0x12345678
    assert  eq, a2, a3
.data
    .word 0x12345678, 0xa5a5137f, 0xa5a5137f, 0xa5a5137f
1:
.text
test_end

#endif

test_suite_end

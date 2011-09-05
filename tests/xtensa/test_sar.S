.include "macros.inc"

test_suite sar

.macro test_sar prefix, imm
    \prefix\()_set \imm
    \prefix\()_ver \imm
.endm

.macro tests_sar prefix
    test_sar \prefix, 0
    test_sar \prefix, 1
    test_sar \prefix, 2
    test_sar \prefix, 3
    test_sar \prefix, 0x1f
    test_sar \prefix, 0x20
    test_sar \prefix, 0x3f
    test_sar \prefix, 0x40
    test_sar \prefix, 0xfffffffe
.endm

.macro sar_set imm
    movi    a2, \imm
    wsr     a2, sar
.endm

.macro sar_ver imm
    rsr     a3, sar
    movi    a2, \imm & 0x3f
    assert  eq, a2, a3
.endm

test sar
    tests_sar sar
test_end

.macro ssr_set imm
    movi    a2, \imm
    ssr     a2
.endm

.macro ssr_ver imm
    rsr     a3, sar
    movi    a2, \imm & 0x1f
    assert  eq, a2, a3
.endm

test ssr
    tests_sar ssr
test_end

.macro ssl_set imm
    movi    a2, \imm
    ssl     a2
.endm

.macro ssl_ver imm
    rsr     a3, sar
    movi    a2, 32 - (\imm & 0x1f)
    assert  eq, a2, a3
.endm

test ssl
    tests_sar ssl
test_end

.macro ssa8l_set imm
    movi    a2, \imm
    ssa8l   a2
.endm

.macro ssa8l_ver imm
    rsr     a3, sar
    movi    a2, (\imm & 0x3) << 3
    assert  eq, a2, a3
.endm

test ssa8l
    tests_sar ssa8l
test_end

.macro ssa8b_set imm
    movi    a2, \imm
    ssa8b   a2
.endm

.macro ssa8b_ver imm
    rsr     a3, sar
    movi    a2, 32 - ((\imm & 0x3) << 3)
    assert  eq, a2, a3
.endm

test ssa8b
    tests_sar ssa8b
test_end

.macro ssai_set imm
    ssai    \imm & 0x1f
.endm

.macro ssai_ver imm
    rsr     a3, sar
    movi    a2, \imm & 0x1f
    assert  eq, a2, a3
.endm

test ssai
    tests_sar ssai
test_end

test_suite_end

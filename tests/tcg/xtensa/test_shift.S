#include "macros.inc"

test_suite shift

.macro test_shift prefix, dst, src, v, imm
    \prefix\()_set \dst, \src, \v, \imm
    \prefix\()_ver \dst, \v, \imm
.endm

.macro test_shift_sd prefix, v, imm
    test_shift \prefix, a3, a2, \v, \imm
    test_shift \prefix, a2, a2, \v, \imm
.endm

.macro tests_imm_shift prefix, v
    test_shift_sd \prefix, \v, 1
    test_shift_sd \prefix, \v, 2
    test_shift_sd \prefix, \v, 7
    test_shift_sd \prefix, \v, 8
    test_shift_sd \prefix, \v, 15
    test_shift_sd \prefix, \v, 16
    test_shift_sd \prefix, \v, 31
.endm

.macro tests_shift prefix, v
    test_shift_sd \prefix, \v, 0
    tests_imm_shift \prefix, \v
    test_shift_sd \prefix, \v, 32
.endm


.macro slli_set dst, src, v, imm
    movi    \src, \v
    slli    \dst, \src, \imm
.endm

.macro slli_ver dst, v, imm
    mov     a2, \dst
    movi    a3, ((\v) << (\imm)) & 0xffffffff
    assert  eq, a2, a3
.endm

test slli
    tests_imm_shift slli, 0xa3c51249
test_end


.macro srai_set dst, src, v, imm
    movi    \src, \v
    srai    \dst, \src, \imm
.endm

.macro srai_ver dst, v, imm
    mov     a2, \dst
    .if (\imm)
    movi    a3, (((\v) >> (\imm)) & 0xffffffff) | \
                ~((((\v) & 0x80000000) >> ((\imm) - 1)) - 1)
    .else
    movi    a3, \v
    .endif
    assert  eq, a2, a3
.endm

test srai
    tests_imm_shift srai, 0x49a3c512
    tests_imm_shift srai, 0xa3c51249
test_end


.macro srli_set dst, src, v, imm
    movi    \src, \v
    srli    \dst, \src, \imm
.endm

.macro srli_ver dst, v, imm
    mov     a2, \dst
    movi    a3, (((\v) >> (\imm)) & 0xffffffff)
    assert  eq, a2, a3
.endm

test srli
    tests_imm_shift srli, 0x49a3c512
    tests_imm_shift srli, 0xa3c51249
test_end


.macro sll_set dst, src, v, imm
    movi    a2, \imm
    ssl     a2
    movi    \src, \v
    sll     \dst, \src
.endm

.macro sll_sar_set dst, src, v, imm
    movi    a2, 32 - \imm
    wsr     a2, sar
    movi    \src, \v
    sll     \dst, \src
.endm

.macro sll_ver dst, v, imm
    slli_ver \dst, \v, (\imm) & 0x1f
.endm

.macro sll_sar_ver dst, v, imm
    slli_ver \dst, \v, \imm
.endm

test sll
    tests_shift sll, 0xa3c51249
    tests_shift sll_sar, 0xa3c51249
test_end


.macro srl_set dst, src, v, imm
    movi    a2, \imm
    ssr     a2
    movi    \src, \v
    srl     \dst, \src
.endm

.macro srl_sar_set dst, src, v, imm
    movi    a2, \imm
    wsr     a2, sar
    movi    \src, \v
    srl     \dst, \src
.endm

.macro srl_ver dst, v, imm
    srli_ver \dst, \v, (\imm) & 0x1f
.endm

.macro srl_sar_ver dst, v, imm
    srli_ver \dst, \v, \imm
.endm

test srl
    tests_shift srl, 0xa3c51249
    tests_shift srl_sar, 0xa3c51249
    tests_shift srl, 0x49a3c512
    tests_shift srl_sar, 0x49a3c512
test_end


.macro sra_set dst, src, v, imm
    movi    a2, \imm
    ssr     a2
    movi    \src, \v
    sra     \dst, \src
.endm

.macro sra_sar_set dst, src, v, imm
    movi    a2, \imm
    wsr     a2, sar
    movi    \src, \v
    sra     \dst, \src
.endm

.macro sra_ver dst, v, imm
    srai_ver \dst, \v, (\imm) & 0x1f
.endm

.macro sra_sar_ver dst, v, imm
    srai_ver \dst, \v, \imm
.endm

test sra
    tests_shift sra, 0xa3c51249
    tests_shift sra_sar, 0xa3c51249
    tests_shift sra, 0x49a3c512
    tests_shift sra_sar, 0x49a3c512
test_end


.macro src_set dst, src, v, imm
    movi    a2, \imm
    ssr     a2
    movi    \src, (\v) & 0xffffffff
    movi    a4, (\v) >> 32
    src     \dst, a4, \src
.endm

.macro src_sar_set dst, src, v, imm
    movi    a2, \imm
    wsr     a2, sar
    movi    \src, (\v) & 0xffffffff
    movi    a4, (\v) >> 32
    src     \dst, a4, \src
.endm

.macro src_ver dst, v, imm
    src_sar_ver \dst, \v, (\imm) & 0x1f
.endm

.macro src_sar_ver dst, v, imm
    mov     a2, \dst
    movi    a3, ((\v) >> (\imm)) & 0xffffffff
    assert  eq, a2, a3
.endm

test src
    tests_shift src, 0xa3c51249215c3a94
    tests_shift src_sar, 0xa3c51249215c3a94
test_end

test_suite_end

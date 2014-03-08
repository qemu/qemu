#include "macros.inc"

.purgem test_init
.macro test_init
    call0   cache_unlock_invalidate
.endm

test_suite cache

.macro      pf_op op
    \op     a2, 0
    \op     a3, 0
    \op     a4, 0
.endm

test prefetch
    movi    a2, 0xd0000000 /* cacheable */
    movi    a3, 0xd8000000 /* non-cacheable */
    movi    a4, 0x00001235 /* unmapped */

    pf_op   dpfr
    pf_op   dpfro
    pf_op   dpfw
    pf_op   dpfwo
    pf_op   ipf

    dpfl    a2, 0
    ipfl    a2, 0
test_end

.macro cache_fault op, addr, exc_code
    set_vector kernel, 2f

    movi    a4, \addr
1:
    \op     a4, 0
    test_fail
2:
    rsr     a2, epc1
    movi    a3, 1b
    assert  eq, a2, a3
    rsr     a2, excvaddr
    assert  eq, a2, a4
    rsr     a2, exccause
    movi    a3, \exc_code
    assert  eq, a2, a3
.endm

test dpfl_tlb_miss
    cache_fault dpfl, 0x00002345, 24
test_end

test dhwb_tlb_miss
    cache_fault dhwb, 0x00002345, 24
test_end

test dhwbi_tlb_miss
    cache_fault dhwbi, 0x00002345, 24
test_end

test dhi_tlb_miss
    cache_fault dhi, 0x00002345, 24
test_end

test dhu_tlb_miss
    cache_fault dhu, 0x00002345, 24
test_end


test ipfl_tlb_miss
    cache_fault ipfl, 0x00002345, 16
test_end

test ihu_tlb_miss
    cache_fault ihu, 0x00002345, 16
test_end

test ihi_tlb_miss
    cache_fault ihi, 0x00002345, 16
test_end

test_suite_end

.macro cache_all op1, op2, size, linesize
    movi    a2, 0
    movi    a3, \size
1:
    \op1    a2, 0
    \op2    a2, 0
    addi    a2, a2, \linesize
    bltu    a2, a3, 1b
.endm

cache_unlock_invalidate:
    cache_all diu, dii, XCHAL_DCACHE_SIZE, XCHAL_DCACHE_LINESIZE
    cache_all iiu, iii, XCHAL_ICACHE_SIZE, XCHAL_ICACHE_LINESIZE
    ret

#include "macros.inc"

.purgem test_init
.macro test_init
    call0   cache_unlock_invalidate
.endm

test_suite cache

#if XCHAL_HAVE_PTP_MMU

.macro      pf_op op
    \op     a2, 0
    \op     a3, 0
    \op     a4, 0
.endm

test prefetch
    movi    a2, 0xd0000000 /* cacheable */
    movi    a3, 0xd8000000 /* non-cacheable */
    movi    a4, 0x00001235 /* unmapped */

#if XCHAL_DCACHE_SIZE
    pf_op   dpfr
    pf_op   dpfro
    pf_op   dpfw
    pf_op   dpfwo
#endif
#ifdef XCHAL_ICACHE_SIZE
    pf_op   ipf
#endif
#if XCHAL_HAVE_PTP_MMU && !XCHAL_HAVE_SPANNING_WAY
#if XCHAL_DCACHE_LINE_LOCKABLE
    dpfl    a2, 0
#endif
#if XCHAL_ICACHE_LINE_LOCKABLE
    ipfl    a2, 0
#endif
#endif
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

#if XCHAL_HAVE_PTP_MMU && !XCHAL_HAVE_SPANNING_WAY

#if XCHAL_DCACHE_LINE_LOCKABLE
test dpfl_tlb_miss
    cache_fault dpfl, 0x00002345, 24
test_end
#endif

#if XCHAL_DCACHE_SIZE
#if XCHAL_DCACHE_IS_WRITEBACK
test dhwb_tlb_miss
    cache_fault dhwb, 0x00002345, 24
test_end

test dhwbi_tlb_miss
    cache_fault dhwbi, 0x00002345, 24
test_end
#endif

test dhi_tlb_miss
    cache_fault dhi, 0x00002345, 24
test_end

#if XCHAL_DCACHE_LINE_LOCKABLE
test dhu_tlb_miss
    cache_fault dhu, 0x00002345, 24
test_end
#endif
#endif

#if XCHAL_ICACHE_SIZE
#if XCHAL_ICACHE_LINE_LOCKABLE
test ipfl_tlb_miss
    cache_fault ipfl, 0x00002345, 16
test_end

test ihu_tlb_miss
    cache_fault ihu, 0x00002345, 16
test_end
#endif

test ihi_tlb_miss
    cache_fault ihi, 0x00002345, 16
test_end
#endif

#endif

#endif

test_suite_end

cache_unlock_invalidate:
#if XCHAL_DCACHE_SIZE
    movi    a2, 0
    movi    a3, XCHAL_DCACHE_SIZE
1:
#if XCHAL_DCACHE_LINE_LOCKABLE
    diu     a2, 0
#endif
    dii     a2, 0
    addi    a2, a2, XCHAL_DCACHE_LINESIZE
    bltu    a2, a3, 1b
#endif
#if XCHAL_ICACHE_SIZE
    movi    a2, 0
    movi    a3, XCHAL_ICACHE_SIZE
1:
#if XCHAL_ICACHE_LINE_LOCKABLE
    iiu     a2, 0
#endif
    iii     a2, 0
    addi    a2, a2, XCHAL_ICACHE_LINESIZE
    bltu    a2, a3, 1b
#endif
    ret

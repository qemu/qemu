#include "macros.inc"

test_suite phys_mem

#if XCHAL_HAVE_PTP_MMU

.purgem test_init

.macro test_init
    movi    a2, 0xc0000003 /* PPN */
    movi    a3, 0xc0000004 /* VPN */
    wdtlb   a2, a3
    witlb   a2, a3
    movi    a2, 0xc0000000
    wsr     a2, ptevaddr
#if XCHAL_HAVE_SPANNING_WAY
    movi    a2, 0xc0000000 | XCHAL_SPANNING_WAY
    idtlb   a2
    iitlb   a2
    movi    a2, 0x20000000 | XCHAL_SPANNING_WAY
    idtlb   a2
    iitlb   a2
#endif
.endm

test inst_fetch_get_pte_no_phys
    set_vector kernel, 2f

    movi    a2, 0x20000000
    jx      a2
2:
    movi    a2, 0x20000000
    rsr     a3, excvaddr
    assert  eq, a2, a3
    rsr     a3, epc1
    assert  eq, a2, a3
    rsr     a3, exccause
    movi    a2, 16
    assert  eq, a2, a3
test_end

test read_get_pte_no_phys
    set_vector kernel, 2f

    movi    a2, 0x20000000
1:
    l32i    a3, a2, 0
    test_fail
2:
    movi    a2, 0x20000000
    rsr     a3, excvaddr
    assert  eq, a2, a3
    movi    a2, 1b
    rsr     a3, epc1
    assert  eq, a2, a3
    rsr     a3, exccause
    movi    a2, 24
    assert  eq, a2, a3
test_end

test write_get_pte_no_phys
    set_vector kernel, 2f

    movi    a2, 0x20000000
1:
    s32i    a3, a2, 0
    test_fail
2:
    movi    a2, 0x20000000
    rsr     a3, excvaddr
    assert  eq, a2, a3
    movi    a2, 1b
    rsr     a3, epc1
    assert  eq, a2, a3
    rsr     a3, exccause
    movi    a2, 24
    assert  eq, a2, a3
test_end

#endif

test inst_fetch_no_phys
    set_vector kernel, 2f

    movi    a2, 0xc0000000
    jx      a2
2:
    movi    a2, 0xc0000000
    rsr     a3, excvaddr
    assert  eq, a2, a3
    rsr     a3, epc1
    assert  eq, a2, a3
    rsr     a3, exccause
    movi    a2, 14
    assert  eq, a2, a3
test_end

test read_no_phys
    set_vector kernel, 2f

    movi    a2, 0xc0000000
1:
    l32i    a3, a2, 0
    test_fail
2:
    movi    a2, 0xc0000000
    rsr     a3, excvaddr
    assert  eq, a2, a3
    movi    a2, 1b
    rsr     a3, epc1
    assert  eq, a2, a3
    rsr     a3, exccause
    movi    a2, 15
    assert  eq, a2, a3
test_end

test write_no_phys
    set_vector kernel, 2f

    movi    a2, 0xc0000000
1:
    s32i    a3, a2, 0
    test_fail
2:
    movi    a2, 0xc0000000
    rsr     a3, excvaddr
    assert  eq, a2, a3
    movi    a2, 1b
    rsr     a3, epc1
    assert  eq, a2, a3
    rsr     a3, exccause
    movi    a2, 15
    assert  eq, a2, a3
test_end

test_suite_end

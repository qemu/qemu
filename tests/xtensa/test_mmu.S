.include "macros.inc"

test_suite mmu

.purgem test

.macro test name
    movi    a2, 0x00000004
    idtlb   a2
    movi    a2, 0x00100004
    idtlb   a2
    movi    a2, 0x00200004
    idtlb   a2
    movi    a2, 0x00300004
    idtlb   a2
    movi    a2, 0x00000007
    idtlb   a2
.endm

test tlb_group
    movi    a2, 0x04000002 /* PPN */
    movi    a3, 0x01200004 /* VPN */
    wdtlb   a2, a3
    witlb   a2, a3
    movi    a3, 0x00200004
    rdtlb0  a1, a3
    ritlb0  a2, a3
    movi    a3, 0x01000001
    assert  eq, a1, a3
    assert  eq, a2, a3
    movi    a3, 0x00200004
    rdtlb1  a1, a3
    ritlb1  a2, a3
    movi    a3, 0x04000002
    assert  eq, a1, a3
    assert  eq, a2, a3
    movi    a3, 0x01234567
    pdtlb   a1, a3
    pitlb   a2, a3
    movi    a3, 0x01234014
    assert  eq, a1, a3
    movi    a3, 0x0123400c
    assert  eq, a2, a3
    movi    a3, 0x00200004
    idtlb   a3
    iitlb   a3
    movi    a3, 0x01234567
    pdtlb   a1, a3
    pitlb   a2, a3
    movi    a3, 0x00000010
    and     a1, a1, a3
    assert  eqi, a1, 0
    movi    a3, 0x00000008
    and     a2, a2, a3
    assert  eqi, a2, 0
test_end

test itlb_miss
    set_vector kernel, 1f

    movi    a3, 0x00100000
    jx      a3
    test_fail
1:
    rsr     a2, excvaddr
    assert  eq, a2, a3
    rsr     a2, exccause
    movi    a3, 16
    assert  eq, a2, a3
test_end

test dtlb_miss
    set_vector kernel, 1f

    movi    a3, 0x00100000
    l8ui    a2, a3, 0
    test_fail
1:
    rsr     a2, excvaddr
    assert  eq, a2, a3
    rsr     a2, exccause
    movi    a3, 24
    assert  eq, a2, a3
test_end

test itlb_multi_hit
    set_vector kernel, 1f

    movi    a2, 0x04000002 /* PPN */
    movi    a3, 0xf0000004 /* VPN */
    witlb   a2, a3
    movi    a3, 0xf0000000
    pitlb   a2, a3
    test_fail
1:
    rsr     a2, exccause
    movi    a3, 17
    assert  eq, a2, a3
test_end

test dtlb_multi_hit
    set_vector kernel, 1f

    movi    a2, 0x04000002 /* PPN */
    movi    a3, 0x01200004 /* VPN */
    wdtlb   a2, a3
    movi    a3, 0x01200007 /* VPN */
    wdtlb   a2, a3
    movi    a3, 0x01200000
    pdtlb   a2, a3
    test_fail
1:
    rsr     a2, exccause
    movi    a3, 25
    assert  eq, a2, a3
test_end

test inst_fetch_privilege
    set_vector kernel, 3f

    movi    a2, 0x4004f
    wsr     a2, ps
1:
    isync
    nop
2:
    test_fail
3:
    movi    a1, 1b
    rsr     a2, excvaddr
    rsr     a3, epc1
    assert  ge, a2, a1
    assert  ge, a3, a1
    movi    a1, 2b
    assert  lt, a2, a1
    assert  lt, a3, a1
    rsr     a2, exccause
    movi    a3, 18
    assert  eq, a2, a3
    rsr     a2, ps
    movi    a3, 0x4005f
    assert  eq, a2, a3
test_end

test load_store_privilege
    set_vector kernel, 2f

    movi    a3, 10f
    pitlb   a3, a3
    ritlb1  a2, a3
    movi    a1, 0x10
    or      a2, a2, a1
    movi    a1, 0x000ff000
    and     a3, a3, a1
    movi    a1, 4
    or      a3, a3, a1
    witlb   a2, a3
    movi    a3, 10f
    movi    a1, 0x000fffff
    and     a1, a3, a1

    movi    a2, 0x04000003 /* PPN */
    movi    a3, 0x01200004 /* VPN */
    wdtlb   a2, a3
    movi    a3, 0x01200001
    movi    a2, 0x4004f
    jx      a1
10:
    wsr     a2, ps
    isync
1:
    l8ui    a2, a3, 0
    test_fail
2:
    rsr     a2, excvaddr
    assert  eq, a2, a3
    rsr     a2, epc1
    movi    a3, 1b
    movi    a1, 0x000fffff
    and     a3, a3, a1
    assert  eq, a2, a3
    rsr     a2, exccause
    movi    a3, 26
    assert  eq, a2, a3
    rsr     a2, ps
    movi    a3, 0x4005f
    assert  eq, a2, a3
test_end

test cring_load_store_privilege
    set_vector kernel, 0
    set_vector double, 2f

    movi    a2, 0x04000003 /* PPN */
    movi    a3, 0x01200004 /* VPN */
    wdtlb   a2, a3
    movi    a3, 0x01200004
    movi    a2, 0x4005f    /* ring 1 + excm => cring == 0 */
    wsr     a2, ps
    isync
    l8ui    a2, a3, 0      /* cring used */
1:
    l32e    a2, a3, -4     /* ring used */
    test_fail
2:
    rsr     a2, excvaddr
    addi    a2, a2, 4
    assert  eq, a2, a3
    rsr     a2, depc
    movi    a3, 1b
    assert  eq, a2, a3
    rsr     a2, exccause
    movi    a3, 26
    assert  eq, a2, a3
    rsr     a2, ps
    movi    a3, 0x4005f
    assert  eq, a2, a3
test_end

test inst_fetch_prohibited
    set_vector kernel, 2f

    movi    a3, 10f
    pitlb   a3, a3
    ritlb1  a2, a3
    movi    a1, 0xfffff000
    and     a2, a2, a1
    movi    a1, 0x4
    or      a2, a2, a1
    movi    a1, 0x000ff000
    and     a3, a3, a1
    movi    a1, 4
    or      a3, a3, a1
    witlb   a2, a3
    movi    a3, 10f
    movi    a1, 0x000fffff
    and     a1, a3, a1
    jx      a1
    .align  4
10:
    nop
    test_fail
2:
    rsr     a2, excvaddr
    assert  eq, a2, a1
    rsr     a2, epc1
    assert  eq, a2, a1
    rsr     a2, exccause
    movi    a3, 20
    assert  eq, a2, a3
test_end

test load_prohibited
    set_vector kernel, 2f

    movi    a2, 0x0400000c /* PPN */
    movi    a3, 0x01200004 /* VPN */
    wdtlb   a2, a3
    movi    a3, 0x01200002
1:
    l8ui    a2, a3, 0
    test_fail
2:
    rsr     a2, excvaddr
    assert  eq, a2, a3
    rsr     a2, epc1
    movi    a3, 1b
    assert  eq, a2, a3
    rsr     a2, exccause
    movi    a3, 28
    assert  eq, a2, a3
test_end

test store_prohibited
    set_vector kernel, 2f

    movi    a2, 0x04000001 /* PPN */
    movi    a3, 0x01200004 /* VPN */
    wdtlb   a2, a3
    movi    a3, 0x01200003
    l8ui    a2, a3, 0
1:
    s8i     a2, a3, 0
    test_fail
2:
    rsr     a2, excvaddr
    assert  eq, a2, a3
    rsr     a2, epc1
    movi    a3, 1b
    assert  eq, a2, a3
    rsr     a2, exccause
    movi    a3, 29
    assert  eq, a2, a3
test_end

test dtlb_autoload
    set_vector kernel, 0

    movi    a2, 0xd4000000
    wsr     a2, ptevaddr
    movi    a3, 0x00001013
    s32i    a3, a2, 4
    pdtlb   a2, a3
    movi    a1, 0x10
    and     a1, a1, a2
    assert  eqi, a1, 0
    l8ui    a1, a3, 0
    pdtlb   a2, a3
    movi    a1, 0xfffff010
    and     a1, a1, a2
    movi    a3, 0x00001010
    assert  eq, a1, a3
    movi    a1, 0xf
    and     a1, a1, a2
    assert  lti, a1, 4
test_end

test_suite_end

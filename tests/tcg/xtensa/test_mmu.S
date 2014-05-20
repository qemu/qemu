#include "macros.inc"

test_suite mmu

.purgem test_init

.macro clean_tlb_way way, page_size, n_entries
    movi    a2, \way
    movi    a3, \page_size
    movi    a4, \n_entries
    loop    a4, 1f
    idtlb   a2
    iitlb   a2
    add     a2, a2, a3
1:
.endm

.macro test_init
    clean_tlb_way 0, 0x00001000, 4
    clean_tlb_way 1, 0x00001000, 4
    clean_tlb_way 2, 0x00001000, 4
    clean_tlb_way 3, 0x00001000, 4
    clean_tlb_way 4, 0x00100000, 4
    movi    a2, 0x00000007
    idtlb   a2
    movi    a2, 0x00000008
    idtlb   a2
    movi    a2, 0x00000009
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

/* Set up page table entry vaddr->paddr, ring=pte_ring, attr=pte_attr
 * and DTLB way 7 to cover this PTE, ring=pt_ring, attr=pt_attr
 */
.macro pt_setup pt_ring, pt_attr, pte_ring, vaddr, paddr, pte_attr
    movi    a2, 0x80000000
    wsr     a2, ptevaddr

    movi    a3, 0x80000007 | (((\vaddr) >> 10) & 0xfffff000) /* way 7 */
    movi    a4, 0x04000003 | ((\pt_ring) << 4) /* PADDR 64M */
    wdtlb   a4, a3
    isync

    movi    a3, ((\paddr) & 0xfffff000) | ((\pte_ring) << 4) | (\pte_attr)
    movi    a1, ((\vaddr) >> 12) << 2
    add     a2, a1, a2
    s32i    a3, a2, 0

    movi    a3, 0x80000007 | (((\vaddr) >> 10) & 0xfffff000) /* way 7 */
    movi    a4, 0x04000000 | ((\pt_ring) << 4) | (\pt_attr) /* PADDR 64M */
    wdtlb   a4, a3
    isync

    movi    a3, (\vaddr)
.endm

/* out: PS.RING=ring, PS.EXCM=excm, a3=vaddr */
.macro go_ring ring, excm, vaddr
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

    movi    a2, 0
    wsr     a2, excvaddr

    movi    a3, \vaddr
    movi    a2, 0x4000f | ((\ring) << 6) | ((\excm) << 4)
    jx      a1
10:
    wsr     a2, ps
    isync
.endm

/* in: a3 -- virtual address to test */
.macro assert_auto_tlb
    movi    a2, 0x4000f
    wsr     a2, ps
    isync
    pdtlb   a2, a3
    movi    a1, 0xfffff01f
    and     a2, a2, a1
    movi    a1, 0xfffff000
    and     a1, a1, a3
    xor     a1, a1, a2
    assert  gei, a1, 0x10
    movi    a2, 0x14
    assert  lt, a1, a2
.endm

/* in: a3 -- virtual address to test */
.macro assert_no_auto_tlb
    movi    a2, 0x4000f
    wsr     a2, ps
    isync
    pdtlb   a2, a3
    movi    a1, 0x10
    and     a1, a1, a2
    assert  eqi, a1, 0
.endm

.macro assert_sr sr, v
    rsr     a2, \sr
    movi    a1, (\v)
    assert  eq, a1, a2
.endm

.macro assert_epc1_1m vaddr
    movi    a2, (\vaddr)
    movi    a1, 0xfffff
    and     a1, a1, a2
    rsr     a2, epc1
    assert  eq, a1, a2
.endm

test dtlb_autoload
    set_vector kernel, 0

    pt_setup    0, 3, 1, 0x1000, 0x1000, 3
    assert_no_auto_tlb

    l8ui    a1, a3, 0

    rsr     a2, excvaddr
    assert  eq, a2, a3

    assert_auto_tlb
test_end

test autoload_load_store_privilege
    set_vector kernel, 0
    set_vector double, 2f

    pt_setup    0, 3, 0, 0x2000, 0x2000, 3
    movi    a3, 0x2004
    assert_no_auto_tlb

    movi    a2, 0x4005f    /* ring 1 + excm => cring == 0 */
    wsr     a2, ps
    isync
1:
    l32e    a2, a3, -4     /* ring used */
    test_fail
2:
    rsr     a2, excvaddr
    addi    a1, a3, -4
    assert  eq, a1, a2

    assert_auto_tlb
    assert_sr depc, 1b
    assert_sr exccause, 26
test_end

test autoload_pte_load_prohibited
    set_vector kernel, 2f

    pt_setup    0, 3, 0, 0x3000, 0, 0xc
    assert_no_auto_tlb
1:
    l32i    a2, a3, 0
    test_fail
2:
    rsr     a2, excvaddr
    assert  eq, a2, a3

    assert_auto_tlb
    assert_sr epc1, 1b
    assert_sr exccause, 28
test_end

test autoload_pt_load_prohibited
    set_vector kernel, 2f

    pt_setup    0, 0xc, 0, 0x4000, 0x4000, 3
    assert_no_auto_tlb
1:
    l32i    a2, a3, 0
    test_fail
2:
    rsr     a2, excvaddr
    assert  eq, a2, a3

    assert_no_auto_tlb
    assert_sr epc1, 1b
    assert_sr exccause, 24
test_end

test autoload_pt_privilege
    set_vector  kernel, 2f
    pt_setup    0, 3, 1, 0x5000, 0, 3
    go_ring     1, 0, 0x5001

    l8ui    a2, a3, 0
1:
    syscall
2:
    rsr     a2, excvaddr
    assert  eq, a2, a3

    assert_auto_tlb
    assert_epc1_1m 1b
    assert_sr exccause, 1
test_end

test autoload_pte_privilege
    set_vector  kernel, 2f
    pt_setup    0, 3, 0, 0x6000, 0, 3
    go_ring     1, 0, 0x6001
1:
    l8ui    a2, a3, 0
    syscall
2:
    rsr     a2, excvaddr
    assert  eq, a2, a3

    assert_auto_tlb
    assert_epc1_1m 1b
    assert_sr exccause, 26
test_end

test autoload_3_level_pt
    set_vector  kernel, 2f
    pt_setup    1, 3, 1, 0x00400000, 0, 3
    pt_setup    1, 3, 1, 0x80001000, 0x2000000, 3
    go_ring     1, 0, 0x00400001
1:
    l8ui    a2, a3, 0
    syscall
2:
    rsr     a2, excvaddr
    assert  eq, a2, a3

    assert_no_auto_tlb
    assert_epc1_1m 1b
    assert_sr exccause, 24
test_end

test_suite_end

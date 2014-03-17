#include "macros.inc"

test_suite sext

test sext
    movi    a2, 0xffffff5a
    movi    a3, 0x0000005a
    sext    a4, a2, 7
    assert  eq, a3, a4

    movi    a2, 0x000000a5
    movi    a3, 0xffffffa5
    sext    a4, a2, 7
    assert  eq, a3, a4

    movi    a2, 0xfffffaa5
    movi    a3, 0x000000a5
    sext    a4, a2, 8
    assert  eq, a3, a4

    movi    a2, 0x0000055a
    movi    a3, 0xffffff5a
    sext    a4, a2, 8
    assert  eq, a3, a4

    movi    a2, 0xffff5a5a
    movi    a3, 0x00005a5a
    sext    a4, a2, 15
    assert  eq, a3, a4

    movi    a2, 0x0000a5a5
    movi    a3, 0xffffa5a5
    sext    a4, a2, 15
    assert  eq, a3, a4

    movi    a2, 0x00055a5a
    movi    a3, 0xffff5a5a
    sext    a4, a2, 16
    assert  eq, a3, a4

    movi    a2, 0x000aa5a5
    movi    a3, 0x0000a5a5
    sext    a4, a2, 16
    assert  eq, a3, a4

    movi    a2, 0x005a5a5a
    movi    a3, 0xffda5a5a
    sext    a4, a2, 22
    assert  eq, a3, a4

    movi    a2, 0xffa5a5a5
    movi    a3, 0x0025a5a5
    sext    a4, a2, 22
    assert  eq, a3, a4
test_end

test sext_same_rs
    movi    a2, 0xffffff5a
    movi    a3, 0x0000005a
    sext    a2, a2, 7
    assert  eq, a3, a2

    movi    a2, 0x000000a5
    movi    a3, 0xffffffa5
    sext    a2, a2, 7
    assert  eq, a3, a2
test_end

test_suite_end

#include "macros.h"
.text
.global _start
_start:
    TEST_D_DD_PSW(muls.u, 1, 0xffffffff, 0x78000b80, 0x80000001, 0xffffffff)
    TEST_D_DD_PSW(muls.u, 2, 0xffffffff, 0x60000b80, 0xfffffffe, 0xffffffff)

    TEST_PASSFAIL


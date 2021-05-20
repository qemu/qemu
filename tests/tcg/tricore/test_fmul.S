#include "macros.h"
.text
.global _start
_start:
    TEST_D_DD_PSW(mul.f, 1, 0x974f4f0a, 0x84000b80, 0x1a0b1980, 0xbcbec42d)

    TEST_PASSFAIL


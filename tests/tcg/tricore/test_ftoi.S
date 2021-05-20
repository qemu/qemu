#include "macros.h"
.text
.global _start
_start:
    TEST_D_D_PSW(ftoi, 1, 0x0, 0x84000b80, 0x05f6e605)
    TEST_D_D_PSW(ftoi, 2, 0x0, 0x04000b80, 0x00012200)
    TEST_D_D_PSW(ftoi, 3, 0x0, 0xc4000b80, 0xffffffff)

    TEST_PASSFAIL


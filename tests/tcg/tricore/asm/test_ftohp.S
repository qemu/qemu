#include "macros.h"
.text
.global _start
_start:
    TEST_D_D(ftohp, 1, 0xffff, 0xffffffff)
    TEST_D_D(ftohp, 2, 0xfc00, 0xff800000)
    TEST_D_D(ftohp, 3, 0x7c00, 0x7f800000)
    TEST_D_D(ftohp, 4, 0x0, 0x0)
    TEST_D_D(ftohp, 5, 0x5, 0x34a43580)

    #TEST_D_D_PSW(ftohp, 6, 0x400, 0x8c000b80, 0x387fee74)

    TEST_PASSFAIL


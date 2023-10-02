#include "macros.h"
.text
.global _start
_start:
    TEST_D_D(ftou, 1, 0x00000000, 0x1733f6c2)
    TEST_D_D(ftou, 2, 0x00000000, 0x2c9d9cdc)
    TEST_D_D(ftou, 3, 0xffffffff, 0x56eb7395)
    TEST_D_D(ftou, 4, 0x79900800, 0x4ef32010)
    TEST_D_D(ftou, 5, 0x0353f510, 0x4c54fd44)

    TEST_PASSFAIL


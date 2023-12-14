#include "macros.h"
.text
.global _start
_start:
    TEST_D_DD_PSW(bmerge, 1, 0x555557f7, 0x00000b80, 0x0000001d, 0x0000ffff)

    TEST_PASSFAIL


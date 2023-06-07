#include "macros.h"
.text
.global _start
_start:
    TEST_D_D(cls.h, 1, 0x0, 0x6db17976)
    TEST_D_D(cls.h, 2, 0x000f000f, 0x0)

    TEST_PASSFAIL


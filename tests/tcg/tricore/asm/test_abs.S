#include "macros.h"
.text
.global _start
_start:
    TEST_D_D(abs, 1, 0, 0)

    TEST_PASSFAIL

#include "macros.h"
.text
.global _start
_start:
    TEST_D_D(hptof, 1, 0xba190000, 0xcc0e90c8)
    TEST_D_D(hptof, 2, 0x3eaea000, 0x8be23575)
    TEST_D_D(hptof, 3, 0xc33b8000, 0xcc48d9dc)
    TEST_D_D(hptof, 4, 0x43e2a000, 0xaef95f15)
    TEST_D_D(hptof, 5, 0x3d55e000, 0x04932aaf)

    TEST_PASSFAIL


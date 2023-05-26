#include "macros.h"
.text
.global _start
_start:
    TEST_D_DDI_PSW(msub, 1, 0xd2fbe5e0, 0x00000b80,0x64003300, 0xff5420d4, -216)
    TEST_D_DDI_PSW(msub, 2, 0xfffffc10, 0x00000b80,0xfffffe68, 0xfffffffd, -200)
    TEST_D_DDD_PSW(msubs.u, 3, 0x0, 0x60000b80, 0x1, 0xffffffff, 0xffffffdb)
    TEST_PASSFAIL


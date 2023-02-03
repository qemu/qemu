#include "macros.h"
.data
test_data:
    .word 0xaffedead
    .word 0x001122ff
.text
.global _start
_start:
#                               expect. addr reg val after load
#              insn  num  expect. load value |          pattern for loading
#                |    |     |                |              |
    TEST_LD    (ld.h, 1, 0xffffaffe, MEM_BASE_ADDR, [AREG_ADDR]2)
    TEST_LD_SRO(ld.h, 2, 0x000022ff, MEM_BASE_ADDR, [AREG_ADDR]4)

    TEST_PASSFAIL

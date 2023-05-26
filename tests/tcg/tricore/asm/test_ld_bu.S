#include "macros.h"
.data
test_data:
    .word 0xaffedead
    .word 0x001122ff
.text
.global _start
_start:
#                            expect. addr reg val after load
#           insn  num  expect. load value |          pattern for loading
#             |    |     |                |              |
    TEST_LD(ld.bu, 1, 0xff, MEM_BASE_ADDR + 4, [+AREG_ADDR]4) # pre_inc
    TEST_LD(ld.bu, 2, 0xad, MEM_BASE_ADDR + 4, [AREG_ADDR+]4) # post_inc

    TEST_PASSFAIL

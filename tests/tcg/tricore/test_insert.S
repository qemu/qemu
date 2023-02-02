#include "macros.h"
.text
.global _start
_start:
#                insn num    result        rs1    imm1   rs2  imm2
#                 |     |      |            |       |     |    |
    TEST_D_DIDI(insert, 1, 0x7fffffff, 0xffffffff, 0xa, 0x10, 0x8)

    TEST_PASSFAIL

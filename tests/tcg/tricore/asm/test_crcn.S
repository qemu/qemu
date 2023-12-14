#include "macros.h"
.text
.global _start
_start:
#                insn num    result   rs1    rs2     rs3
#                 |     |      |      |       |       |
    TEST_D_DDD(crcn, 1, 0x00002bed, 0x0, 0xa10ddeed, 0x0)

    TEST_PASSFAIL

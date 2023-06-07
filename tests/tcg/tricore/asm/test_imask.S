#include "macros.h"
.text
.global _start
_start:
#                                   res[31:0]
#              insn  num  res[63:32]   |        imm1  rs1   imm2
#                |    |       |        |          |    |     |
    TEST_E_IDI(imask, 1, 0x000f0000, 0x00050000, 0x5, 0x10, 0x4)

    TEST_PASSFAIL

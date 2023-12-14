#include "macros.h"
.text
.global _start
_start:
#                insn num    result        rs1    imm1   rs2  imm2
#                 |     |      |            |       |     |    |
    TEST_D_DIDI(insert, 1, 0x7fffffff, 0xffffffff, 0xa, 0x10, 0x8)

#                insn num    result        rs1    imm1   imm2 imm3
#                 |     |      |            |       |     |    |
    TEST_D_DIII(insert, 2, 0xd38fe370, 0xd38fe370, 0x4, 0x4 , 0x0)
    TEST_D_DIII(insert, 3, 0xd38fe374, 0xd38fe370, 0x4, 0x0 , 0x4)

#                insn  num   result       rs1         rs2      pos  width
#                 |     |      |           |           |        |    |
    TEST_D_DDII(insert, 4, 0x03c1e53c, 0x03c1e53c, 0x45821385, 0x7 ,0x0)

#                insn num   result       rs1     imm1      rs2_h       rs2_l
#                 |    |      |           |        |         |           |
    TEST_D_DIE(insert, 5, 0xe30c308d, 0xe30c308d ,0x3 , 0x00000000 ,0x00000000)
    TEST_D_DIE(insert, 6, 0x669b0120, 0x669b2820 ,0x2 , 0x5530a1c7 ,0x3a2b0f67)

    TEST_PASSFAIL

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

    TEST_PASSFAIL

/*
 *  CRISv10 insn decoding macros.
 *
 *  Copyright (c) 2010 AXIS Communications AB
 *  Written by Edgar E. Iglesias.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef TARGET_CRIS_CRISV10_DECODE_H
#define TARGET_CRIS_CRISV10_DECODE_H

#define CRISV10_MODE_QIMMEDIATE  0
#define CRISV10_MODE_REG         1
#define CRISV10_MODE_INDIRECT    2
#define CRISV10_MODE_AUTOINC     3

/* Quick Immediate.  */
#define CRISV10_QIMM_BCC_R0      0
#define CRISV10_QIMM_BCC_R1      1
#define CRISV10_QIMM_BCC_R2      2
#define CRISV10_QIMM_BCC_R3      3

#define CRISV10_QIMM_BDAP_R0     4
#define CRISV10_QIMM_BDAP_R1     5
#define CRISV10_QIMM_BDAP_R2     6
#define CRISV10_QIMM_BDAP_R3     7

#define CRISV10_QIMM_ADDQ        8
#define CRISV10_QIMM_MOVEQ       9
#define CRISV10_QIMM_SUBQ       10
#define CRISV10_QIMM_CMPQ       11
#define CRISV10_QIMM_ANDQ       12
#define CRISV10_QIMM_ORQ        13
#define CRISV10_QIMM_ASHQ       14
#define CRISV10_QIMM_LSHQ       15


#define CRISV10_REG_ADDX         0
#define CRISV10_REG_MOVX         1
#define CRISV10_REG_SUBX         2
#define CRISV10_REG_LSL          3
#define CRISV10_REG_ADDI         4
#define CRISV10_REG_BIAP         5
#define CRISV10_REG_NEG          6
#define CRISV10_REG_BOUND        7
#define CRISV10_REG_ADD          8
#define CRISV10_REG_MOVE_R       9
#define CRISV10_REG_MOVE_SPR_R   9
#define CRISV10_REG_MOVE_R_SPR   8
#define CRISV10_REG_SUB         10
#define CRISV10_REG_CMP         11
#define CRISV10_REG_AND         12
#define CRISV10_REG_OR          13
#define CRISV10_REG_ASR         14
#define CRISV10_REG_LSR         15

#define CRISV10_REG_BTST         3
#define CRISV10_REG_SCC          4
#define CRISV10_REG_SETF         6
#define CRISV10_REG_CLEARF       7
#define CRISV10_REG_BIAP         5
#define CRISV10_REG_ABS         10
#define CRISV10_REG_DSTEP       11
#define CRISV10_REG_LZ          12
#define CRISV10_REG_NOT         13
#define CRISV10_REG_SWAP        13
#define CRISV10_REG_XOR         14
#define CRISV10_REG_MSTEP       15

/* Indirect, var size.  */
#define CRISV10_IND_TEST        14
#define CRISV10_IND_MUL          4
#define CRISV10_IND_BDAP_M       5
#define CRISV10_IND_ADD          8
#define CRISV10_IND_MOVE_M_R     9


/* indirect fixed size.  */
#define CRISV10_IND_ADDX         0
#define CRISV10_IND_MOVX         1
#define CRISV10_IND_SUBX         2
#define CRISV10_IND_CMPX         3
#define CRISV10_IND_JUMP_M       4
#define CRISV10_IND_DIP          5
#define CRISV10_IND_JUMP_R       6
#define CRISV17_IND_ADDC         6
#define CRISV10_IND_BOUND        7
#define CRISV10_IND_BCC_M        7
#define CRISV10_IND_MOVE_M_SPR   8
#define CRISV10_IND_MOVE_SPR_M   9
#define CRISV10_IND_SUB         10
#define CRISV10_IND_CMP         11
#define CRISV10_IND_AND         12
#define CRISV10_IND_OR          13
#define CRISV10_IND_MOVE_R_M    15

#define CRISV10_IND_MOVEM_M_R    14
#define CRISV10_IND_MOVEM_R_M    15

#endif

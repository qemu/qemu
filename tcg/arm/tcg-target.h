/*
 * Tiny Code Generator for QEMU
 *
 * Copyright (c) 2008 Fabrice Bellard
 * Copyright (c) 2008 Andrzej Zaborowski
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef ARM_TCG_TARGET_H
#define ARM_TCG_TARGET_H

#define TCG_TARGET_INSN_UNIT_SIZE 4
#define MAX_CODE_GEN_BUFFER_SIZE  UINT32_MAX

typedef enum {
    TCG_REG_R0 = 0,
    TCG_REG_R1,
    TCG_REG_R2,
    TCG_REG_R3,
    TCG_REG_R4,
    TCG_REG_R5,
    TCG_REG_R6,
    TCG_REG_R7,
    TCG_REG_R8,
    TCG_REG_R9,
    TCG_REG_R10,
    TCG_REG_R11,
    TCG_REG_R12,
    TCG_REG_R13,
    TCG_REG_R14,
    TCG_REG_PC,

    TCG_REG_Q0,
    TCG_REG_Q1,
    TCG_REG_Q2,
    TCG_REG_Q3,
    TCG_REG_Q4,
    TCG_REG_Q5,
    TCG_REG_Q6,
    TCG_REG_Q7,
    TCG_REG_Q8,
    TCG_REG_Q9,
    TCG_REG_Q10,
    TCG_REG_Q11,
    TCG_REG_Q12,
    TCG_REG_Q13,
    TCG_REG_Q14,
    TCG_REG_Q15,

    TCG_AREG0 = TCG_REG_R6,
    TCG_REG_CALL_STACK = TCG_REG_R13,
} TCGReg;

#define TCG_TARGET_NB_REGS 32

#endif

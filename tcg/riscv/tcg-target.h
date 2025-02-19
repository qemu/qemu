/*
 * Tiny Code Generator for QEMU
 *
 * Copyright (c) 2018 SiFive, Inc
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

#ifndef RISCV_TCG_TARGET_H
#define RISCV_TCG_TARGET_H

#define TCG_TARGET_INSN_UNIT_SIZE 4
#define TCG_TARGET_NB_REGS 64
#define MAX_CODE_GEN_BUFFER_SIZE  ((size_t)-1)

typedef enum {
    TCG_REG_ZERO, TCG_REG_RA,  TCG_REG_SP,  TCG_REG_GP,
    TCG_REG_TP,   TCG_REG_T0,  TCG_REG_T1,  TCG_REG_T2,
    TCG_REG_S0,   TCG_REG_S1,  TCG_REG_A0,  TCG_REG_A1,
    TCG_REG_A2,   TCG_REG_A3,  TCG_REG_A4,  TCG_REG_A5,
    TCG_REG_A6,   TCG_REG_A7,  TCG_REG_S2,  TCG_REG_S3,
    TCG_REG_S4,   TCG_REG_S5,  TCG_REG_S6,  TCG_REG_S7,
    TCG_REG_S8,   TCG_REG_S9,  TCG_REG_S10, TCG_REG_S11,
    TCG_REG_T3,   TCG_REG_T4,  TCG_REG_T5,  TCG_REG_T6,

    /* RISC-V V Extension registers */
    TCG_REG_V0,   TCG_REG_V1,  TCG_REG_V2,  TCG_REG_V3,
    TCG_REG_V4,   TCG_REG_V5,  TCG_REG_V6,  TCG_REG_V7,
    TCG_REG_V8,   TCG_REG_V9,  TCG_REG_V10, TCG_REG_V11,
    TCG_REG_V12,  TCG_REG_V13, TCG_REG_V14, TCG_REG_V15,
    TCG_REG_V16,  TCG_REG_V17, TCG_REG_V18, TCG_REG_V19,
    TCG_REG_V20,  TCG_REG_V21, TCG_REG_V22, TCG_REG_V23,
    TCG_REG_V24,  TCG_REG_V25, TCG_REG_V26, TCG_REG_V27,
    TCG_REG_V28,  TCG_REG_V29, TCG_REG_V30, TCG_REG_V31,

    /* aliases */
    TCG_AREG0          = TCG_REG_S0,
    TCG_GUEST_BASE_REG = TCG_REG_S1,
    TCG_REG_TMP0       = TCG_REG_T6,
    TCG_REG_TMP1       = TCG_REG_T5,
    TCG_REG_TMP2       = TCG_REG_T4,
} TCGReg;

#define TCG_REG_ZERO  TCG_REG_ZERO

#endif

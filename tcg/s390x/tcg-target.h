/*
 * Tiny Code Generator for QEMU
 *
 * Copyright (c) 2009 Ulrich Hecht <uli@suse.de>
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

#ifndef S390_TCG_TARGET_H
#define S390_TCG_TARGET_H

#define TCG_TARGET_INSN_UNIT_SIZE 2

/* We have a +- 4GB range on the branches; leave some slop.  */
#define MAX_CODE_GEN_BUFFER_SIZE  (3 * GiB)

typedef enum TCGReg {
    TCG_REG_R0,  TCG_REG_R1,  TCG_REG_R2,  TCG_REG_R3,
    TCG_REG_R4,  TCG_REG_R5,  TCG_REG_R6,  TCG_REG_R7,
    TCG_REG_R8,  TCG_REG_R9,  TCG_REG_R10, TCG_REG_R11,
    TCG_REG_R12, TCG_REG_R13, TCG_REG_R14, TCG_REG_R15,

    TCG_REG_V0 = 32, TCG_REG_V1,  TCG_REG_V2,  TCG_REG_V3,
    TCG_REG_V4,  TCG_REG_V5,  TCG_REG_V6,  TCG_REG_V7,
    TCG_REG_V8,  TCG_REG_V9,  TCG_REG_V10, TCG_REG_V11,
    TCG_REG_V12, TCG_REG_V13, TCG_REG_V14, TCG_REG_V15,
    TCG_REG_V16, TCG_REG_V17, TCG_REG_V18, TCG_REG_V19,
    TCG_REG_V20, TCG_REG_V21, TCG_REG_V22, TCG_REG_V23,
    TCG_REG_V24, TCG_REG_V25, TCG_REG_V26, TCG_REG_V27,
    TCG_REG_V28, TCG_REG_V29, TCG_REG_V30, TCG_REG_V31,

    TCG_AREG0 = TCG_REG_R10,
    TCG_REG_CALL_STACK = TCG_REG_R15
} TCGReg;

#define TCG_TARGET_NB_REGS 64

#endif

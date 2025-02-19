/*
 * Tiny Code Generator for QEMU
 *
 * Copyright (c) 2009, 2011 Stefan Weil
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

/*
 * This code implements a TCG which does not generate machine code for some
 * real target machine but which generates virtual machine code for an
 * interpreter. Interpreted pseudo code is slow, but it works on any host.
 *
 * Some remarks might help in understanding the code:
 *
 * "target" or "TCG target" is the machine which runs the generated code.
 * This is different to the usual meaning in QEMU where "target" is the
 * emulated machine. So normally QEMU host is identical to TCG target.
 * Here the TCG target is a virtual machine, but this virtual machine must
 * use the same word size like the real machine.
 * Therefore, we need both 32 and 64 bit virtual machines (interpreter).
 */

#ifndef TCG_TARGET_H
#define TCG_TARGET_H

#define TCG_TARGET_INTERPRETER 1
#define TCG_TARGET_INSN_UNIT_SIZE 4
#define MAX_CODE_GEN_BUFFER_SIZE  ((size_t)-1)

/* Number of registers available. */
#define TCG_TARGET_NB_REGS 16

/* List of registers which are used by TCG. */
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
    TCG_REG_R15,

    TCG_REG_TMP = TCG_REG_R13,
    TCG_AREG0 = TCG_REG_R14,
    TCG_REG_CALL_STACK = TCG_REG_R15,
} TCGReg;

#define HAVE_TCG_QEMU_TB_EXEC

#endif /* TCG_TARGET_H */

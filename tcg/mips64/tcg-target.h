/*
 * Tiny Code Generator for QEMU
 *
 * Copyright (c) 2008-2009 Arnaud Patard <arnaud.patard@rtp-net.org>
 * Copyright (c) 2009 Aurelien Jarno <aurelien@aurel32.net>
 * Based on i386/tcg-target.c - Copyright (c) 2008 Fabrice Bellard
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

#ifndef MIPS_TCG_TARGET_H
#define MIPS_TCG_TARGET_H

#define TCG_TARGET_INSN_UNIT_SIZE 4
#define TCG_TARGET_NB_REGS 32

#define MAX_CODE_GEN_BUFFER_SIZE  ((size_t)-1)

typedef enum {
    TCG_REG_ZERO = 0,
    TCG_REG_AT,
    TCG_REG_V0,
    TCG_REG_V1,
    TCG_REG_A0,
    TCG_REG_A1,
    TCG_REG_A2,
    TCG_REG_A3,
    TCG_REG_T0,
    TCG_REG_T1,
    TCG_REG_T2,
    TCG_REG_T3,
    TCG_REG_T4,
    TCG_REG_T5,
    TCG_REG_T6,
    TCG_REG_T7,
    TCG_REG_S0,
    TCG_REG_S1,
    TCG_REG_S2,
    TCG_REG_S3,
    TCG_REG_S4,
    TCG_REG_S5,
    TCG_REG_S6,
    TCG_REG_S7,
    TCG_REG_T8,
    TCG_REG_T9,
    TCG_REG_K0,
    TCG_REG_K1,
    TCG_REG_GP,
    TCG_REG_SP,
    TCG_REG_S8,
    TCG_REG_RA,

    TCG_REG_CALL_STACK = TCG_REG_SP,
    TCG_AREG0 = TCG_REG_S8,
} TCGReg;

#define TCG_REG_ZERO  TCG_REG_ZERO

#endif

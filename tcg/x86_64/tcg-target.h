/*
 * Tiny Code Generator for QEMU
 *
 * Copyright (c) 2008 Fabrice Bellard
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
#define TCG_TARGET_X86_64 1

#define TCG_TARGET_REG_BITS 64
//#define TCG_TARGET_WORDS_BIGENDIAN

#define TCG_TARGET_NB_REGS 16

enum {
    TCG_REG_RAX = 0,
    TCG_REG_RCX,
    TCG_REG_RDX,
    TCG_REG_RBX,
    TCG_REG_RSP,
    TCG_REG_RBP,
    TCG_REG_RSI,
    TCG_REG_RDI,
    TCG_REG_R8,
    TCG_REG_R9,
    TCG_REG_R10,
    TCG_REG_R11,
    TCG_REG_R12,
    TCG_REG_R13,
    TCG_REG_R14,
    TCG_REG_R15,
};

#define TCG_CT_CONST_S32 0x100
#define TCG_CT_CONST_U32 0x200

/* used for function call generation */
#define TCG_REG_CALL_STACK TCG_REG_RSP 
#define TCG_TARGET_STACK_ALIGN 16
#define TCG_TARGET_CALL_STACK_OFFSET 0

/* optional instructions */
#define TCG_TARGET_HAS_bswap_i32
#define TCG_TARGET_HAS_bswap_i64
#define TCG_TARGET_HAS_neg_i32
#define TCG_TARGET_HAS_neg_i64
#define TCG_TARGET_HAS_ext8s_i32
#define TCG_TARGET_HAS_ext16s_i32
#define TCG_TARGET_HAS_ext8s_i64
#define TCG_TARGET_HAS_ext16s_i64
#define TCG_TARGET_HAS_ext32s_i64

/* Note: must be synced with dyngen-exec.h */
#define TCG_AREG0 TCG_REG_R14
#define TCG_AREG1 TCG_REG_R15
#define TCG_AREG2 TCG_REG_R12
#define TCG_AREG3 TCG_REG_R13

static inline void flush_icache_range(unsigned long start, unsigned long stop)
{
}

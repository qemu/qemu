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
#define TCG_TARGET_I386 1

#if defined(__x86_64__)
# define TCG_TARGET_REG_BITS 64
#else
# define TCG_TARGET_REG_BITS 32
#endif
//#define TCG_TARGET_WORDS_BIGENDIAN

#if TCG_TARGET_REG_BITS == 64
# define TCG_TARGET_NB_REGS 16
#else
# define TCG_TARGET_NB_REGS 8
#endif

enum {
    TCG_REG_EAX = 0,
    TCG_REG_ECX,
    TCG_REG_EDX,
    TCG_REG_EBX,
    TCG_REG_ESP,
    TCG_REG_EBP,
    TCG_REG_ESI,
    TCG_REG_EDI,

    /* 64-bit registers; always define the symbols to avoid
       too much if-deffing.  */
    TCG_REG_R8,
    TCG_REG_R9,
    TCG_REG_R10,
    TCG_REG_R11,
    TCG_REG_R12,
    TCG_REG_R13,
    TCG_REG_R14,
    TCG_REG_R15,
    TCG_REG_RAX = TCG_REG_EAX,
    TCG_REG_RCX = TCG_REG_ECX,
    TCG_REG_RDX = TCG_REG_EDX,
    TCG_REG_RBX = TCG_REG_EBX,
    TCG_REG_RSP = TCG_REG_ESP,
    TCG_REG_RBP = TCG_REG_EBP,
    TCG_REG_RSI = TCG_REG_ESI,
    TCG_REG_RDI = TCG_REG_EDI,
};

#define TCG_CT_CONST_S32 0x100
#define TCG_CT_CONST_U32 0x200

/* used for function call generation */
#define TCG_REG_CALL_STACK TCG_REG_ESP 
#define TCG_TARGET_STACK_ALIGN 16
#define TCG_TARGET_CALL_STACK_OFFSET 0

/* optional instructions */
#define TCG_TARGET_HAS_div2_i32         1
#define TCG_TARGET_HAS_rot_i32          1
#define TCG_TARGET_HAS_ext8s_i32        1
#define TCG_TARGET_HAS_ext16s_i32       1
#define TCG_TARGET_HAS_ext8u_i32        1
#define TCG_TARGET_HAS_ext16u_i32       1
#define TCG_TARGET_HAS_bswap16_i32      1
#define TCG_TARGET_HAS_bswap32_i32      1
#define TCG_TARGET_HAS_neg_i32          1
#define TCG_TARGET_HAS_not_i32          1
#define TCG_TARGET_HAS_andc_i32         0
#define TCG_TARGET_HAS_orc_i32          0
#define TCG_TARGET_HAS_eqv_i32          0
#define TCG_TARGET_HAS_nand_i32         0
#define TCG_TARGET_HAS_nor_i32          0
#define TCG_TARGET_HAS_deposit_i32      0

#if TCG_TARGET_REG_BITS == 64
#define TCG_TARGET_HAS_div2_i64         1
#define TCG_TARGET_HAS_rot_i64          1
#define TCG_TARGET_HAS_ext8s_i64        1
#define TCG_TARGET_HAS_ext16s_i64       1
#define TCG_TARGET_HAS_ext32s_i64       1
#define TCG_TARGET_HAS_ext8u_i64        1
#define TCG_TARGET_HAS_ext16u_i64       1
#define TCG_TARGET_HAS_ext32u_i64       1
#define TCG_TARGET_HAS_bswap16_i64      1
#define TCG_TARGET_HAS_bswap32_i64      1
#define TCG_TARGET_HAS_bswap64_i64      1
#define TCG_TARGET_HAS_neg_i64          1
#define TCG_TARGET_HAS_not_i64          1
#define TCG_TARGET_HAS_andc_i64         0
#define TCG_TARGET_HAS_orc_i64          0
#define TCG_TARGET_HAS_eqv_i64          0
#define TCG_TARGET_HAS_nand_i64         0
#define TCG_TARGET_HAS_nor_i64          0
#define TCG_TARGET_HAS_deposit_i64      0
#endif

#define TCG_TARGET_HAS_GUEST_BASE

/* Note: must be synced with dyngen-exec.h */
#if TCG_TARGET_REG_BITS == 64
# define TCG_AREG0 TCG_REG_R14
#else
# define TCG_AREG0 TCG_REG_EBP
#endif

static inline void flush_icache_range(unsigned long start, unsigned long stop)
{
}

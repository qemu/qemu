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

#ifndef TCG_TARGET_HPPA
#define TCG_TARGET_HPPA 1

#if TCG_TARGET_REG_BITS != 32
#error unsupported
#endif

#define TCG_TARGET_WORDS_BIGENDIAN

#define TCG_TARGET_NB_REGS 32

typedef enum {
    TCG_REG_R0 = 0,
    TCG_REG_R1,
    TCG_REG_RP,
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
    TCG_REG_R16,
    TCG_REG_R17,
    TCG_REG_R18,
    TCG_REG_R19,
    TCG_REG_R20,
    TCG_REG_R21,
    TCG_REG_R22,
    TCG_REG_R23,
    TCG_REG_R24,
    TCG_REG_R25,
    TCG_REG_R26,
    TCG_REG_DP,
    TCG_REG_RET0,
    TCG_REG_RET1,
    TCG_REG_SP,
    TCG_REG_R31,
} TCGReg;

#define TCG_CT_CONST_0    0x0100
#define TCG_CT_CONST_S5   0x0200
#define TCG_CT_CONST_S11  0x0400
#define TCG_CT_CONST_MS11 0x0800
#define TCG_CT_CONST_AND  0x1000
#define TCG_CT_CONST_OR   0x2000

/* used for function call generation */
#define TCG_REG_CALL_STACK TCG_REG_SP
#define TCG_TARGET_STACK_ALIGN 64
#define TCG_TARGET_CALL_STACK_OFFSET -48
#define TCG_TARGET_STATIC_CALL_ARGS_SIZE 8*4
#define TCG_TARGET_CALL_ALIGN_ARGS 1
#define TCG_TARGET_STACK_GROWSUP

/* optional instructions */
#define TCG_TARGET_HAS_div_i32          0
#define TCG_TARGET_HAS_rot_i32          1
#define TCG_TARGET_HAS_ext8s_i32        1
#define TCG_TARGET_HAS_ext16s_i32       1
#define TCG_TARGET_HAS_bswap16_i32      1
#define TCG_TARGET_HAS_bswap32_i32      1
#define TCG_TARGET_HAS_not_i32          1
#define TCG_TARGET_HAS_andc_i32         1
#define TCG_TARGET_HAS_orc_i32          0
#define TCG_TARGET_HAS_eqv_i32          0
#define TCG_TARGET_HAS_nand_i32         0
#define TCG_TARGET_HAS_nor_i32          0
#define TCG_TARGET_HAS_deposit_i32      1
#define TCG_TARGET_HAS_movcond_i32      1
#define TCG_TARGET_HAS_muls2_i32        0

/* optional instructions automatically implemented */
#define TCG_TARGET_HAS_neg_i32          0 /* sub rd, 0, rs */
#define TCG_TARGET_HAS_ext8u_i32        0 /* and rd, rs, 0xff */
#define TCG_TARGET_HAS_ext16u_i32       0 /* and rd, rs, 0xffff */

#define TCG_AREG0 TCG_REG_R17


static inline void flush_icache_range(tcg_target_ulong start,
                                      tcg_target_ulong stop)
{
    start &= ~31;
    while (start <= stop) {
        asm volatile ("fdc 0(%0)\n\t"
                      "sync\n\t"
                      "fic 0(%%sr4, %0)\n\t"
                      "sync"
                      : : "r"(start) : "memory");
        start += 32;
    }
}

#endif

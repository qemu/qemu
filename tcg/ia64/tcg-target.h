/*
 * Tiny Code Generator for QEMU
 *
 * Copyright (c) 2009-2010 Aurelien Jarno <aurelien@aurel32.net>
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
#ifndef TCG_TARGET_IA64 
#define TCG_TARGET_IA64 1

#define TCG_TARGET_INSN_UNIT_SIZE 16
typedef struct {
    uint64_t lo __attribute__((aligned(16)));
    uint64_t hi;
} tcg_insn_unit;

/* We only map the first 64 registers */
#define TCG_TARGET_NB_REGS 64
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
    TCG_REG_R27,
    TCG_REG_R28,
    TCG_REG_R29,
    TCG_REG_R30,
    TCG_REG_R31,
    TCG_REG_R32,
    TCG_REG_R33,
    TCG_REG_R34,
    TCG_REG_R35,
    TCG_REG_R36,
    TCG_REG_R37,
    TCG_REG_R38,
    TCG_REG_R39,
    TCG_REG_R40,
    TCG_REG_R41,
    TCG_REG_R42,
    TCG_REG_R43,
    TCG_REG_R44,
    TCG_REG_R45,
    TCG_REG_R46,
    TCG_REG_R47,
    TCG_REG_R48,
    TCG_REG_R49,
    TCG_REG_R50,
    TCG_REG_R51,
    TCG_REG_R52,
    TCG_REG_R53,
    TCG_REG_R54,
    TCG_REG_R55,
    TCG_REG_R56,
    TCG_REG_R57,
    TCG_REG_R58,
    TCG_REG_R59,
    TCG_REG_R60,
    TCG_REG_R61,
    TCG_REG_R62,
    TCG_REG_R63,

    TCG_AREG0 = TCG_REG_R32,
} TCGReg;

#define TCG_CT_CONST_ZERO 0x100
#define TCG_CT_CONST_S22 0x200

/* used for function call generation */
#define TCG_REG_CALL_STACK TCG_REG_R12
#define TCG_TARGET_STACK_ALIGN 16
#define TCG_TARGET_CALL_STACK_OFFSET 16

/* optional instructions */
#define TCG_TARGET_HAS_div_i32          0
#define TCG_TARGET_HAS_rem_i32          0
#define TCG_TARGET_HAS_div_i64          0
#define TCG_TARGET_HAS_rem_i64          0
#define TCG_TARGET_HAS_andc_i32         1
#define TCG_TARGET_HAS_andc_i64         1
#define TCG_TARGET_HAS_bswap16_i32      1
#define TCG_TARGET_HAS_bswap16_i64      1
#define TCG_TARGET_HAS_bswap32_i32      1
#define TCG_TARGET_HAS_bswap32_i64      1
#define TCG_TARGET_HAS_bswap64_i64      1
#define TCG_TARGET_HAS_eqv_i32          1
#define TCG_TARGET_HAS_eqv_i64          1
#define TCG_TARGET_HAS_ext8s_i32        1
#define TCG_TARGET_HAS_ext16s_i32       1
#define TCG_TARGET_HAS_ext8s_i64        1
#define TCG_TARGET_HAS_ext16s_i64       1
#define TCG_TARGET_HAS_ext32s_i64       1
#define TCG_TARGET_HAS_ext8u_i32        1
#define TCG_TARGET_HAS_ext16u_i32       1
#define TCG_TARGET_HAS_ext8u_i64        1
#define TCG_TARGET_HAS_ext16u_i64       1
#define TCG_TARGET_HAS_ext32u_i64       1
#define TCG_TARGET_HAS_nand_i32         1
#define TCG_TARGET_HAS_nand_i64         1
#define TCG_TARGET_HAS_nor_i32          1
#define TCG_TARGET_HAS_nor_i64          1
#define TCG_TARGET_HAS_orc_i32          1
#define TCG_TARGET_HAS_orc_i64          1
#define TCG_TARGET_HAS_rot_i32          1
#define TCG_TARGET_HAS_rot_i64          1
#define TCG_TARGET_HAS_movcond_i32      1
#define TCG_TARGET_HAS_movcond_i64      1
#define TCG_TARGET_HAS_deposit_i32      1
#define TCG_TARGET_HAS_deposit_i64      1
#define TCG_TARGET_HAS_add2_i32         0
#define TCG_TARGET_HAS_add2_i64         0
#define TCG_TARGET_HAS_sub2_i32         0
#define TCG_TARGET_HAS_sub2_i64         0
#define TCG_TARGET_HAS_mulu2_i32        0
#define TCG_TARGET_HAS_mulu2_i64        0
#define TCG_TARGET_HAS_muls2_i32        0
#define TCG_TARGET_HAS_muls2_i64        0
#define TCG_TARGET_HAS_muluh_i32        0
#define TCG_TARGET_HAS_muluh_i64        0
#define TCG_TARGET_HAS_mulsh_i32        0
#define TCG_TARGET_HAS_mulsh_i64        0
#define TCG_TARGET_HAS_trunc_shr_i32    0

#define TCG_TARGET_HAS_new_ldst         1

#define TCG_TARGET_deposit_i32_valid(ofs, len) ((len) <= 16)
#define TCG_TARGET_deposit_i64_valid(ofs, len) ((len) <= 16)

/* optional instructions automatically implemented */
#define TCG_TARGET_HAS_neg_i32          0 /* sub r1, r0, r3 */
#define TCG_TARGET_HAS_neg_i64          0 /* sub r1, r0, r3 */
#define TCG_TARGET_HAS_not_i32          0 /* xor r1, -1, r3 */
#define TCG_TARGET_HAS_not_i64          0 /* xor r1, -1, r3 */

static inline void flush_icache_range(uintptr_t start, uintptr_t stop)
{
    start = start & ~(32UL - 1UL);
    stop = (stop + (32UL - 1UL)) & ~(32UL - 1UL);

    for (; start < stop; start += 32UL) {
        asm volatile ("fc.i %0" :: "r" (start));
    }
    asm volatile (";;sync.i;;srlz.i;;");
}

#endif

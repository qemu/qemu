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
#define TCG_TARGET_ARM 1

#define TCG_TARGET_REG_BITS 32
#undef TCG_TARGET_WORDS_BIGENDIAN
#undef TCG_TARGET_STACK_GROWSUP

enum {
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
};

#define TCG_TARGET_NB_REGS 16

#define TCG_CT_CONST_ARM 0x100

/* used for function call generation */
#define TCG_REG_CALL_STACK		TCG_REG_R13
#define TCG_TARGET_STACK_ALIGN		8
#define TCG_TARGET_CALL_ALIGN_ARGS	1
#define TCG_TARGET_CALL_STACK_OFFSET	0

/* optional instructions */
#define TCG_TARGET_HAS_div_i32          0
#define TCG_TARGET_HAS_ext8s_i32        1
#define TCG_TARGET_HAS_ext16s_i32       1
#define TCG_TARGET_HAS_ext8u_i32        0 /* and r0, r1, #0xff */
#define TCG_TARGET_HAS_ext16u_i32       1
#define TCG_TARGET_HAS_bswap16_i32      1
#define TCG_TARGET_HAS_bswap32_i32      1
#define TCG_TARGET_HAS_not_i32          1
#define TCG_TARGET_HAS_neg_i32          1
#define TCG_TARGET_HAS_rot_i32          1
#define TCG_TARGET_HAS_andc_i32         1
#define TCG_TARGET_HAS_orc_i32          0
#define TCG_TARGET_HAS_eqv_i32          0
#define TCG_TARGET_HAS_nand_i32         0
#define TCG_TARGET_HAS_nor_i32          0
#define TCG_TARGET_HAS_deposit_i32      0

#define TCG_TARGET_HAS_GUEST_BASE

enum {
    /* Note: must be synced with dyngen-exec.h */
    TCG_AREG0 = TCG_REG_R7,
};

static inline void flush_icache_range(unsigned long start, unsigned long stop)
{
#if QEMU_GNUC_PREREQ(4, 1)
    __builtin___clear_cache((char *) start, (char *) stop);
#else
    register unsigned long _beg __asm ("a1") = start;
    register unsigned long _end __asm ("a2") = stop;
    register unsigned long _flg __asm ("a3") = 0;
    __asm __volatile__ ("swi 0x9f0002" : : "r" (_beg), "r" (_end), "r" (_flg));
#endif
}

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

#define TCG_TARGET_HPPA 1

#if defined(_PA_RISC1_1)
#define TCG_TARGET_REG_BITS 32
#else
#error unsupported
#endif

#define TCG_TARGET_WORDS_BIGENDIAN

#define TCG_TARGET_NB_REGS 32

enum {
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
};

/* used for function call generation */
#define TCG_REG_CALL_STACK TCG_REG_SP
#define TCG_TARGET_STACK_ALIGN 16
#define TCG_TARGET_STACK_GROWSUP

/* optional instructions */
//#define TCG_TARGET_HAS_ext8s_i32
//#define TCG_TARGET_HAS_ext16s_i32
//#define TCG_TARGET_HAS_bswap16_i32
//#define TCG_TARGET_HAS_bswap_i32

/* Note: must be synced with dyngen-exec.h */
#define TCG_AREG0 TCG_REG_R17
#define TCG_AREG1 TCG_REG_R14
#define TCG_AREG2 TCG_REG_R15

static inline void flush_icache_range(unsigned long start, unsigned long stop)
{
    start &= ~31;
    while (start <= stop)
    {
        asm volatile ("fdc 0(%0)\n"
                      "sync\n"
                      "fic 0(%%sr4, %0)\n"
                      "sync\n"
                      : : "r"(start) : "memory");
        start += 32;
    }
}

/* supplied by libgcc */
extern void *__canonicalize_funcptr_for_compare(void *);

/* Field selection types defined by hppa */
#define rnd(x)                  (((x)+0x1000)&~0x1fff)
/* lsel: select left 21 bits */
#define lsel(v,a)               (((v)+(a))>>11)
/* rsel: select right 11 bits */
#define rsel(v,a)               (((v)+(a))&0x7ff)
/* lrsel with rounding of addend to nearest 8k */
#define lrsel(v,a)              (((v)+rnd(a))>>11)
/* rrsel with rounding of addend to nearest 8k */
#define rrsel(v,a)              ((((v)+rnd(a))&0x7ff)+((a)-rnd(a)))

#define mask(x,sz)              ((x) & ~((1<<(sz))-1))

static inline int reassemble_12(int as12)
{
    return (((as12 & 0x800) >> 11) |
            ((as12 & 0x400) >> 8) |
            ((as12 & 0x3ff) << 3));
}

static inline int reassemble_14(int as14)
{
    return (((as14 & 0x1fff) << 1) |
            ((as14 & 0x2000) >> 13));
}

static inline int reassemble_17(int as17)
{
    return (((as17 & 0x10000) >> 16) |
            ((as17 & 0x0f800) << 5) |
            ((as17 & 0x00400) >> 8) |
            ((as17 & 0x003ff) << 3));
}

static inline int reassemble_21(int as21)
{
    return (((as21 & 0x100000) >> 20) |
            ((as21 & 0x0ffe00) >> 8) |
            ((as21 & 0x000180) << 7) |
            ((as21 & 0x00007c) << 14) |
            ((as21 & 0x000003) << 12));
}

static inline void hppa_patch21l(uint32_t *insn, int val, int addend)
{
    val = lrsel(val, addend);
    *insn = mask(*insn, 21) | reassemble_21(val);
}

static inline void hppa_patch14r(uint32_t *insn, int val, int addend)
{
    val = rrsel(val, addend);
    *insn = mask(*insn, 14) | reassemble_14(val);
}

static inline void hppa_patch17r(uint32_t *insn, int val, int addend)
{
    val = rrsel(val, addend);
    *insn = (*insn & ~0x1f1ffd) | reassemble_17(val);
}


static inline void hppa_patch21l_dprel(uint32_t *insn, int val, int addend)
{
    register unsigned int dp asm("r27");
    hppa_patch21l(insn, val - dp, addend);
}

static inline void hppa_patch14r_dprel(uint32_t *insn, int val, int addend)
{
    register unsigned int dp asm("r27");
    hppa_patch14r(insn, val - dp, addend);
}

static inline void hppa_patch17f(uint32_t *insn, int val, int addend)
{
    int dot = (int)insn & ~0x3;
    int v = ((val + addend) - dot - 8) / 4;
    if (v > (1 << 16) || v < -(1 << 16)) {
        printf("cannot fit branch to offset %d [%08x->%08x]\n", v, dot, val);
        abort();
    }
    *insn = (*insn & ~0x1f1ffd) | reassemble_17(v);
}

static inline void hppa_load_imm21l(uint32_t *insn, int val, int addend)
{
    /* Transform addil L'sym(%dp) to ldil L'val, %r1 */
    *insn = 0x20200000 | reassemble_21(lrsel(val, 0));
}

static inline void hppa_load_imm14r(uint32_t *insn, int val, int addend)
{
    /* Transform ldw R'sym(%r1), %rN to ldo R'sym(%r1), %rN */
    hppa_patch14r(insn, val, addend);
    /* HACK */
    if (addend == 0)
        *insn = (*insn & ~0xfc000000) | (0x0d << 26);
}

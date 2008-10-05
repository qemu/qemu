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

#ifndef NDEBUG
static const char * const tcg_target_reg_names[TCG_TARGET_NB_REGS] = {
    "%r0",
    "%r1",
    "%rp",
    "%r3",
    "%r4",
    "%r5",
    "%r6",
    "%r7",
    "%r8",
    "%r9",
    "%r10",
    "%r11",
    "%r12",
    "%r13",
    "%r14",
    "%r15",
    "%r16",
    "%r17",
    "%r18",
    "%r19",
    "%r20",
    "%r21",
    "%r22",
    "%r23",
    "%r24",
    "%r25",
    "%r26",
    "%dp",
    "%ret0",
    "%ret1",
    "%sp",
    "%r31",
};
#endif

static const int tcg_target_reg_alloc_order[] = {
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

    TCG_REG_R17,
    TCG_REG_R14,
    TCG_REG_R15,
    TCG_REG_R16,
};

static const int tcg_target_call_iarg_regs[4] = {
    TCG_REG_R26,
    TCG_REG_R25,
    TCG_REG_R24,
    TCG_REG_R23,
};

static const int tcg_target_call_oarg_regs[2] = {
    TCG_REG_RET0,
    TCG_REG_RET1,
};

static void patch_reloc(uint8_t *code_ptr, int type,
                        tcg_target_long value, tcg_target_long addend)
{
    switch (type) {
    case R_PARISC_PCREL17F:
        hppa_patch17f((uint32_t *)code_ptr, value, addend);
        break;
    default:
        tcg_abort();
    }
}

/* maximum number of register used for input function arguments */
static inline int tcg_target_get_call_iarg_regs_count(int flags)
{
    return 4;
}

/* parse target specific constraints */
static int target_parse_constraint(TCGArgConstraint *ct, const char **pct_str)
{
    const char *ct_str;

    ct_str = *pct_str;
    switch (ct_str[0]) {
    case 'r':
        ct->ct |= TCG_CT_REG;
        tcg_regset_set32(ct->u.regs, 0, 0xffffffff);
        break;
    case 'L': /* qemu_ld/st constraint */
        ct->ct |= TCG_CT_REG;
        tcg_regset_set32(ct->u.regs, 0, 0xffffffff);
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_R26);
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_R25);
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_R24);
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_R23);
        break;
    default:
        return -1;
    }
    ct_str++;
    *pct_str = ct_str;
    return 0;
}

/* test if a constant matches the constraint */
static inline int tcg_target_const_match(tcg_target_long val,
                                         const TCGArgConstraint *arg_ct)
{
    int ct;

    ct = arg_ct->ct;

    /* TODO */

    return 0;
}

#define INSN_OP(x)       ((x) << 26)
#define INSN_EXT3BR(x)   ((x) << 13)
#define INSN_EXT3SH(x)   ((x) << 10)
#define INSN_EXT4(x)     ((x) << 6)
#define INSN_EXT5(x)     (x)
#define INSN_EXT6(x)     ((x) << 6)
#define INSN_EXT7(x)     ((x) << 6)
#define INSN_EXT8A(x)    ((x) << 6)
#define INSN_EXT8B(x)    ((x) << 5)
#define INSN_T(x)        (x)
#define INSN_R1(x)       ((x) << 16)
#define INSN_R2(x)       ((x) << 21)
#define INSN_DEP_LEN(x)  (32 - (x))
#define INSN_SHDEP_CP(x) ((31 - (x)) << 5)
#define INSN_SHDEP_P(x)  ((x) << 5)
#define INSN_COND(x)     ((x) << 13)

#define COND_NEVER 0
#define COND_EQUAL 1
#define COND_LT    2
#define COND_LTEQ  3
#define COND_LTU   4
#define COND_LTUEQ 5
#define COND_SV    6
#define COND_OD    7


/* Logical ADD */
#define ARITH_ADD  (INSN_OP(0x02) | INSN_EXT6(0x28))
#define ARITH_AND  (INSN_OP(0x02) | INSN_EXT6(0x08))
#define ARITH_OR   (INSN_OP(0x02) | INSN_EXT6(0x09))
#define ARITH_XOR  (INSN_OP(0x02) | INSN_EXT6(0x0a))
#define ARITH_SUB  (INSN_OP(0x02) | INSN_EXT6(0x10))

#define SHD        (INSN_OP(0x34) | INSN_EXT3SH(2))
#define VSHD       (INSN_OP(0x34) | INSN_EXT3SH(0))
#define DEP        (INSN_OP(0x35) | INSN_EXT3SH(3))
#define ZDEP       (INSN_OP(0x35) | INSN_EXT3SH(2))
#define ZVDEP      (INSN_OP(0x35) | INSN_EXT3SH(0))
#define EXTRU      (INSN_OP(0x34) | INSN_EXT3SH(6))
#define EXTRS      (INSN_OP(0x34) | INSN_EXT3SH(7))
#define VEXTRS     (INSN_OP(0x34) | INSN_EXT3SH(5))

#define SUBI       (INSN_OP(0x25))
#define MTCTL      (INSN_OP(0x00) | INSN_EXT8B(0xc2))

#define BL         (INSN_OP(0x3a) | INSN_EXT3BR(0))
#define BLE_SR4    (INSN_OP(0x39) | (1 << 13))
#define BV         (INSN_OP(0x3a) | INSN_EXT3BR(6))
#define BV_N       (INSN_OP(0x3a) | INSN_EXT3BR(6) | 2)
#define LDIL       (INSN_OP(0x08))
#define LDO        (INSN_OP(0x0d))

#define LDB        (INSN_OP(0x10))
#define LDH        (INSN_OP(0x11))
#define LDW        (INSN_OP(0x12))
#define LDWM       (INSN_OP(0x13))

#define STB        (INSN_OP(0x18))
#define STH        (INSN_OP(0x19))
#define STW        (INSN_OP(0x1a))
#define STWM       (INSN_OP(0x1b))

#define COMBT      (INSN_OP(0x20))
#define COMBF      (INSN_OP(0x22))

static int lowsignext(uint32_t val, int start, int length)
{
    return (((val << 1) & ~(~0 << length)) |
            ((val >> (length - 1)) & 1)) << start;
}

static inline void tcg_out_mov(TCGContext *s, int ret, int arg)
{
    /* PA1.1 defines COPY as OR r,0,t */
    tcg_out32(s, ARITH_OR | INSN_T(ret) | INSN_R1(arg) | INSN_R2(TCG_REG_R0));

    /* PA2.0 defines COPY as LDO 0(r),t
     * but hppa-dis.c is unaware of this definition */
    /* tcg_out32(s, LDO | INSN_R1(ret) | INSN_R2(arg) | reassemble_14(0)); */
}

static inline void tcg_out_movi(TCGContext *s, TCGType type,
                                int ret, tcg_target_long arg)
{
    if (arg == (arg & 0x1fff)) {
        tcg_out32(s, LDO | INSN_R1(ret) | INSN_R2(TCG_REG_R0) |
                     reassemble_14(arg));
    } else {
        tcg_out32(s, LDIL | INSN_R2(ret) |
                     reassemble_21(lrsel((uint32_t)arg, 0)));
        if (arg & 0x7ff)
            tcg_out32(s, LDO | INSN_R1(ret) | INSN_R2(ret) |
                         reassemble_14(rrsel((uint32_t)arg, 0)));
    }
}

static inline void tcg_out_ld_raw(TCGContext *s, int ret,
                                  tcg_target_long arg)
{
    tcg_out32(s, LDIL | INSN_R2(ret) |
                 reassemble_21(lrsel((uint32_t)arg, 0)));
    tcg_out32(s, LDW | INSN_R1(ret) | INSN_R2(ret) |
                 reassemble_14(rrsel((uint32_t)arg, 0)));
}

static inline void tcg_out_ld_ptr(TCGContext *s, int ret,
                                  tcg_target_long arg)
{
    tcg_out_ld_raw(s, ret, arg);
}

static inline void tcg_out_ldst(TCGContext *s, int ret, int addr, int offset,
                                int op)
{
    if (offset == (offset & 0xfff))
        tcg_out32(s, op | INSN_R1(ret) | INSN_R2(addr) |
                 reassemble_14(offset));
    else {
        fprintf(stderr, "unimplemented %s with offset %d\n", __func__, offset);
        tcg_abort();
    }
}

static inline void tcg_out_ld(TCGContext *s, TCGType type, int ret,
                              int arg1, tcg_target_long arg2)
{
    fprintf(stderr, "unimplemented %s\n", __func__);
    tcg_abort();
}

static inline void tcg_out_st(TCGContext *s, TCGType type, int ret,
                              int arg1, tcg_target_long arg2)
{
    fprintf(stderr, "unimplemented %s\n", __func__);
    tcg_abort();
}

static inline void tcg_out_arith(TCGContext *s, int t, int r1, int r2, int op)
{
    tcg_out32(s, op | INSN_T(t) | INSN_R1(r1) | INSN_R2(r2));
}

static inline void tcg_out_arithi(TCGContext *s, int t, int r1,
                                  tcg_target_long val, int op)
{
    tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_R20, val);
    tcg_out_arith(s, t, r1, TCG_REG_R20, op);
}

static inline void tcg_out_addi(TCGContext *s, int reg, tcg_target_long val)
{
    tcg_out_arithi(s, reg, reg, val, ARITH_ADD);
}

static inline void tcg_out_nop(TCGContext *s)
{
    tcg_out32(s, ARITH_OR | INSN_T(TCG_REG_R0) | INSN_R1(TCG_REG_R0) |
                 INSN_R2(TCG_REG_R0));
}

static inline void tcg_out_ext8s(TCGContext *s, int ret, int arg) {
    tcg_out32(s, EXTRS | INSN_R1(ret) | INSN_R2(arg) |
                 INSN_SHDEP_P(31) | INSN_DEP_LEN(8));
}

static inline void tcg_out_ext16s(TCGContext *s, int ret, int arg) {
    tcg_out32(s, EXTRS | INSN_R1(ret) | INSN_R2(arg) |
                 INSN_SHDEP_P(31) | INSN_DEP_LEN(16));
}

static inline void tcg_out_bswap16(TCGContext *s, int ret, int arg) {
    if(ret != arg)
        tcg_out_mov(s, ret, arg);
    tcg_out32(s, DEP | INSN_R2(ret) | INSN_R1(ret) |
                 INSN_SHDEP_CP(15) | INSN_DEP_LEN(8));
    tcg_out32(s, SHD | INSN_T(ret) | INSN_R1(TCG_REG_R0) |
                 INSN_R2(ret) | INSN_SHDEP_CP(8));
}

static inline void tcg_out_bswap32(TCGContext *s, int ret, int arg, int temp) {
    tcg_out32(s, SHD | INSN_T(temp) | INSN_R1(arg) |
                 INSN_R2(arg) | INSN_SHDEP_CP(16));
    tcg_out32(s, DEP | INSN_R2(temp) | INSN_R1(temp) |
                 INSN_SHDEP_CP(15) | INSN_DEP_LEN(8));
    tcg_out32(s, SHD | INSN_T(ret) | INSN_R1(arg) |
                 INSN_R2(temp) | INSN_SHDEP_CP(8));
}

static inline void tcg_out_call(TCGContext *s, void *func)
{
    uint32_t val = (uint32_t)__canonicalize_funcptr_for_compare(func);
    tcg_out32(s, LDIL | INSN_R2(TCG_REG_R20) |
                 reassemble_21(lrsel(val, 0)));
    tcg_out32(s, BLE_SR4 | INSN_R2(TCG_REG_R20) |
                 reassemble_17(rrsel(val, 0) >> 2));
    tcg_out_mov(s, TCG_REG_RP, TCG_REG_R31);
}

#if defined(CONFIG_SOFTMMU)

#include "../../softmmu_defs.h"

static void *qemu_ld_helpers[4] = {
    __ldb_mmu,
    __ldw_mmu,
    __ldl_mmu,
    __ldq_mmu,
};

static void *qemu_st_helpers[4] = {
    __stb_mmu,
    __stw_mmu,
    __stl_mmu,
    __stq_mmu,
};
#endif

static void tcg_out_qemu_ld(TCGContext *s, const TCGArg *args, int opc)
{
    int addr_reg, data_reg, data_reg2, r0, r1, mem_index, s_bits, bswap;
#if defined(CONFIG_SOFTMMU)
    uint32_t *label1_ptr, *label2_ptr;
#endif
#if TARGET_LONG_BITS == 64
#if defined(CONFIG_SOFTMMU)
    uint32_t *label3_ptr;
#endif
    int addr_reg2;
#endif

    data_reg = *args++;
    if (opc == 3)
        data_reg2 = *args++;
    else
        data_reg2 = 0; /* surpress warning */
    addr_reg = *args++;
#if TARGET_LONG_BITS == 64
    addr_reg2 = *args++;
#endif
    mem_index = *args;
    s_bits = opc & 3;

    r0 = TCG_REG_R26;
    r1 = TCG_REG_R25;

#if defined(CONFIG_SOFTMMU)
    tcg_out_mov(s, r1, addr_reg);

    tcg_out_mov(s, r0, addr_reg);

    tcg_out32(s, SHD | INSN_T(r1) | INSN_R1(TCG_REG_R0) | INSN_R2(r1) |
                 INSN_SHDEP_CP(TARGET_PAGE_BITS - CPU_TLB_ENTRY_BITS));

    tcg_out_arithi(s, r0, r0, TARGET_PAGE_MASK | ((1 << s_bits) - 1),
                   ARITH_AND);

    tcg_out_arithi(s, r1, r1, (CPU_TLB_SIZE - 1) << CPU_TLB_ENTRY_BITS,
                   ARITH_AND);

    tcg_out_arith(s, r1, r1, TCG_AREG0, ARITH_ADD);
    tcg_out_arithi(s, r1, r1,
                   offsetof(CPUState, tlb_table[mem_index][0].addr_read),
                   ARITH_ADD);

    tcg_out_ldst(s, TCG_REG_R20, r1, 0, LDW);

#if TARGET_LONG_BITS == 32
    /* if equal, jump to label1 */
    label1_ptr = (uint32_t *)s->code_ptr;
    tcg_out32(s, COMBT | INSN_R1(TCG_REG_R20) | INSN_R2(r0) |
                 INSN_COND(COND_EQUAL));
    tcg_out_mov(s, r0, addr_reg); /* delay slot */
#else
    /* if not equal, jump to label3 */
    label3_ptr = (uint32_t *)s->code_ptr;
    tcg_out32(s, COMBF | INSN_R1(TCG_REG_R20) | INSN_R2(r0) |
                 INSN_COND(COND_EQUAL));
    tcg_out_mov(s, r0, addr_reg); /* delay slot */

    tcg_out_ldst(s, TCG_REG_R20, r1, 4, LDW);

    /* if equal, jump to label1 */
    label1_ptr = (uint32_t *)s->code_ptr;
    tcg_out32(s, COMBT | INSN_R1(TCG_REG_R20) | INSN_R2(addr_reg2) |
                 INSN_COND(COND_EQUAL));
    tcg_out_nop(s); /* delay slot */

    /* label3: */
    *label3_ptr |= reassemble_12((uint32_t *)s->code_ptr - label3_ptr - 2);
#endif

#if TARGET_LONG_BITS == 32
    tcg_out_mov(s, TCG_REG_R26, addr_reg);
    tcg_out_movi(s, TCG_TYPE_I32, TCG_REG_R25, mem_index);
#else
    tcg_out_mov(s, TCG_REG_R26, addr_reg);
    tcg_out_mov(s, TCG_REG_R25, addr_reg2);
    tcg_out_movi(s, TCG_TYPE_I32, TCG_REG_R24, mem_index);
#endif

    tcg_out_call(s, qemu_ld_helpers[s_bits]);

    switch(opc) {
        case 0 | 4:
            tcg_out_ext8s(s, data_reg, TCG_REG_RET0);
            break;
        case 1 | 4:
            tcg_out_ext16s(s, data_reg, TCG_REG_RET0);
            break;
        case 0:
        case 1:
        case 2:
        default:
            tcg_out_mov(s, data_reg, TCG_REG_RET0);
            break;
        case 3:
            tcg_abort();
            tcg_out_mov(s, data_reg, TCG_REG_RET0);
            tcg_out_mov(s, data_reg2, TCG_REG_RET1);
            break;
    }

    /* jump to label2 */
    label2_ptr = (uint32_t *)s->code_ptr;
    tcg_out32(s, BL | INSN_R2(TCG_REG_R0) | 2);

    /* label1: */
    *label1_ptr |= reassemble_12((uint32_t *)s->code_ptr - label1_ptr - 2);

    tcg_out_arithi(s, TCG_REG_R20, r1,
                   offsetof(CPUTLBEntry, addend) - offsetof(CPUTLBEntry, addr_read),
                   ARITH_ADD);
    tcg_out_ldst(s, TCG_REG_R20, TCG_REG_R20, 0, LDW);
    tcg_out_arith(s, r0, r0, TCG_REG_R20, ARITH_ADD);
#else
    r0 = addr_reg;
#endif

#ifdef TARGET_WORDS_BIGENDIAN
    bswap = 0;
#else
    bswap = 1;
#endif
    switch (opc) {
        case 0:
            tcg_out_ldst(s, data_reg, r0, 0, LDB);
            break;
        case 0 | 4:
            tcg_out_ldst(s, data_reg, r0, 0, LDB);
            tcg_out_ext8s(s, data_reg, data_reg);
            break;
        case 1:
            tcg_out_ldst(s, data_reg, r0, 0, LDH);
            if (bswap)
                tcg_out_bswap16(s, data_reg, data_reg);
            break;
        case 1 | 4:
            tcg_out_ldst(s, data_reg, r0, 0, LDH);
            if (bswap)
                tcg_out_bswap16(s, data_reg, data_reg);
            tcg_out_ext16s(s, data_reg, data_reg);
            break;
        case 2:
            tcg_out_ldst(s, data_reg, r0, 0, LDW);
            if (bswap)
                tcg_out_bswap32(s, data_reg, data_reg, TCG_REG_R20);
            break;
        case 3:
            tcg_abort();
            if (!bswap) {
                tcg_out_ldst(s, data_reg, r0, 0, LDW);
                tcg_out_ldst(s, data_reg2, r0, 4, LDW);
            } else {
                tcg_out_ldst(s, data_reg, r0, 4, LDW);
                tcg_out_bswap32(s, data_reg, data_reg, TCG_REG_R20);
                tcg_out_ldst(s, data_reg2, r0, 0, LDW);
                tcg_out_bswap32(s, data_reg2, data_reg2, TCG_REG_R20);
            }
            break;
        default:
            tcg_abort();
    }

#if defined(CONFIG_SOFTMMU)
    /* label2: */
    *label2_ptr |= reassemble_17((uint32_t *)s->code_ptr - label2_ptr - 2);
#endif
}

static void tcg_out_qemu_st(TCGContext *s, const TCGArg *args, int opc)
{
    int addr_reg, data_reg, data_reg2, r0, r1, mem_index, s_bits, bswap;
#if defined(CONFIG_SOFTMMU)
    uint32_t *label1_ptr, *label2_ptr;
#endif
#if TARGET_LONG_BITS == 64
#if defined(CONFIG_SOFTMMU)
    uint32_t *label3_ptr;
#endif
    int addr_reg2;
#endif

    data_reg = *args++;
    if (opc == 3)
        data_reg2 = *args++;
    else
        data_reg2 = 0; /* surpress warning */
    addr_reg = *args++;
#if TARGET_LONG_BITS == 64
    addr_reg2 = *args++;
#endif
    mem_index = *args;

    s_bits = opc;

    r0 = TCG_REG_R26;
    r1 = TCG_REG_R25;

#if defined(CONFIG_SOFTMMU)
    tcg_out_mov(s, r1, addr_reg);

    tcg_out_mov(s, r0, addr_reg);

    tcg_out32(s, SHD | INSN_T(r1) | INSN_R1(TCG_REG_R0) | INSN_R2(r1) |
                 INSN_SHDEP_CP(TARGET_PAGE_BITS - CPU_TLB_ENTRY_BITS));

    tcg_out_arithi(s, r0, r0, TARGET_PAGE_MASK | ((1 << s_bits) - 1),
                   ARITH_AND);

    tcg_out_arithi(s, r1, r1, (CPU_TLB_SIZE - 1) << CPU_TLB_ENTRY_BITS,
                   ARITH_AND);

    tcg_out_arith(s, r1, r1, TCG_AREG0, ARITH_ADD);
    tcg_out_arithi(s, r1, r1,
                   offsetof(CPUState, tlb_table[mem_index][0].addr_write),
                   ARITH_ADD);

    tcg_out_ldst(s, TCG_REG_R20, r1, 0, LDW);

#if TARGET_LONG_BITS == 32
    /* if equal, jump to label1 */
    label1_ptr = (uint32_t *)s->code_ptr;
    tcg_out32(s, COMBT | INSN_R1(TCG_REG_R20) | INSN_R2(r0) |
                 INSN_COND(COND_EQUAL));
    tcg_out_mov(s, r0, addr_reg); /* delay slot */
#else
    /* if not equal, jump to label3 */
    label3_ptr = (uint32_t *)s->code_ptr;
    tcg_out32(s, COMBF | INSN_R1(TCG_REG_R20) | INSN_R2(r0) |
                 INSN_COND(COND_EQUAL));
    tcg_out_mov(s, r0, addr_reg); /* delay slot */

    tcg_out_ldst(s, TCG_REG_R20, r1, 4, LDW);

    /* if equal, jump to label1 */
    label1_ptr = (uint32_t *)s->code_ptr;
    tcg_out32(s, COMBT | INSN_R1(TCG_REG_R20) | INSN_R2(addr_reg2) |
                 INSN_COND(COND_EQUAL));
    tcg_out_nop(s); /* delay slot */

    /* label3: */
    *label3_ptr |= reassemble_12((uint32_t *)s->code_ptr - label3_ptr - 2);
#endif

    tcg_out_mov(s, TCG_REG_R26, addr_reg);
#if TARGET_LONG_BITS == 64
    tcg_out_mov(s, TCG_REG_R25, addr_reg2);
    if (opc == 3) {
        tcg_abort();
        tcg_out_mov(s, TCG_REG_R24, data_reg);
        tcg_out_mov(s, TCG_REG_R23, data_reg2);
        /* TODO: push mem_index */
        tcg_abort();
    } else {
        switch(opc) {
        case 0:
            tcg_out32(s, EXTRU | INSN_R1(TCG_REG_R24) | INSN_R2(data_reg) |
                         INSN_SHDEP_P(31) | INSN_DEP_LEN(8));
            break;
        case 1:
            tcg_out32(s, EXTRU | INSN_R1(TCG_REG_R24) | INSN_R2(data_reg) |
                         INSN_SHDEP_P(31) | INSN_DEP_LEN(16));
            break;
        case 2:
            tcg_out_mov(s, TCG_REG_R24, data_reg);
            break;
        }
        tcg_out_movi(s, TCG_TYPE_I32, TCG_REG_R23, mem_index);
    }
#else
    if (opc == 3) {
        tcg_abort();
        tcg_out_mov(s, TCG_REG_R25, data_reg);
        tcg_out_mov(s, TCG_REG_R24, data_reg2);
        tcg_out_movi(s, TCG_TYPE_I32, TCG_REG_R23, mem_index);
    } else {
        switch(opc) {
        case 0:
            tcg_out32(s, EXTRU | INSN_R1(TCG_REG_R25) | INSN_R2(data_reg) |
                         INSN_SHDEP_P(31) | INSN_DEP_LEN(8));
            break;
        case 1:
            tcg_out32(s, EXTRU | INSN_R1(TCG_REG_R25) | INSN_R2(data_reg) |
                         INSN_SHDEP_P(31) | INSN_DEP_LEN(16));
            break;
        case 2:
            tcg_out_mov(s, TCG_REG_R25, data_reg);
            break;
        }
        tcg_out_movi(s, TCG_TYPE_I32, TCG_REG_R24, mem_index);
    }
#endif
    tcg_out_call(s, qemu_st_helpers[s_bits]);

    /* jump to label2 */
    label2_ptr = (uint32_t *)s->code_ptr;
    tcg_out32(s, BL | INSN_R2(TCG_REG_R0) | 2);

    /* label1: */
    *label1_ptr |= reassemble_12((uint32_t *)s->code_ptr - label1_ptr - 2);

    tcg_out_arithi(s, TCG_REG_R20, r1,
                   offsetof(CPUTLBEntry, addend) - offsetof(CPUTLBEntry, addr_write),
                   ARITH_ADD);
    tcg_out_ldst(s, TCG_REG_R20, TCG_REG_R20, 0, LDW);
    tcg_out_arith(s, r0, r0, TCG_REG_R20, ARITH_ADD);
#else
    r0 = addr_reg;
#endif

#ifdef TARGET_WORDS_BIGENDIAN
    bswap = 0;
#else
    bswap = 1;
#endif
    switch (opc) {
    case 0:
        tcg_out_ldst(s, data_reg, r0, 0, STB);
        break;
    case 1:
        if (bswap) {
            tcg_out_bswap16(s, TCG_REG_R20, data_reg);
            data_reg = TCG_REG_R20;
        }
        tcg_out_ldst(s, data_reg, r0, 0, STH);
        break;
    case 2:
        if (bswap) {
            tcg_out_bswap32(s, TCG_REG_R20, data_reg, TCG_REG_R20);
            data_reg = TCG_REG_R20;
        }
        tcg_out_ldst(s, data_reg, r0, 0, STW);
        break;
    case 3:
        tcg_abort();
        if (!bswap) {
            tcg_out_ldst(s, data_reg, r0, 0, STW);
            tcg_out_ldst(s, data_reg2, r0, 4, STW);
        } else {
            tcg_out_bswap32(s, TCG_REG_R20, data_reg, TCG_REG_R20);
            tcg_out_ldst(s, TCG_REG_R20, r0, 4, STW);
            tcg_out_bswap32(s, TCG_REG_R20, data_reg2, TCG_REG_R20);
            tcg_out_ldst(s, TCG_REG_R20, r0, 0, STW);
        }
        break;
    default:
        tcg_abort();
    }

#if defined(CONFIG_SOFTMMU)
    /* label2: */
    *label2_ptr |= reassemble_17((uint32_t *)s->code_ptr - label2_ptr - 2);
#endif
}

static inline void tcg_out_op(TCGContext *s, int opc, const TCGArg *args,
                              const int *const_args)
{
    int c;

    switch (opc) {
    case INDEX_op_exit_tb:
        tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_RET0, args[0]);
        tcg_out32(s, BV_N | INSN_R2(TCG_REG_R18));
        break;
    case INDEX_op_goto_tb:
        if (s->tb_jmp_offset) {
            /* direct jump method */
            fprintf(stderr, "goto_tb direct\n");
            tcg_abort();
            tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_R20, args[0]);
            tcg_out32(s, BV_N | INSN_R2(TCG_REG_R20));
            s->tb_jmp_offset[args[0]] = s->code_ptr - s->code_buf;
        } else {
            /* indirect jump method */
            tcg_out_ld_ptr(s, TCG_REG_R20,
                           (tcg_target_long)(s->tb_next + args[0]));
            tcg_out32(s, BV_N | INSN_R2(TCG_REG_R20));
        }
        s->tb_next_offset[args[0]] = s->code_ptr - s->code_buf;
        break;
    case INDEX_op_call:
        tcg_out32(s, BLE_SR4 | INSN_R2(args[0]));
        tcg_out_mov(s, TCG_REG_RP, TCG_REG_R31);
        break;
    case INDEX_op_jmp:
        fprintf(stderr, "unimplemented jmp\n");
        tcg_abort();
        break;
    case INDEX_op_br:
        fprintf(stderr, "unimplemented br\n");
        tcg_abort();
        break;
    case INDEX_op_movi_i32:
        tcg_out_movi(s, TCG_TYPE_I32, args[0], (uint32_t)args[1]);
        break;

    case INDEX_op_ld8u_i32:
        tcg_out_ldst(s, args[0], args[1], args[2], LDB);
        break;
    case INDEX_op_ld8s_i32:
        tcg_out_ldst(s, args[0], args[1], args[2], LDB);
        tcg_out_ext8s(s, args[0], args[0]);
        break;
    case INDEX_op_ld16u_i32:
        tcg_out_ldst(s, args[0], args[1], args[2], LDH);
        break;
    case INDEX_op_ld16s_i32:
        tcg_out_ldst(s, args[0], args[1], args[2], LDH);
        tcg_out_ext16s(s, args[0], args[0]);
        break;
    case INDEX_op_ld_i32:
        tcg_out_ldst(s, args[0], args[1], args[2], LDW);
        break;

    case INDEX_op_st8_i32:
        tcg_out_ldst(s, args[0], args[1], args[2], STB);
        break;
    case INDEX_op_st16_i32:
        tcg_out_ldst(s, args[0], args[1], args[2], STH);
        break;
    case INDEX_op_st_i32:
        tcg_out_ldst(s, args[0], args[1], args[2], STW);
        break;

    case INDEX_op_sub_i32:
        c = ARITH_SUB;
        goto gen_arith;
    case INDEX_op_and_i32:
        c = ARITH_AND;
        goto gen_arith;
    case INDEX_op_or_i32:
        c = ARITH_OR;
        goto gen_arith;
    case INDEX_op_xor_i32:
        c = ARITH_XOR;
        goto gen_arith;
    case INDEX_op_add_i32:
        c = ARITH_ADD;
        goto gen_arith;

    case INDEX_op_shl_i32:
        tcg_out32(s, SUBI | INSN_R1(TCG_REG_R20) | INSN_R2(args[2]) |
                     lowsignext(0x1f, 0, 11));
        tcg_out32(s, MTCTL | INSN_R2(11) | INSN_R1(TCG_REG_R20));
        tcg_out32(s, ZVDEP | INSN_R2(args[0]) | INSN_R1(args[1]) |
                     INSN_DEP_LEN(32));
        break;
    case INDEX_op_shr_i32:
        tcg_out32(s, MTCTL | INSN_R2(11) | INSN_R1(args[2]));
        tcg_out32(s, VSHD | INSN_T(args[0]) | INSN_R1(TCG_REG_R0) |
                     INSN_R2(args[1]));
        break;
    case INDEX_op_sar_i32:
        tcg_out32(s, SUBI | INSN_R1(TCG_REG_R20) | INSN_R2(args[2]) |
                     lowsignext(0x1f, 0, 11));
        tcg_out32(s, MTCTL | INSN_R2(11) | INSN_R1(TCG_REG_R20));
        tcg_out32(s, VEXTRS | INSN_R1(args[0]) | INSN_R2(args[1]) |
                     INSN_DEP_LEN(32));
        break;

    case INDEX_op_mul_i32:
        fprintf(stderr, "unimplemented mul\n");
        tcg_abort();
        break;
    case INDEX_op_mulu2_i32:
        fprintf(stderr, "unimplemented mulu2\n");
        tcg_abort();
        break;
    case INDEX_op_div2_i32:
        fprintf(stderr, "unimplemented div2\n");
        tcg_abort();
        break;
    case INDEX_op_divu2_i32:
        fprintf(stderr, "unimplemented divu2\n");
        tcg_abort();
        break;

    case INDEX_op_brcond_i32:
        fprintf(stderr, "unimplemented brcond\n");
        tcg_abort();
        break;

    case INDEX_op_qemu_ld8u:
        tcg_out_qemu_ld(s, args, 0);
        break;
    case INDEX_op_qemu_ld8s:
        tcg_out_qemu_ld(s, args, 0 | 4);
        break;
    case INDEX_op_qemu_ld16u:
        tcg_out_qemu_ld(s, args, 1);
        break;
    case INDEX_op_qemu_ld16s:
        tcg_out_qemu_ld(s, args, 1 | 4);
        break;
    case INDEX_op_qemu_ld32u:
        tcg_out_qemu_ld(s, args, 2);
        break;

    case INDEX_op_qemu_st8:
        tcg_out_qemu_st(s, args, 0);
        break;
    case INDEX_op_qemu_st16:
        tcg_out_qemu_st(s, args, 1);
        break;
    case INDEX_op_qemu_st32:
        tcg_out_qemu_st(s, args, 2);
        break;

    default:
        fprintf(stderr, "unknown opcode 0x%x\n", opc);
        tcg_abort();
    }
    return;

gen_arith:
    tcg_out_arith(s, args[0], args[1], args[2], c);
}

static const TCGTargetOpDef hppa_op_defs[] = {
    { INDEX_op_exit_tb, { } },
    { INDEX_op_goto_tb, { } },

    { INDEX_op_call, { "r" } },
    { INDEX_op_jmp, { "r" } },
    { INDEX_op_br, { } },

    { INDEX_op_mov_i32, { "r", "r" } },
    { INDEX_op_movi_i32, { "r" } },
    { INDEX_op_ld8u_i32, { "r", "r" } },
    { INDEX_op_ld8s_i32, { "r", "r" } },
    { INDEX_op_ld16u_i32, { "r", "r" } },
    { INDEX_op_ld16s_i32, { "r", "r" } },
    { INDEX_op_ld_i32, { "r", "r" } },
    { INDEX_op_st8_i32, { "r", "r" } },
    { INDEX_op_st16_i32, { "r", "r" } },
    { INDEX_op_st_i32, { "r", "r" } },

    { INDEX_op_add_i32, { "r", "r", "r" } },
    { INDEX_op_sub_i32, { "r", "r", "r" } },
    { INDEX_op_and_i32, { "r", "r", "r" } },
    { INDEX_op_or_i32, { "r", "r", "r" } },
    { INDEX_op_xor_i32, { "r", "r", "r" } },

    { INDEX_op_shl_i32, { "r", "r", "r" } },
    { INDEX_op_shr_i32, { "r", "r", "r" } },
    { INDEX_op_sar_i32, { "r", "r", "r" } },

    { INDEX_op_brcond_i32, { "r", "r" } },

#if TARGET_LONG_BITS == 32
    { INDEX_op_qemu_ld8u, { "r", "L" } },
    { INDEX_op_qemu_ld8s, { "r", "L" } },
    { INDEX_op_qemu_ld16u, { "r", "L" } },
    { INDEX_op_qemu_ld16s, { "r", "L" } },
    { INDEX_op_qemu_ld32u, { "r", "L" } },
    { INDEX_op_qemu_ld64, { "r", "r", "L" } },

    { INDEX_op_qemu_st8, { "L", "L" } },
    { INDEX_op_qemu_st16, { "L", "L" } },
    { INDEX_op_qemu_st32, { "L", "L" } },
    { INDEX_op_qemu_st64, { "L", "L", "L" } },
#else
    { INDEX_op_qemu_ld8u, { "r", "L", "L" } },
    { INDEX_op_qemu_ld8s, { "r", "L", "L" } },
    { INDEX_op_qemu_ld16u, { "r", "L", "L" } },
    { INDEX_op_qemu_ld16s, { "r", "L", "L" } },
    { INDEX_op_qemu_ld32u, { "r", "L", "L" } },
    { INDEX_op_qemu_ld32s, { "r", "L", "L" } },
    { INDEX_op_qemu_ld64, { "r", "r", "L", "L" } },

    { INDEX_op_qemu_st8, { "L", "L", "L" } },
    { INDEX_op_qemu_st16, { "L", "L", "L" } },
    { INDEX_op_qemu_st32, { "L", "L", "L" } },
    { INDEX_op_qemu_st64, { "L", "L", "L", "L" } },
#endif
    { -1 },
};

void tcg_target_init(TCGContext *s)
{
    tcg_regset_set32(tcg_target_available_regs[TCG_TYPE_I32], 0, 0xffffffff);
    tcg_regset_set32(tcg_target_call_clobber_regs, 0,
                     (1 << TCG_REG_R20) |
                     (1 << TCG_REG_R21) |
                     (1 << TCG_REG_R22) |
                     (1 << TCG_REG_R23) |
                     (1 << TCG_REG_R24) |
                     (1 << TCG_REG_R25) |
                     (1 << TCG_REG_R26));

    tcg_regset_clear(s->reserved_regs);
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_R0);  /* hardwired to zero */
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_R1);  /* addil target */
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_RP);  /* link register */
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_R3);  /* frame pointer */
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_R18); /* return pointer */
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_R19); /* clobbered w/o pic */
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_R20); /* reserved */
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_DP);  /* data pointer */
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_SP);  /* stack pointer */
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_R31); /* ble link reg */

    tcg_add_target_add_op_defs(hppa_op_defs);
}

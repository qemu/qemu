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

static const char * const tcg_target_reg_names[TCG_TARGET_NB_REGS] = {
    "%g0",
    "%g1",
    "%g2",
    "%g3",
    "%g4",
    "%g5",
    "%g6",
    "%g7",
    "%o0",
    "%o1",
    "%o2",
    "%o3",
    "%o4",
    "%o5",
    "%o6",
    "%o7",
    "%l0",
    "%l1",
    "%l2",
    "%l3",
    "%l4",
    "%l5",
    "%l6",
    "%l7",
    "%i0",
    "%i1",
    "%i2",
    "%i3",
    "%i4",
    "%i5",
    "%i6",
    "%i7",
};

static const int tcg_target_reg_alloc_order[] = {
    TCG_REG_L0,
    TCG_REG_L1,
    TCG_REG_L2,
    TCG_REG_L3,
    TCG_REG_L4,
    TCG_REG_L5,
    TCG_REG_L6,
    TCG_REG_L7,
    TCG_REG_I0,
    TCG_REG_I1,
    TCG_REG_I2,
    TCG_REG_I3,
    TCG_REG_I4,
};

static const int tcg_target_call_iarg_regs[6] = {
    TCG_REG_O0,
    TCG_REG_O1,
    TCG_REG_O2,
    TCG_REG_O3,
    TCG_REG_O4,
    TCG_REG_O5,
};

static const int tcg_target_call_oarg_regs[2] = {
    TCG_REG_O0,
    TCG_REG_O1,
};

static inline int check_fit_tl(tcg_target_long val, unsigned int bits)
{
    return (val << ((sizeof(tcg_target_long) * 8 - bits))
            >> (sizeof(tcg_target_long) * 8 - bits)) == val;
}

static inline int check_fit_i32(uint32_t val, unsigned int bits)
{
    return ((val << (32 - bits)) >> (32 - bits)) == val;
}

static void patch_reloc(uint8_t *code_ptr, int type,
                        tcg_target_long value, tcg_target_long addend)
{
    value += addend;
    switch (type) {
    case R_SPARC_32:
        if (value != (uint32_t)value)
            tcg_abort();
        *(uint32_t *)code_ptr = value;
        break;
    case R_SPARC_WDISP22:
        value -= (long)code_ptr;
        value >>= 2;
        if (!check_fit_tl(value, 22))
            tcg_abort();
        *(uint32_t *)code_ptr = ((*(uint32_t *)code_ptr) & ~0x3fffff) | value;
        break;
    default:
        tcg_abort();
    }
}

/* maximum number of register used for input function arguments */
static inline int tcg_target_get_call_iarg_regs_count(int flags)
{
    return 6;
}

/* parse target specific constraints */
static int target_parse_constraint(TCGArgConstraint *ct, const char **pct_str)
{
    const char *ct_str;

    ct_str = *pct_str;
    switch (ct_str[0]) {
    case 'r':
    case 'L': /* qemu_ld/st constraint */
        ct->ct |= TCG_CT_REG;
        tcg_regset_set32(ct->u.regs, 0, 0xffffffff);
        // Helper args
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_O0);
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_O1);
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_O2);
        break;
    case 'I':
        ct->ct |= TCG_CT_CONST_S11;
        break;
    case 'J':
        ct->ct |= TCG_CT_CONST_S13;
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
    if (ct & TCG_CT_CONST)
        return 1;
    else if ((ct & TCG_CT_CONST_S11) && check_fit_tl(val, 11))
        return 1;
    else if ((ct & TCG_CT_CONST_S13) && check_fit_tl(val, 13))
        return 1;
    else
        return 0;
}

#define INSN_OP(x)  ((x) << 30)
#define INSN_OP2(x) ((x) << 22)
#define INSN_OP3(x) ((x) << 19)
#define INSN_OPF(x) ((x) << 5)
#define INSN_RD(x)  ((x) << 25)
#define INSN_RS1(x) ((x) << 14)
#define INSN_RS2(x) (x)
#define INSN_ASI(x) ((x) << 5)

#define INSN_IMM13(x) ((1 << 13) | ((x) & 0x1fff))
#define INSN_OFF22(x) (((x) >> 2) & 0x3fffff)

#define INSN_COND(x, a) (((x) << 25) | ((a) << 29))
#define COND_N     0x0
#define COND_E     0x1
#define COND_LE    0x2
#define COND_L     0x3
#define COND_LEU   0x4
#define COND_CS    0x5
#define COND_NEG   0x6
#define COND_VS    0x7
#define COND_A     0x8
#define COND_NE    0x9
#define COND_G     0xa
#define COND_GE    0xb
#define COND_GU    0xc
#define COND_CC    0xd
#define COND_POS   0xe
#define COND_VC    0xf
#define BA         (INSN_OP(0) | INSN_COND(COND_A, 0) | INSN_OP2(0x2))

#define ARITH_ADD  (INSN_OP(2) | INSN_OP3(0x00))
#define ARITH_AND  (INSN_OP(2) | INSN_OP3(0x01))
#define ARITH_OR   (INSN_OP(2) | INSN_OP3(0x02))
#define ARITH_ORCC (INSN_OP(2) | INSN_OP3(0x12))
#define ARITH_XOR  (INSN_OP(2) | INSN_OP3(0x03))
#define ARITH_SUB  (INSN_OP(2) | INSN_OP3(0x04))
#define ARITH_SUBCC (INSN_OP(2) | INSN_OP3(0x14))
#define ARITH_ADDX (INSN_OP(2) | INSN_OP3(0x10))
#define ARITH_SUBX (INSN_OP(2) | INSN_OP3(0x0c))
#define ARITH_UMUL (INSN_OP(2) | INSN_OP3(0x0a))
#define ARITH_UDIV (INSN_OP(2) | INSN_OP3(0x0e))
#define ARITH_SDIV (INSN_OP(2) | INSN_OP3(0x0f))
#define ARITH_MULX (INSN_OP(2) | INSN_OP3(0x09))
#define ARITH_UDIVX (INSN_OP(2) | INSN_OP3(0x0d))
#define ARITH_SDIVX (INSN_OP(2) | INSN_OP3(0x2d))

#define SHIFT_SLL  (INSN_OP(2) | INSN_OP3(0x25))
#define SHIFT_SRL  (INSN_OP(2) | INSN_OP3(0x26))
#define SHIFT_SRA  (INSN_OP(2) | INSN_OP3(0x27))

#define SHIFT_SLLX (INSN_OP(2) | INSN_OP3(0x25) | (1 << 12))
#define SHIFT_SRLX (INSN_OP(2) | INSN_OP3(0x26) | (1 << 12))
#define SHIFT_SRAX (INSN_OP(2) | INSN_OP3(0x27) | (1 << 12))

#define WRY        (INSN_OP(2) | INSN_OP3(0x30))
#define JMPL       (INSN_OP(2) | INSN_OP3(0x38))
#define SAVE       (INSN_OP(2) | INSN_OP3(0x3c))
#define RESTORE    (INSN_OP(2) | INSN_OP3(0x3d))
#define SETHI      (INSN_OP(0) | INSN_OP2(0x4))
#define CALL       INSN_OP(1)
#define LDUB       (INSN_OP(3) | INSN_OP3(0x01))
#define LDSB       (INSN_OP(3) | INSN_OP3(0x09))
#define LDUH       (INSN_OP(3) | INSN_OP3(0x02))
#define LDSH       (INSN_OP(3) | INSN_OP3(0x0a))
#define LDUW       (INSN_OP(3) | INSN_OP3(0x00))
#define LDSW       (INSN_OP(3) | INSN_OP3(0x08))
#define LDX        (INSN_OP(3) | INSN_OP3(0x0b))
#define STB        (INSN_OP(3) | INSN_OP3(0x05))
#define STH        (INSN_OP(3) | INSN_OP3(0x06))
#define STW        (INSN_OP(3) | INSN_OP3(0x04))
#define STX        (INSN_OP(3) | INSN_OP3(0x0e))
#define LDUBA      (INSN_OP(3) | INSN_OP3(0x11))
#define LDSBA      (INSN_OP(3) | INSN_OP3(0x19))
#define LDUHA      (INSN_OP(3) | INSN_OP3(0x12))
#define LDSHA      (INSN_OP(3) | INSN_OP3(0x1a))
#define LDUWA      (INSN_OP(3) | INSN_OP3(0x10))
#define LDSWA      (INSN_OP(3) | INSN_OP3(0x18))
#define LDXA       (INSN_OP(3) | INSN_OP3(0x1b))
#define STBA       (INSN_OP(3) | INSN_OP3(0x15))
#define STHA       (INSN_OP(3) | INSN_OP3(0x16))
#define STWA       (INSN_OP(3) | INSN_OP3(0x14))
#define STXA       (INSN_OP(3) | INSN_OP3(0x1e))

#ifndef ASI_PRIMARY_LITTLE
#define ASI_PRIMARY_LITTLE 0x88
#endif

static inline void tcg_out_arith(TCGContext *s, int rd, int rs1, int rs2,
                                 int op)
{
    tcg_out32(s, op | INSN_RD(rd) | INSN_RS1(rs1) |
              INSN_RS2(rs2));
}

static inline void tcg_out_arithi(TCGContext *s, int rd, int rs1, int offset,
                                  int op)
{
    tcg_out32(s, op | INSN_RD(rd) | INSN_RS1(rs1) |
              INSN_IMM13(offset));
}

static inline void tcg_out_mov(TCGContext *s, int ret, int arg)
{
    tcg_out_arith(s, ret, arg, TCG_REG_G0, ARITH_OR);
}

static inline void tcg_out_sethi(TCGContext *s, int ret, uint32_t arg)
{
    tcg_out32(s, SETHI | INSN_RD(ret) | ((arg & 0xfffffc00) >> 10));
}

static inline void tcg_out_movi_imm13(TCGContext *s, int ret, uint32_t arg)
{
    tcg_out_arithi(s, ret, TCG_REG_G0, arg, ARITH_OR);
}

static inline void tcg_out_movi_imm32(TCGContext *s, int ret, uint32_t arg)
{
    if (check_fit_i32(arg, 12))
        tcg_out_movi_imm13(s, ret, arg);
    else {
        tcg_out_sethi(s, ret, arg);
        if (arg & 0x3ff)
            tcg_out_arithi(s, ret, ret, arg & 0x3ff, ARITH_OR);
    }
}

static inline void tcg_out_movi(TCGContext *s, TCGType type,
                                int ret, tcg_target_long arg)
{
#if defined(__sparc_v9__) && !defined(__sparc_v8plus__)
    if (!check_fit_tl(arg, 32) && (arg & ~0xffffffffULL) != 0) {
        tcg_out_movi_imm32(s, TCG_REG_I4, arg >> 32);
        tcg_out_arithi(s, TCG_REG_I4, TCG_REG_I4, 32, SHIFT_SLLX);
        tcg_out_movi_imm32(s, ret, arg);
        tcg_out_arith(s, ret, ret, TCG_REG_I4, ARITH_OR);
    } else
#endif
        tcg_out_movi_imm32(s, ret, arg);
}

static inline void tcg_out_ld_raw(TCGContext *s, int ret,
                                  tcg_target_long arg)
{
    tcg_out_sethi(s, ret, arg);
    tcg_out32(s, LDUW | INSN_RD(ret) | INSN_RS1(ret) |
              INSN_IMM13(arg & 0x3ff));
}

static inline void tcg_out_ld_ptr(TCGContext *s, int ret,
                                  tcg_target_long arg)
{
    if (!check_fit_tl(arg, 10))
        tcg_out_movi(s, TCG_TYPE_PTR, ret, arg & ~0x3ffULL);
#if defined(__sparc_v9__) && !defined(__sparc_v8plus__)
    tcg_out32(s, LDX | INSN_RD(ret) | INSN_RS1(ret) |
              INSN_IMM13(arg & 0x3ff));
#else
    tcg_out32(s, LDUW | INSN_RD(ret) | INSN_RS1(ret) |
              INSN_IMM13(arg & 0x3ff));
#endif
}

static inline void tcg_out_ldst(TCGContext *s, int ret, int addr, int offset, int op)
{
    if (check_fit_tl(offset, 13))
        tcg_out32(s, op | INSN_RD(ret) | INSN_RS1(addr) |
                  INSN_IMM13(offset));
    else {
        tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_I5, offset);
        tcg_out32(s, op | INSN_RD(ret) | INSN_RS1(TCG_REG_I5) |
                  INSN_RS2(addr));
    }
}

static inline void tcg_out_ldst_asi(TCGContext *s, int ret, int addr,
                                    int offset, int op, int asi)
{
    tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_I5, offset);
    tcg_out32(s, op | INSN_RD(ret) | INSN_RS1(TCG_REG_I5) |
              INSN_ASI(asi) | INSN_RS2(addr));
}

static inline void tcg_out_ld(TCGContext *s, TCGType type, int ret,
                              int arg1, tcg_target_long arg2)
{
    if (type == TCG_TYPE_I32)
        tcg_out_ldst(s, ret, arg1, arg2, LDUW);
    else
        tcg_out_ldst(s, ret, arg1, arg2, LDX);
}

static inline void tcg_out_st(TCGContext *s, TCGType type, int arg,
                              int arg1, tcg_target_long arg2)
{
    if (type == TCG_TYPE_I32)
        tcg_out_ldst(s, arg, arg1, arg2, STW);
    else
        tcg_out_ldst(s, arg, arg1, arg2, STX);
}

static inline void tcg_out_sety(TCGContext *s, tcg_target_long val)
{
    if (val == 0 || val == -1)
        tcg_out32(s, WRY | INSN_IMM13(val));
    else
        fprintf(stderr, "unimplemented sety %ld\n", (long)val);
}

static inline void tcg_out_addi(TCGContext *s, int reg, tcg_target_long val)
{
    if (val != 0) {
        if (check_fit_tl(val, 13))
            tcg_out_arithi(s, reg, reg, val, ARITH_ADD);
        else {
            tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_I5, val);
            tcg_out_arith(s, reg, reg, TCG_REG_I5, ARITH_ADD);
        }
    }
}

static inline void tcg_out_andi(TCGContext *s, int reg, tcg_target_long val)
{
    if (val != 0) {
        if (check_fit_tl(val, 13))
            tcg_out_arithi(s, reg, reg, val, ARITH_AND);
        else {
            tcg_out_movi(s, TCG_TYPE_I32, TCG_REG_I5, val);
            tcg_out_arith(s, reg, reg, TCG_REG_I5, ARITH_AND);
        }
    }
}

static inline void tcg_out_nop(TCGContext *s)
{
    tcg_out_sethi(s, TCG_REG_G0, 0);
}

static void tcg_out_branch(TCGContext *s, int opc, int label_index)
{
    int32_t val;
    TCGLabel *l = &s->labels[label_index];

    if (l->has_value) {
        val = l->u.value - (tcg_target_long)s->code_ptr;
        tcg_out32(s, (INSN_OP(0) | INSN_COND(opc, 0) | INSN_OP2(0x2)
                      | INSN_OFF22(l->u.value - (unsigned long)s->code_ptr)));
    } else {
        tcg_out_reloc(s, s->code_ptr, R_SPARC_WDISP22, label_index, 0);
        tcg_out32(s, (INSN_OP(0) | INSN_COND(opc, 0) | INSN_OP2(0x2) | 0));
    }
}

static const uint8_t tcg_cond_to_bcond[10] = {
    [TCG_COND_EQ] = COND_E,
    [TCG_COND_NE] = COND_NE,
    [TCG_COND_LT] = COND_L,
    [TCG_COND_GE] = COND_GE,
    [TCG_COND_LE] = COND_LE,
    [TCG_COND_GT] = COND_G,
    [TCG_COND_LTU] = COND_CS,
    [TCG_COND_GEU] = COND_CC,
    [TCG_COND_LEU] = COND_LEU,
    [TCG_COND_GTU] = COND_GU,
};

static void tcg_out_brcond(TCGContext *s, int cond,
                           TCGArg arg1, TCGArg arg2, int const_arg2,
                           int label_index)
{
    if (const_arg2 && arg2 == 0)
        /* orcc %g0, r, %g0 */
        tcg_out_arith(s, TCG_REG_G0, TCG_REG_G0, arg1, ARITH_ORCC);
    else
        /* subcc r1, r2, %g0 */
        tcg_out_arith(s, TCG_REG_G0, arg1, arg2, ARITH_SUBCC);
    tcg_out_branch(s, tcg_cond_to_bcond[cond], label_index);
    tcg_out_nop(s);
}

/* Generate global QEMU prologue and epilogue code */
void tcg_target_qemu_prologue(TCGContext *s)
{
    tcg_out32(s, SAVE | INSN_RD(TCG_REG_O6) | INSN_RS1(TCG_REG_O6) |
              INSN_IMM13(-TCG_TARGET_STACK_MINFRAME));
    tcg_out32(s, JMPL | INSN_RD(TCG_REG_G0) | INSN_RS1(TCG_REG_I0) |
              INSN_RS2(TCG_REG_G0));
    tcg_out_nop(s);
}

#if defined(CONFIG_SOFTMMU)
extern void __ldb_mmu(void);
extern void __ldw_mmu(void);
extern void __ldl_mmu(void);
extern void __ldq_mmu(void);

extern void __stb_mmu(void);
extern void __stw_mmu(void);
extern void __stl_mmu(void);
extern void __stq_mmu(void);


static const void * const qemu_ld_helpers[4] = {
    __ldb_mmu,
    __ldw_mmu,
    __ldl_mmu,
    __ldq_mmu,
};

static const void * const qemu_st_helpers[4] = {
    __stb_mmu,
    __stw_mmu,
    __stl_mmu,
    __stq_mmu,
};
#endif

#if TARGET_LONG_BITS == 32
#define TARGET_LD_OP LDUW
#else
#define TARGET_LD_OP LDX
#endif

#ifdef __arch64__
#define HOST_LD_OP LDX
#define HOST_ST_OP STX
#define HOST_SLL_OP SHIFT_SLLX
#define HOST_SRA_OP SHIFT_SRAX
#else
#define HOST_LD_OP LDUW
#define HOST_ST_OP STW
#define HOST_SLL_OP SHIFT_SLL
#define HOST_SRA_OP SHIFT_SRA
#endif

static void tcg_out_qemu_ld(TCGContext *s, const TCGArg *args,
                            int opc)
{
    int addr_reg, data_reg, arg0, arg1, arg2, mem_index, s_bits;
#if defined(CONFIG_SOFTMMU)
    uint32_t *label1_ptr, *label2_ptr;
#endif

    data_reg = *args++;
    addr_reg = *args++;
    mem_index = *args;
    s_bits = opc & 3;

    arg0 = TCG_REG_O0;
    arg1 = TCG_REG_O1;
    arg2 = TCG_REG_O2;

#if defined(CONFIG_SOFTMMU)
    /* srl addr_reg, x, arg1 */
    tcg_out_arithi(s, arg1, addr_reg, TARGET_PAGE_BITS - CPU_TLB_ENTRY_BITS,
                   SHIFT_SRL);
    /* and addr_reg, x, arg0 */
    tcg_out_arithi(s, arg0, addr_reg, TARGET_PAGE_MASK | ((1 << s_bits) - 1),
                   ARITH_AND);

    /* and arg1, x, arg1 */
    tcg_out_andi(s, arg1, (CPU_TLB_SIZE - 1) << CPU_TLB_ENTRY_BITS);

    /* add arg1, x, arg1 */
    tcg_out_addi(s, arg1, offsetof(CPUState,
                                   tlb_table[mem_index][0].addr_read));

    /* add env, arg1, arg1 */
    tcg_out_arith(s, arg1, TCG_AREG0, arg1, ARITH_ADD);

    /* ld [arg1], arg2 */
    tcg_out32(s, TARGET_LD_OP | INSN_RD(arg2) | INSN_RS1(arg1) |
              INSN_RS2(TCG_REG_G0));

    /* subcc arg0, arg2, %g0 */
    tcg_out_arith(s, TCG_REG_G0, arg0, arg2, ARITH_SUBCC);

    /* will become:
       be label1 */
    label1_ptr = (uint32_t *)s->code_ptr;
    tcg_out32(s, 0);

    /* mov (delay slot) */
    tcg_out_mov(s, arg0, addr_reg);

    /* mov */
    tcg_out_movi(s, TCG_TYPE_I32, arg1, mem_index);

    /* XXX: move that code at the end of the TB */
    /* qemu_ld_helper[s_bits](arg0, arg1) */
    tcg_out32(s, CALL | ((((tcg_target_ulong)qemu_ld_helpers[s_bits]
                           - (tcg_target_ulong)s->code_ptr) >> 2)
                         & 0x3fffffff));
    /* Store AREG0 in stack to avoid ugly glibc bugs that mangle
       global registers */
    // delay slot
    tcg_out_ldst(s, TCG_AREG0, TCG_REG_CALL_STACK,
                 TCG_TARGET_CALL_STACK_OFFSET - sizeof(long), HOST_ST_OP);
    tcg_out_ldst(s, TCG_AREG0, TCG_REG_CALL_STACK,
                 TCG_TARGET_CALL_STACK_OFFSET - sizeof(long), HOST_LD_OP);

    /* data_reg = sign_extend(arg0) */
    switch(opc) {
    case 0 | 4:
        /* sll arg0, 24/56, data_reg */
        tcg_out_arithi(s, data_reg, arg0, (int)sizeof(tcg_target_long) * 8 - 8,
                       HOST_SLL_OP);
        /* sra data_reg, 24/56, data_reg */
        tcg_out_arithi(s, data_reg, data_reg,
                       (int)sizeof(tcg_target_long) * 8 - 8, HOST_SRA_OP);
        break;
    case 1 | 4:
        /* sll arg0, 16/48, data_reg */
        tcg_out_arithi(s, data_reg, arg0,
                       (int)sizeof(tcg_target_long) * 8 - 16, HOST_SLL_OP);
        /* sra data_reg, 16/48, data_reg */
        tcg_out_arithi(s, data_reg, data_reg,
                       (int)sizeof(tcg_target_long) * 8 - 16, HOST_SRA_OP);
        break;
    case 2 | 4:
        /* sll arg0, 32, data_reg */
        tcg_out_arithi(s, data_reg, arg0, 32, HOST_SLL_OP);
        /* sra data_reg, 32, data_reg */
        tcg_out_arithi(s, data_reg, data_reg, 32, HOST_SRA_OP);
        break;
    case 0:
    case 1:
    case 2:
    case 3:
    default:
        /* mov */
        tcg_out_mov(s, data_reg, arg0);
        break;
    }

    /* will become:
       ba label2 */
    label2_ptr = (uint32_t *)s->code_ptr;
    tcg_out32(s, 0);

    /* nop (delay slot */
    tcg_out_nop(s);

    /* label1: */
    *label1_ptr = (INSN_OP(0) | INSN_COND(COND_E, 0) | INSN_OP2(0x2) |
                   INSN_OFF22((unsigned long)s->code_ptr -
                              (unsigned long)label1_ptr));

    /* ld [arg1 + x], arg1 */
    tcg_out_ldst(s, arg1, arg1, offsetof(CPUTLBEntry, addend) -
                 offsetof(CPUTLBEntry, addr_read), HOST_LD_OP);

#if TARGET_LONG_BITS == 32
    /* and addr_reg, x, arg0 */
    tcg_out_movi(s, TCG_TYPE_I32, TCG_REG_I5, 0xffffffff);
    tcg_out_arith(s, arg0, addr_reg, TCG_REG_I5, ARITH_AND);
    /* add arg0, arg1, arg0 */
    tcg_out_arith(s, arg0, arg0, arg1, ARITH_ADD);
#else
    /* add addr_reg, arg1, arg0 */
    tcg_out_arith(s, arg0, addr_reg, arg1, ARITH_ADD);
#endif

#else
    arg0 = addr_reg;
#endif

    switch(opc) {
    case 0:
        /* ldub [arg0], data_reg */
        tcg_out_ldst(s, data_reg, arg0, 0, LDUB);
        break;
    case 0 | 4:
        /* ldsb [arg0], data_reg */
        tcg_out_ldst(s, data_reg, arg0, 0, LDSB);
        break;
    case 1:
#ifdef TARGET_WORDS_BIGENDIAN
        /* lduh [arg0], data_reg */
        tcg_out_ldst(s, data_reg, arg0, 0, LDUH);
#else
        /* lduha [arg0] ASI_PRIMARY_LITTLE, data_reg */
        tcg_out_ldst_asi(s, data_reg, arg0, 0, LDUHA, ASI_PRIMARY_LITTLE);
#endif
        break;
    case 1 | 4:
#ifdef TARGET_WORDS_BIGENDIAN
        /* ldsh [arg0], data_reg */
        tcg_out_ldst(s, data_reg, arg0, 0, LDSH);
#else
        /* ldsha [arg0] ASI_PRIMARY_LITTLE, data_reg */
        tcg_out_ldst_asi(s, data_reg, arg0, 0, LDSHA, ASI_PRIMARY_LITTLE);
#endif
        break;
    case 2:
#ifdef TARGET_WORDS_BIGENDIAN
        /* lduw [arg0], data_reg */
        tcg_out_ldst(s, data_reg, arg0, 0, LDUW);
#else
        /* lduwa [arg0] ASI_PRIMARY_LITTLE, data_reg */
        tcg_out_ldst_asi(s, data_reg, arg0, 0, LDUWA, ASI_PRIMARY_LITTLE);
#endif
        break;
    case 2 | 4:
#ifdef TARGET_WORDS_BIGENDIAN
        /* ldsw [arg0], data_reg */
        tcg_out_ldst(s, data_reg, arg0, 0, LDSW);
#else
        /* ldswa [arg0] ASI_PRIMARY_LITTLE, data_reg */
        tcg_out_ldst_asi(s, data_reg, arg0, 0, LDSWA, ASI_PRIMARY_LITTLE);
#endif
        break;
    case 3:
#ifdef TARGET_WORDS_BIGENDIAN
        /* ldx [arg0], data_reg */
        tcg_out_ldst(s, data_reg, arg0, 0, LDX);
#else
        /* ldxa [arg0] ASI_PRIMARY_LITTLE, data_reg */
        tcg_out_ldst_asi(s, data_reg, arg0, 0, LDXA, ASI_PRIMARY_LITTLE);
#endif
        break;
    default:
        tcg_abort();
    }

#if defined(CONFIG_SOFTMMU)
    /* label2: */
    *label2_ptr = (INSN_OP(0) | INSN_COND(COND_A, 0) | INSN_OP2(0x2) |
                   INSN_OFF22((unsigned long)s->code_ptr -
                              (unsigned long)label2_ptr));
#endif
}

static void tcg_out_qemu_st(TCGContext *s, const TCGArg *args,
                            int opc)
{
    int addr_reg, data_reg, arg0, arg1, arg2, mem_index, s_bits;
#if defined(CONFIG_SOFTMMU)
    uint32_t *label1_ptr, *label2_ptr;
#endif

    data_reg = *args++;
    addr_reg = *args++;
    mem_index = *args;

    s_bits = opc;

    arg0 = TCG_REG_O0;
    arg1 = TCG_REG_O1;
    arg2 = TCG_REG_O2;

#if defined(CONFIG_SOFTMMU)
    /* srl addr_reg, x, arg1 */
    tcg_out_arithi(s, arg1, addr_reg, TARGET_PAGE_BITS - CPU_TLB_ENTRY_BITS,
                   SHIFT_SRL);

    /* and addr_reg, x, arg0 */
    tcg_out_arithi(s, arg0, addr_reg, TARGET_PAGE_MASK | ((1 << s_bits) - 1),
                   ARITH_AND);

    /* and arg1, x, arg1 */
    tcg_out_andi(s, arg1, (CPU_TLB_SIZE - 1) << CPU_TLB_ENTRY_BITS);

    /* add arg1, x, arg1 */
    tcg_out_addi(s, arg1, offsetof(CPUState,
                                   tlb_table[mem_index][0].addr_write));

    /* add env, arg1, arg1 */
    tcg_out_arith(s, arg1, TCG_AREG0, arg1, ARITH_ADD);

    /* ld [arg1], arg2 */
    tcg_out32(s, TARGET_LD_OP | INSN_RD(arg2) | INSN_RS1(arg1) |
              INSN_RS2(TCG_REG_G0));

    /* subcc arg0, arg2, %g0 */
    tcg_out_arith(s, TCG_REG_G0, arg0, arg2, ARITH_SUBCC);

    /* will become:
       be label1 */
    label1_ptr = (uint32_t *)s->code_ptr;
    tcg_out32(s, 0);

    /* mov (delay slot) */
    tcg_out_mov(s, arg0, addr_reg);

    /* mov */
    tcg_out_mov(s, arg1, data_reg);

    /* mov */
    tcg_out_movi(s, TCG_TYPE_I32, arg2, mem_index);

    /* XXX: move that code at the end of the TB */
    /* qemu_st_helper[s_bits](arg0, arg1, arg2) */
    tcg_out32(s, CALL | ((((tcg_target_ulong)qemu_st_helpers[s_bits]
                           - (tcg_target_ulong)s->code_ptr) >> 2)
                         & 0x3fffffff));
    /* Store AREG0 in stack to avoid ugly glibc bugs that mangle
       global registers */
    // delay slot
    tcg_out_ldst(s, TCG_AREG0, TCG_REG_CALL_STACK,
                 TCG_TARGET_CALL_STACK_OFFSET - sizeof(long), HOST_ST_OP);
    tcg_out_ldst(s, TCG_AREG0, TCG_REG_CALL_STACK,
                 TCG_TARGET_CALL_STACK_OFFSET - sizeof(long), HOST_LD_OP);

    /* will become:
       ba label2 */
    label2_ptr = (uint32_t *)s->code_ptr;
    tcg_out32(s, 0);

    /* nop (delay slot) */
    tcg_out_nop(s);

    /* label1: */
    *label1_ptr = (INSN_OP(0) | INSN_COND(COND_E, 0) | INSN_OP2(0x2) |
                   INSN_OFF22((unsigned long)s->code_ptr -
                              (unsigned long)label1_ptr));

    /* ld [arg1 + x], arg1 */
    tcg_out_ldst(s, arg1, arg1, offsetof(CPUTLBEntry, addend) -
                 offsetof(CPUTLBEntry, addr_write), HOST_LD_OP);

#if TARGET_LONG_BITS == 32
    /* and addr_reg, x, arg0 */
    tcg_out_movi(s, TCG_TYPE_I32, TCG_REG_I5, 0xffffffff);
    tcg_out_arith(s, arg0, addr_reg, TCG_REG_I5, ARITH_AND);
    /* add arg0, arg1, arg0 */
    tcg_out_arith(s, arg0, arg0, arg1, ARITH_ADD);
#else
    /* add addr_reg, arg1, arg0 */
    tcg_out_arith(s, arg0, addr_reg, arg1, ARITH_ADD);
#endif

#else
    arg0 = addr_reg;
#endif

    switch(opc) {
    case 0:
        /* stb data_reg, [arg0] */
        tcg_out_ldst(s, data_reg, arg0, 0, STB);
        break;
    case 1:
#ifdef TARGET_WORDS_BIGENDIAN
        /* sth data_reg, [arg0] */
        tcg_out_ldst(s, data_reg, arg0, 0, STH);
#else
        /* stha data_reg, [arg0] ASI_PRIMARY_LITTLE */
        tcg_out_ldst_asi(s, data_reg, arg0, 0, STHA, ASI_PRIMARY_LITTLE);
#endif
        break;
    case 2:
#ifdef TARGET_WORDS_BIGENDIAN
        /* stw data_reg, [arg0] */
        tcg_out_ldst(s, data_reg, arg0, 0, STW);
#else
        /* stwa data_reg, [arg0] ASI_PRIMARY_LITTLE */
        tcg_out_ldst_asi(s, data_reg, arg0, 0, STWA, ASI_PRIMARY_LITTLE);
#endif
        break;
    case 3:
#ifdef TARGET_WORDS_BIGENDIAN
        /* stx data_reg, [arg0] */
        tcg_out_ldst(s, data_reg, arg0, 0, STX);
#else
        /* stxa data_reg, [arg0] ASI_PRIMARY_LITTLE */
        tcg_out_ldst_asi(s, data_reg, arg0, 0, STXA, ASI_PRIMARY_LITTLE);
#endif
        break;
    default:
        tcg_abort();
    }

#if defined(CONFIG_SOFTMMU)
    /* label2: */
    *label2_ptr = (INSN_OP(0) | INSN_COND(COND_A, 0) | INSN_OP2(0x2) |
                   INSN_OFF22((unsigned long)s->code_ptr -
                              (unsigned long)label2_ptr));
#endif
}

static inline void tcg_out_op(TCGContext *s, int opc, const TCGArg *args,
                              const int *const_args)
{
    int c;

    switch (opc) {
    case INDEX_op_exit_tb:
        tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_I0, args[0]);
        tcg_out32(s, JMPL | INSN_RD(TCG_REG_G0) | INSN_RS1(TCG_REG_I7) |
                  INSN_IMM13(8));
        tcg_out32(s, RESTORE | INSN_RD(TCG_REG_G0) | INSN_RS1(TCG_REG_G0) |
                      INSN_RS2(TCG_REG_G0));
        break;
    case INDEX_op_goto_tb:
        if (s->tb_jmp_offset) {
            /* direct jump method */
            tcg_out_sethi(s, TCG_REG_I5, args[0] & 0xffffe000);
            tcg_out32(s, JMPL | INSN_RD(TCG_REG_G0) | INSN_RS1(TCG_REG_I5) |
                      INSN_IMM13((args[0] & 0x1fff)));
            s->tb_jmp_offset[args[0]] = s->code_ptr - s->code_buf;
        } else {
            /* indirect jump method */
            tcg_out_ld_ptr(s, TCG_REG_I5, (tcg_target_long)(s->tb_next + args[0]));
            tcg_out32(s, JMPL | INSN_RD(TCG_REG_G0) | INSN_RS1(TCG_REG_I5) |
                      INSN_RS2(TCG_REG_G0));
        }
        tcg_out_nop(s);
        s->tb_next_offset[args[0]] = s->code_ptr - s->code_buf;
        break;
    case INDEX_op_call:
        if (const_args[0])
            tcg_out32(s, CALL | ((((tcg_target_ulong)args[0]
                                   - (tcg_target_ulong)s->code_ptr) >> 2)
                                 & 0x3fffffff));
        else {
            tcg_out_ld_ptr(s, TCG_REG_I5,
                           (tcg_target_long)(s->tb_next + args[0]));
            tcg_out32(s, JMPL | INSN_RD(TCG_REG_O7) | INSN_RS1(TCG_REG_I5) |
                      INSN_RS2(TCG_REG_G0));
        }
        /* Store AREG0 in stack to avoid ugly glibc bugs that mangle
           global registers */
        // delay slot
        tcg_out_ldst(s, TCG_AREG0, TCG_REG_CALL_STACK,
                     TCG_TARGET_CALL_STACK_OFFSET - sizeof(long), HOST_ST_OP);
        tcg_out_ldst(s, TCG_AREG0, TCG_REG_CALL_STACK,
                     TCG_TARGET_CALL_STACK_OFFSET - sizeof(long), HOST_LD_OP);
        break;
    case INDEX_op_jmp:
    case INDEX_op_br:
        tcg_out_branch(s, COND_A, args[0]);
        tcg_out_nop(s);
        break;
    case INDEX_op_movi_i32:
        tcg_out_movi(s, TCG_TYPE_I32, args[0], (uint32_t)args[1]);
        break;

#if defined(__sparc_v9__) && !defined(__sparc_v8plus__)
#define OP_32_64(x)                             \
        glue(glue(case INDEX_op_, x), _i32:)    \
        glue(glue(case INDEX_op_, x), _i64:)
#else
#define OP_32_64(x)                             \
        glue(glue(case INDEX_op_, x), _i32:)
#endif
        OP_32_64(ld8u);
        tcg_out_ldst(s, args[0], args[1], args[2], LDUB);
        break;
        OP_32_64(ld8s);
        tcg_out_ldst(s, args[0], args[1], args[2], LDSB);
        break;
        OP_32_64(ld16u);
        tcg_out_ldst(s, args[0], args[1], args[2], LDUH);
        break;
        OP_32_64(ld16s);
        tcg_out_ldst(s, args[0], args[1], args[2], LDSH);
        break;
    case INDEX_op_ld_i32:
#if defined(__sparc_v9__) && !defined(__sparc_v8plus__)
    case INDEX_op_ld32u_i64:
#endif
        tcg_out_ldst(s, args[0], args[1], args[2], LDUW);
        break;
        OP_32_64(st8);
        tcg_out_ldst(s, args[0], args[1], args[2], STB);
        break;
        OP_32_64(st16);
        tcg_out_ldst(s, args[0], args[1], args[2], STH);
        break;
    case INDEX_op_st_i32:
#if defined(__sparc_v9__) && !defined(__sparc_v8plus__)
    case INDEX_op_st32_i64:
#endif
        tcg_out_ldst(s, args[0], args[1], args[2], STW);
        break;
        OP_32_64(add);
        c = ARITH_ADD;
        goto gen_arith32;
        OP_32_64(sub);
        c = ARITH_SUB;
        goto gen_arith32;
        OP_32_64(and);
        c = ARITH_AND;
        goto gen_arith32;
        OP_32_64(or);
        c = ARITH_OR;
        goto gen_arith32;
        OP_32_64(xor);
        c = ARITH_XOR;
        goto gen_arith32;
    case INDEX_op_shl_i32:
        c = SHIFT_SLL;
        goto gen_arith32;
    case INDEX_op_shr_i32:
        c = SHIFT_SRL;
        goto gen_arith32;
    case INDEX_op_sar_i32:
        c = SHIFT_SRA;
        goto gen_arith32;
    case INDEX_op_mul_i32:
        c = ARITH_UMUL;
        goto gen_arith32;
    case INDEX_op_div2_i32:
#if defined(__sparc_v9__) || defined(__sparc_v8plus__)
        c = ARITH_SDIVX;
        goto gen_arith32;
#else
        tcg_out_sety(s, 0);
        c = ARITH_SDIV;
        goto gen_arith32;
#endif
    case INDEX_op_divu2_i32:
#if defined(__sparc_v9__) || defined(__sparc_v8plus__)
        c = ARITH_UDIVX;
        goto gen_arith32;
#else
        tcg_out_sety(s, 0);
        c = ARITH_UDIV;
        goto gen_arith32;
#endif

    case INDEX_op_brcond_i32:
        tcg_out_brcond(s, args[2], args[0], args[1], const_args[1],
                       args[3]);
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
    case INDEX_op_qemu_ld32s:
        tcg_out_qemu_ld(s, args, 2 | 4);
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

#if defined(__sparc_v9__) && !defined(__sparc_v8plus__)
    case INDEX_op_movi_i64:
        tcg_out_movi(s, TCG_TYPE_I64, args[0], args[1]);
        break;
    case INDEX_op_ld32s_i64:
        tcg_out_ldst(s, args[0], args[1], args[2], LDSW);
        break;
    case INDEX_op_ld_i64:
        tcg_out_ldst(s, args[0], args[1], args[2], LDX);
        break;
    case INDEX_op_st_i64:
        tcg_out_ldst(s, args[0], args[1], args[2], STX);
        break;
    case INDEX_op_shl_i64:
        c = SHIFT_SLLX;
        goto gen_arith32;
    case INDEX_op_shr_i64:
        c = SHIFT_SRLX;
        goto gen_arith32;
    case INDEX_op_sar_i64:
        c = SHIFT_SRAX;
        goto gen_arith32;
    case INDEX_op_mul_i64:
        c = ARITH_MULX;
        goto gen_arith32;
    case INDEX_op_div2_i64:
        c = ARITH_SDIVX;
        goto gen_arith32;
    case INDEX_op_divu2_i64:
        c = ARITH_UDIVX;
        goto gen_arith32;

    case INDEX_op_brcond_i64:
        tcg_out_brcond(s, args[2], args[0], args[1], const_args[1],
                       args[3]);
        break;
    case INDEX_op_qemu_ld64:
        tcg_out_qemu_ld(s, args, 3);
        break;
    case INDEX_op_qemu_st64:
        tcg_out_qemu_st(s, args, 3);
        break;

#endif
    gen_arith32:
        if (const_args[2]) {
            tcg_out_arithi(s, args[0], args[1], args[2], c);
        } else {
            tcg_out_arith(s, args[0], args[1], args[2], c);
        }
        break;

    default:
        fprintf(stderr, "unknown opcode 0x%x\n", opc);
        tcg_abort();
    }
}

static const TCGTargetOpDef sparc_op_defs[] = {
    { INDEX_op_exit_tb, { } },
    { INDEX_op_goto_tb, { } },
    { INDEX_op_call, { "ri" } },
    { INDEX_op_jmp, { "ri" } },
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

    { INDEX_op_add_i32, { "r", "r", "rJ" } },
    { INDEX_op_mul_i32, { "r", "r", "rJ" } },
    { INDEX_op_div2_i32, { "r", "r", "0", "1", "r" } },
    { INDEX_op_divu2_i32, { "r", "r", "0", "1", "r" } },
    { INDEX_op_sub_i32, { "r", "r", "rJ" } },
    { INDEX_op_and_i32, { "r", "r", "rJ" } },
    { INDEX_op_or_i32, { "r", "r", "rJ" } },
    { INDEX_op_xor_i32, { "r", "r", "rJ" } },

    { INDEX_op_shl_i32, { "r", "r", "rJ" } },
    { INDEX_op_shr_i32, { "r", "r", "rJ" } },
    { INDEX_op_sar_i32, { "r", "r", "rJ" } },

    { INDEX_op_brcond_i32, { "r", "ri" } },

    { INDEX_op_qemu_ld8u, { "r", "L" } },
    { INDEX_op_qemu_ld8s, { "r", "L" } },
    { INDEX_op_qemu_ld16u, { "r", "L" } },
    { INDEX_op_qemu_ld16s, { "r", "L" } },
    { INDEX_op_qemu_ld32u, { "r", "L" } },
    { INDEX_op_qemu_ld32s, { "r", "L" } },

    { INDEX_op_qemu_st8, { "L", "L" } },
    { INDEX_op_qemu_st16, { "L", "L" } },
    { INDEX_op_qemu_st32, { "L", "L" } },

#if defined(__sparc_v9__) && !defined(__sparc_v8plus__)
    { INDEX_op_mov_i64, { "r", "r" } },
    { INDEX_op_movi_i64, { "r" } },
    { INDEX_op_ld8u_i64, { "r", "r" } },
    { INDEX_op_ld8s_i64, { "r", "r" } },
    { INDEX_op_ld16u_i64, { "r", "r" } },
    { INDEX_op_ld16s_i64, { "r", "r" } },
    { INDEX_op_ld32u_i64, { "r", "r" } },
    { INDEX_op_ld32s_i64, { "r", "r" } },
    { INDEX_op_ld_i64, { "r", "r" } },
    { INDEX_op_st8_i64, { "r", "r" } },
    { INDEX_op_st16_i64, { "r", "r" } },
    { INDEX_op_st32_i64, { "r", "r" } },
    { INDEX_op_st_i64, { "r", "r" } },
    { INDEX_op_qemu_ld64, { "L", "L" } },
    { INDEX_op_qemu_st64, { "L", "L" } },

    { INDEX_op_add_i64, { "r", "r", "rJ" } },
    { INDEX_op_mul_i64, { "r", "r", "rJ" } },
    { INDEX_op_div2_i64, { "r", "r", "0", "1", "r" } },
    { INDEX_op_divu2_i64, { "r", "r", "0", "1", "r" } },
    { INDEX_op_sub_i64, { "r", "r", "rJ" } },
    { INDEX_op_and_i64, { "r", "r", "rJ" } },
    { INDEX_op_or_i64, { "r", "r", "rJ" } },
    { INDEX_op_xor_i64, { "r", "r", "rJ" } },

    { INDEX_op_shl_i64, { "r", "r", "rJ" } },
    { INDEX_op_shr_i64, { "r", "r", "rJ" } },
    { INDEX_op_sar_i64, { "r", "r", "rJ" } },

    { INDEX_op_brcond_i64, { "r", "ri" } },
#endif
    { -1 },
};

void tcg_target_init(TCGContext *s)
{
    tcg_regset_set32(tcg_target_available_regs[TCG_TYPE_I32], 0, 0xffffffff);
#if defined(__sparc_v9__) && !defined(__sparc_v8plus__)
    tcg_regset_set32(tcg_target_available_regs[TCG_TYPE_I64], 0, 0xffffffff);
#endif
    tcg_regset_set32(tcg_target_call_clobber_regs, 0,
                     (1 << TCG_REG_G1) |
                     (1 << TCG_REG_G2) |
                     (1 << TCG_REG_G3) |
                     (1 << TCG_REG_G4) |
                     (1 << TCG_REG_G5) |
                     (1 << TCG_REG_G6) |
                     (1 << TCG_REG_G7) |
                     (1 << TCG_REG_O0) |
                     (1 << TCG_REG_O1) |
                     (1 << TCG_REG_O2) |
                     (1 << TCG_REG_O3) |
                     (1 << TCG_REG_O4) |
                     (1 << TCG_REG_O5) |
                     (1 << TCG_REG_O7));

    tcg_regset_clear(s->reserved_regs);
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_G0);
#if defined(__sparc_v9__) && !defined(__sparc_v8plus__)
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_I4); // for internal use
#endif
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_I5); // for internal use
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_I6);
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_I7);
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_O6);
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_O7);
    tcg_add_target_add_op_defs(sparc_op_defs);
}

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

#if defined(TCG_TARGET_WORDS_BIGENDIAN) == defined(TARGET_WORDS_BIGENDIAN)
# define TCG_NEED_BSWAP 0
#else
# define TCG_NEED_BSWAP 1
#endif

#ifndef NDEBUG
static const char * const tcg_target_reg_names[TCG_TARGET_NB_REGS] = {
    "zero",
    "at",
    "v0",
    "v1",
    "a0",
    "a1",
    "a2",
    "a3",
    "t0",
    "t1",
    "t2",
    "t3",
    "t4",
    "t5",
    "t6",
    "t7",
    "s0",
    "s1",
    "s2",
    "s3",
    "s4",
    "s5",
    "s6",
    "s7",
    "t8",
    "t9",
    "k0",
    "k1",
    "gp",
    "sp",
    "fp",
    "ra",
};
#endif

/* check if we really need so many registers :P */
static const int tcg_target_reg_alloc_order[] = {
    TCG_REG_S0,
    TCG_REG_S1,
    TCG_REG_S2,
    TCG_REG_S3,
    TCG_REG_S4,
    TCG_REG_S5,
    TCG_REG_S6,
    TCG_REG_S7,
    TCG_REG_T1,
    TCG_REG_T2,
    TCG_REG_T3,
    TCG_REG_T4,
    TCG_REG_T5,
    TCG_REG_T6,
    TCG_REG_T7,
    TCG_REG_T8,
    TCG_REG_T9,
    TCG_REG_A0,
    TCG_REG_A1,
    TCG_REG_A2,
    TCG_REG_A3,
    TCG_REG_V0,
    TCG_REG_V1
};

static const int tcg_target_call_iarg_regs[4] = {
    TCG_REG_A0,
    TCG_REG_A1,
    TCG_REG_A2,
    TCG_REG_A3
};

static const int tcg_target_call_oarg_regs[2] = {
    TCG_REG_V0,
    TCG_REG_V1
};

static uint8_t *tb_ret_addr;

static inline uint32_t reloc_lo16_val (void *pc, tcg_target_long target)
{
    return target & 0xffff;
}

static inline void reloc_lo16 (void *pc, tcg_target_long target)
{
    *(uint32_t *) pc = (*(uint32_t *) pc & ~0xffff)
                       | reloc_lo16_val(pc, target);
}

static inline uint32_t reloc_hi16_val (void *pc, tcg_target_long target)
{
    return (target >> 16) & 0xffff;
}

static inline void reloc_hi16 (void *pc, tcg_target_long target)
{
    *(uint32_t *) pc = (*(uint32_t *) pc & ~0xffff)
                       | reloc_hi16_val(pc, target);
}

static inline uint32_t reloc_pc16_val (void *pc, tcg_target_long target)
{
    int32_t disp;

    disp = target - (tcg_target_long) pc - 4;
    if (disp != (disp << 14) >> 14) {
        tcg_abort ();
    }

    return (disp >> 2) & 0xffff;
}

static inline void reloc_pc16 (void *pc, tcg_target_long target)
{
    *(uint32_t *) pc = (*(uint32_t *) pc & ~0xffff)
                       | reloc_pc16_val(pc, target);
}

static inline uint32_t reloc_26_val (void *pc, tcg_target_long target)
{
    if ((((tcg_target_long)pc + 4) & 0xf0000000) != (target & 0xf0000000)) {
        tcg_abort ();
    }

    return (target >> 2) & 0x3ffffff;
}

static inline void reloc_pc26 (void *pc, tcg_target_long target)
{
    *(uint32_t *) pc = (*(uint32_t *) pc & ~0x3ffffff)
                       | reloc_26_val(pc, target);
}

static void patch_reloc(uint8_t *code_ptr, int type,
                        tcg_target_long value, tcg_target_long addend)
{
    value += addend;
    switch(type) {
    case R_MIPS_LO16:
        reloc_lo16(code_ptr, value);
        break;
    case R_MIPS_HI16:
        reloc_hi16(code_ptr, value);
        break;
    case R_MIPS_PC16:
        reloc_pc16(code_ptr, value);
        break;
    case R_MIPS_26:
        reloc_pc26(code_ptr, value);
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
    switch(ct_str[0]) {
    case 'r':
        ct->ct |= TCG_CT_REG;
        tcg_regset_set(ct->u.regs, 0xffffffff);
        break;
    case 'C':
        ct->ct |= TCG_CT_REG;
        tcg_regset_clear(ct->u.regs);
        tcg_regset_set_reg(ct->u.regs, TCG_REG_T9);
        break;
    case 'L': /* qemu_ld output arg constraint */
        ct->ct |= TCG_CT_REG;
        tcg_regset_set(ct->u.regs, 0xffffffff);
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_V0);
        break;
    case 'l': /* qemu_ld input arg constraint */
        ct->ct |= TCG_CT_REG;
        tcg_regset_set(ct->u.regs, 0xffffffff);
#if defined(CONFIG_SOFTMMU)
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_A0);
#endif
        break;
    case 'S': /* qemu_st constraint */
        ct->ct |= TCG_CT_REG;
        tcg_regset_set(ct->u.regs, 0xffffffff);
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_A0);
#if defined(CONFIG_SOFTMMU)
# if TARGET_LONG_BITS == 64
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_A1);
# endif
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_A2);
#endif
        break;
    case 'I':
        ct->ct |= TCG_CT_CONST_U16;
        break;
    case 'J':
        ct->ct |= TCG_CT_CONST_S16;
        break;
    case 'Z':
        /* We are cheating a bit here, using the fact that the register
           ZERO is also the register number 0. Hence there is no need
           to check for const_args in each instruction. */
        ct->ct |= TCG_CT_CONST_ZERO;
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
    else if ((ct & TCG_CT_CONST_ZERO) && val == 0)
        return 1;
    else if ((ct & TCG_CT_CONST_U16) && val == (uint16_t)val)
        return 1;
    else if ((ct & TCG_CT_CONST_S16) && val == (int16_t)val)
        return 1;
    else
        return 0;
}

/* instruction opcodes */
enum {
    OPC_BEQ      = 0x04 << 26,
    OPC_BNE      = 0x05 << 26,
    OPC_ADDIU    = 0x09 << 26,
    OPC_SLTI     = 0x0A << 26,
    OPC_SLTIU    = 0x0B << 26,
    OPC_ANDI     = 0x0C << 26,
    OPC_ORI      = 0x0D << 26,
    OPC_XORI     = 0x0E << 26,
    OPC_LUI      = 0x0F << 26,
    OPC_LB       = 0x20 << 26,
    OPC_LH       = 0x21 << 26,
    OPC_LW       = 0x23 << 26,
    OPC_LBU      = 0x24 << 26,
    OPC_LHU      = 0x25 << 26,
    OPC_LWU      = 0x27 << 26,
    OPC_SB       = 0x28 << 26,
    OPC_SH       = 0x29 << 26,
    OPC_SW       = 0x2B << 26,

    OPC_SPECIAL  = 0x00 << 26,
    OPC_SLL      = OPC_SPECIAL | 0x00,
    OPC_SRL      = OPC_SPECIAL | 0x02,
    OPC_SRA      = OPC_SPECIAL | 0x03,
    OPC_SLLV     = OPC_SPECIAL | 0x04,
    OPC_SRLV     = OPC_SPECIAL | 0x06,
    OPC_SRAV     = OPC_SPECIAL | 0x07,
    OPC_JR       = OPC_SPECIAL | 0x08,
    OPC_JALR     = OPC_SPECIAL | 0x09,
    OPC_MFHI     = OPC_SPECIAL | 0x10,
    OPC_MFLO     = OPC_SPECIAL | 0x12,
    OPC_MULT     = OPC_SPECIAL | 0x18,
    OPC_MULTU    = OPC_SPECIAL | 0x19,
    OPC_DIV      = OPC_SPECIAL | 0x1A,
    OPC_DIVU     = OPC_SPECIAL | 0x1B,
    OPC_ADDU     = OPC_SPECIAL | 0x21,
    OPC_SUBU     = OPC_SPECIAL | 0x23,
    OPC_AND      = OPC_SPECIAL | 0x24,
    OPC_OR       = OPC_SPECIAL | 0x25,
    OPC_XOR      = OPC_SPECIAL | 0x26,
    OPC_NOR      = OPC_SPECIAL | 0x27,
    OPC_SLT      = OPC_SPECIAL | 0x2A,
    OPC_SLTU     = OPC_SPECIAL | 0x2B,

    OPC_SPECIAL3 = 0x1f << 26,
    OPC_SEB      = OPC_SPECIAL3 | 0x420,
    OPC_SEH      = OPC_SPECIAL3 | 0x620,
};

/*
 * Type reg
 */
static inline void tcg_out_opc_reg(TCGContext *s, int opc, int rd, int rs, int rt)
{
    int32_t inst;

    inst = opc;
    inst |= (rs & 0x1F) << 21;
    inst |= (rt & 0x1F) << 16;
    inst |= (rd & 0x1F) << 11;
    tcg_out32(s, inst);
}

/*
 * Type immediate
 */
static inline void tcg_out_opc_imm(TCGContext *s, int opc, int rt, int rs, int imm)
{
    int32_t inst;

    inst = opc;
    inst |= (rs & 0x1F) << 21;
    inst |= (rt & 0x1F) << 16;
    inst |= (imm & 0xffff);
    tcg_out32(s, inst);
}

/*
 * Type branch
 */
static inline void tcg_out_opc_br(TCGContext *s, int opc, int rt, int rs)
{
    /* We pay attention here to not modify the branch target by reading
       the existing value and using it again. This ensure that caches and
       memory are kept coherent during retranslation. */
    uint16_t offset = (uint16_t)(*(uint32_t *) s->code_ptr);

    tcg_out_opc_imm(s, opc, rt, rs, offset);
}

/*
 * Type sa
 */
static inline void tcg_out_opc_sa(TCGContext *s, int opc, int rd, int rt, int sa)
{
    int32_t inst;

    inst = opc;
    inst |= (rt & 0x1F) << 16;
    inst |= (rd & 0x1F) << 11;
    inst |= (sa & 0x1F) <<  6;
    tcg_out32(s, inst);

}

static inline void tcg_out_nop(TCGContext *s)
{
    tcg_out32(s, 0);
}

static inline void tcg_out_mov(TCGContext *s, TCGType type, int ret, int arg)
{
    tcg_out_opc_reg(s, OPC_ADDU, ret, arg, TCG_REG_ZERO);
}

static inline void tcg_out_movi(TCGContext *s, TCGType type,
                                int reg, int32_t arg)
{
    if (arg == (int16_t)arg) {
        tcg_out_opc_imm(s, OPC_ADDIU, reg, TCG_REG_ZERO, arg);
    } else if (arg == (uint16_t)arg) {
        tcg_out_opc_imm(s, OPC_ORI, reg, TCG_REG_ZERO, arg);
    } else {
        tcg_out_opc_imm(s, OPC_LUI, reg, 0, arg >> 16);
        tcg_out_opc_imm(s, OPC_ORI, reg, reg, arg & 0xffff);
    }
}

static inline void tcg_out_bswap16(TCGContext *s, int ret, int arg)
{
    /* ret and arg can't be register at */
    if (ret == TCG_REG_AT || arg == TCG_REG_AT) {
        tcg_abort();
    }

    tcg_out_opc_sa(s, OPC_SRL, TCG_REG_AT, arg, 8);
    tcg_out_opc_imm(s, OPC_ANDI, TCG_REG_AT, TCG_REG_AT, 0x00ff);

    tcg_out_opc_sa(s, OPC_SLL, ret, arg, 8);
    tcg_out_opc_imm(s, OPC_ANDI, ret, ret, 0xff00);
    tcg_out_opc_reg(s, OPC_OR, ret, ret, TCG_REG_AT);
}

static inline void tcg_out_bswap16s(TCGContext *s, int ret, int arg)
{
    /* ret and arg can't be register at */
    if (ret == TCG_REG_AT || arg == TCG_REG_AT) {
        tcg_abort();
    }

    tcg_out_opc_sa(s, OPC_SRL, TCG_REG_AT, arg, 8);
    tcg_out_opc_imm(s, OPC_ANDI, TCG_REG_AT, TCG_REG_AT, 0xff);

    tcg_out_opc_sa(s, OPC_SLL, ret, arg, 24);
    tcg_out_opc_sa(s, OPC_SRA, ret, ret, 16);
    tcg_out_opc_reg(s, OPC_OR, ret, ret, TCG_REG_AT);
}

static inline void tcg_out_bswap32(TCGContext *s, int ret, int arg)
{
    /* ret and arg must be different and can't be register at */
    if (ret == arg || ret == TCG_REG_AT || arg == TCG_REG_AT) {
        tcg_abort();
    }

    tcg_out_opc_sa(s, OPC_SLL, ret, arg, 24);

    tcg_out_opc_sa(s, OPC_SRL, TCG_REG_AT, arg, 24);
    tcg_out_opc_reg(s, OPC_OR, ret, ret, TCG_REG_AT);

    tcg_out_opc_imm(s, OPC_ANDI, TCG_REG_AT, arg, 0xff00);
    tcg_out_opc_sa(s, OPC_SLL, TCG_REG_AT, TCG_REG_AT, 8);
    tcg_out_opc_reg(s, OPC_OR, ret, ret, TCG_REG_AT);

    tcg_out_opc_sa(s, OPC_SRL, TCG_REG_AT, arg, 8);
    tcg_out_opc_imm(s, OPC_ANDI, TCG_REG_AT, TCG_REG_AT, 0xff00);
    tcg_out_opc_reg(s, OPC_OR, ret, ret, TCG_REG_AT);
}

static inline void tcg_out_ext8s(TCGContext *s, int ret, int arg)
{
#ifdef _MIPS_ARCH_MIPS32R2
    tcg_out_opc_reg(s, OPC_SEB, ret, 0, arg);
#else
    tcg_out_opc_sa(s, OPC_SLL, ret, arg, 24);
    tcg_out_opc_sa(s, OPC_SRA, ret, ret, 24);
#endif
}

static inline void tcg_out_ext16s(TCGContext *s, int ret, int arg)
{
#ifdef _MIPS_ARCH_MIPS32R2
    tcg_out_opc_reg(s, OPC_SEH, ret, 0, arg);
#else
    tcg_out_opc_sa(s, OPC_SLL, ret, arg, 16);
    tcg_out_opc_sa(s, OPC_SRA, ret, ret, 16);
#endif
}

static inline void tcg_out_ldst(TCGContext *s, int opc, int arg,
                              int arg1, tcg_target_long arg2)
{
    if (arg2 == (int16_t) arg2) {
        tcg_out_opc_imm(s, opc, arg, arg1, arg2);
    } else {
        tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_AT, arg2);
        tcg_out_opc_reg(s, OPC_ADDU, TCG_REG_AT, TCG_REG_AT, arg1);
        tcg_out_opc_imm(s, opc, arg, TCG_REG_AT, 0);
    }
}

static inline void tcg_out_ld(TCGContext *s, TCGType type, int arg,
                              int arg1, tcg_target_long arg2)
{
    tcg_out_ldst(s, OPC_LW, arg, arg1, arg2);
}

static inline void tcg_out_st(TCGContext *s, TCGType type, int arg,
                              int arg1, tcg_target_long arg2)
{
    tcg_out_ldst(s, OPC_SW, arg, arg1, arg2);
}

static inline void tcg_out_addi(TCGContext *s, int reg, tcg_target_long val)
{
    if (val == (int16_t)val) {
        tcg_out_opc_imm(s, OPC_ADDIU, reg, reg, val);
    } else {
        tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_AT, val);
        tcg_out_opc_reg(s, OPC_ADDU, reg, reg, TCG_REG_AT);
    }
}

static void tcg_out_brcond(TCGContext *s, TCGCond cond, int arg1,
                           int arg2, int label_index)
{
    TCGLabel *l = &s->labels[label_index];

    switch (cond) {
    case TCG_COND_EQ:
        tcg_out_opc_br(s, OPC_BEQ, arg1, arg2);
        break;
    case TCG_COND_NE:
        tcg_out_opc_br(s, OPC_BNE, arg1, arg2);
        break;
    case TCG_COND_LT:
        tcg_out_opc_reg(s, OPC_SLT, TCG_REG_AT, arg1, arg2);
        tcg_out_opc_br(s, OPC_BNE, TCG_REG_AT, TCG_REG_ZERO);
        break;
    case TCG_COND_LTU:
        tcg_out_opc_reg(s, OPC_SLTU, TCG_REG_AT, arg1, arg2);
        tcg_out_opc_br(s, OPC_BNE, TCG_REG_AT, TCG_REG_ZERO);
        break;
    case TCG_COND_GE:
        tcg_out_opc_reg(s, OPC_SLT, TCG_REG_AT, arg1, arg2);
        tcg_out_opc_br(s, OPC_BEQ, TCG_REG_AT, TCG_REG_ZERO);
        break;
    case TCG_COND_GEU:
        tcg_out_opc_reg(s, OPC_SLTU, TCG_REG_AT, arg1, arg2);
        tcg_out_opc_br(s, OPC_BEQ, TCG_REG_AT, TCG_REG_ZERO);
        break;
    case TCG_COND_LE:
        tcg_out_opc_reg(s, OPC_SLT, TCG_REG_AT, arg2, arg1);
        tcg_out_opc_br(s, OPC_BEQ, TCG_REG_AT, TCG_REG_ZERO);
        break;
    case TCG_COND_LEU:
        tcg_out_opc_reg(s, OPC_SLTU, TCG_REG_AT, arg2, arg1);
        tcg_out_opc_br(s, OPC_BEQ, TCG_REG_AT, TCG_REG_ZERO);
        break;
    case TCG_COND_GT:
        tcg_out_opc_reg(s, OPC_SLT, TCG_REG_AT, arg2, arg1);
        tcg_out_opc_br(s, OPC_BNE, TCG_REG_AT, TCG_REG_ZERO);
        break;
    case TCG_COND_GTU:
        tcg_out_opc_reg(s, OPC_SLTU, TCG_REG_AT, arg2, arg1);
        tcg_out_opc_br(s, OPC_BNE, TCG_REG_AT, TCG_REG_ZERO);
        break;
    default:
        tcg_abort();
        break;
    }
    if (l->has_value) {
        reloc_pc16(s->code_ptr - 4, l->u.value);
    } else {
        tcg_out_reloc(s, s->code_ptr - 4, R_MIPS_PC16, label_index, 0);
    }
    tcg_out_nop(s);
}

/* XXX: we implement it at the target level to avoid having to
   handle cross basic blocks temporaries */
static void tcg_out_brcond2(TCGContext *s, TCGCond cond, int arg1,
                            int arg2, int arg3, int arg4, int label_index)
{
    void *label_ptr;

    switch(cond) {
    case TCG_COND_NE:
        tcg_out_brcond(s, TCG_COND_NE, arg2, arg4, label_index);
        tcg_out_brcond(s, TCG_COND_NE, arg1, arg3, label_index);
        return;
    case TCG_COND_EQ:
        break;
    case TCG_COND_LT:
    case TCG_COND_LE:
        tcg_out_brcond(s, TCG_COND_LT, arg2, arg4, label_index);
        break;
    case TCG_COND_GT:
    case TCG_COND_GE:
        tcg_out_brcond(s, TCG_COND_GT, arg2, arg4, label_index);
        break;
    case TCG_COND_LTU:
    case TCG_COND_LEU:
        tcg_out_brcond(s, TCG_COND_LTU, arg2, arg4, label_index);
        break;
    case TCG_COND_GTU:
    case TCG_COND_GEU:
        tcg_out_brcond(s, TCG_COND_GTU, arg2, arg4, label_index);
        break;
    default:
        tcg_abort();
    }

    label_ptr = s->code_ptr;
    tcg_out_opc_br(s, OPC_BNE, arg2, arg4);
    tcg_out_nop(s);

    switch(cond) {
    case TCG_COND_EQ:
        tcg_out_brcond(s, TCG_COND_EQ, arg1, arg3, label_index);
        break;
    case TCG_COND_LT:
    case TCG_COND_LTU:
        tcg_out_brcond(s, TCG_COND_LTU, arg1, arg3, label_index);
        break;
    case TCG_COND_LE:
    case TCG_COND_LEU:
        tcg_out_brcond(s, TCG_COND_LEU, arg1, arg3, label_index);
        break;
    case TCG_COND_GT:
    case TCG_COND_GTU:
        tcg_out_brcond(s, TCG_COND_GTU, arg1, arg3, label_index);
        break;
    case TCG_COND_GE:
    case TCG_COND_GEU:
        tcg_out_brcond(s, TCG_COND_GEU, arg1, arg3, label_index);
        break;
    default:
        tcg_abort();
    }

    reloc_pc16(label_ptr, (tcg_target_long) s->code_ptr);
}

static void tcg_out_setcond(TCGContext *s, TCGCond cond, int ret,
                            int arg1, int arg2)
{
    switch (cond) {
    case TCG_COND_EQ:
        if (arg1 == 0) {
            tcg_out_opc_imm(s, OPC_SLTIU, ret, arg2, 1);
        } else if (arg2 == 0) {
            tcg_out_opc_imm(s, OPC_SLTIU, ret, arg1, 1);
        } else {
            tcg_out_opc_reg(s, OPC_XOR, ret, arg1, arg2);
            tcg_out_opc_imm(s, OPC_SLTIU, ret, ret, 1);
        }
        break;
    case TCG_COND_NE:
        if (arg1 == 0) {
            tcg_out_opc_reg(s, OPC_SLTU, ret, TCG_REG_ZERO, arg2);
        } else if (arg2 == 0) {
            tcg_out_opc_reg(s, OPC_SLTU, ret, TCG_REG_ZERO, arg1);
        } else {
            tcg_out_opc_reg(s, OPC_XOR, ret, arg1, arg2);
            tcg_out_opc_reg(s, OPC_SLTU, ret, TCG_REG_ZERO, ret);
        }
        break;
    case TCG_COND_LT:
        tcg_out_opc_reg(s, OPC_SLT, ret, arg1, arg2);
        break;
    case TCG_COND_LTU:
        tcg_out_opc_reg(s, OPC_SLTU, ret, arg1, arg2);
        break;
    case TCG_COND_GE:
        tcg_out_opc_reg(s, OPC_SLT, ret, arg1, arg2);
        tcg_out_opc_imm(s, OPC_XORI, ret, ret, 1);
        break;
    case TCG_COND_GEU:
        tcg_out_opc_reg(s, OPC_SLTU, ret, arg1, arg2);
        tcg_out_opc_imm(s, OPC_XORI, ret, ret, 1);
        break;
    case TCG_COND_LE:
        tcg_out_opc_reg(s, OPC_SLT, ret, arg2, arg1);
        tcg_out_opc_imm(s, OPC_XORI, ret, ret, 1);
        break;
    case TCG_COND_LEU:
        tcg_out_opc_reg(s, OPC_SLTU, ret, arg2, arg1);
        tcg_out_opc_imm(s, OPC_XORI, ret, ret, 1);
        break;
    case TCG_COND_GT:
        tcg_out_opc_reg(s, OPC_SLT, ret, arg2, arg1);
        break;
    case TCG_COND_GTU:
        tcg_out_opc_reg(s, OPC_SLTU, ret, arg2, arg1);
        break;
    default:
        tcg_abort();
        break;
    }
}

/* XXX: we implement it at the target level to avoid having to
   handle cross basic blocks temporaries */
static void tcg_out_setcond2(TCGContext *s, TCGCond cond, int ret,
                             int arg1, int arg2, int arg3, int arg4)
{
    switch (cond) {
    case TCG_COND_EQ:
        tcg_out_setcond(s, TCG_COND_EQ, TCG_REG_AT, arg2, arg4);
        tcg_out_setcond(s, TCG_COND_EQ, TCG_REG_T0, arg1, arg3);
        tcg_out_opc_reg(s, OPC_AND, ret, TCG_REG_AT, TCG_REG_T0);
        return;
    case TCG_COND_NE:
        tcg_out_setcond(s, TCG_COND_NE, TCG_REG_AT, arg2, arg4);
        tcg_out_setcond(s, TCG_COND_NE, TCG_REG_T0, arg1, arg3);
        tcg_out_opc_reg(s, OPC_OR, ret, TCG_REG_AT, TCG_REG_T0);
        return;
    case TCG_COND_LT:
    case TCG_COND_LE:
        tcg_out_setcond(s, TCG_COND_LT, TCG_REG_AT, arg2, arg4);
        break;
    case TCG_COND_GT:
    case TCG_COND_GE:
        tcg_out_setcond(s, TCG_COND_GT, TCG_REG_AT, arg2, arg4);
        break;
    case TCG_COND_LTU:
    case TCG_COND_LEU:
        tcg_out_setcond(s, TCG_COND_LTU, TCG_REG_AT, arg2, arg4);
        break;
    case TCG_COND_GTU:
    case TCG_COND_GEU:
        tcg_out_setcond(s, TCG_COND_GTU, TCG_REG_AT, arg2, arg4);
        break;
    default:
        tcg_abort();
        break;
    }

    tcg_out_setcond(s, TCG_COND_EQ, TCG_REG_T0, arg2, arg4);

    switch(cond) {
    case TCG_COND_LT:
    case TCG_COND_LTU:
        tcg_out_setcond(s, TCG_COND_LTU, ret, arg1, arg3);
        break;
    case TCG_COND_LE:
    case TCG_COND_LEU:
        tcg_out_setcond(s, TCG_COND_LEU, ret, arg1, arg3);
        break;
    case TCG_COND_GT:
    case TCG_COND_GTU:
        tcg_out_setcond(s, TCG_COND_GTU, ret, arg1, arg3);
        break;
    case TCG_COND_GE:
    case TCG_COND_GEU:
        tcg_out_setcond(s, TCG_COND_GEU, ret, arg1, arg3);
        break;
    default:
        tcg_abort();
    }

    tcg_out_opc_reg(s, OPC_AND, ret, ret, TCG_REG_T0);
    tcg_out_opc_reg(s, OPC_OR, ret, ret, TCG_REG_AT);
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

static void tcg_out_qemu_ld(TCGContext *s, const TCGArg *args,
                            int opc)
{
    int addr_regl, addr_reg1, addr_meml;
    int data_regl, data_regh, data_reg1, data_reg2;
    int mem_index, s_bits;
#if defined(CONFIG_SOFTMMU)
    void *label1_ptr, *label2_ptr;
    int sp_args;
#endif
#if TARGET_LONG_BITS == 64
# if defined(CONFIG_SOFTMMU)
    uint8_t *label3_ptr;
# endif
    int addr_regh, addr_reg2, addr_memh;
#endif
    data_regl = *args++;
    if (opc == 3)
        data_regh = *args++;
    else
        data_regh = 0;
    addr_regl = *args++;
#if TARGET_LONG_BITS == 64
    addr_regh = *args++;
#endif
    mem_index = *args;
    s_bits = opc & 3;

    if (opc == 3) {
#if defined(TCG_TARGET_WORDS_BIGENDIAN)
        data_reg1 = data_regh;
        data_reg2 = data_regl;
#else
        data_reg1 = data_regl;
        data_reg2 = data_regh;
#endif
    } else {
        data_reg1 = data_regl;
        data_reg2 = 0;
    }
#if TARGET_LONG_BITS == 64
# if defined(TCG_TARGET_WORDS_BIGENDIAN)
    addr_reg1 = addr_regh;
    addr_reg2 = addr_regl;
    addr_memh = 0;
    addr_meml = 4;
# else
    addr_reg1 = addr_regl;
    addr_reg2 = addr_regh;
    addr_memh = 4;
    addr_meml = 0;
# endif
#else
    addr_reg1 = addr_regl;
    addr_meml = 0;
#endif

#if defined(CONFIG_SOFTMMU)
    tcg_out_opc_sa(s, OPC_SRL, TCG_REG_A0, addr_regl, TARGET_PAGE_BITS - CPU_TLB_ENTRY_BITS);
    tcg_out_opc_imm(s, OPC_ANDI, TCG_REG_A0, TCG_REG_A0, (CPU_TLB_SIZE - 1) << CPU_TLB_ENTRY_BITS);
    tcg_out_opc_reg(s, OPC_ADDU, TCG_REG_A0, TCG_REG_A0, TCG_AREG0);
    tcg_out_opc_imm(s, OPC_LW, TCG_REG_AT, TCG_REG_A0,
                    offsetof(CPUState, tlb_table[mem_index][0].addr_read) + addr_meml);
    tcg_out_movi(s, TCG_TYPE_I32, TCG_REG_T0, TARGET_PAGE_MASK | ((1 << s_bits) - 1));
    tcg_out_opc_reg(s, OPC_AND, TCG_REG_T0, TCG_REG_T0, addr_regl);

# if TARGET_LONG_BITS == 64
    label3_ptr = s->code_ptr;
    tcg_out_opc_br(s, OPC_BNE, TCG_REG_T0, TCG_REG_AT);
    tcg_out_nop(s);

    tcg_out_opc_imm(s, OPC_LW, TCG_REG_AT, TCG_REG_A0,
                    offsetof(CPUState, tlb_table[mem_index][0].addr_read) + addr_memh);

    label1_ptr = s->code_ptr;
    tcg_out_opc_br(s, OPC_BEQ, addr_regh, TCG_REG_AT);
    tcg_out_nop(s);

    reloc_pc16(label3_ptr, (tcg_target_long) s->code_ptr);
# else
    label1_ptr = s->code_ptr;
    tcg_out_opc_br(s, OPC_BEQ, TCG_REG_T0, TCG_REG_AT);
    tcg_out_nop(s);
# endif

    /* slow path */
    sp_args = TCG_REG_A0;
    tcg_out_mov(s, TCG_TYPE_I32, sp_args++, addr_reg1);
# if TARGET_LONG_BITS == 64
    tcg_out_mov(s, TCG_TYPE_I32, sp_args++, addr_reg2);
# endif
    tcg_out_movi(s, TCG_TYPE_I32, sp_args++, mem_index);
    tcg_out_movi(s, TCG_TYPE_I32, TCG_REG_T9, (tcg_target_long)qemu_ld_helpers[s_bits]);
    tcg_out_opc_reg(s, OPC_JALR, TCG_REG_RA, TCG_REG_T9, 0);
    tcg_out_nop(s);

    switch(opc) {
    case 0:
        tcg_out_opc_imm(s, OPC_ANDI, data_reg1, TCG_REG_V0, 0xff);
        break;
    case 0 | 4:
        tcg_out_ext8s(s, data_reg1, TCG_REG_V0);
        break;
    case 1:
        tcg_out_opc_imm(s, OPC_ANDI, data_reg1, TCG_REG_V0, 0xffff);
        break;
    case 1 | 4:
        tcg_out_ext16s(s, data_reg1, TCG_REG_V0);
        break;
    case 2:
        tcg_out_mov(s, TCG_TYPE_I32, data_reg1, TCG_REG_V0);
        break;
    case 3:
        tcg_out_mov(s, TCG_TYPE_I32, data_reg2, TCG_REG_V1);
        tcg_out_mov(s, TCG_TYPE_I32, data_reg1, TCG_REG_V0);
        break;
    default:
        tcg_abort();
    }

    label2_ptr = s->code_ptr;
    tcg_out_opc_br(s, OPC_BEQ, TCG_REG_ZERO, TCG_REG_ZERO);
    tcg_out_nop(s);

    /* label1: fast path */
    reloc_pc16(label1_ptr, (tcg_target_long) s->code_ptr);

    tcg_out_opc_imm(s, OPC_LW, TCG_REG_A0, TCG_REG_A0,
                    offsetof(CPUState, tlb_table[mem_index][0].addend));
    tcg_out_opc_reg(s, OPC_ADDU, TCG_REG_V0, TCG_REG_A0, addr_regl);
#else
    if (GUEST_BASE == (int16_t)GUEST_BASE) {
        tcg_out_opc_imm(s, OPC_ADDIU, TCG_REG_V0, addr_regl, GUEST_BASE);
    } else {
        tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_V0, GUEST_BASE);
        tcg_out_opc_reg(s, OPC_ADDU, TCG_REG_V0, TCG_REG_V0, addr_regl);
    }
#endif

    switch(opc) {
    case 0:
        tcg_out_opc_imm(s, OPC_LBU, data_reg1, TCG_REG_V0, 0);
        break;
    case 0 | 4:
        tcg_out_opc_imm(s, OPC_LB, data_reg1, TCG_REG_V0, 0);
        break;
    case 1:
        if (TCG_NEED_BSWAP) {
            tcg_out_opc_imm(s, OPC_LHU, TCG_REG_T0, TCG_REG_V0, 0);
            tcg_out_bswap16(s, data_reg1, TCG_REG_T0);
        } else {
            tcg_out_opc_imm(s, OPC_LHU, data_reg1, TCG_REG_V0, 0);
        }
        break;
    case 1 | 4:
        if (TCG_NEED_BSWAP) {
            tcg_out_opc_imm(s, OPC_LHU, TCG_REG_T0, TCG_REG_V0, 0);
            tcg_out_bswap16s(s, data_reg1, TCG_REG_T0);
        } else {
            tcg_out_opc_imm(s, OPC_LH, data_reg1, TCG_REG_V0, 0);
        }
        break;
    case 2:
        if (TCG_NEED_BSWAP) {
            tcg_out_opc_imm(s, OPC_LW, TCG_REG_T0, TCG_REG_V0, 0);
            tcg_out_bswap32(s, data_reg1, TCG_REG_T0);
        } else {
            tcg_out_opc_imm(s, OPC_LW, data_reg1, TCG_REG_V0, 0);
        }
        break;
    case 3:
        if (TCG_NEED_BSWAP) {
            tcg_out_opc_imm(s, OPC_LW, TCG_REG_T0, TCG_REG_V0, 4);
            tcg_out_bswap32(s, data_reg1, TCG_REG_T0);
            tcg_out_opc_imm(s, OPC_LW, TCG_REG_T0, TCG_REG_V0, 0);
            tcg_out_bswap32(s, data_reg2, TCG_REG_T0);
        } else {
            tcg_out_opc_imm(s, OPC_LW, data_reg1, TCG_REG_V0, 0);
            tcg_out_opc_imm(s, OPC_LW, data_reg2, TCG_REG_V0, 4);
        }
        break;
    default:
        tcg_abort();
    }

#if defined(CONFIG_SOFTMMU)
    reloc_pc16(label2_ptr, (tcg_target_long) s->code_ptr);
#endif
}

static void tcg_out_qemu_st(TCGContext *s, const TCGArg *args,
                            int opc)
{
    int addr_regl, addr_reg1, addr_meml;
    int data_regl, data_regh, data_reg1, data_reg2;
    int mem_index, s_bits;
#if defined(CONFIG_SOFTMMU)
    uint8_t *label1_ptr, *label2_ptr;
    int sp_args;
#endif
#if TARGET_LONG_BITS == 64
# if defined(CONFIG_SOFTMMU)
    uint8_t *label3_ptr;
# endif
    int addr_regh, addr_reg2, addr_memh;
#endif

    data_regl = *args++;
    if (opc == 3) {
        data_regh = *args++;
#if defined(TCG_TARGET_WORDS_BIGENDIAN)
        data_reg1 = data_regh;
        data_reg2 = data_regl;
#else
        data_reg1 = data_regl;
        data_reg2 = data_regh;
#endif
    } else {
        data_reg1 = data_regl;
        data_reg2 = 0;
        data_regh = 0;
    }
    addr_regl = *args++;
#if TARGET_LONG_BITS == 64
    addr_regh = *args++;
# if defined(TCG_TARGET_WORDS_BIGENDIAN)
    addr_reg1 = addr_regh;
    addr_reg2 = addr_regl;
    addr_memh = 0;
    addr_meml = 4;
# else
    addr_reg1 = addr_regl;
    addr_reg2 = addr_regh;
    addr_memh = 4;
    addr_meml = 0;
# endif
#else
    addr_reg1 = addr_regl;
    addr_meml = 0;
#endif
    mem_index = *args;
    s_bits = opc;

#if defined(CONFIG_SOFTMMU)
    tcg_out_opc_sa(s, OPC_SRL, TCG_REG_A0, addr_regl, TARGET_PAGE_BITS - CPU_TLB_ENTRY_BITS);
    tcg_out_opc_imm(s, OPC_ANDI, TCG_REG_A0, TCG_REG_A0, (CPU_TLB_SIZE - 1) << CPU_TLB_ENTRY_BITS);
    tcg_out_opc_reg(s, OPC_ADDU, TCG_REG_A0, TCG_REG_A0, TCG_AREG0);
    tcg_out_opc_imm(s, OPC_LW, TCG_REG_AT, TCG_REG_A0,
                    offsetof(CPUState, tlb_table[mem_index][0].addr_write) + addr_meml);
    tcg_out_movi(s, TCG_TYPE_I32, TCG_REG_T0, TARGET_PAGE_MASK | ((1 << s_bits) - 1));
    tcg_out_opc_reg(s, OPC_AND, TCG_REG_T0, TCG_REG_T0, addr_regl);

# if TARGET_LONG_BITS == 64
    label3_ptr = s->code_ptr;
    tcg_out_opc_br(s, OPC_BNE, TCG_REG_T0, TCG_REG_AT);
    tcg_out_nop(s);

    tcg_out_opc_imm(s, OPC_LW, TCG_REG_AT, TCG_REG_A0,
                    offsetof(CPUState, tlb_table[mem_index][0].addr_write) + addr_memh);

    label1_ptr = s->code_ptr;
    tcg_out_opc_br(s, OPC_BEQ, addr_regh, TCG_REG_AT);
    tcg_out_nop(s);

    reloc_pc16(label3_ptr, (tcg_target_long) s->code_ptr);
# else
    label1_ptr = s->code_ptr;
    tcg_out_opc_br(s, OPC_BEQ, TCG_REG_T0, TCG_REG_AT);
    tcg_out_nop(s);
# endif

    /* slow path */
    sp_args = TCG_REG_A0;
    tcg_out_mov(s, TCG_TYPE_I32, sp_args++, addr_reg1);
# if TARGET_LONG_BITS == 64
    tcg_out_mov(s, TCG_TYPE_I32, sp_args++, addr_reg2);
# endif
    switch(opc) {
    case 0:
        tcg_out_opc_imm(s, OPC_ANDI, sp_args++, data_reg1, 0xff);
        break;
    case 1:
        tcg_out_opc_imm(s, OPC_ANDI, sp_args++, data_reg1, 0xffff);
        break;
    case 2:
        tcg_out_mov(s, TCG_TYPE_I32, sp_args++, data_reg1);
        break;
    case 3:
        sp_args = (sp_args + 1) & ~1;
        tcg_out_mov(s, TCG_TYPE_I32, sp_args++, data_reg1);
        tcg_out_mov(s, TCG_TYPE_I32, sp_args++, data_reg2);
        break;
    default:
        tcg_abort();
    }
    if (sp_args > TCG_REG_A3) {
        /* Push mem_index on the stack */
        tcg_out_movi(s, TCG_TYPE_I32, TCG_REG_AT, mem_index);
        tcg_out_st(s, TCG_TYPE_I32, TCG_REG_AT, TCG_REG_SP, 16);
    } else {
        tcg_out_movi(s, TCG_TYPE_I32, sp_args, mem_index);
    }

    tcg_out_movi(s, TCG_TYPE_I32, TCG_REG_T9, (tcg_target_long)qemu_st_helpers[s_bits]);
    tcg_out_opc_reg(s, OPC_JALR, TCG_REG_RA, TCG_REG_T9, 0);
    tcg_out_nop(s);

    label2_ptr = s->code_ptr;
    tcg_out_opc_br(s, OPC_BEQ, TCG_REG_ZERO, TCG_REG_ZERO);
    tcg_out_nop(s);

    /* label1: fast path */
    reloc_pc16(label1_ptr, (tcg_target_long) s->code_ptr);

    tcg_out_opc_imm(s, OPC_LW, TCG_REG_A0, TCG_REG_A0,
                    offsetof(CPUState, tlb_table[mem_index][0].addend));
    tcg_out_opc_reg(s, OPC_ADDU, TCG_REG_A0, TCG_REG_A0, addr_regl);
#else
    if (GUEST_BASE == (int16_t)GUEST_BASE) {
        tcg_out_opc_imm(s, OPC_ADDIU, TCG_REG_A0, addr_regl, GUEST_BASE);
    } else {
        tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_A0, GUEST_BASE);
        tcg_out_opc_reg(s, OPC_ADDU, TCG_REG_A0, TCG_REG_A0, addr_regl);
    }

#endif

    switch(opc) {
    case 0:
        tcg_out_opc_imm(s, OPC_SB, data_reg1, TCG_REG_A0, 0);
        break;
    case 1:
        if (TCG_NEED_BSWAP) {
            tcg_out_bswap16(s, TCG_REG_T0, data_reg1);
            tcg_out_opc_imm(s, OPC_SH, TCG_REG_T0, TCG_REG_A0, 0);
        } else {
            tcg_out_opc_imm(s, OPC_SH, data_reg1, TCG_REG_A0, 0);
        }
        break;
    case 2:
        if (TCG_NEED_BSWAP) {
            tcg_out_bswap32(s, TCG_REG_T0, data_reg1);
            tcg_out_opc_imm(s, OPC_SW, TCG_REG_T0, TCG_REG_A0, 0);
        } else {
            tcg_out_opc_imm(s, OPC_SW, data_reg1, TCG_REG_A0, 0);
        }
        break;
    case 3:
        if (TCG_NEED_BSWAP) {
            tcg_out_bswap32(s, TCG_REG_T0, data_reg2);
            tcg_out_opc_imm(s, OPC_SW, TCG_REG_T0, TCG_REG_A0, 0);
            tcg_out_bswap32(s, TCG_REG_T0, data_reg1);
            tcg_out_opc_imm(s, OPC_SW, TCG_REG_T0, TCG_REG_A0, 4);
        } else {
            tcg_out_opc_imm(s, OPC_SW, data_reg1, TCG_REG_A0, 0);
            tcg_out_opc_imm(s, OPC_SW, data_reg2, TCG_REG_A0, 4);
        }
        break;
    default:
        tcg_abort();
    }

#if defined(CONFIG_SOFTMMU)
    reloc_pc16(label2_ptr, (tcg_target_long) s->code_ptr);
#endif
}

static inline void tcg_out_op(TCGContext *s, TCGOpcode opc,
                              const TCGArg *args, const int *const_args)
{
    switch(opc) {
    case INDEX_op_exit_tb:
        tcg_out_movi(s, TCG_TYPE_I32, TCG_REG_V0, args[0]);
        tcg_out_movi(s, TCG_TYPE_I32, TCG_REG_AT, (tcg_target_long)tb_ret_addr);
        tcg_out_opc_reg(s, OPC_JR, 0, TCG_REG_AT, 0);
        tcg_out_nop(s);
        break;
    case INDEX_op_goto_tb:
        if (s->tb_jmp_offset) {
            /* direct jump method */
            tcg_abort();
        } else {
            /* indirect jump method */
            tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_AT, (tcg_target_long)(s->tb_next + args[0]));
            tcg_out_ld(s, TCG_TYPE_PTR, TCG_REG_AT, TCG_REG_AT, 0);
            tcg_out_opc_reg(s, OPC_JR, 0, TCG_REG_AT, 0);
        }
        tcg_out_nop(s);
        s->tb_next_offset[args[0]] = s->code_ptr - s->code_buf;
        break;
    case INDEX_op_call:
        tcg_out_opc_reg(s, OPC_JALR, TCG_REG_RA, args[0], 0);
        tcg_out_nop(s);
        break;
    case INDEX_op_jmp:
        tcg_out_opc_reg(s, OPC_JR, 0, args[0], 0);
        tcg_out_nop(s);
        break;
    case INDEX_op_br:
        tcg_out_brcond(s, TCG_COND_EQ, TCG_REG_ZERO, TCG_REG_ZERO, args[0]);
        break;

    case INDEX_op_mov_i32:
        tcg_out_mov(s, TCG_TYPE_I32, args[0], args[1]);
        break;
    case INDEX_op_movi_i32:
        tcg_out_movi(s, TCG_TYPE_I32, args[0], args[1]);
        break;

    case INDEX_op_ld8u_i32:
        tcg_out_ldst(s, OPC_LBU, args[0], args[1], args[2]);
        break;
    case INDEX_op_ld8s_i32:
        tcg_out_ldst(s, OPC_LB, args[0], args[1], args[2]);
        break;
    case INDEX_op_ld16u_i32:
        tcg_out_ldst(s, OPC_LHU, args[0], args[1], args[2]);
        break;
    case INDEX_op_ld16s_i32:
        tcg_out_ldst(s, OPC_LH, args[0], args[1], args[2]);
        break;
    case INDEX_op_ld_i32:
        tcg_out_ldst(s, OPC_LW, args[0], args[1], args[2]);
        break;
    case INDEX_op_st8_i32:
        tcg_out_ldst(s, OPC_SB, args[0], args[1], args[2]);
        break;
    case INDEX_op_st16_i32:
        tcg_out_ldst(s, OPC_SH, args[0], args[1], args[2]);
        break;
    case INDEX_op_st_i32:
        tcg_out_ldst(s, OPC_SW, args[0], args[1], args[2]);
        break;

    case INDEX_op_add_i32:
        if (const_args[2]) {
            tcg_out_opc_imm(s, OPC_ADDIU, args[0], args[1], args[2]);
        } else {
            tcg_out_opc_reg(s, OPC_ADDU, args[0], args[1], args[2]);
        }
        break;
    case INDEX_op_add2_i32:
        if (const_args[4]) {
            tcg_out_opc_imm(s, OPC_ADDIU, TCG_REG_AT, args[2], args[4]);
        } else {
            tcg_out_opc_reg(s, OPC_ADDU, TCG_REG_AT, args[2], args[4]);
        }
        tcg_out_opc_reg(s, OPC_SLTU, TCG_REG_T0, TCG_REG_AT, args[2]);
        if (const_args[5]) {
            tcg_out_opc_imm(s, OPC_ADDIU, args[1], args[3], args[5]);
        } else {
             tcg_out_opc_reg(s, OPC_ADDU, args[1], args[3], args[5]);
        }
        tcg_out_opc_reg(s, OPC_ADDU, args[1], args[1], TCG_REG_T0);
        tcg_out_mov(s, TCG_TYPE_I32, args[0], TCG_REG_AT);
        break;
    case INDEX_op_sub_i32:
        if (const_args[2]) {
            tcg_out_opc_imm(s, OPC_ADDIU, args[0], args[1], -args[2]);
        } else {
            tcg_out_opc_reg(s, OPC_SUBU, args[0], args[1], args[2]);
        }
        break;
    case INDEX_op_sub2_i32:
        if (const_args[4]) {
            tcg_out_opc_imm(s, OPC_ADDIU, TCG_REG_AT, args[2], -args[4]);
        } else {
            tcg_out_opc_reg(s, OPC_SUBU, TCG_REG_AT, args[2], args[4]);
        }
        tcg_out_opc_reg(s, OPC_SLTU, TCG_REG_T0, args[2], TCG_REG_AT);
        if (const_args[5]) {
            tcg_out_opc_imm(s, OPC_ADDIU, args[1], args[3], -args[5]);
        } else {
             tcg_out_opc_reg(s, OPC_SUBU, args[1], args[3], args[5]);
        }
        tcg_out_opc_reg(s, OPC_SUBU, args[1], args[1], TCG_REG_T0);
        tcg_out_mov(s, TCG_TYPE_I32, args[0], TCG_REG_AT);
        break;
    case INDEX_op_mul_i32:
        tcg_out_opc_reg(s, OPC_MULT, 0, args[1], args[2]);
        tcg_out_opc_reg(s, OPC_MFLO, args[0], 0, 0);
        break;
    case INDEX_op_mulu2_i32:
        tcg_out_opc_reg(s, OPC_MULTU, 0, args[2], args[3]);
        tcg_out_opc_reg(s, OPC_MFLO, args[0], 0, 0);
        tcg_out_opc_reg(s, OPC_MFHI, args[1], 0, 0);
        break;
    case INDEX_op_div_i32:
        tcg_out_opc_reg(s, OPC_DIV, 0, args[1], args[2]);
        tcg_out_opc_reg(s, OPC_MFLO, args[0], 0, 0);
        break;
    case INDEX_op_divu_i32:
        tcg_out_opc_reg(s, OPC_DIVU, 0, args[1], args[2]);
        tcg_out_opc_reg(s, OPC_MFLO, args[0], 0, 0);
        break;
    case INDEX_op_rem_i32:
        tcg_out_opc_reg(s, OPC_DIV, 0, args[1], args[2]);
        tcg_out_opc_reg(s, OPC_MFHI, args[0], 0, 0);
        break;
    case INDEX_op_remu_i32:
        tcg_out_opc_reg(s, OPC_DIVU, 0, args[1], args[2]);
        tcg_out_opc_reg(s, OPC_MFHI, args[0], 0, 0);
        break;

    case INDEX_op_and_i32:
        if (const_args[2]) {
            tcg_out_opc_imm(s, OPC_ANDI, args[0], args[1], args[2]);
        } else {
            tcg_out_opc_reg(s, OPC_AND, args[0], args[1], args[2]);
        }
        break;
    case INDEX_op_or_i32:
        if (const_args[2]) {
            tcg_out_opc_imm(s, OPC_ORI, args[0], args[1], args[2]);
        } else {
            tcg_out_opc_reg(s, OPC_OR, args[0], args[1], args[2]);
        }
        break;
    case INDEX_op_nor_i32:
        tcg_out_opc_reg(s, OPC_NOR, args[0], args[1], args[2]);
        break;
    case INDEX_op_not_i32:
        tcg_out_opc_reg(s, OPC_NOR, args[0], TCG_REG_ZERO, args[1]);
        break;
    case INDEX_op_xor_i32:
        if (const_args[2]) {
            tcg_out_opc_imm(s, OPC_XORI, args[0], args[1], args[2]);
        } else {
            tcg_out_opc_reg(s, OPC_XOR, args[0], args[1], args[2]);
        }
        break;

    case INDEX_op_sar_i32:
        if (const_args[2]) {
            tcg_out_opc_sa(s, OPC_SRA, args[0], args[1], args[2]);
        } else {
            tcg_out_opc_reg(s, OPC_SRAV, args[0], args[2], args[1]);
        }
        break;
    case INDEX_op_shl_i32:
        if (const_args[2]) {
            tcg_out_opc_sa(s, OPC_SLL, args[0], args[1], args[2]);
        } else {
            tcg_out_opc_reg(s, OPC_SLLV, args[0], args[2], args[1]);
        }
        break;
    case INDEX_op_shr_i32:
        if (const_args[2]) {
            tcg_out_opc_sa(s, OPC_SRL, args[0], args[1], args[2]);
        } else {
            tcg_out_opc_reg(s, OPC_SRLV, args[0], args[2], args[1]);
        }
        break;

    case INDEX_op_ext8s_i32:
        tcg_out_ext8s(s, args[0], args[1]);
        break;
    case INDEX_op_ext16s_i32:
        tcg_out_ext16s(s, args[0], args[1]);
        break;

    case INDEX_op_brcond_i32:
        tcg_out_brcond(s, args[2], args[0], args[1], args[3]);
        break;
    case INDEX_op_brcond2_i32:
        tcg_out_brcond2(s, args[4], args[0], args[1], args[2], args[3], args[5]);
        break;

    case INDEX_op_setcond_i32:
        tcg_out_setcond(s, args[3], args[0], args[1], args[2]);
        break;
    case INDEX_op_setcond2_i32:
        tcg_out_setcond2(s, args[5], args[0], args[1], args[2], args[3], args[4]);
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
    case INDEX_op_qemu_ld32:
        tcg_out_qemu_ld(s, args, 2);
        break;
    case INDEX_op_qemu_ld64:
        tcg_out_qemu_ld(s, args, 3);
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
    case INDEX_op_qemu_st64:
        tcg_out_qemu_st(s, args, 3);
        break;

    default:
        tcg_abort();
    }
}

static const TCGTargetOpDef mips_op_defs[] = {
    { INDEX_op_exit_tb, { } },
    { INDEX_op_goto_tb, { } },
    { INDEX_op_call, { "C" } },
    { INDEX_op_jmp, { "r" } },
    { INDEX_op_br, { } },

    { INDEX_op_mov_i32, { "r", "r" } },
    { INDEX_op_movi_i32, { "r" } },
    { INDEX_op_ld8u_i32, { "r", "r" } },
    { INDEX_op_ld8s_i32, { "r", "r" } },
    { INDEX_op_ld16u_i32, { "r", "r" } },
    { INDEX_op_ld16s_i32, { "r", "r" } },
    { INDEX_op_ld_i32, { "r", "r" } },
    { INDEX_op_st8_i32, { "rZ", "r" } },
    { INDEX_op_st16_i32, { "rZ", "r" } },
    { INDEX_op_st_i32, { "rZ", "r" } },

    { INDEX_op_add_i32, { "r", "rZ", "rJZ" } },
    { INDEX_op_mul_i32, { "r", "rZ", "rZ" } },
    { INDEX_op_mulu2_i32, { "r", "r", "rZ", "rZ" } },
    { INDEX_op_div_i32, { "r", "rZ", "rZ" } },
    { INDEX_op_divu_i32, { "r", "rZ", "rZ" } },
    { INDEX_op_rem_i32, { "r", "rZ", "rZ" } },
    { INDEX_op_remu_i32, { "r", "rZ", "rZ" } },
    { INDEX_op_sub_i32, { "r", "rZ", "rJZ" } },

    { INDEX_op_and_i32, { "r", "rZ", "rIZ" } },
    { INDEX_op_nor_i32, { "r", "rZ", "rZ" } },
    { INDEX_op_not_i32, { "r", "rZ" } },
    { INDEX_op_or_i32, { "r", "rZ", "rIZ" } },
    { INDEX_op_xor_i32, { "r", "rZ", "rIZ" } },

    { INDEX_op_shl_i32, { "r", "rZ", "riZ" } },
    { INDEX_op_shr_i32, { "r", "rZ", "riZ" } },
    { INDEX_op_sar_i32, { "r", "rZ", "riZ" } },

    { INDEX_op_ext8s_i32, { "r", "rZ" } },
    { INDEX_op_ext16s_i32, { "r", "rZ" } },

    { INDEX_op_brcond_i32, { "rZ", "rZ" } },
    { INDEX_op_setcond_i32, { "r", "rZ", "rZ" } },
    { INDEX_op_setcond2_i32, { "r", "rZ", "rZ", "rZ", "rZ" } },

    { INDEX_op_add2_i32, { "r", "r", "rZ", "rZ", "rJZ", "rJZ" } },
    { INDEX_op_sub2_i32, { "r", "r", "rZ", "rZ", "rJZ", "rJZ" } },
    { INDEX_op_brcond2_i32, { "rZ", "rZ", "rZ", "rZ" } },

#if TARGET_LONG_BITS == 32
    { INDEX_op_qemu_ld8u, { "L", "lZ" } },
    { INDEX_op_qemu_ld8s, { "L", "lZ" } },
    { INDEX_op_qemu_ld16u, { "L", "lZ" } },
    { INDEX_op_qemu_ld16s, { "L", "lZ" } },
    { INDEX_op_qemu_ld32, { "L", "lZ" } },
    { INDEX_op_qemu_ld64, { "L", "L", "lZ" } },

    { INDEX_op_qemu_st8, { "SZ", "SZ" } },
    { INDEX_op_qemu_st16, { "SZ", "SZ" } },
    { INDEX_op_qemu_st32, { "SZ", "SZ" } },
    { INDEX_op_qemu_st64, { "SZ", "SZ", "SZ" } },
#else
    { INDEX_op_qemu_ld8u, { "L", "lZ", "lZ" } },
    { INDEX_op_qemu_ld8s, { "L", "lZ", "lZ" } },
    { INDEX_op_qemu_ld16u, { "L", "lZ", "lZ" } },
    { INDEX_op_qemu_ld16s, { "L", "lZ", "lZ" } },
    { INDEX_op_qemu_ld32, { "L", "lZ", "lZ" } },
    { INDEX_op_qemu_ld64, { "L", "L", "lZ", "lZ" } },

    { INDEX_op_qemu_st8, { "SZ", "SZ", "SZ" } },
    { INDEX_op_qemu_st16, { "SZ", "SZ", "SZ" } },
    { INDEX_op_qemu_st32, { "SZ", "SZ", "SZ" } },
    { INDEX_op_qemu_st64, { "SZ", "SZ", "SZ", "SZ" } },
#endif
    { -1 },
};

static int tcg_target_callee_save_regs[] = {
#if 0 /* used for the global env (TCG_AREG0), so no need to save */
    TCG_REG_S0,
#endif
    TCG_REG_S1,
    TCG_REG_S2,
    TCG_REG_S3,
    TCG_REG_S4,
    TCG_REG_S5,
    TCG_REG_S6,
    TCG_REG_S7,
    TCG_REG_GP,
    TCG_REG_FP,
    TCG_REG_RA,       /* should be last for ABI compliance */
};

/* Generate global QEMU prologue and epilogue code */
static void tcg_target_qemu_prologue(TCGContext *s)
{
    int i, frame_size;

    /* reserve some stack space */
    frame_size = ARRAY_SIZE(tcg_target_callee_save_regs) * 4
                 + TCG_STATIC_CALL_ARGS_SIZE;
    frame_size = (frame_size + TCG_TARGET_STACK_ALIGN - 1) &
                 ~(TCG_TARGET_STACK_ALIGN - 1);

    /* TB prologue */
    tcg_out_addi(s, TCG_REG_SP, -frame_size);
    for(i = 0 ; i < ARRAY_SIZE(tcg_target_callee_save_regs) ; i++) {
        tcg_out_st(s, TCG_TYPE_I32, tcg_target_callee_save_regs[i],
                   TCG_REG_SP, TCG_STATIC_CALL_ARGS_SIZE + i * 4);
    }

    /* Call generated code */
    tcg_out_opc_reg(s, OPC_JR, 0, TCG_REG_A0, 0);
    tcg_out_nop(s);
    tb_ret_addr = s->code_ptr;

    /* TB epilogue */
    for(i = 0 ; i < ARRAY_SIZE(tcg_target_callee_save_regs) ; i++) {
        tcg_out_ld(s, TCG_TYPE_I32, tcg_target_callee_save_regs[i],
                   TCG_REG_SP, TCG_STATIC_CALL_ARGS_SIZE + i * 4);
    }

    tcg_out_opc_reg(s, OPC_JR, 0, TCG_REG_RA, 0);
    tcg_out_addi(s, TCG_REG_SP, frame_size);
}

static void tcg_target_init(TCGContext *s)
{
    tcg_regset_set(tcg_target_available_regs[TCG_TYPE_I32], 0xffffffff);
    tcg_regset_set(tcg_target_call_clobber_regs,
                   (1 << TCG_REG_V0) |
                   (1 << TCG_REG_V1) |
                   (1 << TCG_REG_A0) |
                   (1 << TCG_REG_A1) |
                   (1 << TCG_REG_A2) |
                   (1 << TCG_REG_A3) |
                   (1 << TCG_REG_T1) |
                   (1 << TCG_REG_T2) |
                   (1 << TCG_REG_T3) |
                   (1 << TCG_REG_T4) |
                   (1 << TCG_REG_T5) |
                   (1 << TCG_REG_T6) |
                   (1 << TCG_REG_T7) |
                   (1 << TCG_REG_T8) |
                   (1 << TCG_REG_T9));

    tcg_regset_clear(s->reserved_regs);
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_ZERO); /* zero register */
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_K0);   /* kernel use only */
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_K1);   /* kernel use only */
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_AT);   /* internal use */
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_T0);   /* internal use */
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_RA);   /* return address */
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_SP);   /* stack pointer */

    tcg_add_target_add_op_defs(mips_op_defs);
}

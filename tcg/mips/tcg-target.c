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
        tcg_regset_set32(ct->u.regs, 0, 0xffffffff);
        break;
    case 'L': /* qemu_ld output arg constraint */
        ct->ct |= TCG_CT_REG;
        tcg_regset_set32(ct->u.regs, 0, 0xffffffff);
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_V0);
        break;
    case 'l': /* qemu_ld input arg constraint */
        ct->ct |= TCG_CT_REG;
        tcg_regset_set32(ct->u.regs, 0, 0xffffffff);
#if defined(CONFIG_SOFTMMU)
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_A0);
#endif
        break;
    case 'S': /* qemu_st constraint */
        ct->ct |= TCG_CT_REG;
        tcg_regset_set32(ct->u.regs, 0, 0xffffffff);
#if defined(CONFIG_SOFTMMU)
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_A0);
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

/*
 * Type reg
 */
static inline void tcg_out_opc_reg(TCGContext *s, int opc, int sa, int funct, int rd, int rs, int rt)
{
    int32_t inst;

    inst = (opc & 0x3F) << 26;
    inst |= (rs & 0x1F) << 21;
    inst |= (rt & 0x1F) << 16;
    inst |= (rd & 0x1F) << 11;
    inst |= (sa & 0x1F) <<  6;
    inst |= (funct & 0x3F);
    tcg_out32(s, inst);
}

/*
 * Type immediate
 */
static inline void tcg_out_opc_imm(TCGContext *s, int opc, int rt, int rs, int imm)
{
    int32_t inst;

    inst = (opc & 0x3F) << 26;
    inst |= (rs & 0x1F) << 21;
    inst |= (rt & 0x1F) << 16;
    inst |= (imm & 0xffff);
    tcg_out32(s, inst);
}

/*
 * Type shift immediate
 */
static inline void tcg_out_shift_imm(TCGContext *s, int funct, int rd, int rt, int sa)
{
    tcg_out_opc_reg(s, 0, sa, funct, rd, 0, rt);
}

static inline void tcg_out_nop(TCGContext *s)
{
    tcg_out32(s, 0);
}

static inline void tcg_out_mov(TCGContext *s, int ret, int arg)
{
    /* addu ret, arg, 0 */
    tcg_out_opc_reg(s, 0, 0, 33, ret, arg, TCG_REG_ZERO);
}

static inline void tcg_out_movi(TCGContext *s, TCGType type,
                                int reg, int32_t arg)
{
    if (arg == (int16_t)arg) {
        /* addiu reg, zero, arg */
        tcg_out_opc_imm(s, 9, reg, TCG_REG_ZERO, arg);
    } else if (arg == (uint16_t)arg) {
        /* ori reg, zero, arg */
        tcg_out_opc_imm(s, 13, reg, TCG_REG_ZERO, arg);
    } else {
        /* lui reg, arg >> 16 */
        tcg_out_opc_imm(s, 15, reg, 0, arg >> 16);
        /* ori reg, reg, arg & 0xffff */
        tcg_out_opc_imm(s, 13, reg, reg, arg & 0xffff);
    }
}

static inline void tcg_out_bswap16(TCGContext *s, int ret, int arg)
{
    /* ret and arg can't be register at */
    if (ret == TCG_REG_AT || arg == TCG_REG_AT) {
        tcg_abort();
    }

    /* shr at, arg, 8 */
    tcg_out_shift_imm(s, 2, TCG_REG_AT, arg, 8);
    /* andi at, at, 0x00ff */
    tcg_out_opc_imm(s, 12, TCG_REG_AT, TCG_REG_AT, 0x00ff);
    /* shl ret, arg, 8 */
    tcg_out_shift_imm(s, 0, ret, arg, 8);
    /* andi ret, ret, 0xff00 */
    tcg_out_opc_imm(s, 12, ret, ret, 0xff00);
    /* or ret, ret, at */
    tcg_out_opc_reg(s, 0, 0, 37, ret, ret, TCG_REG_AT);
}

static inline void tcg_out_bswap16s(TCGContext *s, int ret, int arg)
{
    /* ret and arg can't be register at */
    if (ret == TCG_REG_AT || arg == TCG_REG_AT) {
        tcg_abort();
    }

    /* srl at, arg, 8 */
    tcg_out_shift_imm(s, 2, TCG_REG_AT, arg, 8);
    /* andi at, at, 0xff */
    tcg_out_opc_imm(s, 12, TCG_REG_AT, TCG_REG_AT, 0xff);

    /* sll ret, arg, 24 */
    tcg_out_shift_imm(s, 0, ret, arg, 24);
    /* sra ret, ret, 16 */
    tcg_out_shift_imm(s, 3, ret, ret, 16);
    /* or ret, ret, at */
    tcg_out_opc_reg(s, 0, 0, 37, ret, ret, TCG_REG_AT);
}

static inline void tcg_out_bswap32(TCGContext *s, int ret, int arg)
{
    /* ret and arg must be different and can't be register at */
    if (ret == arg || ret == TCG_REG_AT || arg == TCG_REG_AT) {
        tcg_abort();
    }

    /* sll ret, arg, 24 */
    tcg_out_opc_reg(s, 0, 24, 0, ret, 0, arg);

    /* srl at, arg, 8 */
    tcg_out_shift_imm(s, 2, TCG_REG_AT, arg, 24);
    /* or ret, ret, at */
    tcg_out_opc_reg(s, 0, 0, 37, ret, ret, TCG_REG_AT);

    /* andi at, arg, 0xff00 */
    tcg_out_opc_imm(s, 12, TCG_REG_AT, arg, 0xff00);
    /* sll at, at, 8 */
    tcg_out_shift_imm(s, 0, TCG_REG_AT, TCG_REG_AT, 8);
    /* or ret, ret, at */
    tcg_out_opc_reg(s, 0, 0, 37, ret, ret, TCG_REG_AT);

    /* srl at, arg, 8 */
    tcg_out_shift_imm(s, 2, TCG_REG_AT, arg, 8);
    /* andi at, at, 0xff00 */
    tcg_out_opc_imm(s, 12, TCG_REG_AT, TCG_REG_AT, 0xff00);
    /* or ret, ret, at */
    tcg_out_opc_reg(s, 0, 0, 37, ret, ret, TCG_REG_AT);
}

static inline void tcg_out_ldst(TCGContext *s, int opc, int arg,
                              int arg1, tcg_target_long arg2)
{
    if (arg2 == (int16_t) arg2) {
        /* ldst arg, arg2(arg1) */
        tcg_out_opc_imm(s, opc, arg, arg1, arg2);
    } else {
        /* movi at, arg2 */
        tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_AT, arg2);
        /* addu at, at, arg1 */
        tcg_out_opc_reg(s, 0, 0, 33, TCG_REG_AT, TCG_REG_AT, arg1);
        /* ldst arg, arg2(arg1) */
        tcg_out_opc_imm(s, opc, arg, TCG_REG_AT, 0);
    }
}

static inline void tcg_out_ld(TCGContext *s, TCGType type, int arg,
                              int arg1, tcg_target_long arg2)
{
    tcg_out_ldst(s, 35, arg, arg1, arg2);
}

static inline void tcg_out_st(TCGContext *s, TCGType type, int arg,
                              int arg1, tcg_target_long arg2)
{
    tcg_out_ldst(s, 43, arg, arg1, arg2);
}

static inline void tcg_out_addi(TCGContext *s, int reg, tcg_target_long val)
{
    if (val == (int16_t)val) {
        /* addiu reg, reg, val */
        tcg_out_opc_imm(s, 9, reg, reg, val);
    } else {
        tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_AT, val);
        /* addu reg, reg, at */
        tcg_out_opc_reg(s, 0, 0, 33, reg, reg, TCG_REG_AT);
    }
}

static void tcg_out_brcond(TCGContext *s, int cond, int arg1,
                           int arg2, int label_index)
{
    TCGLabel *l = &s->labels[label_index];

    switch (cond) {
    case TCG_COND_EQ:
        /* beq arg1, arg2, 0 */
        tcg_out_opc_imm(s, 4, arg2, arg1, 0);
        break;
    case TCG_COND_NE:
        /* bne arg1, arg2, 0 */
        tcg_out_opc_imm(s, 5, arg2, arg1, 0);
        break;
    case TCG_COND_LT:
        /* slt at, arg1, arg2 */
        tcg_out_opc_reg(s, 0, 0, 42, TCG_REG_AT, arg1, arg2);
        /* bne at, zero, 0 */
        tcg_out_opc_imm(s, 5, TCG_REG_ZERO, TCG_REG_AT, 0);
        break;
    case TCG_COND_LTU:
        /* sltu at, arg1, arg2 */
        tcg_out_opc_reg(s, 0, 0, 43, TCG_REG_AT, arg1, arg2);
        /* bne at, zero, 0 */
        tcg_out_opc_imm(s, 5, TCG_REG_ZERO, TCG_REG_AT, 0);
        break;
    case TCG_COND_GE:
        /* slt at, arg1, arg2 */
        tcg_out_opc_reg(s, 0, 0, 42, TCG_REG_AT, arg1, arg2);
        /* beq at, zero, 0 */
        tcg_out_opc_imm(s, 4, TCG_REG_ZERO, TCG_REG_AT, 0);
        break;
    case TCG_COND_GEU:
        /* sltu at, arg1, arg2 */
        tcg_out_opc_reg(s, 0, 0, 43, TCG_REG_AT, arg1, arg2);
        /* beq at, zero, 0 */
        tcg_out_opc_imm(s, 4, TCG_REG_ZERO, TCG_REG_AT, 0);
        break;
    case TCG_COND_LE:
        /* slt at, arg2, arg1 */
        tcg_out_opc_reg(s, 0, 0, 42, TCG_REG_AT, arg2, arg1);
        /* beq at, zero, 0 */
        tcg_out_opc_imm(s, 4, TCG_REG_ZERO, TCG_REG_AT, 0);
        break;
    case TCG_COND_LEU:
        /* sltu at, arg2, arg1 */
        tcg_out_opc_reg(s, 0, 0, 43, TCG_REG_AT, arg2, arg1);
        /* beq at, zero, 0 */
        tcg_out_opc_imm(s, 4, TCG_REG_ZERO, TCG_REG_AT, 0);
        break;
    case TCG_COND_GT:
        /* slt at, arg2, arg1 */
        tcg_out_opc_reg(s, 0, 0, 42, TCG_REG_AT, arg2, arg1);
        /* bne at, zero, 0 */
        tcg_out_opc_imm(s, 5, TCG_REG_ZERO, TCG_REG_AT, 0);
        break;
    case TCG_COND_GTU:
        /* sltu at, arg2, arg1 */
        tcg_out_opc_reg(s, 0, 0, 43, TCG_REG_AT, arg2, arg1);
        /* bne at, zero, 0 */
        tcg_out_opc_imm(s, 5, TCG_REG_ZERO, TCG_REG_AT, 0);
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
static void tcg_out_brcond2(TCGContext *s, int cond, int arg1,
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

    /* bne arg2, arg4, 0 */
    label_ptr = s->code_ptr;
    tcg_out_opc_imm(s, 5, arg2, arg4, 0);
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
    /* srl a0, addr_regl, $x */
    tcg_out_shift_imm(s, 2, TCG_REG_A0, addr_regl, TARGET_PAGE_BITS - CPU_TLB_ENTRY_BITS);
    /* andi a0, a0, 0xff */
    tcg_out_opc_imm(s, 12, TCG_REG_A0, TCG_REG_A0, (CPU_TLB_SIZE - 1) << CPU_TLB_ENTRY_BITS);
    /* addu a0, a0, fp */
    tcg_out_opc_reg(s, 0, 0, 33, TCG_REG_A0, TCG_REG_A0, TCG_AREG0);
    /* lw at, $x(a0) */
    tcg_out_opc_imm(s, 35, TCG_REG_AT, TCG_REG_A0,
                    offsetof(CPUState, tlb_table[mem_index][0].addr_read) + addr_meml);
    /* andi a1, addr_regl, $x */
    tcg_out_movi(s, TCG_TYPE_I32, TCG_REG_T0, TARGET_PAGE_MASK | ((1 << s_bits) - 1));
    tcg_out_opc_reg(s, 0, 0, 36, TCG_REG_T0, TCG_REG_T0, addr_regl);

# if TARGET_LONG_BITS == 64
    /* bne a1, at, label3 */
    label3_ptr = s->code_ptr;
    tcg_out_opc_imm(s, 5, TCG_REG_T0, TCG_REG_AT, 0);
    tcg_out_nop(s);

    /* lw at, $x(a0) */
    tcg_out_opc_imm(s, 35, TCG_REG_AT, TCG_REG_A0,
                    offsetof(CPUState, tlb_table[mem_index][0].addr_read) + addr_memh);

    /* beq addr_regh, at, label1 */
    label1_ptr = s->code_ptr;
    tcg_out_opc_imm(s, 4, addr_regh, TCG_REG_AT, 0);
    tcg_out_nop(s);

    reloc_pc16(label3_ptr, (tcg_target_long) s->code_ptr);
# else
    /* beq a1, at, label1 */
    label1_ptr = s->code_ptr;
    tcg_out_opc_imm(s, 4, TCG_REG_T0, TCG_REG_AT, 0);
    tcg_out_nop(s);
# endif

    /* slow path */
    sp_args = TCG_REG_A0;
    tcg_out_mov(s, sp_args++, addr_reg1);
# if TARGET_LONG_BITS == 64
    tcg_out_mov(s, sp_args++, addr_reg2);
# endif
    tcg_out_movi(s, TCG_TYPE_I32, sp_args++, mem_index);
    tcg_out_movi(s, TCG_TYPE_I32, TCG_REG_AT, (tcg_target_long)qemu_ld_helpers[s_bits]);
    tcg_out_opc_reg(s, 0, 0, 9, TCG_REG_RA, TCG_REG_AT, 0);
    tcg_out_nop(s);

    switch(opc) {
    case 0:
        /* andi data_reg1, v0, 0xff */
        tcg_out_opc_imm(s, 12, data_reg1, TCG_REG_V0, 0xff);
        break;
    case 0 | 4:
        /* sll v0, v0, 24 */
        tcg_out_shift_imm(s, 0, TCG_REG_V0, TCG_REG_V0, 24);
        /* sra data_reg1, v0, 24 */
        tcg_out_shift_imm(s, 3, data_reg1, TCG_REG_V0, 24);
        break;
    case 1:
        /* andi data_reg1, v0, 0xffff */
        tcg_out_opc_imm(s, 12, data_reg1, TCG_REG_V0, 0xffff);
        break;
    case 1 | 4:
        /* sll v0, v0, 16 */
        tcg_out_shift_imm(s, 0, TCG_REG_V0, TCG_REG_V0, 16);
        /* sra data_reg1, v0, 16 */
        tcg_out_shift_imm(s, 3, data_reg1, TCG_REG_V0, 16);
        break;
    case 2:
        /* move data_reg1, v0 */
        tcg_out_mov(s, data_reg1, TCG_REG_V0);
        break;
    case 3:
        /* move data_reg2, v1 */
        tcg_out_mov(s, data_reg2, TCG_REG_V1);
        /* move data_reg1, v0 */
        tcg_out_mov(s, data_reg1, TCG_REG_V0);
        break;
    default:
        tcg_abort();
    }

    /* beq zero, zero, label2 */
    label2_ptr = s->code_ptr;
    tcg_out_opc_imm(s, 4, TCG_REG_ZERO, TCG_REG_ZERO, 0);
    tcg_out_nop(s);

    /* label1: fast path */
    reloc_pc16(label1_ptr, (tcg_target_long) s->code_ptr);

    /* lw v0, $x(a0) */
    tcg_out_opc_imm(s, 35, TCG_REG_V0, TCG_REG_A0,
                    offsetof(CPUState, tlb_table[mem_index][0].addend) + addr_meml);
    /* addu v0, v0, addr_regl */
    tcg_out_opc_reg(s, 0, 0, 33, TCG_REG_V0, TCG_REG_V0, addr_regl);

    addr_reg1 = TCG_REG_V0;
#endif

    switch(opc) {
    case 0:
        /* lbu data_reg1, 0(addr_reg1) */
        tcg_out_opc_imm(s, 36, data_reg1, addr_reg1, 0);
        break;
    case 0 | 4:
        /* lb data_reg1, 0(addr_reg1) */
        tcg_out_opc_imm(s, 32, data_reg1, addr_reg1, 0);
        break;
    case 1:
        if (TCG_NEED_BSWAP) {
            /* lhu t0, 0(addr_reg1) */
            tcg_out_opc_imm(s, 37, TCG_REG_T0, addr_reg1, 0);
            /* bswap16 data_reg1, t0 */
            tcg_out_bswap16(s, data_reg1, TCG_REG_T0);
        } else {
            /* lhu data_reg1, 0(addr_reg1) */
            tcg_out_opc_imm(s, 37, data_reg1, addr_reg1, 0);
        }
        break;
    case 1 | 4:
        if (TCG_NEED_BSWAP) {
            /* lhu t0, 0(addr_reg1) */
            tcg_out_opc_imm(s, 37, TCG_REG_T0, addr_reg1, 0);
            /* bswap16s data_reg1, t0 */
            tcg_out_bswap16s(s, data_reg1, TCG_REG_T0);
        } else {
            /* lh data_reg1, 0(addr_reg1) */
            tcg_out_opc_imm(s, 33, data_reg1, addr_reg1, 0);
        }
        break;
    case 2:
        if (TCG_NEED_BSWAP) {
            /* lw t0, 0(addr_reg1) */
            tcg_out_opc_imm(s, 35, TCG_REG_T0, addr_reg1, 0);
            /* bswap32 data_reg1, t0 */
            tcg_out_bswap32(s, data_reg1, TCG_REG_T0);
        } else {
            /* lw data_reg1, 0(addr_reg1) */
            tcg_out_opc_imm(s, 35, data_reg1, addr_reg1, 0);
        }
        break;
    case 3:
#if !defined(CONFIG_SOFTMMU)
        tcg_out_mov(s, TCG_REG_V0, addr_reg1);
        addr_reg1 = TCG_REG_V0;
#endif
        if (TCG_NEED_BSWAP) {
            /* lw data_reg1, 4(a0) */
            tcg_out_opc_imm(s, 35, TCG_REG_T0, addr_reg1, 4);
            /* bswap32 data_reg1, a0 */
            tcg_out_bswap32(s, data_reg1, TCG_REG_T0);
            /* lw data_reg1, 0(a0) */
            tcg_out_opc_imm(s, 35, TCG_REG_T0, addr_reg1, 0);
            /* bswap32 data_reg2, a0 */
            tcg_out_bswap32(s, data_reg2, TCG_REG_T0);
        } else {
            /* lw data_reg1, 0(addr_reg1) */
            tcg_out_opc_imm(s, 35, data_reg1, addr_reg1, 0);
            /* lw data_reg2, 4(addr_reg1) */
            tcg_out_opc_imm(s, 35, data_reg2, addr_reg1, 4);
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
    /* srl a0, addr_regl, $x */
    tcg_out_shift_imm(s, 2, TCG_REG_A0, addr_regl, TARGET_PAGE_BITS - CPU_TLB_ENTRY_BITS);
    /* andi a0, a0, 0xff */
    tcg_out_opc_imm(s, 12, TCG_REG_A0, TCG_REG_A0, (CPU_TLB_SIZE - 1) << CPU_TLB_ENTRY_BITS);
    /* addu a0, a0, fp */
    tcg_out_opc_reg(s, 0, 0, 33, TCG_REG_A0, TCG_REG_A0, TCG_AREG0);
    /* lw at, $x(a0) */
    tcg_out_opc_imm(s, 35, TCG_REG_AT, TCG_REG_A0,
                    offsetof(CPUState, tlb_table[mem_index][0].addr_write) + addr_meml);
    /* andi t0, addr_regl, $x */
    tcg_out_movi(s, TCG_TYPE_I32, TCG_REG_T0, TARGET_PAGE_MASK | ((1 << s_bits) - 1));
    tcg_out_opc_reg(s, 0, 0, 36, TCG_REG_T0, TCG_REG_T0, addr_regl);

# if TARGET_LONG_BITS == 64
    /* bne t0, at, label3 */
    label3_ptr = s->code_ptr;
    tcg_out_opc_imm(s, 5, TCG_REG_T0, TCG_REG_AT, 0);
    tcg_out_nop(s);

    /* lw at, $x(a0) */
    tcg_out_opc_imm(s, 35, TCG_REG_AT, TCG_REG_A0,
                    offsetof(CPUState, tlb_table[mem_index][0].addr_write) + addr_memh);

    /* beq addr_regh, at, label1 */
    label1_ptr = s->code_ptr;
    tcg_out_opc_imm(s, 4, addr_regh, TCG_REG_AT, 0);
    tcg_out_nop(s);

    reloc_pc16(label3_ptr, (tcg_target_long) s->code_ptr);
# else
    /* beq t0, at, label1 */
    label1_ptr = s->code_ptr;
    tcg_out_opc_imm(s, 4, TCG_REG_T0, TCG_REG_AT, 0);
    tcg_out_nop(s);
# endif

    /* slow path */
    sp_args = TCG_REG_A0;
    tcg_out_mov(s, sp_args++, addr_reg1);
# if TARGET_LONG_BITS == 64
    tcg_out_mov(s, sp_args++, addr_reg2);
# endif
    switch(opc) {
    case 0:
        /* andi aX, data_reg1, 0xff */
        tcg_out_opc_imm(s, 12, sp_args++, data_reg1, 0xff);
        break;
    case 1:
        /* andi aX, data_reg1, 0xffff */
        tcg_out_opc_imm(s, 12, sp_args++, data_reg1, 0xffff);
        break;
    case 2:
        /* move aX, data_reg1 */
        tcg_out_mov(s, sp_args++, data_reg1);
        break;
    case 3:
        sp_args = (sp_args + 1) & ~1;
        /* move aX, data_reg1 */
        tcg_out_mov(s, sp_args++, data_reg1);
        /* move aX, data_reg2 */
        tcg_out_mov(s, sp_args++, data_reg2);
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

    /* call qemu_st_helpers[s_bits] */
    tcg_out_movi(s, TCG_TYPE_I32, TCG_REG_AT, (tcg_target_long)qemu_st_helpers[s_bits]);
    tcg_out_opc_reg(s, 0, 0, 9, TCG_REG_RA, TCG_REG_AT, 0);
    tcg_out_nop(s);

    /* beq zero, zero, label2 */
    label2_ptr = s->code_ptr;
    tcg_out_opc_imm(s, 4, TCG_REG_ZERO, TCG_REG_ZERO, 0);
    tcg_out_nop(s);

    /* label1: fast path */
    reloc_pc16(label1_ptr, (tcg_target_long) s->code_ptr);

    /* lw a0, $x(a0) */
    tcg_out_opc_imm(s, 35, TCG_REG_A0, TCG_REG_A0,
                    offsetof(CPUState, tlb_table[mem_index][0].addend) + addr_meml);
    /* addu a0, a0, addr_regl */
    tcg_out_opc_reg(s, 0, 0, 33, TCG_REG_A0, TCG_REG_A0, addr_regl);

    addr_reg1 = TCG_REG_A0;
#endif

    switch(opc) {
    case 0:
        /* sb data_reg1, 0(addr_reg1) */
        tcg_out_opc_imm(s, 40, data_reg1, addr_reg1, 0);
        break;
    case 1:
        if (TCG_NEED_BSWAP) {
            /* bswap16 t0, data_reg1 */
            tcg_out_bswap16(s, TCG_REG_T0, data_reg1);
            /* sh t0, 0(addr_reg1) */
            tcg_out_opc_imm(s, 41, TCG_REG_T0, addr_reg1, 0);
        } else {
            /* sh data_reg1, 0(addr_reg1) */
            tcg_out_opc_imm(s, 41, data_reg1, addr_reg1, 0);
        }
        break;
    case 2:
        if (TCG_NEED_BSWAP) {
            /* bswap32 t0, data_reg1 */
            tcg_out_bswap32(s, TCG_REG_T0, data_reg1);
            /* sw t0, 0(addr_reg1) */
            tcg_out_opc_imm(s, 43, TCG_REG_T0, addr_reg1, 0);
        } else {
            /* sw data_reg1, 0(addr_reg1) */
            tcg_out_opc_imm(s, 43, data_reg1, addr_reg1, 0);
        }
        break;
    case 3:
        if (TCG_NEED_BSWAP) {
            /* bswap32 t0, data_reg2 */
            tcg_out_bswap32(s, TCG_REG_T0, data_reg2);
            /* sw t0, 0(addr_reg1) */
            tcg_out_opc_imm(s, 43, TCG_REG_T0, addr_reg1, 0);
            /* bswap32 t0, data_reg1 */
            tcg_out_bswap32(s, TCG_REG_T0, data_reg1);
            /* sw t0, 4(addr_reg1) */
            tcg_out_opc_imm(s, 43, TCG_REG_T0, addr_reg1, 4);
        } else {
            /* sw data_reg1, 0(addr_reg1) */
            tcg_out_opc_imm(s, 43, data_reg1, addr_reg1, 0);
            /* sw data_reg2, 4(addr_reg1) */
            tcg_out_opc_imm(s, 43, data_reg2, addr_reg1, 4);
        }
        break;
    default:
        tcg_abort();
    }

#if defined(CONFIG_SOFTMMU)
    reloc_pc16(label2_ptr, (tcg_target_long) s->code_ptr);
#endif
}

static inline void tcg_out_op(TCGContext *s, int opc,
                              const TCGArg *args, const int *const_args)
{
    switch(opc) {
    case INDEX_op_exit_tb:
        tcg_out_movi(s, TCG_TYPE_I32, TCG_REG_V0, args[0]);
        tcg_out_movi(s, TCG_TYPE_I32, TCG_REG_AT, (tcg_target_long)tb_ret_addr);
        /* jr at */
        tcg_out_opc_reg(s, 0, 0, 8, 0, TCG_REG_AT, 0);
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
            /* jr at */
            tcg_out_opc_reg(s, 0, 0, 8, 0, TCG_REG_AT, 0);
        }
        tcg_out_nop(s);
        s->tb_next_offset[args[0]] = s->code_ptr - s->code_buf;
        break;
    case INDEX_op_call:
        /* jalr args[0] */
        tcg_out_opc_reg(s, 0, 0, 9, TCG_REG_RA, args[0], 0);
        tcg_out_nop(s);
        break;
    case INDEX_op_jmp:
        /* jr args[0] */
        tcg_out_opc_reg(s, 0, 0, 8, 0, args[0], 0);
        tcg_out_nop(s);
        break;
    case INDEX_op_br:
        tcg_out_brcond(s, TCG_COND_EQ, TCG_REG_ZERO, TCG_REG_ZERO, args[0]);
        break;

    case INDEX_op_mov_i32:
        tcg_out_mov(s, args[0], args[1]);
        break;
    case INDEX_op_movi_i32:
        tcg_out_movi(s, TCG_TYPE_I32, args[0], args[1]);
        break;

    case INDEX_op_ld8u_i32:
        /* lbu args[0], args[2](args[1]) */
	tcg_out_ldst(s, 36, args[0], args[1], args[2]);
        break;
    case INDEX_op_ld8s_i32:
        /* lb args[0], args[2](args[1]) */
        tcg_out_ldst(s, 32, args[0], args[1], args[2]);
        break;
    case INDEX_op_ld16u_i32:
        /* lhu args[0], args[2](args[1]) */
        tcg_out_ldst(s, 37, args[0], args[1], args[2]);
        break;
    case INDEX_op_ld16s_i32:
        /* lh args[0], args[2](args[1]) */
        tcg_out_ldst(s, 33, args[0], args[1], args[2]);
        break;
    case INDEX_op_ld_i32:
        /* lw args[0], args[2](args[1]) */
        tcg_out_ldst(s, 35, args[0], args[1], args[2]);
        break;
    case INDEX_op_st8_i32:
        /* sb args[0], args[2](args[1]) */
        tcg_out_ldst(s, 40, args[0], args[1], args[2]);
        break;
    case INDEX_op_st16_i32:
        /* sh args[0], args[2](args[1]) */
        tcg_out_ldst(s, 41, args[0], args[1], args[2]);
        break;
    case INDEX_op_st_i32:
        /* sw args[0], args[2](args[1]) */
        tcg_out_ldst(s, 43, args[0], args[1], args[2]);
        break;

    case INDEX_op_add_i32:
        if (const_args[2]) {
            /* addiu args[0], args[1], args[2] */
            tcg_out_opc_imm(s, 9, args[0], args[1], args[2]);
        } else {
            /* addu args[0], args[1], args[2] */
            tcg_out_opc_reg(s, 0, 0, 33, args[0], args[1], args[2]);
        }
        break;
    case INDEX_op_add2_i32:
        if (const_args[4]) {
            /* addiu at, args[2], args[4] */
            tcg_out_opc_imm(s, 9, TCG_REG_AT, args[2], args[4]);
        } else {
            /* addu at, args[2], args[4] */
            tcg_out_opc_reg(s, 0, 0, 33, TCG_REG_AT, args[2], args[4]);
        }
        /* sltu t0, at, args[2] */
        tcg_out_opc_reg(s, 0, 0, 43, TCG_REG_T0, TCG_REG_AT, args[2]);
        if (const_args[5]) {
            /* addiu args[1], args[3], args[5] */
            tcg_out_opc_imm(s, 9, args[1], args[3], args[5]);
        } else {
             /* addu args[1], args[3], args[5] */
             tcg_out_opc_reg(s, 0, 0, 33, args[1], args[3], args[5]);
        }
        /* addu args[1], args[1], t0 */
        tcg_out_opc_reg(s, 0, 0, 33, args[1], args[1], TCG_REG_T0);
        /* mov args[0], at */
        tcg_out_mov(s, args[0], TCG_REG_AT);
        break;
    case INDEX_op_sub_i32:
        if (const_args[2]) {
            /* addiu args[0], arg [1], -args[2] */
            tcg_out_opc_imm(s, 9, args[0], args[1], -args[2]);
        } else {
            /* subu args[0], args[1], args[2] */
            tcg_out_opc_reg(s, 0, 0, 35, args[0], args[1], args[2]);
        }
        break;
    case INDEX_op_sub2_i32:
        if (const_args[4]) {
            /* addiu at, args[2], -args[4] */
            tcg_out_opc_imm(s, 9, TCG_REG_AT, args[2], -args[4]);
        } else {
            /* addu at, args[2], args[4] */
            tcg_out_opc_reg(s, 0, 0, 35, TCG_REG_AT, args[2], args[4]);
        }
        /* sltu t0, args[2], at */
        tcg_out_opc_reg(s, 0, 0, 43, TCG_REG_T0, args[2], TCG_REG_AT);
        if (const_args[5]) {
            /* addiu args[1], args[3], -args[5] */
            tcg_out_opc_imm(s, 9, args[1], args[3], -args[5]);
        } else {
             /* subu args[1], args[3], args[5] */
             tcg_out_opc_reg(s, 0, 0, 35, args[1], args[3], args[5]);
        }
        /* subu args[1], args[1], t0 */
        tcg_out_opc_reg(s, 0, 0, 35, args[1], args[1], TCG_REG_T0);
        /* mov args[0], at */
        tcg_out_mov(s, args[0], TCG_REG_AT);
        break;
    case INDEX_op_mul_i32:
        /* mult args[1], args[2] */
        tcg_out_opc_reg(s, 0, 0, 24, 0, args[1], args[2]);
        /* mflo args[0] */
        tcg_out_opc_reg(s, 0, 0, 18, args[0], 0, 0);
        break;
    case INDEX_op_mulu2_i32:
        /* multu args[2], args[3] */
        tcg_out_opc_reg(s, 0, 0, 25, 0, args[2], args[3]);
        /* mflo args[0] */
        tcg_out_opc_reg(s, 0, 0, 18, args[0], 0, 0);
        /* mfhi args[1] */
        tcg_out_opc_reg(s, 0, 0, 16, args[1], 0, 0);
        break;
    case INDEX_op_div_i32:
        /* div args[1], args[2] */
        tcg_out_opc_reg(s, 0, 0, 26, 0, args[1], args[2]);
        /* mflo args[0] */
        tcg_out_opc_reg(s, 0, 0, 18, args[0], 0, 0);
        break;
    case INDEX_op_divu_i32:
        /* divu args[1], args[2] */
        tcg_out_opc_reg(s, 0, 0, 27, 0, args[1], args[2]);
        /* mflo args[0] */
        tcg_out_opc_reg(s, 0, 0, 18, args[0], 0, 0);
        break;
    case INDEX_op_rem_i32:
        /* div args[1], args[2] */
        tcg_out_opc_reg(s, 0, 0, 26, 0, args[1], args[2]);
        /* mfhi args[0] */
        tcg_out_opc_reg(s, 0, 0, 16, args[0], 0, 0);
        break;
    case INDEX_op_remu_i32:
        /* divu args[1], args[2] */
        tcg_out_opc_reg(s, 0, 0, 27, 0, args[1], args[2]);
        /* mfhi args[0] */
        tcg_out_opc_reg(s, 0, 0, 16, args[0], 0, 0);
        break;

    case INDEX_op_and_i32:
        if (const_args[2]) {
            /* andi args[0], args [1], args[2] */
            tcg_out_opc_imm(s, 12, args[0], args[1], args[2]);
        } else {
            /* and args[0], args [1], args[2] */
            tcg_out_opc_reg(s, 0, 0, 36, args[0], args[1], args[2]);
        }
        break;
    case INDEX_op_or_i32:
        if (const_args[2]) {
            /* ori args[0], args [1], args[2] */
            tcg_out_opc_imm(s, 13, args[0], args[1], args[2]);
        } else {
            /* or args[0], args [1], args[2] */
            tcg_out_opc_reg(s, 0, 0, 37, args[0], args[1], args[2]);
        }
        break;
    case INDEX_op_not_i32:
        /* nor args[0], args [1], zero */
        tcg_out_opc_reg(s, 0, 0, 39, args[0], args[1], args[1]);
        break;
    case INDEX_op_xor_i32:
        if (const_args[2]) {
            /* xori args[0], args [1], args[2] */
            tcg_out_opc_imm(s, 14, args[0], args[1], args[2]);
        } else {
            /* xor args[0], args [1], args[2] */
            tcg_out_opc_reg(s, 0, 0, 38, args[0], args[1], args[2]);
        }
        break;

    case INDEX_op_sar_i32:
        if (const_args[2]) {
            /* sra args[0], args [1], args[2] */
            tcg_out_shift_imm(s, 3, args[0], args[1], args[2]);
        } else {
            /* srav args[0], args [1], args[2] */
            tcg_out_opc_reg(s, 0, 0, 7, args[0], args[2], args[1]);
        }
        break;
    case INDEX_op_shl_i32:
        if (const_args[2]) {
            /* sll args[0], args [1], args[2] */
            tcg_out_shift_imm(s, 0, args[0], args[1], args[2]);
        } else {
            /* sllv args[0], args [1], args[2] */
            tcg_out_opc_reg(s, 0, 0, 4, args[0], args[2], args[1]);
        }
        break;
    case INDEX_op_shr_i32:
        if (const_args[2]) {
            /* srl args[0], args [1], args[2] */
            tcg_out_shift_imm(s, 2, args[0], args[1], args[2]);
        } else {
            /* srlv args[0], args [1], args[2] */
            tcg_out_opc_reg(s, 0, 0, 6, args[0], args[2], args[1]);
        }
        break;

    case INDEX_op_brcond_i32:
        tcg_out_brcond(s, args[2], args[0], args[1], args[3]);
        break;
    case INDEX_op_brcond2_i32:
        tcg_out_brcond2(s, args[4], args[0], args[1], args[2], args[3], args[5]);
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
    { INDEX_op_not_i32, { "r", "rZ" } },
    { INDEX_op_or_i32, { "r", "rZ", "rIZ" } },
    { INDEX_op_xor_i32, { "r", "rZ", "rIZ" } },

    { INDEX_op_shl_i32, { "r", "rZ", "riZ" } },
    { INDEX_op_shr_i32, { "r", "rZ", "riZ" } },
    { INDEX_op_sar_i32, { "r", "rZ", "riZ" } },

    { INDEX_op_brcond_i32, { "rZ", "rZ" } },

    { INDEX_op_add2_i32, { "r", "r", "rZ", "rZ", "rJZ", "rJZ" } },
    { INDEX_op_sub2_i32, { "r", "r", "rZ", "rZ", "rJZ", "rJZ" } },
    { INDEX_op_brcond2_i32, { "rZ", "rZ", "rZ", "rZ" } },

#if TARGET_LONG_BITS == 32
    { INDEX_op_qemu_ld8u, { "L", "lZ" } },
    { INDEX_op_qemu_ld8s, { "L", "lZ" } },
    { INDEX_op_qemu_ld16u, { "L", "lZ" } },
    { INDEX_op_qemu_ld16s, { "L", "lZ" } },
    { INDEX_op_qemu_ld32u, { "L", "lZ" } },
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
    { INDEX_op_qemu_ld32u, { "L", "lZ", "lZ" } },
    { INDEX_op_qemu_ld64, { "L", "L", "lZ", "lZ" } },

    { INDEX_op_qemu_st8, { "SZ", "SZ", "SZ" } },
    { INDEX_op_qemu_st16, { "SZ", "SZ", "SZ" } },
    { INDEX_op_qemu_st32, { "SZ", "SZ", "SZ" } },
    { INDEX_op_qemu_st64, { "SZ", "SZ", "SZ", "SZ" } },
#endif
    { -1 },
};

static int tcg_target_callee_save_regs[] = {
    TCG_REG_S0,
    TCG_REG_S1,
    TCG_REG_S2,
    TCG_REG_S3,
    TCG_REG_S4,
    TCG_REG_S5,
    TCG_REG_S6,
    TCG_REG_S7,
    TCG_REG_GP,
    /* TCG_REG_FP, */ /* currently used for the global env, so np
                         need to save */
    TCG_REG_RA,       /* should be last for ABI compliance */
};

/* Generate global QEMU prologue and epilogue code */
void tcg_target_qemu_prologue(TCGContext *s)
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
    tcg_out_opc_reg(s, 0, 0, 8, 0, TCG_REG_A0, 0);
    tcg_out_nop(s);
    tb_ret_addr = s->code_ptr;

    /* TB epilogue */
    for(i = 0 ; i < ARRAY_SIZE(tcg_target_callee_save_regs) ; i++) {
        tcg_out_ld(s, TCG_TYPE_I32, tcg_target_callee_save_regs[i],
                   TCG_REG_SP, TCG_STATIC_CALL_ARGS_SIZE + i * 4);
    }

    /* jr ra */
    tcg_out_opc_reg(s, 0, 0, 8, 0, TCG_REG_RA, 0);
    tcg_out_addi(s, TCG_REG_SP, frame_size);
}

void tcg_target_init(TCGContext *s)
{
    tcg_regset_set32(tcg_target_available_regs[TCG_TYPE_I32], 0, 0xffffffff);
    tcg_regset_set32(tcg_target_call_clobber_regs, 0,
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

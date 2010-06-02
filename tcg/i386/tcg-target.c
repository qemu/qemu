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
    "%eax",
    "%ecx",
    "%edx",
    "%ebx",
    "%esp",
    "%ebp",
    "%esi",
    "%edi",
};
#endif

static const int tcg_target_reg_alloc_order[] = {
    TCG_REG_EBX,
    TCG_REG_ESI,
    TCG_REG_EDI,
    TCG_REG_EBP,
    TCG_REG_ECX,
    TCG_REG_EDX,
    TCG_REG_EAX,
};

static const int tcg_target_call_iarg_regs[3] = { TCG_REG_EAX, TCG_REG_EDX, TCG_REG_ECX };
static const int tcg_target_call_oarg_regs[2] = { TCG_REG_EAX, TCG_REG_EDX };

static uint8_t *tb_ret_addr;

static void patch_reloc(uint8_t *code_ptr, int type,
                        tcg_target_long value, tcg_target_long addend)
{
    value += addend;
    switch(type) {
    case R_386_32:
        *(uint32_t *)code_ptr = value;
        break;
    case R_386_PC32:
        *(uint32_t *)code_ptr = value - (long)code_ptr;
        break;
    case R_386_PC8:
        value -= (long)code_ptr;
        if (value != (int8_t)value) {
            tcg_abort();
        }
        *(uint8_t *)code_ptr = value;
        break;
    default:
        tcg_abort();
    }
}

/* maximum number of register used for input function arguments */
static inline int tcg_target_get_call_iarg_regs_count(int flags)
{
    flags &= TCG_CALL_TYPE_MASK;
    switch(flags) {
    case TCG_CALL_TYPE_STD:
        return 0;
    case TCG_CALL_TYPE_REGPARM_1:
    case TCG_CALL_TYPE_REGPARM_2:
    case TCG_CALL_TYPE_REGPARM:
        return flags - TCG_CALL_TYPE_REGPARM_1 + 1;
    default:
        tcg_abort();
    }
}

/* parse target specific constraints */
static int target_parse_constraint(TCGArgConstraint *ct, const char **pct_str)
{
    const char *ct_str;

    ct_str = *pct_str;
    switch(ct_str[0]) {
    case 'a':
        ct->ct |= TCG_CT_REG;
        tcg_regset_set_reg(ct->u.regs, TCG_REG_EAX);
        break;
    case 'b':
        ct->ct |= TCG_CT_REG;
        tcg_regset_set_reg(ct->u.regs, TCG_REG_EBX);
        break;
    case 'c':
        ct->ct |= TCG_CT_REG;
        tcg_regset_set_reg(ct->u.regs, TCG_REG_ECX);
        break;
    case 'd':
        ct->ct |= TCG_CT_REG;
        tcg_regset_set_reg(ct->u.regs, TCG_REG_EDX);
        break;
    case 'S':
        ct->ct |= TCG_CT_REG;
        tcg_regset_set_reg(ct->u.regs, TCG_REG_ESI);
        break;
    case 'D':
        ct->ct |= TCG_CT_REG;
        tcg_regset_set_reg(ct->u.regs, TCG_REG_EDI);
        break;
    case 'q':
        ct->ct |= TCG_CT_REG;
        tcg_regset_set32(ct->u.regs, 0, 0xf);
        break;
    case 'r':
        ct->ct |= TCG_CT_REG;
        tcg_regset_set32(ct->u.regs, 0, 0xff);
        break;

        /* qemu_ld/st address constraint */
    case 'L':
        ct->ct |= TCG_CT_REG;
        tcg_regset_set32(ct->u.regs, 0, 0xff);
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_EAX);
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_EDX);
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
    else
        return 0;
}

#define P_EXT		0x100		/* 0x0f opcode prefix */
#define P_DATA16	0x200		/* 0x66 opcode prefix */

#define OPC_ARITH_EvIz	(0x81)
#define OPC_ARITH_EvIb	(0x83)
#define OPC_ARITH_GvEv	(0x03)		/* ... plus (ARITH_FOO << 3) */
#define OPC_ADD_GvEv	(OPC_ARITH_GvEv | (ARITH_ADD << 3))
#define OPC_BSWAP	(0xc8 | P_EXT)
#define OPC_CALL_Jz	(0xe8)
#define OPC_CMP_GvEv	(OPC_ARITH_GvEv | (ARITH_CMP << 3))
#define OPC_DEC_r32	(0x48)
#define OPC_IMUL_GvEv	(0xaf | P_EXT)
#define OPC_IMUL_GvEvIb	(0x6b)
#define OPC_IMUL_GvEvIz	(0x69)
#define OPC_INC_r32	(0x40)
#define OPC_JCC_long	(0x80 | P_EXT)	/* ... plus condition code */
#define OPC_JCC_short	(0x70)		/* ... plus condition code */
#define OPC_JMP_long	(0xe9)
#define OPC_JMP_short	(0xeb)
#define OPC_LEA         (0x8d)
#define OPC_MOVB_EvGv	(0x88)		/* stores, more or less */
#define OPC_MOVL_EvGv	(0x89)		/* stores, more or less */
#define OPC_MOVL_GvEv	(0x8b)		/* loads, more or less */
#define OPC_MOVL_Iv     (0xb8)
#define OPC_MOVSBL	(0xbe | P_EXT)
#define OPC_MOVSWL	(0xbf | P_EXT)
#define OPC_MOVZBL	(0xb6 | P_EXT)
#define OPC_MOVZWL	(0xb7 | P_EXT)
#define OPC_POP_r32	(0x58)
#define OPC_PUSH_r32	(0x50)
#define OPC_PUSH_Iv	(0x68)
#define OPC_PUSH_Ib	(0x6a)
#define OPC_RET		(0xc3)
#define OPC_SETCC	(0x90 | P_EXT)	/* ... plus condition code */
#define OPC_SHIFT_1	(0xd1)
#define OPC_SHIFT_Ib	(0xc1)
#define OPC_SHIFT_cl	(0xd3)
#define OPC_TESTL	(0x85)
#define OPC_XCHG_ax_r32	(0x90)

#define OPC_GRP3_Ev	(0xf7)
#define OPC_GRP5	(0xff)

/* Group 1 opcode extensions for 0x80-0x83.
   These are also used as modifiers for OPC_ARITH.  */
#define ARITH_ADD 0
#define ARITH_OR  1
#define ARITH_ADC 2
#define ARITH_SBB 3
#define ARITH_AND 4
#define ARITH_SUB 5
#define ARITH_XOR 6
#define ARITH_CMP 7

/* Group 2 opcode extensions for 0xc0, 0xc1, 0xd0-0xd3.  */
#define SHIFT_ROL 0
#define SHIFT_ROR 1
#define SHIFT_SHL 4
#define SHIFT_SHR 5
#define SHIFT_SAR 7

/* Group 3 opcode extensions for 0xf6, 0xf7.  To be used with OPC_GRP3.  */
#define EXT3_NOT   2
#define EXT3_NEG   3
#define EXT3_MUL   4
#define EXT3_IMUL  5
#define EXT3_DIV   6
#define EXT3_IDIV  7

/* Group 5 opcode extensions for 0xff.  To be used with OPC_GRP5.  */
#define EXT5_CALLN_Ev	2
#define EXT5_JMPN_Ev	4

/* Condition codes to be added to OPC_JCC_{long,short}.  */
#define JCC_JMP (-1)
#define JCC_JO  0x0
#define JCC_JNO 0x1
#define JCC_JB  0x2
#define JCC_JAE 0x3
#define JCC_JE  0x4
#define JCC_JNE 0x5
#define JCC_JBE 0x6
#define JCC_JA  0x7
#define JCC_JS  0x8
#define JCC_JNS 0x9
#define JCC_JP  0xa
#define JCC_JNP 0xb
#define JCC_JL  0xc
#define JCC_JGE 0xd
#define JCC_JLE 0xe
#define JCC_JG  0xf

static const uint8_t tcg_cond_to_jcc[10] = {
    [TCG_COND_EQ] = JCC_JE,
    [TCG_COND_NE] = JCC_JNE,
    [TCG_COND_LT] = JCC_JL,
    [TCG_COND_GE] = JCC_JGE,
    [TCG_COND_LE] = JCC_JLE,
    [TCG_COND_GT] = JCC_JG,
    [TCG_COND_LTU] = JCC_JB,
    [TCG_COND_GEU] = JCC_JAE,
    [TCG_COND_LEU] = JCC_JBE,
    [TCG_COND_GTU] = JCC_JA,
};

static inline void tcg_out_opc(TCGContext *s, int opc)
{
    if (opc & P_DATA16) {
        tcg_out8(s, 0x66);
    }
    if (opc & P_EXT) {
        tcg_out8(s, 0x0f);
    }
    tcg_out8(s, opc);
}

static inline void tcg_out_modrm(TCGContext *s, int opc, int r, int rm)
{
    tcg_out_opc(s, opc);
    tcg_out8(s, 0xc0 | (r << 3) | rm);
}

/* Output an opcode with a full "rm + (index<<shift) + offset" address mode.
   We handle either RM and INDEX missing with a -1 value.  */

static void tcg_out_modrm_sib_offset(TCGContext *s, int opc, int r, int rm,
                                     int index, int shift, int32_t offset)
{
    int mod, len;

    if (index == -1 && rm == -1) {
        /* Absolute address.  */
        tcg_out_opc(s, opc);
        tcg_out8(s, (r << 3) | 5);
        tcg_out32(s, offset);
        return;
    }

    tcg_out_opc(s, opc);

    /* Find the length of the immediate addend.  Note that the encoding
       that would be used for (%ebp) indicates absolute addressing.  */
    if (rm == -1) {
        mod = 0, len = 4, rm = 5;
    } else if (offset == 0 && rm != TCG_REG_EBP) {
        mod = 0, len = 0;
    } else if (offset == (int8_t)offset) {
        mod = 0x40, len = 1;
    } else {
        mod = 0x80, len = 4;
    }

    /* Use a single byte MODRM format if possible.  Note that the encoding
       that would be used for %esp is the escape to the two byte form.  */
    if (index == -1 && rm != TCG_REG_ESP) {
        /* Single byte MODRM format.  */
        tcg_out8(s, mod | (r << 3) | rm);
    } else {
        /* Two byte MODRM+SIB format.  */

        /* Note that the encoding that would place %esp into the index
           field indicates no index register.  */
        if (index == -1) {
            index = 4;
        } else {
            assert(index != TCG_REG_ESP);
        }

        tcg_out8(s, mod | (r << 3) | 4);
        tcg_out8(s, (shift << 6) | (index << 3) | rm);
    }

    if (len == 1) {
        tcg_out8(s, offset);
    } else if (len == 4) {
        tcg_out32(s, offset);
    }
}

/* rm == -1 means no register index */
static inline void tcg_out_modrm_offset(TCGContext *s, int opc, int r, int rm,
                                        int32_t offset)
{
    tcg_out_modrm_sib_offset(s, opc, r, rm, -1, 0, offset);
}

/* Generate dest op= src.  Uses the same ARITH_* codes as tgen_arithi.  */
static inline void tgen_arithr(TCGContext *s, int subop, int dest, int src)
{
    tcg_out_modrm(s, OPC_ARITH_GvEv + (subop << 3), dest, src);
}

static inline void tcg_out_mov(TCGContext *s, int ret, int arg)
{
    if (arg != ret) {
        tcg_out_modrm(s, OPC_MOVL_GvEv, ret, arg);
    }
}

static inline void tcg_out_movi(TCGContext *s, TCGType type,
                                int ret, int32_t arg)
{
    if (arg == 0) {
        tgen_arithr(s, ARITH_XOR, ret, ret);
    } else {
        tcg_out8(s, OPC_MOVL_Iv + ret);
        tcg_out32(s, arg);
    }
}

static inline void tcg_out_pushi(TCGContext *s, tcg_target_long val)
{
    if (val == (int8_t)val) {
        tcg_out_opc(s, OPC_PUSH_Ib);
        tcg_out8(s, val);
    } else {
        tcg_out_opc(s, OPC_PUSH_Iv);
        tcg_out32(s, val);
    }
}

static inline void tcg_out_push(TCGContext *s, int reg)
{
    tcg_out_opc(s, OPC_PUSH_r32 + reg);
}

static inline void tcg_out_pop(TCGContext *s, int reg)
{
    tcg_out_opc(s, OPC_POP_r32 + reg);
}

static inline void tcg_out_ld(TCGContext *s, TCGType type, int ret,
                              int arg1, tcg_target_long arg2)
{
    tcg_out_modrm_offset(s, OPC_MOVL_GvEv, ret, arg1, arg2);
}

static inline void tcg_out_st(TCGContext *s, TCGType type, int arg,
                              int arg1, tcg_target_long arg2)
{
    tcg_out_modrm_offset(s, OPC_MOVL_EvGv, arg, arg1, arg2);
}

static void tcg_out_shifti(TCGContext *s, int subopc, int reg, int count)
{
    /* Propagate an opcode prefix, such as P_DATA16.  */
    int ext = subopc & ~0x7;
    subopc &= 0x7;

    if (count == 1) {
        tcg_out_modrm(s, OPC_SHIFT_1 | ext, subopc, reg);
    } else {
        tcg_out_modrm(s, OPC_SHIFT_Ib | ext, subopc, reg);
        tcg_out8(s, count);
    }
}

static inline void tcg_out_bswap32(TCGContext *s, int reg)
{
    tcg_out_opc(s, OPC_BSWAP + reg);
}

static inline void tcg_out_rolw_8(TCGContext *s, int reg)
{
    tcg_out_shifti(s, SHIFT_ROL | P_DATA16, reg, 8);
}

static inline void tcg_out_ext8u(TCGContext *s, int dest, int src)
{
    /* movzbl */
    assert(src < 4);
    tcg_out_modrm(s, OPC_MOVZBL, dest, src);
}

static void tcg_out_ext8s(TCGContext *s, int dest, int src)
{
    /* movsbl */
    assert(src < 4);
    tcg_out_modrm(s, OPC_MOVSBL, dest, src);
}

static inline void tcg_out_ext16u(TCGContext *s, int dest, int src)
{
    /* movzwl */
    tcg_out_modrm(s, OPC_MOVZWL, dest, src);
}

static inline void tcg_out_ext16s(TCGContext *s, int dest, int src)
{
    /* movswl */
    tcg_out_modrm(s, OPC_MOVSWL, dest, src);
}

static inline void tgen_arithi(TCGContext *s, int c, int r0,
                               int32_t val, int cf)
{
    /* ??? While INC is 2 bytes shorter than ADDL $1, they also induce
       partial flags update stalls on Pentium4 and are not recommended
       by current Intel optimization manuals.  */
    if (!cf && (c == ARITH_ADD || c == ARITH_SUB) && (val == 1 || val == -1)) {
        int opc = ((c == ARITH_ADD) ^ (val < 0) ? OPC_INC_r32 : OPC_DEC_r32);
        tcg_out_opc(s, opc + r0);
    } else if (val == (int8_t)val) {
        tcg_out_modrm(s, OPC_ARITH_EvIb, c, r0);
        tcg_out8(s, val);
    } else if (c == ARITH_AND && val == 0xffu && r0 < 4) {
        tcg_out_ext8u(s, r0, r0);
    } else if (c == ARITH_AND && val == 0xffffu) {
        tcg_out_ext16u(s, r0, r0);
    } else {
        tcg_out_modrm(s, OPC_ARITH_EvIz, c, r0);
        tcg_out32(s, val);
    }
}

static void tcg_out_addi(TCGContext *s, int reg, tcg_target_long val)
{
    if (val != 0)
        tgen_arithi(s, ARITH_ADD, reg, val, 0);
}

/* Use SMALL != 0 to force a short forward branch.  */
static void tcg_out_jxx(TCGContext *s, int opc, int label_index, int small)
{
    int32_t val, val1;
    TCGLabel *l = &s->labels[label_index];

    if (l->has_value) {
        val = l->u.value - (tcg_target_long)s->code_ptr;
        val1 = val - 2;
        if ((int8_t)val1 == val1) {
            if (opc == -1) {
                tcg_out8(s, OPC_JMP_short);
            } else {
                tcg_out8(s, OPC_JCC_short + opc);
            }
            tcg_out8(s, val1);
        } else {
            if (small) {
                tcg_abort();
            }
            if (opc == -1) {
                tcg_out8(s, OPC_JMP_long);
                tcg_out32(s, val - 5);
            } else {
                tcg_out_opc(s, OPC_JCC_long + opc);
                tcg_out32(s, val - 6);
            }
        }
    } else if (small) {
        if (opc == -1) {
            tcg_out8(s, OPC_JMP_short);
        } else {
            tcg_out8(s, OPC_JCC_short + opc);
        }
        tcg_out_reloc(s, s->code_ptr, R_386_PC8, label_index, -1);
        s->code_ptr += 1;
    } else {
        if (opc == -1) {
            tcg_out8(s, OPC_JMP_long);
        } else {
            tcg_out_opc(s, OPC_JCC_long + opc);
        }
        tcg_out_reloc(s, s->code_ptr, R_386_PC32, label_index, -4);
        s->code_ptr += 4;
    }
}

static void tcg_out_cmp(TCGContext *s, TCGArg arg1, TCGArg arg2,
                        int const_arg2)
{
    if (const_arg2) {
        if (arg2 == 0) {
            /* test r, r */
            tcg_out_modrm(s, OPC_TESTL, arg1, arg1);
        } else {
            tgen_arithi(s, ARITH_CMP, arg1, arg2, 0);
        }
    } else {
        tgen_arithr(s, ARITH_CMP, arg1, arg2);
    }
}

static void tcg_out_brcond(TCGContext *s, TCGCond cond,
                           TCGArg arg1, TCGArg arg2, int const_arg2,
                           int label_index, int small)
{
    tcg_out_cmp(s, arg1, arg2, const_arg2);
    tcg_out_jxx(s, tcg_cond_to_jcc[cond], label_index, small);
}

/* XXX: we implement it at the target level to avoid having to
   handle cross basic blocks temporaries */
static void tcg_out_brcond2(TCGContext *s, const TCGArg *args,
                            const int *const_args, int small)
{
    int label_next;
    label_next = gen_new_label();
    switch(args[4]) {
    case TCG_COND_EQ:
        tcg_out_brcond(s, TCG_COND_NE, args[0], args[2], const_args[2],
                       label_next, 1);
        tcg_out_brcond(s, TCG_COND_EQ, args[1], args[3], const_args[3],
                       args[5], small);
        break;
    case TCG_COND_NE:
        tcg_out_brcond(s, TCG_COND_NE, args[0], args[2], const_args[2],
                       args[5], small);
        tcg_out_brcond(s, TCG_COND_NE, args[1], args[3], const_args[3],
                       args[5], small);
        break;
    case TCG_COND_LT:
        tcg_out_brcond(s, TCG_COND_LT, args[1], args[3], const_args[3],
                       args[5], small);
        tcg_out_jxx(s, JCC_JNE, label_next, 1);
        tcg_out_brcond(s, TCG_COND_LTU, args[0], args[2], const_args[2],
                       args[5], small);
        break;
    case TCG_COND_LE:
        tcg_out_brcond(s, TCG_COND_LT, args[1], args[3], const_args[3],
                       args[5], small);
        tcg_out_jxx(s, JCC_JNE, label_next, 1);
        tcg_out_brcond(s, TCG_COND_LEU, args[0], args[2], const_args[2],
                       args[5], small);
        break;
    case TCG_COND_GT:
        tcg_out_brcond(s, TCG_COND_GT, args[1], args[3], const_args[3],
                       args[5], small);
        tcg_out_jxx(s, JCC_JNE, label_next, 1);
        tcg_out_brcond(s, TCG_COND_GTU, args[0], args[2], const_args[2],
                       args[5], small);
        break;
    case TCG_COND_GE:
        tcg_out_brcond(s, TCG_COND_GT, args[1], args[3], const_args[3],
                       args[5], small);
        tcg_out_jxx(s, JCC_JNE, label_next, 1);
        tcg_out_brcond(s, TCG_COND_GEU, args[0], args[2], const_args[2],
                       args[5], small);
        break;
    case TCG_COND_LTU:
        tcg_out_brcond(s, TCG_COND_LTU, args[1], args[3], const_args[3],
                       args[5], small);
        tcg_out_jxx(s, JCC_JNE, label_next, 1);
        tcg_out_brcond(s, TCG_COND_LTU, args[0], args[2], const_args[2],
                       args[5], small);
        break;
    case TCG_COND_LEU:
        tcg_out_brcond(s, TCG_COND_LTU, args[1], args[3], const_args[3],
                       args[5], small);
        tcg_out_jxx(s, JCC_JNE, label_next, 1);
        tcg_out_brcond(s, TCG_COND_LEU, args[0], args[2], const_args[2],
                       args[5], small);
        break;
    case TCG_COND_GTU:
        tcg_out_brcond(s, TCG_COND_GTU, args[1], args[3], const_args[3],
                       args[5], small);
        tcg_out_jxx(s, JCC_JNE, label_next, 1);
        tcg_out_brcond(s, TCG_COND_GTU, args[0], args[2], const_args[2],
                       args[5], small);
        break;
    case TCG_COND_GEU:
        tcg_out_brcond(s, TCG_COND_GTU, args[1], args[3], const_args[3],
                       args[5], small);
        tcg_out_jxx(s, JCC_JNE, label_next, 1);
        tcg_out_brcond(s, TCG_COND_GEU, args[0], args[2], const_args[2],
                       args[5], small);
        break;
    default:
        tcg_abort();
    }
    tcg_out_label(s, label_next, (tcg_target_long)s->code_ptr);
}

static void tcg_out_setcond(TCGContext *s, TCGCond cond, TCGArg dest,
                            TCGArg arg1, TCGArg arg2, int const_arg2)
{
    tcg_out_cmp(s, arg1, arg2, const_arg2);
    tcg_out_modrm(s, OPC_SETCC | tcg_cond_to_jcc[cond], 0, dest);
    tcg_out_ext8u(s, dest, dest);
}

static void tcg_out_setcond2(TCGContext *s, const TCGArg *args,
                             const int *const_args)
{
    TCGArg new_args[6];
    int label_true, label_over;

    memcpy(new_args, args+1, 5*sizeof(TCGArg));

    if (args[0] == args[1] || args[0] == args[2]
        || (!const_args[3] && args[0] == args[3])
        || (!const_args[4] && args[0] == args[4])) {
        /* When the destination overlaps with one of the argument
           registers, don't do anything tricky.  */
        label_true = gen_new_label();
        label_over = gen_new_label();

        new_args[5] = label_true;
        tcg_out_brcond2(s, new_args, const_args+1, 1);

        tcg_out_movi(s, TCG_TYPE_I32, args[0], 0);
        tcg_out_jxx(s, JCC_JMP, label_over, 1);
        tcg_out_label(s, label_true, (tcg_target_long)s->code_ptr);

        tcg_out_movi(s, TCG_TYPE_I32, args[0], 1);
        tcg_out_label(s, label_over, (tcg_target_long)s->code_ptr);
    } else {
        /* When the destination does not overlap one of the arguments,
           clear the destination first, jump if cond false, and emit an
           increment in the true case.  This results in smaller code.  */

        tcg_out_movi(s, TCG_TYPE_I32, args[0], 0);

        label_over = gen_new_label();
        new_args[4] = tcg_invert_cond(new_args[4]);
        new_args[5] = label_over;
        tcg_out_brcond2(s, new_args, const_args+1, 1);

        tgen_arithi(s, ARITH_ADD, args[0], 1, 0);
        tcg_out_label(s, label_over, (tcg_target_long)s->code_ptr);
    }
}

static void tcg_out_calli(TCGContext *s, tcg_target_long dest)
{
    tcg_out_opc(s, OPC_CALL_Jz);
    tcg_out32(s, dest - (tcg_target_long)s->code_ptr - 4);
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

/* Perform the TLB load and compare.

   Inputs:
   ADDRLO_IDX contains the index into ARGS of the low part of the
   address; the high part of the address is at ADDR_LOW_IDX+1.

   MEM_INDEX and S_BITS are the memory context and log2 size of the load.

   WHICH is the offset into the CPUTLBEntry structure of the slot to read.
   This should be offsetof addr_read or addr_write.

   Outputs:
   LABEL_PTRS is filled with 1 (32-bit addresses) or 2 (64-bit addresses)
   positions of the displacements of forward jumps to the TLB miss case.

   EAX is loaded with the low part of the address.  In the TLB hit case,
   it has been adjusted as indicated by the TLB and so is a host address.
   In the TLB miss case, it continues to hold a guest address.

   EDX is clobbered.  */

static inline void tcg_out_tlb_load(TCGContext *s, int addrlo_idx,
                                    int mem_index, int s_bits,
                                    const TCGArg *args,
                                    uint8_t **label_ptr, int which)
{
    const int addrlo = args[addrlo_idx];
    const int r0 = TCG_REG_EAX;
    const int r1 = TCG_REG_EDX;

    tcg_out_mov(s, r1, addrlo);
    tcg_out_mov(s, r0, addrlo);

    tcg_out_shifti(s, SHIFT_SHR, r1, TARGET_PAGE_BITS - CPU_TLB_ENTRY_BITS);

    tgen_arithi(s, ARITH_AND, r0, TARGET_PAGE_MASK | ((1 << s_bits) - 1), 0);
    tgen_arithi(s, ARITH_AND, r1, (CPU_TLB_SIZE - 1) << CPU_TLB_ENTRY_BITS, 0);

    tcg_out_modrm_sib_offset(s, OPC_LEA, r1, TCG_AREG0, r1, 0,
                             offsetof(CPUState, tlb_table[mem_index][0])
                             + which);

    /* cmp 0(r1), r0 */
    tcg_out_modrm_offset(s, OPC_CMP_GvEv, r0, r1, 0);

    tcg_out_mov(s, r0, addrlo);

    /* jne label1 */
    tcg_out8(s, OPC_JCC_short + JCC_JNE);
    label_ptr[0] = s->code_ptr;
    s->code_ptr++;

    if (TARGET_LONG_BITS == 64) {
        /* cmp 4(r1), addrhi */
        tcg_out_modrm_offset(s, OPC_CMP_GvEv, args[addrlo_idx+1], r1, 4);

        /* jne label1 */
        tcg_out8(s, OPC_JCC_short + JCC_JNE);
        label_ptr[1] = s->code_ptr;
        s->code_ptr++;
    }

    /* TLB Hit.  */

    /* add addend(r1), r0 */
    tcg_out_modrm_offset(s, OPC_ADD_GvEv, r0, r1,
                         offsetof(CPUTLBEntry, addend) - which);
}
#endif

static void tcg_out_qemu_ld_direct(TCGContext *s, int datalo, int datahi,
                                   int base, tcg_target_long ofs, int sizeop)
{
#ifdef TARGET_WORDS_BIGENDIAN
    const int bswap = 1;
#else
    const int bswap = 0;
#endif
    switch (sizeop) {
    case 0:
        /* movzbl */
        tcg_out_modrm_offset(s, OPC_MOVZBL, datalo, base, ofs);
        break;
    case 0 | 4:
        /* movsbl */
        tcg_out_modrm_offset(s, OPC_MOVSBL, datalo, base, ofs);
        break;
    case 1:
        /* movzwl */
        tcg_out_modrm_offset(s, OPC_MOVZWL, datalo, base, ofs);
        if (bswap) {
            tcg_out_rolw_8(s, datalo);
        }
        break;
    case 1 | 4:
        /* movswl */
        tcg_out_modrm_offset(s, OPC_MOVSWL, datalo, base, ofs);
        if (bswap) {
            tcg_out_rolw_8(s, datalo);
            tcg_out_modrm(s, OPC_MOVSWL, datalo, datalo);
        }
        break;
    case 2:
        tcg_out_ld(s, TCG_TYPE_I32, datalo, base, ofs);
        if (bswap) {
            tcg_out_bswap32(s, datalo);
        }
        break;
    case 3:
        if (bswap) {
            int t = datalo;
            datalo = datahi;
            datahi = t;
        }
        if (base != datalo) {
            tcg_out_ld(s, TCG_TYPE_I32, datalo, base, ofs);
            tcg_out_ld(s, TCG_TYPE_I32, datahi, base, ofs + 4);
        } else {
            tcg_out_ld(s, TCG_TYPE_I32, datahi, base, ofs + 4);
            tcg_out_ld(s, TCG_TYPE_I32, datalo, base, ofs);
        }
        if (bswap) {
            tcg_out_bswap32(s, datalo);
            tcg_out_bswap32(s, datahi);
        }
        break;
    default:
        tcg_abort();
    }
}

/* XXX: qemu_ld and qemu_st could be modified to clobber only EDX and
   EAX. It will be useful once fixed registers globals are less
   common. */
static void tcg_out_qemu_ld(TCGContext *s, const TCGArg *args,
                            int opc)
{
    int data_reg, data_reg2 = 0;
    int addrlo_idx;
#if defined(CONFIG_SOFTMMU)
    int mem_index, s_bits, arg_idx;
    uint8_t *label_ptr[3];
#endif

    data_reg = args[0];
    addrlo_idx = 1;
    if (opc == 3) {
        data_reg2 = args[1];
        addrlo_idx = 2;
    }

#if defined(CONFIG_SOFTMMU)
    mem_index = args[addrlo_idx + (TARGET_LONG_BITS / 32)];
    s_bits = opc & 3;

    tcg_out_tlb_load(s, addrlo_idx, mem_index, s_bits, args,
                     label_ptr, offsetof(CPUTLBEntry, addr_read));

    /* TLB Hit.  */
    tcg_out_qemu_ld_direct(s, data_reg, data_reg2, TCG_REG_EAX, 0, opc);

    /* jmp label2 */
    tcg_out8(s, OPC_JMP_short);
    label_ptr[2] = s->code_ptr;
    s->code_ptr++;

    /* TLB Miss.  */

    /* label1: */
    *label_ptr[0] = s->code_ptr - label_ptr[0] - 1;
    if (TARGET_LONG_BITS == 64) {
        *label_ptr[1] = s->code_ptr - label_ptr[1] - 1;
    }

    /* XXX: move that code at the end of the TB */
    /* EAX is already loaded.  */
    arg_idx = 1;
    if (TARGET_LONG_BITS == 64) {
        tcg_out_mov(s, tcg_target_call_iarg_regs[arg_idx++],
                    args[addrlo_idx + 1]);
    }
    tcg_out_movi(s, TCG_TYPE_I32, tcg_target_call_iarg_regs[arg_idx],
                 mem_index);
    tcg_out_calli(s, (tcg_target_long)qemu_ld_helpers[s_bits]);

    switch(opc) {
    case 0 | 4:
        tcg_out_ext8s(s, data_reg, TCG_REG_EAX);
        break;
    case 1 | 4:
        tcg_out_ext16s(s, data_reg, TCG_REG_EAX);
        break;
    case 0:
        tcg_out_ext8u(s, data_reg, TCG_REG_EAX);
        break;
    case 1:
        tcg_out_ext16u(s, data_reg, TCG_REG_EAX);
        break;
    case 2:
    default:
        tcg_out_mov(s, data_reg, TCG_REG_EAX);
        break;
    case 3:
        if (data_reg == TCG_REG_EDX) {
            /* xchg %edx, %eax */
            tcg_out_opc(s, OPC_XCHG_ax_r32 + TCG_REG_EDX);
            tcg_out_mov(s, data_reg2, TCG_REG_EAX);
        } else {
            tcg_out_mov(s, data_reg, TCG_REG_EAX);
            tcg_out_mov(s, data_reg2, TCG_REG_EDX);
        }
        break;
    }

    /* label2: */
    *label_ptr[2] = s->code_ptr - label_ptr[2] - 1;
#else
    tcg_out_qemu_ld_direct(s, data_reg, data_reg2,
                           args[addrlo_idx], GUEST_BASE, opc);
#endif
}

static void tcg_out_qemu_st_direct(TCGContext *s, int datalo, int datahi,
                                   int base, tcg_target_long ofs, int sizeop)
{
#ifdef TARGET_WORDS_BIGENDIAN
    const int bswap = 1;
#else
    const int bswap = 0;
#endif
    /* ??? Ideally we wouldn't need a scratch register.  For user-only,
       we could perform the bswap twice to restore the original value
       instead of moving to the scratch.  But as it is, the L constraint
       means that EDX is definitely free here.  */
    int scratch = TCG_REG_EDX;

    switch (sizeop) {
    case 0:
        tcg_out_modrm_offset(s, OPC_MOVB_EvGv, datalo, base, ofs);
        break;
    case 1:
        if (bswap) {
            tcg_out_mov(s, scratch, datalo);
            tcg_out_rolw_8(s, scratch);
            datalo = scratch;
        }
        /* movw */
        tcg_out_modrm_offset(s, OPC_MOVL_EvGv | P_DATA16,
                             datalo, base, ofs);
        break;
    case 2:
        if (bswap) {
            tcg_out_mov(s, scratch, datalo);
            tcg_out_bswap32(s, scratch);
            datalo = scratch;
        }
        tcg_out_st(s, TCG_TYPE_I32, datalo, base, ofs);
        break;
    case 3:
        if (bswap) {
            tcg_out_mov(s, scratch, datahi);
            tcg_out_bswap32(s, scratch);
            tcg_out_st(s, TCG_TYPE_I32, scratch, base, ofs);
            tcg_out_mov(s, scratch, datalo);
            tcg_out_bswap32(s, scratch);
            tcg_out_st(s, TCG_TYPE_I32, scratch, base, ofs + 4);
        } else {
            tcg_out_st(s, TCG_TYPE_I32, datalo, base, ofs);
            tcg_out_st(s, TCG_TYPE_I32, datahi, base, ofs + 4);
        }
        break;
    default:
        tcg_abort();
    }
}

static void tcg_out_qemu_st(TCGContext *s, const TCGArg *args,
                            int opc)
{
    int data_reg, data_reg2 = 0;
    int addrlo_idx;
#if defined(CONFIG_SOFTMMU)
    int mem_index, s_bits;
    int stack_adjust;
    uint8_t *label_ptr[3];
#endif

    data_reg = args[0];
    addrlo_idx = 1;
    if (opc == 3) {
        data_reg2 = args[1];
        addrlo_idx = 2;
    }

#if defined(CONFIG_SOFTMMU)
    mem_index = args[addrlo_idx + (TARGET_LONG_BITS / 32)];
    s_bits = opc;

    tcg_out_tlb_load(s, addrlo_idx, mem_index, s_bits, args,
                     label_ptr, offsetof(CPUTLBEntry, addr_write));

    /* TLB Hit.  */
    tcg_out_qemu_st_direct(s, data_reg, data_reg2, TCG_REG_EAX, 0, opc);

    /* jmp label2 */
    tcg_out8(s, OPC_JMP_short);
    label_ptr[2] = s->code_ptr;
    s->code_ptr++;

    /* TLB Miss.  */

    /* label1: */
    *label_ptr[0] = s->code_ptr - label_ptr[0] - 1;
    if (TARGET_LONG_BITS == 64) {
        *label_ptr[1] = s->code_ptr - label_ptr[1] - 1;
    }

    /* XXX: move that code at the end of the TB */
    if (TARGET_LONG_BITS == 32) {
        tcg_out_mov(s, TCG_REG_EDX, data_reg);
        if (opc == 3) {
            tcg_out_mov(s, TCG_REG_ECX, data_reg2);
            tcg_out_pushi(s, mem_index);
            stack_adjust = 4;
        } else {
            tcg_out_movi(s, TCG_TYPE_I32, TCG_REG_ECX, mem_index);
            stack_adjust = 0;
        }
    } else {
        if (opc == 3) {
            tcg_out_mov(s, TCG_REG_EDX, args[addrlo_idx + 1]);
            tcg_out_pushi(s, mem_index);
            tcg_out_push(s, data_reg2);
            tcg_out_push(s, data_reg);
            stack_adjust = 12;
        } else {
            tcg_out_mov(s, TCG_REG_EDX, args[addrlo_idx + 1]);
            switch(opc) {
            case 0:
                tcg_out_ext8u(s, TCG_REG_ECX, data_reg);
                break;
            case 1:
                tcg_out_ext16u(s, TCG_REG_ECX, data_reg);
                break;
            case 2:
                tcg_out_mov(s, TCG_REG_ECX, data_reg);
                break;
            }
            tcg_out_pushi(s, mem_index);
            stack_adjust = 4;
        }
    }

    tcg_out_calli(s, (tcg_target_long)qemu_st_helpers[s_bits]);

    if (stack_adjust == 4) {
        /* Pop and discard.  This is 2 bytes smaller than the add.  */
        tcg_out_pop(s, TCG_REG_ECX);
    } else if (stack_adjust != 0) {
        tcg_out_addi(s, TCG_REG_ESP, stack_adjust);
    }

    /* label2: */
    *label_ptr[2] = s->code_ptr - label_ptr[2] - 1;
#else
    tcg_out_qemu_st_direct(s, data_reg, data_reg2,
                           args[addrlo_idx], GUEST_BASE, opc);
#endif
}

static inline void tcg_out_op(TCGContext *s, TCGOpcode opc,
                              const TCGArg *args, const int *const_args)
{
    int c;

    switch(opc) {
    case INDEX_op_exit_tb:
        tcg_out_movi(s, TCG_TYPE_I32, TCG_REG_EAX, args[0]);
        tcg_out8(s, OPC_JMP_long); /* jmp tb_ret_addr */
        tcg_out32(s, tb_ret_addr - s->code_ptr - 4);
        break;
    case INDEX_op_goto_tb:
        if (s->tb_jmp_offset) {
            /* direct jump method */
            tcg_out8(s, OPC_JMP_long); /* jmp im */
            s->tb_jmp_offset[args[0]] = s->code_ptr - s->code_buf;
            tcg_out32(s, 0);
        } else {
            /* indirect jump method */
            tcg_out_modrm_offset(s, OPC_GRP5, EXT5_JMPN_Ev, -1,
                                 (tcg_target_long)(s->tb_next + args[0]));
        }
        s->tb_next_offset[args[0]] = s->code_ptr - s->code_buf;
        break;
    case INDEX_op_call:
        if (const_args[0]) {
            tcg_out_calli(s, args[0]);
        } else {
            /* call *reg */
            tcg_out_modrm(s, OPC_GRP5, EXT5_CALLN_Ev, args[0]);
        }
        break;
    case INDEX_op_jmp:
        if (const_args[0]) {
            tcg_out8(s, OPC_JMP_long);
            tcg_out32(s, args[0] - (tcg_target_long)s->code_ptr - 4);
        } else {
            /* jmp *reg */
            tcg_out_modrm(s, OPC_GRP5, EXT5_JMPN_Ev, args[0]);
        }
        break;
    case INDEX_op_br:
        tcg_out_jxx(s, JCC_JMP, args[0], 0);
        break;
    case INDEX_op_movi_i32:
        tcg_out_movi(s, TCG_TYPE_I32, args[0], args[1]);
        break;
    case INDEX_op_ld8u_i32:
        /* movzbl */
        tcg_out_modrm_offset(s, OPC_MOVZBL, args[0], args[1], args[2]);
        break;
    case INDEX_op_ld8s_i32:
        /* movsbl */
        tcg_out_modrm_offset(s, OPC_MOVSBL, args[0], args[1], args[2]);
        break;
    case INDEX_op_ld16u_i32:
        /* movzwl */
        tcg_out_modrm_offset(s, OPC_MOVZWL, args[0], args[1], args[2]);
        break;
    case INDEX_op_ld16s_i32:
        /* movswl */
        tcg_out_modrm_offset(s, OPC_MOVSWL, args[0], args[1], args[2]);
        break;
    case INDEX_op_ld_i32:
        tcg_out_ld(s, TCG_TYPE_I32, args[0], args[1], args[2]);
        break;
    case INDEX_op_st8_i32:
        /* movb */
        tcg_out_modrm_offset(s, OPC_MOVB_EvGv, args[0], args[1], args[2]);
        break;
    case INDEX_op_st16_i32:
        /* movw */
        tcg_out_modrm_offset(s, OPC_MOVL_EvGv | P_DATA16,
                             args[0], args[1], args[2]);
        break;
    case INDEX_op_st_i32:
        tcg_out_st(s, TCG_TYPE_I32, args[0], args[1], args[2]);
        break;
    case INDEX_op_add_i32:
        /* For 3-operand addition, use LEA.  */
        if (args[0] != args[1]) {
            TCGArg a0 = args[0], a1 = args[1], a2 = args[2], c3 = 0;

            if (const_args[2]) {
                c3 = a2, a2 = -1;
            } else if (a0 == a2) {
                /* Watch out for dest = src + dest, since we've removed
                   the matching constraint on the add.  */
                tgen_arithr(s, ARITH_ADD, a0, a1);
                break;
            }

            tcg_out_modrm_sib_offset(s, OPC_LEA, a0, a1, a2, 0, c3);
            break;
        }
        c = ARITH_ADD;
        goto gen_arith;
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
    gen_arith:
        if (const_args[2]) {
            tgen_arithi(s, c, args[0], args[2], 0);
        } else {
            tgen_arithr(s, c, args[0], args[2]);
        }
        break;
    case INDEX_op_mul_i32:
        if (const_args[2]) {
            int32_t val;
            val = args[2];
            if (val == (int8_t)val) {
                tcg_out_modrm(s, OPC_IMUL_GvEvIb, args[0], args[0]);
                tcg_out8(s, val);
            } else {
                tcg_out_modrm(s, OPC_IMUL_GvEvIz, args[0], args[0]);
                tcg_out32(s, val);
            }
        } else {
            tcg_out_modrm(s, OPC_IMUL_GvEv, args[0], args[2]);
        }
        break;
    case INDEX_op_mulu2_i32:
        tcg_out_modrm(s, OPC_GRP3_Ev, EXT3_MUL, args[3]);
        break;
    case INDEX_op_div2_i32:
        tcg_out_modrm(s, OPC_GRP3_Ev, EXT3_IDIV, args[4]);
        break;
    case INDEX_op_divu2_i32:
        tcg_out_modrm(s, OPC_GRP3_Ev, EXT3_DIV, args[4]);
        break;
    case INDEX_op_shl_i32:
        c = SHIFT_SHL;
    gen_shift32:
        if (const_args[2]) {
            tcg_out_shifti(s, c, args[0], args[2]);
        } else {
            tcg_out_modrm(s, OPC_SHIFT_cl, c, args[0]);
        }
        break;
    case INDEX_op_shr_i32:
        c = SHIFT_SHR;
        goto gen_shift32;
    case INDEX_op_sar_i32:
        c = SHIFT_SAR;
        goto gen_shift32;
    case INDEX_op_rotl_i32:
        c = SHIFT_ROL;
        goto gen_shift32;
    case INDEX_op_rotr_i32:
        c = SHIFT_ROR;
        goto gen_shift32;

    case INDEX_op_add2_i32:
        if (const_args[4]) {
            tgen_arithi(s, ARITH_ADD, args[0], args[4], 1);
        } else {
            tgen_arithr(s, ARITH_ADD, args[0], args[4]);
        }
        if (const_args[5]) {
            tgen_arithi(s, ARITH_ADC, args[1], args[5], 1);
        } else {
            tgen_arithr(s, ARITH_ADC, args[1], args[5]);
        }
        break;
    case INDEX_op_sub2_i32:
        if (const_args[4]) {
            tgen_arithi(s, ARITH_SUB, args[0], args[4], 1);
        } else {
            tgen_arithr(s, ARITH_SUB, args[0], args[4]);
        }
        if (const_args[5]) {
            tgen_arithi(s, ARITH_SBB, args[1], args[5], 1);
        } else {
            tgen_arithr(s, ARITH_SBB, args[1], args[5]);
        }
        break;
    case INDEX_op_brcond_i32:
        tcg_out_brcond(s, args[2], args[0], args[1], const_args[1],
                       args[3], 0);
        break;
    case INDEX_op_brcond2_i32:
        tcg_out_brcond2(s, args, const_args, 0);
        break;

    case INDEX_op_bswap16_i32:
        tcg_out_rolw_8(s, args[0]);
        break;
    case INDEX_op_bswap32_i32:
        tcg_out_bswap32(s, args[0]);
        break;

    case INDEX_op_neg_i32:
        tcg_out_modrm(s, OPC_GRP3_Ev, EXT3_NEG, args[0]);
        break;

    case INDEX_op_not_i32:
        tcg_out_modrm(s, OPC_GRP3_Ev, EXT3_NOT, args[0]);
        break;

    case INDEX_op_ext8s_i32:
        tcg_out_ext8s(s, args[0], args[1]);
        break;
    case INDEX_op_ext16s_i32:
        tcg_out_ext16s(s, args[0], args[1]);
        break;
    case INDEX_op_ext8u_i32:
        tcg_out_ext8u(s, args[0], args[1]);
        break;
    case INDEX_op_ext16u_i32:
        tcg_out_ext16u(s, args[0], args[1]);
        break;

    case INDEX_op_setcond_i32:
        tcg_out_setcond(s, args[3], args[0], args[1], args[2], const_args[2]);
        break;
    case INDEX_op_setcond2_i32:
        tcg_out_setcond2(s, args, const_args);
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

static const TCGTargetOpDef x86_op_defs[] = {
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
    { INDEX_op_st8_i32, { "q", "r" } },
    { INDEX_op_st16_i32, { "r", "r" } },
    { INDEX_op_st_i32, { "r", "r" } },

    { INDEX_op_add_i32, { "r", "r", "ri" } },
    { INDEX_op_sub_i32, { "r", "0", "ri" } },
    { INDEX_op_mul_i32, { "r", "0", "ri" } },
    { INDEX_op_mulu2_i32, { "a", "d", "a", "r" } },
    { INDEX_op_div2_i32, { "a", "d", "0", "1", "r" } },
    { INDEX_op_divu2_i32, { "a", "d", "0", "1", "r" } },
    { INDEX_op_and_i32, { "r", "0", "ri" } },
    { INDEX_op_or_i32, { "r", "0", "ri" } },
    { INDEX_op_xor_i32, { "r", "0", "ri" } },

    { INDEX_op_shl_i32, { "r", "0", "ci" } },
    { INDEX_op_shr_i32, { "r", "0", "ci" } },
    { INDEX_op_sar_i32, { "r", "0", "ci" } },
    { INDEX_op_rotl_i32, { "r", "0", "ci" } },
    { INDEX_op_rotr_i32, { "r", "0", "ci" } },

    { INDEX_op_brcond_i32, { "r", "ri" } },

    { INDEX_op_add2_i32, { "r", "r", "0", "1", "ri", "ri" } },
    { INDEX_op_sub2_i32, { "r", "r", "0", "1", "ri", "ri" } },
    { INDEX_op_brcond2_i32, { "r", "r", "ri", "ri" } },

    { INDEX_op_bswap16_i32, { "r", "0" } },
    { INDEX_op_bswap32_i32, { "r", "0" } },

    { INDEX_op_neg_i32, { "r", "0" } },

    { INDEX_op_not_i32, { "r", "0" } },

    { INDEX_op_ext8s_i32, { "r", "q" } },
    { INDEX_op_ext16s_i32, { "r", "r" } },
    { INDEX_op_ext8u_i32, { "r", "q" } },
    { INDEX_op_ext16u_i32, { "r", "r" } },

    { INDEX_op_setcond_i32, { "q", "r", "ri" } },
    { INDEX_op_setcond2_i32, { "r", "r", "r", "ri", "ri" } },

#if TARGET_LONG_BITS == 32
    { INDEX_op_qemu_ld8u, { "r", "L" } },
    { INDEX_op_qemu_ld8s, { "r", "L" } },
    { INDEX_op_qemu_ld16u, { "r", "L" } },
    { INDEX_op_qemu_ld16s, { "r", "L" } },
    { INDEX_op_qemu_ld32, { "r", "L" } },
    { INDEX_op_qemu_ld64, { "r", "r", "L" } },

    { INDEX_op_qemu_st8, { "cb", "L" } },
    { INDEX_op_qemu_st16, { "L", "L" } },
    { INDEX_op_qemu_st32, { "L", "L" } },
    { INDEX_op_qemu_st64, { "L", "L", "L" } },
#else
    { INDEX_op_qemu_ld8u, { "r", "L", "L" } },
    { INDEX_op_qemu_ld8s, { "r", "L", "L" } },
    { INDEX_op_qemu_ld16u, { "r", "L", "L" } },
    { INDEX_op_qemu_ld16s, { "r", "L", "L" } },
    { INDEX_op_qemu_ld32, { "r", "L", "L" } },
    { INDEX_op_qemu_ld64, { "r", "r", "L", "L" } },

    { INDEX_op_qemu_st8, { "cb", "L", "L" } },
    { INDEX_op_qemu_st16, { "L", "L", "L" } },
    { INDEX_op_qemu_st32, { "L", "L", "L" } },
    { INDEX_op_qemu_st64, { "L", "L", "L", "L" } },
#endif
    { -1 },
};

static int tcg_target_callee_save_regs[] = {
    /*    TCG_REG_EBP, */ /* currently used for the global env, so no
                             need to save */
    TCG_REG_EBX,
    TCG_REG_ESI,
    TCG_REG_EDI,
};

/* Generate global QEMU prologue and epilogue code */
void tcg_target_qemu_prologue(TCGContext *s)
{
    int i, frame_size, push_size, stack_addend;

    /* TB prologue */
    /* save all callee saved registers */
    for(i = 0; i < ARRAY_SIZE(tcg_target_callee_save_regs); i++) {
        tcg_out_push(s, tcg_target_callee_save_regs[i]);
    }
    /* reserve some stack space */
    push_size = 4 + ARRAY_SIZE(tcg_target_callee_save_regs) * 4;
    frame_size = push_size + TCG_STATIC_CALL_ARGS_SIZE;
    frame_size = (frame_size + TCG_TARGET_STACK_ALIGN - 1) &
        ~(TCG_TARGET_STACK_ALIGN - 1);
    stack_addend = frame_size - push_size;
    tcg_out_addi(s, TCG_REG_ESP, -stack_addend);

    tcg_out_modrm(s, OPC_GRP5, EXT5_JMPN_Ev, TCG_REG_EAX); /* jmp *%eax */

    /* TB epilogue */
    tb_ret_addr = s->code_ptr;
    tcg_out_addi(s, TCG_REG_ESP, stack_addend);
    for(i = ARRAY_SIZE(tcg_target_callee_save_regs) - 1; i >= 0; i--) {
        tcg_out_pop(s, tcg_target_callee_save_regs[i]);
    }
    tcg_out_opc(s, OPC_RET);
}

void tcg_target_init(TCGContext *s)
{
#if !defined(CONFIG_USER_ONLY)
    /* fail safe */
    if ((1 << CPU_TLB_ENTRY_BITS) != sizeof(CPUTLBEntry))
        tcg_abort();
#endif

    tcg_regset_set32(tcg_target_available_regs[TCG_TYPE_I32], 0, 0xff);

    tcg_regset_clear(tcg_target_call_clobber_regs);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_EAX);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_EDX);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_ECX);

    tcg_regset_clear(s->reserved_regs);
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_ESP);

    tcg_add_target_add_op_defs(x86_op_defs);
}

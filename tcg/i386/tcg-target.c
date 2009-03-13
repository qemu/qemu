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
    TCG_REG_EAX,
    TCG_REG_EDX,
    TCG_REG_ECX,
    TCG_REG_EBX,
    TCG_REG_ESI,
    TCG_REG_EDI,
    TCG_REG_EBP,
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

#define ARITH_ADD 0
#define ARITH_OR  1
#define ARITH_ADC 2
#define ARITH_SBB 3
#define ARITH_AND 4
#define ARITH_SUB 5
#define ARITH_XOR 6
#define ARITH_CMP 7

#define SHIFT_ROL 0
#define SHIFT_ROR 1
#define SHIFT_SHL 4
#define SHIFT_SHR 5
#define SHIFT_SAR 7

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

#define P_EXT   0x100 /* 0x0f opcode prefix */

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
    if (opc & P_EXT)
        tcg_out8(s, 0x0f);
    tcg_out8(s, opc);
}

static inline void tcg_out_modrm(TCGContext *s, int opc, int r, int rm)
{
    tcg_out_opc(s, opc);
    tcg_out8(s, 0xc0 | (r << 3) | rm);
}

/* rm == -1 means no register index */
static inline void tcg_out_modrm_offset(TCGContext *s, int opc, int r, int rm, 
                                        int32_t offset)
{
    tcg_out_opc(s, opc);
    if (rm == -1) {
        tcg_out8(s, 0x05 | (r << 3));
        tcg_out32(s, offset);
    } else if (offset == 0 && rm != TCG_REG_EBP) {
        if (rm == TCG_REG_ESP) {
            tcg_out8(s, 0x04 | (r << 3));
            tcg_out8(s, 0x24);
        } else {
            tcg_out8(s, 0x00 | (r << 3) | rm);
        }
    } else if ((int8_t)offset == offset) {
        if (rm == TCG_REG_ESP) {
            tcg_out8(s, 0x44 | (r << 3));
            tcg_out8(s, 0x24);
        } else {
            tcg_out8(s, 0x40 | (r << 3) | rm);
        }
        tcg_out8(s, offset);
    } else {
        if (rm == TCG_REG_ESP) {
            tcg_out8(s, 0x84 | (r << 3));
            tcg_out8(s, 0x24);
        } else {
            tcg_out8(s, 0x80 | (r << 3) | rm);
        }
        tcg_out32(s, offset);
    }
}

static inline void tcg_out_mov(TCGContext *s, int ret, int arg)
{
    if (arg != ret)
        tcg_out_modrm(s, 0x8b, ret, arg);
}

static inline void tcg_out_movi(TCGContext *s, TCGType type,
                                int ret, int32_t arg)
{
    if (arg == 0) {
        /* xor r0,r0 */
        tcg_out_modrm(s, 0x01 | (ARITH_XOR << 3), ret, ret);
    } else {
        tcg_out8(s, 0xb8 + ret);
        tcg_out32(s, arg);
    }
}

static inline void tcg_out_ld(TCGContext *s, TCGType type, int ret,
                              int arg1, tcg_target_long arg2)
{
    /* movl */
    tcg_out_modrm_offset(s, 0x8b, ret, arg1, arg2);
}

static inline void tcg_out_st(TCGContext *s, TCGType type, int arg,
                              int arg1, tcg_target_long arg2)
{
    /* movl */
    tcg_out_modrm_offset(s, 0x89, arg, arg1, arg2);
}

static inline void tgen_arithi(TCGContext *s, int c, int r0, int32_t val)
{
    if (val == (int8_t)val) {
        tcg_out_modrm(s, 0x83, c, r0);
        tcg_out8(s, val);
    } else {
        tcg_out_modrm(s, 0x81, c, r0);
        tcg_out32(s, val);
    }
}

static void tcg_out_addi(TCGContext *s, int reg, tcg_target_long val)
{
    if (val != 0)
        tgen_arithi(s, ARITH_ADD, reg, val);
}

static void tcg_out_jxx(TCGContext *s, int opc, int label_index)
{
    int32_t val, val1;
    TCGLabel *l = &s->labels[label_index];
    
    if (l->has_value) {
        val = l->u.value - (tcg_target_long)s->code_ptr;
        val1 = val - 2;
        if ((int8_t)val1 == val1) {
            if (opc == -1)
                tcg_out8(s, 0xeb);
            else
                tcg_out8(s, 0x70 + opc);
            tcg_out8(s, val1);
        } else {
            if (opc == -1) {
                tcg_out8(s, 0xe9);
                tcg_out32(s, val - 5);
            } else {
                tcg_out8(s, 0x0f);
                tcg_out8(s, 0x80 + opc);
                tcg_out32(s, val - 6);
            }
        }
    } else {
        if (opc == -1) {
            tcg_out8(s, 0xe9);
        } else {
            tcg_out8(s, 0x0f);
            tcg_out8(s, 0x80 + opc);
        }
        tcg_out_reloc(s, s->code_ptr, R_386_PC32, label_index, -4);
        s->code_ptr += 4;
    }
}

static void tcg_out_brcond(TCGContext *s, int cond, 
                           TCGArg arg1, TCGArg arg2, int const_arg2,
                           int label_index)
{
    if (const_arg2) {
        if (arg2 == 0) {
            /* test r, r */
            tcg_out_modrm(s, 0x85, arg1, arg1);
        } else {
            tgen_arithi(s, ARITH_CMP, arg1, arg2);
        }
    } else {
        tcg_out_modrm(s, 0x01 | (ARITH_CMP << 3), arg2, arg1);
    }
    tcg_out_jxx(s, tcg_cond_to_jcc[cond], label_index);
}

/* XXX: we implement it at the target level to avoid having to
   handle cross basic blocks temporaries */
static void tcg_out_brcond2(TCGContext *s,
                            const TCGArg *args, const int *const_args)
{
    int label_next;
    label_next = gen_new_label();
    switch(args[4]) {
    case TCG_COND_EQ:
        tcg_out_brcond(s, TCG_COND_NE, args[0], args[2], const_args[2], label_next);
        tcg_out_brcond(s, TCG_COND_EQ, args[1], args[3], const_args[3], args[5]);
        break;
    case TCG_COND_NE:
        tcg_out_brcond(s, TCG_COND_NE, args[0], args[2], const_args[2], args[5]);
        tcg_out_brcond(s, TCG_COND_NE, args[1], args[3], const_args[3], args[5]);
        break;
    case TCG_COND_LT:
        tcg_out_brcond(s, TCG_COND_LT, args[1], args[3], const_args[3], args[5]);
        tcg_out_jxx(s, JCC_JNE, label_next);
        tcg_out_brcond(s, TCG_COND_LTU, args[0], args[2], const_args[2], args[5]);
        break;
    case TCG_COND_LE:
        tcg_out_brcond(s, TCG_COND_LT, args[1], args[3], const_args[3], args[5]);
        tcg_out_jxx(s, JCC_JNE, label_next);
        tcg_out_brcond(s, TCG_COND_LEU, args[0], args[2], const_args[2], args[5]);
        break;
    case TCG_COND_GT:
        tcg_out_brcond(s, TCG_COND_GT, args[1], args[3], const_args[3], args[5]);
        tcg_out_jxx(s, JCC_JNE, label_next);
        tcg_out_brcond(s, TCG_COND_GTU, args[0], args[2], const_args[2], args[5]);
        break;
    case TCG_COND_GE:
        tcg_out_brcond(s, TCG_COND_GT, args[1], args[3], const_args[3], args[5]);
        tcg_out_jxx(s, JCC_JNE, label_next);
        tcg_out_brcond(s, TCG_COND_GEU, args[0], args[2], const_args[2], args[5]);
        break;
    case TCG_COND_LTU:
        tcg_out_brcond(s, TCG_COND_LTU, args[1], args[3], const_args[3], args[5]);
        tcg_out_jxx(s, JCC_JNE, label_next);
        tcg_out_brcond(s, TCG_COND_LTU, args[0], args[2], const_args[2], args[5]);
        break;
    case TCG_COND_LEU:
        tcg_out_brcond(s, TCG_COND_LTU, args[1], args[3], const_args[3], args[5]);
        tcg_out_jxx(s, JCC_JNE, label_next);
        tcg_out_brcond(s, TCG_COND_LEU, args[0], args[2], const_args[2], args[5]);
        break;
    case TCG_COND_GTU:
        tcg_out_brcond(s, TCG_COND_GTU, args[1], args[3], const_args[3], args[5]);
        tcg_out_jxx(s, JCC_JNE, label_next);
        tcg_out_brcond(s, TCG_COND_GTU, args[0], args[2], const_args[2], args[5]);
        break;
    case TCG_COND_GEU:
        tcg_out_brcond(s, TCG_COND_GTU, args[1], args[3], const_args[3], args[5]);
        tcg_out_jxx(s, JCC_JNE, label_next);
        tcg_out_brcond(s, TCG_COND_GEU, args[0], args[2], const_args[2], args[5]);
        break;
    default:
        tcg_abort();
    }
    tcg_out_label(s, label_next, (tcg_target_long)s->code_ptr);
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

/* XXX: qemu_ld and qemu_st could be modified to clobber only EDX and
   EAX. It will be useful once fixed registers globals are less
   common. */
static void tcg_out_qemu_ld(TCGContext *s, const TCGArg *args,
                            int opc)
{
    int addr_reg, data_reg, data_reg2, r0, r1, mem_index, s_bits, bswap;
#if defined(CONFIG_SOFTMMU)
    uint8_t *label1_ptr, *label2_ptr;
#endif
#if TARGET_LONG_BITS == 64
#if defined(CONFIG_SOFTMMU)
    uint8_t *label3_ptr;
#endif
    int addr_reg2;
#endif

    data_reg = *args++;
    if (opc == 3)
        data_reg2 = *args++;
    else
        data_reg2 = 0;
    addr_reg = *args++;
#if TARGET_LONG_BITS == 64
    addr_reg2 = *args++;
#endif
    mem_index = *args;
    s_bits = opc & 3;

    r0 = TCG_REG_EAX;
    r1 = TCG_REG_EDX;

#if defined(CONFIG_SOFTMMU)
    tcg_out_mov(s, r1, addr_reg); 

    tcg_out_mov(s, r0, addr_reg); 
 
    tcg_out_modrm(s, 0xc1, 5, r1); /* shr $x, r1 */
    tcg_out8(s, TARGET_PAGE_BITS - CPU_TLB_ENTRY_BITS); 
    
    tcg_out_modrm(s, 0x81, 4, r0); /* andl $x, r0 */
    tcg_out32(s, TARGET_PAGE_MASK | ((1 << s_bits) - 1));
    
    tcg_out_modrm(s, 0x81, 4, r1); /* andl $x, r1 */
    tcg_out32(s, (CPU_TLB_SIZE - 1) << CPU_TLB_ENTRY_BITS);

    tcg_out_opc(s, 0x8d); /* lea offset(r1, %ebp), r1 */
    tcg_out8(s, 0x80 | (r1 << 3) | 0x04);
    tcg_out8(s, (5 << 3) | r1);
    tcg_out32(s, offsetof(CPUState, tlb_table[mem_index][0].addr_read));

    /* cmp 0(r1), r0 */
    tcg_out_modrm_offset(s, 0x3b, r0, r1, 0);
    
    tcg_out_mov(s, r0, addr_reg);
    
#if TARGET_LONG_BITS == 32
    /* je label1 */
    tcg_out8(s, 0x70 + JCC_JE);
    label1_ptr = s->code_ptr;
    s->code_ptr++;
#else
    /* jne label3 */
    tcg_out8(s, 0x70 + JCC_JNE);
    label3_ptr = s->code_ptr;
    s->code_ptr++;
    
    /* cmp 4(r1), addr_reg2 */
    tcg_out_modrm_offset(s, 0x3b, addr_reg2, r1, 4);

    /* je label1 */
    tcg_out8(s, 0x70 + JCC_JE);
    label1_ptr = s->code_ptr;
    s->code_ptr++;
    
    /* label3: */
    *label3_ptr = s->code_ptr - label3_ptr - 1;
#endif

    /* XXX: move that code at the end of the TB */
#if TARGET_LONG_BITS == 32
    tcg_out_movi(s, TCG_TYPE_I32, TCG_REG_EDX, mem_index);
#else
    tcg_out_mov(s, TCG_REG_EDX, addr_reg2);
    tcg_out_movi(s, TCG_TYPE_I32, TCG_REG_ECX, mem_index);
#endif
    tcg_out8(s, 0xe8);
    tcg_out32(s, (tcg_target_long)qemu_ld_helpers[s_bits] - 
              (tcg_target_long)s->code_ptr - 4);

    switch(opc) {
    case 0 | 4:
        /* movsbl */
        tcg_out_modrm(s, 0xbe | P_EXT, data_reg, TCG_REG_EAX);
        break;
    case 1 | 4:
        /* movswl */
        tcg_out_modrm(s, 0xbf | P_EXT, data_reg, TCG_REG_EAX);
        break;
    case 0:
        /* movzbl */
        tcg_out_modrm(s, 0xb6 | P_EXT, data_reg, TCG_REG_EAX);
        break;
    case 1:
        /* movzwl */
        tcg_out_modrm(s, 0xb7 | P_EXT, data_reg, TCG_REG_EAX);
        break;
    case 2:
    default:
        tcg_out_mov(s, data_reg, TCG_REG_EAX);
        break;
    case 3:
        if (data_reg == TCG_REG_EDX) {
            tcg_out_opc(s, 0x90 + TCG_REG_EDX); /* xchg %edx, %eax */
            tcg_out_mov(s, data_reg2, TCG_REG_EAX);
        } else {
            tcg_out_mov(s, data_reg, TCG_REG_EAX);
            tcg_out_mov(s, data_reg2, TCG_REG_EDX);
        }
        break;
    }

    /* jmp label2 */
    tcg_out8(s, 0xeb);
    label2_ptr = s->code_ptr;
    s->code_ptr++;
    
    /* label1: */
    *label1_ptr = s->code_ptr - label1_ptr - 1;

    /* add x(r1), r0 */
    tcg_out_modrm_offset(s, 0x03, r0, r1, offsetof(CPUTLBEntry, addend) - 
                         offsetof(CPUTLBEntry, addr_read));
#else
    r0 = addr_reg;
#endif

#ifdef TARGET_WORDS_BIGENDIAN
    bswap = 1;
#else
    bswap = 0;
#endif
    switch(opc) {
    case 0:
        /* movzbl */
        tcg_out_modrm_offset(s, 0xb6 | P_EXT, data_reg, r0, 0);
        break;
    case 0 | 4:
        /* movsbl */
        tcg_out_modrm_offset(s, 0xbe | P_EXT, data_reg, r0, 0);
        break;
    case 1:
        /* movzwl */
        tcg_out_modrm_offset(s, 0xb7 | P_EXT, data_reg, r0, 0);
        if (bswap) {
            /* rolw $8, data_reg */
            tcg_out8(s, 0x66); 
            tcg_out_modrm(s, 0xc1, 0, data_reg);
            tcg_out8(s, 8);
        }
        break;
    case 1 | 4:
        /* movswl */
        tcg_out_modrm_offset(s, 0xbf | P_EXT, data_reg, r0, 0);
        if (bswap) {
            /* rolw $8, data_reg */
            tcg_out8(s, 0x66); 
            tcg_out_modrm(s, 0xc1, 0, data_reg);
            tcg_out8(s, 8);

            /* movswl data_reg, data_reg */
            tcg_out_modrm(s, 0xbf | P_EXT, data_reg, data_reg);
        }
        break;
    case 2:
        /* movl (r0), data_reg */
        tcg_out_modrm_offset(s, 0x8b, data_reg, r0, 0);
        if (bswap) {
            /* bswap */
            tcg_out_opc(s, (0xc8 + data_reg) | P_EXT);
        }
        break;
    case 3:
        /* XXX: could be nicer */
        if (r0 == data_reg) {
            r1 = TCG_REG_EDX;
            if (r1 == data_reg)
                r1 = TCG_REG_EAX;
            tcg_out_mov(s, r1, r0);
            r0 = r1;
        }
        if (!bswap) {
            tcg_out_modrm_offset(s, 0x8b, data_reg, r0, 0);
            tcg_out_modrm_offset(s, 0x8b, data_reg2, r0, 4);
        } else {
            tcg_out_modrm_offset(s, 0x8b, data_reg, r0, 4);
            tcg_out_opc(s, (0xc8 + data_reg) | P_EXT);

            tcg_out_modrm_offset(s, 0x8b, data_reg2, r0, 0);
            /* bswap */
            tcg_out_opc(s, (0xc8 + data_reg2) | P_EXT);
        }
        break;
    default:
        tcg_abort();
    }

#if defined(CONFIG_SOFTMMU)
    /* label2: */
    *label2_ptr = s->code_ptr - label2_ptr - 1;
#endif
}


static void tcg_out_qemu_st(TCGContext *s, const TCGArg *args,
                            int opc)
{
    int addr_reg, data_reg, data_reg2, r0, r1, mem_index, s_bits, bswap;
#if defined(CONFIG_SOFTMMU)
    uint8_t *label1_ptr, *label2_ptr;
#endif
#if TARGET_LONG_BITS == 64
#if defined(CONFIG_SOFTMMU)
    uint8_t *label3_ptr;
#endif
    int addr_reg2;
#endif

    data_reg = *args++;
    if (opc == 3)
        data_reg2 = *args++;
    else
        data_reg2 = 0;
    addr_reg = *args++;
#if TARGET_LONG_BITS == 64
    addr_reg2 = *args++;
#endif
    mem_index = *args;

    s_bits = opc;

    r0 = TCG_REG_EAX;
    r1 = TCG_REG_EDX;

#if defined(CONFIG_SOFTMMU)
    tcg_out_mov(s, r1, addr_reg); 

    tcg_out_mov(s, r0, addr_reg); 
 
    tcg_out_modrm(s, 0xc1, 5, r1); /* shr $x, r1 */
    tcg_out8(s, TARGET_PAGE_BITS - CPU_TLB_ENTRY_BITS); 
    
    tcg_out_modrm(s, 0x81, 4, r0); /* andl $x, r0 */
    tcg_out32(s, TARGET_PAGE_MASK | ((1 << s_bits) - 1));
    
    tcg_out_modrm(s, 0x81, 4, r1); /* andl $x, r1 */
    tcg_out32(s, (CPU_TLB_SIZE - 1) << CPU_TLB_ENTRY_BITS);

    tcg_out_opc(s, 0x8d); /* lea offset(r1, %ebp), r1 */
    tcg_out8(s, 0x80 | (r1 << 3) | 0x04);
    tcg_out8(s, (5 << 3) | r1);
    tcg_out32(s, offsetof(CPUState, tlb_table[mem_index][0].addr_write));

    /* cmp 0(r1), r0 */
    tcg_out_modrm_offset(s, 0x3b, r0, r1, 0);
    
    tcg_out_mov(s, r0, addr_reg);
    
#if TARGET_LONG_BITS == 32
    /* je label1 */
    tcg_out8(s, 0x70 + JCC_JE);
    label1_ptr = s->code_ptr;
    s->code_ptr++;
#else
    /* jne label3 */
    tcg_out8(s, 0x70 + JCC_JNE);
    label3_ptr = s->code_ptr;
    s->code_ptr++;
    
    /* cmp 4(r1), addr_reg2 */
    tcg_out_modrm_offset(s, 0x3b, addr_reg2, r1, 4);

    /* je label1 */
    tcg_out8(s, 0x70 + JCC_JE);
    label1_ptr = s->code_ptr;
    s->code_ptr++;
    
    /* label3: */
    *label3_ptr = s->code_ptr - label3_ptr - 1;
#endif

    /* XXX: move that code at the end of the TB */
#if TARGET_LONG_BITS == 32
    if (opc == 3) {
        tcg_out_mov(s, TCG_REG_EDX, data_reg);
        tcg_out_mov(s, TCG_REG_ECX, data_reg2);
        tcg_out8(s, 0x6a); /* push Ib */
        tcg_out8(s, mem_index);
        tcg_out8(s, 0xe8);
        tcg_out32(s, (tcg_target_long)qemu_st_helpers[s_bits] - 
                  (tcg_target_long)s->code_ptr - 4);
        tcg_out_addi(s, TCG_REG_ESP, 4);
    } else {
        switch(opc) {
        case 0:
            /* movzbl */
            tcg_out_modrm(s, 0xb6 | P_EXT, TCG_REG_EDX, data_reg);
            break;
        case 1:
            /* movzwl */
            tcg_out_modrm(s, 0xb7 | P_EXT, TCG_REG_EDX, data_reg);
            break;
        case 2:
            tcg_out_mov(s, TCG_REG_EDX, data_reg);
            break;
        }
        tcg_out_movi(s, TCG_TYPE_I32, TCG_REG_ECX, mem_index);
        tcg_out8(s, 0xe8);
        tcg_out32(s, (tcg_target_long)qemu_st_helpers[s_bits] - 
                  (tcg_target_long)s->code_ptr - 4);
    }
#else
    if (opc == 3) {
        tcg_out_mov(s, TCG_REG_EDX, addr_reg2);
        tcg_out8(s, 0x6a); /* push Ib */
        tcg_out8(s, mem_index);
        tcg_out_opc(s, 0x50 + data_reg2); /* push */
        tcg_out_opc(s, 0x50 + data_reg); /* push */
        tcg_out8(s, 0xe8);
        tcg_out32(s, (tcg_target_long)qemu_st_helpers[s_bits] - 
                  (tcg_target_long)s->code_ptr - 4);
        tcg_out_addi(s, TCG_REG_ESP, 12);
    } else {
        tcg_out_mov(s, TCG_REG_EDX, addr_reg2);
        switch(opc) {
        case 0:
            /* movzbl */
            tcg_out_modrm(s, 0xb6 | P_EXT, TCG_REG_ECX, data_reg);
            break;
        case 1:
            /* movzwl */
            tcg_out_modrm(s, 0xb7 | P_EXT, TCG_REG_ECX, data_reg);
            break;
        case 2:
            tcg_out_mov(s, TCG_REG_ECX, data_reg);
            break;
        }
        tcg_out8(s, 0x6a); /* push Ib */
        tcg_out8(s, mem_index);
        tcg_out8(s, 0xe8);
        tcg_out32(s, (tcg_target_long)qemu_st_helpers[s_bits] - 
                  (tcg_target_long)s->code_ptr - 4);
        tcg_out_addi(s, TCG_REG_ESP, 4);
    }
#endif
    
    /* jmp label2 */
    tcg_out8(s, 0xeb);
    label2_ptr = s->code_ptr;
    s->code_ptr++;
    
    /* label1: */
    *label1_ptr = s->code_ptr - label1_ptr - 1;

    /* add x(r1), r0 */
    tcg_out_modrm_offset(s, 0x03, r0, r1, offsetof(CPUTLBEntry, addend) - 
                         offsetof(CPUTLBEntry, addr_write));
#else
    r0 = addr_reg;
#endif

#ifdef TARGET_WORDS_BIGENDIAN
    bswap = 1;
#else
    bswap = 0;
#endif
    switch(opc) {
    case 0:
        /* movb */
        tcg_out_modrm_offset(s, 0x88, data_reg, r0, 0);
        break;
    case 1:
        if (bswap) {
            tcg_out_mov(s, r1, data_reg);
            tcg_out8(s, 0x66); /* rolw $8, %ecx */
            tcg_out_modrm(s, 0xc1, 0, r1);
            tcg_out8(s, 8);
            data_reg = r1;
        }
        /* movw */
        tcg_out8(s, 0x66);
        tcg_out_modrm_offset(s, 0x89, data_reg, r0, 0);
        break;
    case 2:
        if (bswap) {
            tcg_out_mov(s, r1, data_reg);
            /* bswap data_reg */
            tcg_out_opc(s, (0xc8 + r1) | P_EXT);
            data_reg = r1;
        }
        /* movl */
        tcg_out_modrm_offset(s, 0x89, data_reg, r0, 0);
        break;
    case 3:
        if (bswap) {
            tcg_out_mov(s, r1, data_reg2);
            /* bswap data_reg */
            tcg_out_opc(s, (0xc8 + r1) | P_EXT);
            tcg_out_modrm_offset(s, 0x89, r1, r0, 0);
            tcg_out_mov(s, r1, data_reg);
            /* bswap data_reg */
            tcg_out_opc(s, (0xc8 + r1) | P_EXT);
            tcg_out_modrm_offset(s, 0x89, r1, r0, 4);
        } else {
            tcg_out_modrm_offset(s, 0x89, data_reg, r0, 0);
            tcg_out_modrm_offset(s, 0x89, data_reg2, r0, 4);
        }
        break;
    default:
        tcg_abort();
    }

#if defined(CONFIG_SOFTMMU)
    /* label2: */
    *label2_ptr = s->code_ptr - label2_ptr - 1;
#endif
}

static inline void tcg_out_op(TCGContext *s, int opc, 
                              const TCGArg *args, const int *const_args)
{
    int c;
    
    switch(opc) {
    case INDEX_op_exit_tb:
        tcg_out_movi(s, TCG_TYPE_I32, TCG_REG_EAX, args[0]);
        tcg_out8(s, 0xe9); /* jmp tb_ret_addr */
        tcg_out32(s, tb_ret_addr - s->code_ptr - 4);
        break;
    case INDEX_op_goto_tb:
        if (s->tb_jmp_offset) {
            /* direct jump method */
            tcg_out8(s, 0xe9); /* jmp im */
            s->tb_jmp_offset[args[0]] = s->code_ptr - s->code_buf;
            tcg_out32(s, 0);
        } else {
            /* indirect jump method */
            /* jmp Ev */
            tcg_out_modrm_offset(s, 0xff, 4, -1, 
                                 (tcg_target_long)(s->tb_next + args[0]));
        }
        s->tb_next_offset[args[0]] = s->code_ptr - s->code_buf;
        break;
    case INDEX_op_call:
        if (const_args[0]) {
            tcg_out8(s, 0xe8);
            tcg_out32(s, args[0] - (tcg_target_long)s->code_ptr - 4);
        } else {
            tcg_out_modrm(s, 0xff, 2, args[0]);
        }
        break;
    case INDEX_op_jmp:
        if (const_args[0]) {
            tcg_out8(s, 0xe9);
            tcg_out32(s, args[0] - (tcg_target_long)s->code_ptr - 4);
        } else {
            tcg_out_modrm(s, 0xff, 4, args[0]);
        }
        break;
    case INDEX_op_br:
        tcg_out_jxx(s, JCC_JMP, args[0]);
        break;
    case INDEX_op_movi_i32:
        tcg_out_movi(s, TCG_TYPE_I32, args[0], args[1]);
        break;
    case INDEX_op_ld8u_i32:
        /* movzbl */
        tcg_out_modrm_offset(s, 0xb6 | P_EXT, args[0], args[1], args[2]);
        break;
    case INDEX_op_ld8s_i32:
        /* movsbl */
        tcg_out_modrm_offset(s, 0xbe | P_EXT, args[0], args[1], args[2]);
        break;
    case INDEX_op_ld16u_i32:
        /* movzwl */
        tcg_out_modrm_offset(s, 0xb7 | P_EXT, args[0], args[1], args[2]);
        break;
    case INDEX_op_ld16s_i32:
        /* movswl */
        tcg_out_modrm_offset(s, 0xbf | P_EXT, args[0], args[1], args[2]);
        break;
    case INDEX_op_ld_i32:
        /* movl */
        tcg_out_modrm_offset(s, 0x8b, args[0], args[1], args[2]);
        break;
    case INDEX_op_st8_i32:
        /* movb */
        tcg_out_modrm_offset(s, 0x88, args[0], args[1], args[2]);
        break;
    case INDEX_op_st16_i32:
        /* movw */
        tcg_out8(s, 0x66);
        tcg_out_modrm_offset(s, 0x89, args[0], args[1], args[2]);
        break;
    case INDEX_op_st_i32:
        /* movl */
        tcg_out_modrm_offset(s, 0x89, args[0], args[1], args[2]);
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
    gen_arith:
        if (const_args[2]) {
            tgen_arithi(s, c, args[0], args[2]);
        } else {
            tcg_out_modrm(s, 0x01 | (c << 3), args[2], args[0]);
        }
        break;
    case INDEX_op_mul_i32:
        if (const_args[2]) {
            int32_t val;
            val = args[2];
            if (val == (int8_t)val) {
                tcg_out_modrm(s, 0x6b, args[0], args[0]);
                tcg_out8(s, val);
            } else {
                tcg_out_modrm(s, 0x69, args[0], args[0]);
                tcg_out32(s, val);
            }
        } else {
            tcg_out_modrm(s, 0xaf | P_EXT, args[0], args[2]);
        }
        break;
    case INDEX_op_mulu2_i32:
        tcg_out_modrm(s, 0xf7, 4, args[3]);
        break;
    case INDEX_op_div2_i32:
        tcg_out_modrm(s, 0xf7, 7, args[4]);
        break;
    case INDEX_op_divu2_i32:
        tcg_out_modrm(s, 0xf7, 6, args[4]);
        break;
    case INDEX_op_shl_i32:
        c = SHIFT_SHL;
    gen_shift32:
        if (const_args[2]) {
            if (args[2] == 1) {
                tcg_out_modrm(s, 0xd1, c, args[0]);
            } else {
                tcg_out_modrm(s, 0xc1, c, args[0]);
                tcg_out8(s, args[2]);
            }
        } else {
            tcg_out_modrm(s, 0xd3, c, args[0]);
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
        if (const_args[4]) 
            tgen_arithi(s, ARITH_ADD, args[0], args[4]);
        else
            tcg_out_modrm(s, 0x01 | (ARITH_ADD << 3), args[4], args[0]);
        if (const_args[5]) 
            tgen_arithi(s, ARITH_ADC, args[1], args[5]);
        else
            tcg_out_modrm(s, 0x01 | (ARITH_ADC << 3), args[5], args[1]);
        break;
    case INDEX_op_sub2_i32:
        if (const_args[4]) 
            tgen_arithi(s, ARITH_SUB, args[0], args[4]);
        else
            tcg_out_modrm(s, 0x01 | (ARITH_SUB << 3), args[4], args[0]);
        if (const_args[5]) 
            tgen_arithi(s, ARITH_SBB, args[1], args[5]);
        else
            tcg_out_modrm(s, 0x01 | (ARITH_SBB << 3), args[5], args[1]);
        break;
    case INDEX_op_brcond_i32:
        tcg_out_brcond(s, args[2], args[0], args[1], const_args[1], args[3]);
        break;
    case INDEX_op_brcond2_i32:
        tcg_out_brcond2(s, args, const_args);
        break;

    case INDEX_op_bswap32_i32:
        tcg_out_opc(s, (0xc8 + args[0]) | P_EXT);
        break;

    case INDEX_op_neg_i32:
        tcg_out_modrm(s, 0xf7, 3, args[0]);
        break;

    case INDEX_op_not_i32:
        tcg_out_modrm(s, 0xf7, 2, args[0]);
        break;

    case INDEX_op_ext8s_i32:
        tcg_out_modrm(s, 0xbe | P_EXT, args[0], args[1]);
        break;
    case INDEX_op_ext16s_i32:
        tcg_out_modrm(s, 0xbf | P_EXT, args[0], args[1]);
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

    { INDEX_op_add_i32, { "r", "0", "ri" } },
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
    { INDEX_op_sar_i32, { "r", "0", "ci" } },
    { INDEX_op_rotl_i32, { "r", "0", "ci" } },
    { INDEX_op_rotr_i32, { "r", "0", "ci" } },

    { INDEX_op_brcond_i32, { "r", "ri" } },

    { INDEX_op_add2_i32, { "r", "r", "0", "1", "ri", "ri" } },
    { INDEX_op_sub2_i32, { "r", "r", "0", "1", "ri", "ri" } },
    { INDEX_op_brcond2_i32, { "r", "r", "ri", "ri" } },

    { INDEX_op_bswap32_i32, { "r", "0" } },

    { INDEX_op_neg_i32, { "r", "0" } },

    { INDEX_op_not_i32, { "r", "0" } },

    { INDEX_op_ext8s_i32, { "r", "q" } },
    { INDEX_op_ext16s_i32, { "r", "r" } },

#if TARGET_LONG_BITS == 32
    { INDEX_op_qemu_ld8u, { "r", "L" } },
    { INDEX_op_qemu_ld8s, { "r", "L" } },
    { INDEX_op_qemu_ld16u, { "r", "L" } },
    { INDEX_op_qemu_ld16s, { "r", "L" } },
    { INDEX_op_qemu_ld32u, { "r", "L" } },
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
    { INDEX_op_qemu_ld32u, { "r", "L", "L" } },
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

static inline void tcg_out_push(TCGContext *s, int reg)
{
    tcg_out_opc(s, 0x50 + reg);
}

static inline void tcg_out_pop(TCGContext *s, int reg)
{
    tcg_out_opc(s, 0x58 + reg);
}

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

    tcg_out_modrm(s, 0xff, 4, TCG_REG_EAX); /* jmp *%eax */
    
    /* TB epilogue */
    tb_ret_addr = s->code_ptr;
    tcg_out_addi(s, TCG_REG_ESP, stack_addend);
    for(i = ARRAY_SIZE(tcg_target_callee_save_regs) - 1; i >= 0; i--) {
        tcg_out_pop(s, tcg_target_callee_save_regs[i]);
    }
    tcg_out8(s, 0xc3); /* ret */
}

void tcg_target_init(TCGContext *s)
{
    /* fail safe */
    if ((1 << CPU_TLB_ENTRY_BITS) != sizeof(CPUTLBEntry))
        tcg_abort();

    tcg_regset_set32(tcg_target_available_regs[TCG_TYPE_I32], 0, 0xff);
    tcg_regset_set32(tcg_target_call_clobber_regs, 0,
                     (1 << TCG_REG_EAX) | 
                     (1 << TCG_REG_EDX) | 
                     (1 << TCG_REG_ECX));
    
    tcg_regset_clear(s->reserved_regs);
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_ESP);

    tcg_add_target_add_op_defs(x86_op_defs);
}

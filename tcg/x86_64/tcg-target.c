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
    "%rax",
    "%rcx",
    "%rdx",
    "%rbx",
    "%rsp",
    "%rbp",
    "%rsi",
    "%rdi",
    "%r8",
    "%r9",
    "%r10",
    "%r11",
    "%r12",
    "%r13",
    "%r14",
    "%r15",
};
#endif

static const int tcg_target_reg_alloc_order[] = {
    TCG_REG_RDI,
    TCG_REG_RSI,
    TCG_REG_RDX,
    TCG_REG_RCX,
    TCG_REG_R8,
    TCG_REG_R9,
    TCG_REG_RAX,
    TCG_REG_R10,
    TCG_REG_R11,

    TCG_REG_RBP,
    TCG_REG_RBX,
    TCG_REG_R12,
    TCG_REG_R13,
    TCG_REG_R14,
    TCG_REG_R15,
};

static const int tcg_target_call_iarg_regs[6] = {
    TCG_REG_RDI,
    TCG_REG_RSI,
    TCG_REG_RDX,
    TCG_REG_RCX,
    TCG_REG_R8,
    TCG_REG_R9,
};

static const int tcg_target_call_oarg_regs[2] = {
    TCG_REG_RAX, 
    TCG_REG_RDX 
};

static uint8_t *tb_ret_addr;

static void patch_reloc(uint8_t *code_ptr, int type, 
                        tcg_target_long value, tcg_target_long addend)
{
    value += addend;
    switch(type) {
    case R_X86_64_32:
        if (value != (uint32_t)value)
            tcg_abort();
        *(uint32_t *)code_ptr = value;
        break;
    case R_X86_64_32S:
        if (value != (int32_t)value)
            tcg_abort();
        *(uint32_t *)code_ptr = value;
        break;
    case R_386_PC32:
        value -= (long)code_ptr;
        if (value != (int32_t)value)
            tcg_abort();
        *(uint32_t *)code_ptr = value;
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
    switch(ct_str[0]) {
    case 'a':
        ct->ct |= TCG_CT_REG;
        tcg_regset_set_reg(ct->u.regs, TCG_REG_RAX);
        break;
    case 'b':
        ct->ct |= TCG_CT_REG;
        tcg_regset_set_reg(ct->u.regs, TCG_REG_RBX);
        break;
    case 'c':
        ct->ct |= TCG_CT_REG;
        tcg_regset_set_reg(ct->u.regs, TCG_REG_RCX);
        break;
    case 'd':
        ct->ct |= TCG_CT_REG;
        tcg_regset_set_reg(ct->u.regs, TCG_REG_RDX);
        break;
    case 'S':
        ct->ct |= TCG_CT_REG;
        tcg_regset_set_reg(ct->u.regs, TCG_REG_RSI);
        break;
    case 'D':
        ct->ct |= TCG_CT_REG;
        tcg_regset_set_reg(ct->u.regs, TCG_REG_RDI);
        break;
    case 'q':
        ct->ct |= TCG_CT_REG;
        tcg_regset_set32(ct->u.regs, 0, 0xf);
        break;
    case 'r':
        ct->ct |= TCG_CT_REG;
        tcg_regset_set32(ct->u.regs, 0, 0xffff);
        break;
    case 'L': /* qemu_ld/st constraint */
        ct->ct |= TCG_CT_REG;
        tcg_regset_set32(ct->u.regs, 0, 0xffff);
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_RSI);
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_RDI);
        break;
    case 'e':
        ct->ct |= TCG_CT_CONST_S32;
        break;
    case 'Z':
        ct->ct |= TCG_CT_CONST_U32;
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
    else if ((ct & TCG_CT_CONST_S32) && val == (int32_t)val)
        return 1;
    else if ((ct & TCG_CT_CONST_U32) && val == (uint32_t)val)
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
#define P_REXW  0x200 /* set rex.w = 1 */
#define P_REXB  0x400 /* force rex use for byte registers */
                                  
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

static inline void tcg_out_opc(TCGContext *s, int opc, int r, int rm, int x)
{
    int rex;
    rex = ((opc >> 6) & 0x8) | ((r >> 1) & 0x4) | 
        ((x >> 2) & 2) | ((rm >> 3) & 1);
    if (rex || (opc & P_REXB)) {
        tcg_out8(s, rex | 0x40);
    }
    if (opc & P_EXT)
        tcg_out8(s, 0x0f);
    tcg_out8(s, opc);
}

static inline void tcg_out_modrm(TCGContext *s, int opc, int r, int rm)
{
    tcg_out_opc(s, opc, r, rm, 0);
    tcg_out8(s, 0xc0 | ((r & 7) << 3) | (rm & 7));
}

/* rm < 0 means no register index plus (-rm - 1 immediate bytes) */
static inline void tcg_out_modrm_offset(TCGContext *s, int opc, int r, int rm, 
                                        tcg_target_long offset)
{
    if (rm < 0) {
        tcg_target_long val;
        tcg_out_opc(s, opc, r, 0, 0);
        val = offset - ((tcg_target_long)s->code_ptr + 5 + (-rm - 1));
        if (val == (int32_t)val) {
            /* eip relative */
            tcg_out8(s, 0x05 | ((r & 7) << 3));
            tcg_out32(s, val);
        } else if (offset == (int32_t)offset) {
            tcg_out8(s, 0x04 | ((r & 7) << 3));
            tcg_out8(s, 0x25); /* sib */
            tcg_out32(s, offset);
        } else {
            tcg_abort();
        }
    } else if (offset == 0 && (rm & 7) != TCG_REG_RBP) {
        tcg_out_opc(s, opc, r, rm, 0);
        if ((rm & 7) == TCG_REG_RSP) {
            tcg_out8(s, 0x04 | ((r & 7) << 3));
            tcg_out8(s, 0x24);
        } else {
            tcg_out8(s, 0x00 | ((r & 7) << 3) | (rm & 7));
        }
    } else if ((int8_t)offset == offset) {
        tcg_out_opc(s, opc, r, rm, 0);
        if ((rm & 7) == TCG_REG_RSP) {
            tcg_out8(s, 0x44 | ((r & 7) << 3));
            tcg_out8(s, 0x24);
        } else {
            tcg_out8(s, 0x40 | ((r & 7) << 3) | (rm & 7));
        }
        tcg_out8(s, offset);
    } else {
        tcg_out_opc(s, opc, r, rm, 0);
        if ((rm & 7) == TCG_REG_RSP) {
            tcg_out8(s, 0x84 | ((r & 7) << 3));
            tcg_out8(s, 0x24);
        } else {
            tcg_out8(s, 0x80 | ((r & 7) << 3) | (rm & 7));
        }
        tcg_out32(s, offset);
    }
}

#if defined(CONFIG_SOFTMMU)
/* XXX: incomplete. index must be different from ESP */
static void tcg_out_modrm_offset2(TCGContext *s, int opc, int r, int rm, 
                                  int index, int shift,
                                  tcg_target_long offset)
{
    int mod;
    if (rm == -1)
        tcg_abort();
    if (offset == 0 && (rm & 7) != TCG_REG_RBP) {
        mod = 0;
    } else if (offset == (int8_t)offset) {
        mod = 0x40;
    } else if (offset == (int32_t)offset) {
        mod = 0x80;
    } else {
        tcg_abort();
    }
    if (index == -1) {
        tcg_out_opc(s, opc, r, rm, 0);
        if ((rm & 7) == TCG_REG_RSP) {
            tcg_out8(s, mod | ((r & 7) << 3) | 0x04);
            tcg_out8(s, 0x04 | (rm & 7));
        } else {
            tcg_out8(s, mod | ((r & 7) << 3) | (rm & 7));
        }
    } else {
        tcg_out_opc(s, opc, r, rm, index);
        tcg_out8(s, mod | ((r & 7) << 3) | 0x04);
        tcg_out8(s, (shift << 6) | ((index & 7) << 3) | (rm & 7));
    }
    if (mod == 0x40) {
        tcg_out8(s, offset);
    } else if (mod == 0x80) {
        tcg_out32(s, offset);
    }
}
#endif

static inline void tcg_out_mov(TCGContext *s, int ret, int arg)
{
    tcg_out_modrm(s, 0x8b | P_REXW, ret, arg);
}

static inline void tcg_out_movi(TCGContext *s, TCGType type, 
                                int ret, tcg_target_long arg)
{
    if (arg == 0) {
        tcg_out_modrm(s, 0x01 | (ARITH_XOR << 3), ret, ret); /* xor r0,r0 */
    } else if (arg == (uint32_t)arg || type == TCG_TYPE_I32) {
        tcg_out_opc(s, 0xb8 + (ret & 7), 0, ret, 0);
        tcg_out32(s, arg);
    } else if (arg == (int32_t)arg) {
        tcg_out_modrm(s, 0xc7 | P_REXW, 0, ret);
        tcg_out32(s, arg);
    } else {
        tcg_out_opc(s, (0xb8 + (ret & 7)) | P_REXW, 0, ret, 0);
        tcg_out32(s, arg);
        tcg_out32(s, arg >> 32);
    }
}

static inline void tcg_out_ld(TCGContext *s, TCGType type, int ret,
                              int arg1, tcg_target_long arg2)
{
    if (type == TCG_TYPE_I32)
        tcg_out_modrm_offset(s, 0x8b, ret, arg1, arg2); /* movl */
    else
        tcg_out_modrm_offset(s, 0x8b | P_REXW, ret, arg1, arg2); /* movq */
}

static inline void tcg_out_st(TCGContext *s, TCGType type, int arg,
                              int arg1, tcg_target_long arg2)
{
    if (type == TCG_TYPE_I32)
        tcg_out_modrm_offset(s, 0x89, arg, arg1, arg2); /* movl */
    else
        tcg_out_modrm_offset(s, 0x89 | P_REXW, arg, arg1, arg2); /* movq */
}

static inline void tgen_arithi32(TCGContext *s, int c, int r0, int32_t val)
{
    if (val == (int8_t)val) {
        tcg_out_modrm(s, 0x83, c, r0);
        tcg_out8(s, val);
    } else if (c == ARITH_AND && val == 0xffu) {
        /* movzbl */
        tcg_out_modrm(s, 0xb6 | P_EXT | P_REXB, r0, r0);
    } else if (c == ARITH_AND && val == 0xffffu) {
        /* movzwl */
        tcg_out_modrm(s, 0xb7 | P_EXT, r0, r0);
    } else {
        tcg_out_modrm(s, 0x81, c, r0);
        tcg_out32(s, val);
    }
}

static inline void tgen_arithi64(TCGContext *s, int c, int r0, int64_t val)
{
    if (val == (int8_t)val) {
        tcg_out_modrm(s, 0x83 | P_REXW, c, r0);
        tcg_out8(s, val);
    } else if (c == ARITH_AND && val == 0xffu) {
        /* movzbl */
        tcg_out_modrm(s, 0xb6 | P_EXT | P_REXW, r0, r0);
    } else if (c == ARITH_AND && val == 0xffffu) {
        /* movzwl */
        tcg_out_modrm(s, 0xb7 | P_EXT | P_REXW, r0, r0);
    } else if (c == ARITH_AND && val == 0xffffffffu) {
        /* 32-bit mov zero extends */
        tcg_out_modrm(s, 0x8b, r0, r0);
    } else if (val == (int32_t)val) {
        tcg_out_modrm(s, 0x81 | P_REXW, c, r0);
        tcg_out32(s, val);
    } else if (c == ARITH_AND && val == (uint32_t)val) {
        tcg_out_modrm(s, 0x81, c, r0);
        tcg_out32(s, val);
    } else {
        tcg_abort();
    }
}

static void tcg_out_addi(TCGContext *s, int reg, tcg_target_long val)
{
    if (val != 0)
        tgen_arithi64(s, ARITH_ADD, reg, val);
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
                           int label_index, int rexw)
{
    if (const_arg2) {
        if (arg2 == 0) {
            /* test r, r */
            tcg_out_modrm(s, 0x85 | rexw, arg1, arg1);
        } else {
            if (rexw)
                tgen_arithi64(s, ARITH_CMP, arg1, arg2);
            else
                tgen_arithi32(s, ARITH_CMP, arg1, arg2);
        }
    } else {
        tcg_out_modrm(s, 0x01 | (ARITH_CMP << 3) | rexw, arg2, arg1);
    }
    tcg_out_jxx(s, tcg_cond_to_jcc[cond], label_index);
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
    int addr_reg, data_reg, r0, r1, mem_index, s_bits, bswap, rexw;
#if defined(CONFIG_SOFTMMU)
    uint8_t *label1_ptr, *label2_ptr;
#endif

    data_reg = *args++;
    addr_reg = *args++;
    mem_index = *args;
    s_bits = opc & 3;

    r0 = TCG_REG_RDI;
    r1 = TCG_REG_RSI;

#if TARGET_LONG_BITS == 32
    rexw = 0;
#else
    rexw = P_REXW;
#endif
#if defined(CONFIG_SOFTMMU)
    /* mov */
    tcg_out_modrm(s, 0x8b | rexw, r1, addr_reg);

    /* mov */
    tcg_out_modrm(s, 0x8b | rexw, r0, addr_reg);
 
    tcg_out_modrm(s, 0xc1 | rexw, 5, r1); /* shr $x, r1 */
    tcg_out8(s, TARGET_PAGE_BITS - CPU_TLB_ENTRY_BITS); 
    
    tcg_out_modrm(s, 0x81 | rexw, 4, r0); /* andl $x, r0 */
    tcg_out32(s, TARGET_PAGE_MASK | ((1 << s_bits) - 1));
    
    tcg_out_modrm(s, 0x81, 4, r1); /* andl $x, r1 */
    tcg_out32(s, (CPU_TLB_SIZE - 1) << CPU_TLB_ENTRY_BITS);

    /* lea offset(r1, env), r1 */
    tcg_out_modrm_offset2(s, 0x8d | P_REXW, r1, r1, TCG_AREG0, 0,
                          offsetof(CPUState, tlb_table[mem_index][0].addr_read));

    /* cmp 0(r1), r0 */
    tcg_out_modrm_offset(s, 0x3b | rexw, r0, r1, 0);
    
    /* mov */
    tcg_out_modrm(s, 0x8b | rexw, r0, addr_reg);
    
    /* je label1 */
    tcg_out8(s, 0x70 + JCC_JE);
    label1_ptr = s->code_ptr;
    s->code_ptr++;

    /* XXX: move that code at the end of the TB */
    tcg_out_movi(s, TCG_TYPE_I32, TCG_REG_RSI, mem_index);
    tcg_out8(s, 0xe8);
    tcg_out32(s, (tcg_target_long)qemu_ld_helpers[s_bits] - 
              (tcg_target_long)s->code_ptr - 4);

    switch(opc) {
    case 0 | 4:
        /* movsbq */
        tcg_out_modrm(s, 0xbe | P_EXT | P_REXW, data_reg, TCG_REG_RAX);
        break;
    case 1 | 4:
        /* movswq */
        tcg_out_modrm(s, 0xbf | P_EXT | P_REXW, data_reg, TCG_REG_RAX);
        break;
    case 2 | 4:
        /* movslq */
        tcg_out_modrm(s, 0x63 | P_REXW, data_reg, TCG_REG_RAX);
        break;
    case 0:
        /* movzbq */
        tcg_out_modrm(s, 0xb6 | P_EXT | P_REXW, data_reg, TCG_REG_RAX);
        break;
    case 1:
        /* movzwq */
        tcg_out_modrm(s, 0xb7 | P_EXT | P_REXW, data_reg, TCG_REG_RAX);
        break;
    case 2:
    default:
        /* movl */
        tcg_out_modrm(s, 0x8b, data_reg, TCG_REG_RAX);
        break;
    case 3:
        tcg_out_mov(s, data_reg, TCG_REG_RAX);
        break;
    }

    /* jmp label2 */
    tcg_out8(s, 0xeb);
    label2_ptr = s->code_ptr;
    s->code_ptr++;
    
    /* label1: */
    *label1_ptr = s->code_ptr - label1_ptr - 1;

    /* add x(r1), r0 */
    tcg_out_modrm_offset(s, 0x03 | P_REXW, r0, r1, offsetof(CPUTLBEntry, addend) - 
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
        /* movsbX */
        tcg_out_modrm_offset(s, 0xbe | P_EXT | rexw, data_reg, r0, 0);
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
        if (bswap) {
            /* movzwl */
            tcg_out_modrm_offset(s, 0xb7 | P_EXT, data_reg, r0, 0);
            /* rolw $8, data_reg */
            tcg_out8(s, 0x66); 
            tcg_out_modrm(s, 0xc1, 0, data_reg);
            tcg_out8(s, 8);

            /* movswX data_reg, data_reg */
            tcg_out_modrm(s, 0xbf | P_EXT | rexw, data_reg, data_reg);
        } else {
            /* movswX */
            tcg_out_modrm_offset(s, 0xbf | P_EXT | rexw, data_reg, r0, 0);
        }
        break;
    case 2:
        /* movl (r0), data_reg */
        tcg_out_modrm_offset(s, 0x8b, data_reg, r0, 0);
        if (bswap) {
            /* bswap */
            tcg_out_opc(s, (0xc8 + (data_reg & 7)) | P_EXT, 0, data_reg, 0);
        }
        break;
    case 2 | 4:
        if (bswap) {
            /* movl (r0), data_reg */
            tcg_out_modrm_offset(s, 0x8b, data_reg, r0, 0);
            /* bswap */
            tcg_out_opc(s, (0xc8 + (data_reg & 7)) | P_EXT, 0, data_reg, 0);
            /* movslq */
            tcg_out_modrm(s, 0x63 | P_REXW, data_reg, data_reg);
        } else {
            /* movslq */
            tcg_out_modrm_offset(s, 0x63 | P_REXW, data_reg, r0, 0);
        }
        break;
    case 3:
        /* movq (r0), data_reg */
        tcg_out_modrm_offset(s, 0x8b | P_REXW, data_reg, r0, 0);
        if (bswap) {
            /* bswap */
            tcg_out_opc(s, (0xc8 + (data_reg & 7)) | P_EXT | P_REXW, 0, data_reg, 0);
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
    int addr_reg, data_reg, r0, r1, mem_index, s_bits, bswap, rexw;
#if defined(CONFIG_SOFTMMU)
    uint8_t *label1_ptr, *label2_ptr;
#endif

    data_reg = *args++;
    addr_reg = *args++;
    mem_index = *args;

    s_bits = opc;

    r0 = TCG_REG_RDI;
    r1 = TCG_REG_RSI;

#if TARGET_LONG_BITS == 32
    rexw = 0;
#else
    rexw = P_REXW;
#endif
#if defined(CONFIG_SOFTMMU)
    /* mov */
    tcg_out_modrm(s, 0x8b | rexw, r1, addr_reg);

    /* mov */
    tcg_out_modrm(s, 0x8b | rexw, r0, addr_reg);
 
    tcg_out_modrm(s, 0xc1 | rexw, 5, r1); /* shr $x, r1 */
    tcg_out8(s, TARGET_PAGE_BITS - CPU_TLB_ENTRY_BITS); 
    
    tcg_out_modrm(s, 0x81 | rexw, 4, r0); /* andl $x, r0 */
    tcg_out32(s, TARGET_PAGE_MASK | ((1 << s_bits) - 1));
    
    tcg_out_modrm(s, 0x81, 4, r1); /* andl $x, r1 */
    tcg_out32(s, (CPU_TLB_SIZE - 1) << CPU_TLB_ENTRY_BITS);

    /* lea offset(r1, env), r1 */
    tcg_out_modrm_offset2(s, 0x8d | P_REXW, r1, r1, TCG_AREG0, 0,
                          offsetof(CPUState, tlb_table[mem_index][0].addr_write));

    /* cmp 0(r1), r0 */
    tcg_out_modrm_offset(s, 0x3b | rexw, r0, r1, 0);
    
    /* mov */
    tcg_out_modrm(s, 0x8b | rexw, r0, addr_reg);
    
    /* je label1 */
    tcg_out8(s, 0x70 + JCC_JE);
    label1_ptr = s->code_ptr;
    s->code_ptr++;

    /* XXX: move that code at the end of the TB */
    switch(opc) {
    case 0:
        /* movzbl */
        tcg_out_modrm(s, 0xb6 | P_EXT | P_REXB, TCG_REG_RSI, data_reg);
        break;
    case 1:
        /* movzwl */
        tcg_out_modrm(s, 0xb7 | P_EXT, TCG_REG_RSI, data_reg);
        break;
    case 2:
        /* movl */
        tcg_out_modrm(s, 0x8b, TCG_REG_RSI, data_reg);
        break;
    default:
    case 3:
        tcg_out_mov(s, TCG_REG_RSI, data_reg);
        break;
    }
    tcg_out_movi(s, TCG_TYPE_I32, TCG_REG_RDX, mem_index);
    tcg_out8(s, 0xe8);
    tcg_out32(s, (tcg_target_long)qemu_st_helpers[s_bits] - 
              (tcg_target_long)s->code_ptr - 4);

    /* jmp label2 */
    tcg_out8(s, 0xeb);
    label2_ptr = s->code_ptr;
    s->code_ptr++;
    
    /* label1: */
    *label1_ptr = s->code_ptr - label1_ptr - 1;

    /* add x(r1), r0 */
    tcg_out_modrm_offset(s, 0x03 | P_REXW, r0, r1, offsetof(CPUTLBEntry, addend) - 
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
        tcg_out_modrm_offset(s, 0x88 | P_REXB, data_reg, r0, 0);
        break;
    case 1:
        if (bswap) {
            tcg_out_modrm(s, 0x8b, r1, data_reg); /* movl */
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
            tcg_out_modrm(s, 0x8b, r1, data_reg); /* movl */
            /* bswap data_reg */
            tcg_out_opc(s, (0xc8 + r1) | P_EXT, 0, r1, 0);
            data_reg = r1;
        }
        /* movl */
        tcg_out_modrm_offset(s, 0x89, data_reg, r0, 0);
        break;
    case 3:
        if (bswap) {
            tcg_out_mov(s, r1, data_reg);
            /* bswap data_reg */
            tcg_out_opc(s, (0xc8 + r1) | P_EXT | P_REXW, 0, r1, 0);
            data_reg = r1;
        }
        /* movq */
        tcg_out_modrm_offset(s, 0x89 | P_REXW, data_reg, r0, 0);
        break;
    default:
        tcg_abort();
    }

#if defined(CONFIG_SOFTMMU)
    /* label2: */
    *label2_ptr = s->code_ptr - label2_ptr - 1;
#endif
}

static inline void tcg_out_op(TCGContext *s, int opc, const TCGArg *args,
                              const int *const_args)
{
    int c;
    
    switch(opc) {
    case INDEX_op_exit_tb:
        tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_RAX, args[0]);
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
                                 (tcg_target_long)(s->tb_next + 
                                                   args[0]));
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
        tcg_out_movi(s, TCG_TYPE_I32, args[0], (uint32_t)args[1]);
        break;
    case INDEX_op_movi_i64:
        tcg_out_movi(s, TCG_TYPE_I64, args[0], args[1]);
        break;
    case INDEX_op_ld8u_i32:
    case INDEX_op_ld8u_i64:
        /* movzbl */
        tcg_out_modrm_offset(s, 0xb6 | P_EXT, args[0], args[1], args[2]);
        break;
    case INDEX_op_ld8s_i32:
        /* movsbl */
        tcg_out_modrm_offset(s, 0xbe | P_EXT, args[0], args[1], args[2]);
        break;
    case INDEX_op_ld8s_i64:
        /* movsbq */
        tcg_out_modrm_offset(s, 0xbe | P_EXT | P_REXW, args[0], args[1], args[2]);
        break;
    case INDEX_op_ld16u_i32:
    case INDEX_op_ld16u_i64:
        /* movzwl */
        tcg_out_modrm_offset(s, 0xb7 | P_EXT, args[0], args[1], args[2]);
        break;
    case INDEX_op_ld16s_i32:
        /* movswl */
        tcg_out_modrm_offset(s, 0xbf | P_EXT, args[0], args[1], args[2]);
        break;
    case INDEX_op_ld16s_i64:
        /* movswq */
        tcg_out_modrm_offset(s, 0xbf | P_EXT | P_REXW, args[0], args[1], args[2]);
        break;
    case INDEX_op_ld_i32:
    case INDEX_op_ld32u_i64:
        /* movl */
        tcg_out_modrm_offset(s, 0x8b, args[0], args[1], args[2]);
        break;
    case INDEX_op_ld32s_i64:
        /* movslq */
        tcg_out_modrm_offset(s, 0x63 | P_REXW, args[0], args[1], args[2]);
        break;
    case INDEX_op_ld_i64:
        /* movq */
        tcg_out_modrm_offset(s, 0x8b | P_REXW, args[0], args[1], args[2]);
        break;
        
    case INDEX_op_st8_i32:
    case INDEX_op_st8_i64:
        /* movb */
        tcg_out_modrm_offset(s, 0x88 | P_REXB, args[0], args[1], args[2]);
        break;
    case INDEX_op_st16_i32:
    case INDEX_op_st16_i64:
        /* movw */
        tcg_out8(s, 0x66);
        tcg_out_modrm_offset(s, 0x89, args[0], args[1], args[2]);
        break;
    case INDEX_op_st_i32:
    case INDEX_op_st32_i64:
        /* movl */
        tcg_out_modrm_offset(s, 0x89, args[0], args[1], args[2]);
        break;
    case INDEX_op_st_i64:
        /* movq */
        tcg_out_modrm_offset(s, 0x89 | P_REXW, args[0], args[1], args[2]);
        break;

    case INDEX_op_sub_i32:
        c = ARITH_SUB;
        goto gen_arith32;
    case INDEX_op_and_i32:
        c = ARITH_AND;
        goto gen_arith32;
    case INDEX_op_or_i32:
        c = ARITH_OR;
        goto gen_arith32;
    case INDEX_op_xor_i32:
        c = ARITH_XOR;
        goto gen_arith32;
    case INDEX_op_add_i32:
        c = ARITH_ADD;
    gen_arith32:
        if (const_args[2]) {
            tgen_arithi32(s, c, args[0], args[2]);
        } else {
            tcg_out_modrm(s, 0x01 | (c << 3), args[2], args[0]);
        }
        break;

    case INDEX_op_sub_i64:
        c = ARITH_SUB;
        goto gen_arith64;
    case INDEX_op_and_i64:
        c = ARITH_AND;
        goto gen_arith64;
    case INDEX_op_or_i64:
        c = ARITH_OR;
        goto gen_arith64;
    case INDEX_op_xor_i64:
        c = ARITH_XOR;
        goto gen_arith64;
    case INDEX_op_add_i64:
        c = ARITH_ADD;
    gen_arith64:
        if (const_args[2]) {
            tgen_arithi64(s, c, args[0], args[2]);
        } else {
            tcg_out_modrm(s, 0x01 | (c << 3) | P_REXW, args[2], args[0]);
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
    case INDEX_op_mul_i64:
        if (const_args[2]) {
            int32_t val;
            val = args[2];
            if (val == (int8_t)val) {
                tcg_out_modrm(s, 0x6b | P_REXW, args[0], args[0]);
                tcg_out8(s, val);
            } else {
                tcg_out_modrm(s, 0x69 | P_REXW, args[0], args[0]);
                tcg_out32(s, val);
            }
        } else {
            tcg_out_modrm(s, 0xaf | P_EXT | P_REXW, args[0], args[2]);
        }
        break;
    case INDEX_op_div2_i32:
        tcg_out_modrm(s, 0xf7, 7, args[4]);
        break;
    case INDEX_op_divu2_i32:
        tcg_out_modrm(s, 0xf7, 6, args[4]);
        break;
    case INDEX_op_div2_i64:
        tcg_out_modrm(s, 0xf7 | P_REXW, 7, args[4]);
        break;
    case INDEX_op_divu2_i64:
        tcg_out_modrm(s, 0xf7 | P_REXW, 6, args[4]);
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
        
    case INDEX_op_shl_i64:
        c = SHIFT_SHL;
    gen_shift64:
        if (const_args[2]) {
            if (args[2] == 1) {
                tcg_out_modrm(s, 0xd1 | P_REXW, c, args[0]);
            } else {
                tcg_out_modrm(s, 0xc1 | P_REXW, c, args[0]);
                tcg_out8(s, args[2]);
            }
        } else {
            tcg_out_modrm(s, 0xd3 | P_REXW, c, args[0]);
        }
        break;
    case INDEX_op_shr_i64:
        c = SHIFT_SHR;
        goto gen_shift64;
    case INDEX_op_sar_i64:
        c = SHIFT_SAR;
        goto gen_shift64;
        
    case INDEX_op_brcond_i32:
        tcg_out_brcond(s, args[2], args[0], args[1], const_args[1], 
                       args[3], 0);
        break;
    case INDEX_op_brcond_i64:
        tcg_out_brcond(s, args[2], args[0], args[1], const_args[1], 
                       args[3], P_REXW);
        break;

    case INDEX_op_bswap_i32:
        tcg_out_opc(s, (0xc8 + (args[0] & 7)) | P_EXT, 0, args[0], 0);
        break;
    case INDEX_op_bswap_i64:
        tcg_out_opc(s, (0xc8 + (args[0] & 7)) | P_EXT | P_REXW, 0, args[0], 0);
        break;

    case INDEX_op_neg_i32:
        tcg_out_modrm(s, 0xf7, 3, args[0]);
        break;
    case INDEX_op_neg_i64:
        tcg_out_modrm(s, 0xf7 | P_REXW, 3, args[0]);
        break;

    case INDEX_op_ext8s_i32:
        tcg_out_modrm(s, 0xbe | P_EXT | P_REXB, args[0], args[1]);
        break;
    case INDEX_op_ext16s_i32:
        tcg_out_modrm(s, 0xbf | P_EXT, args[0], args[1]);
        break;
    case INDEX_op_ext8s_i64:
        tcg_out_modrm(s, 0xbe | P_EXT | P_REXW, args[0], args[1]);
        break;
    case INDEX_op_ext16s_i64:
        tcg_out_modrm(s, 0xbf | P_EXT | P_REXW, args[0], args[1]);
        break;
    case INDEX_op_ext32s_i64:
        tcg_out_modrm(s, 0x63 | P_REXW, args[0], args[1]);
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

static int tcg_target_callee_save_regs[] = {
    TCG_REG_RBP,
    TCG_REG_RBX,
    TCG_REG_R12,
    TCG_REG_R13,
    /*    TCG_REG_R14, */ /* currently used for the global env, so no
                             need to save */
    TCG_REG_R15,
};

static inline void tcg_out_push(TCGContext *s, int reg)
{
    tcg_out_opc(s, (0x50 + (reg & 7)), 0, reg, 0);
}

static inline void tcg_out_pop(TCGContext *s, int reg)
{
    tcg_out_opc(s, (0x58 + (reg & 7)), 0, reg, 0);
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
    push_size = 8 + ARRAY_SIZE(tcg_target_callee_save_regs) * 8;
    frame_size = push_size + TCG_STATIC_CALL_ARGS_SIZE;
    frame_size = (frame_size + TCG_TARGET_STACK_ALIGN - 1) & 
        ~(TCG_TARGET_STACK_ALIGN - 1);
    stack_addend = frame_size - push_size;
    tcg_out_addi(s, TCG_REG_RSP, -stack_addend);

    tcg_out_modrm(s, 0xff, 4, TCG_REG_RDI); /* jmp *%rdi */
    
    /* TB epilogue */
    tb_ret_addr = s->code_ptr;
    tcg_out_addi(s, TCG_REG_RSP, stack_addend);
    for(i = ARRAY_SIZE(tcg_target_callee_save_regs) - 1; i >= 0; i--) {
        tcg_out_pop(s, tcg_target_callee_save_regs[i]);
    }
    tcg_out8(s, 0xc3); /* ret */
}

static const TCGTargetOpDef x86_64_op_defs[] = {
    { INDEX_op_exit_tb, { } },
    { INDEX_op_goto_tb, { } },
    { INDEX_op_call, { "ri" } }, /* XXX: might need a specific constant constraint */
    { INDEX_op_jmp, { "ri" } }, /* XXX: might need a specific constant constraint */
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

    { INDEX_op_add_i32, { "r", "0", "ri" } },
    { INDEX_op_mul_i32, { "r", "0", "ri" } },
    { INDEX_op_div2_i32, { "a", "d", "0", "1", "r" } },
    { INDEX_op_divu2_i32, { "a", "d", "0", "1", "r" } },
    { INDEX_op_sub_i32, { "r", "0", "ri" } },
    { INDEX_op_and_i32, { "r", "0", "ri" } },
    { INDEX_op_or_i32, { "r", "0", "ri" } },
    { INDEX_op_xor_i32, { "r", "0", "ri" } },

    { INDEX_op_shl_i32, { "r", "0", "ci" } },
    { INDEX_op_shr_i32, { "r", "0", "ci" } },
    { INDEX_op_sar_i32, { "r", "0", "ci" } },

    { INDEX_op_brcond_i32, { "r", "ri" } },

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

    { INDEX_op_add_i64, { "r", "0", "re" } },
    { INDEX_op_mul_i64, { "r", "0", "re" } },
    { INDEX_op_div2_i64, { "a", "d", "0", "1", "r" } },
    { INDEX_op_divu2_i64, { "a", "d", "0", "1", "r" } },
    { INDEX_op_sub_i64, { "r", "0", "re" } },
    { INDEX_op_and_i64, { "r", "0", "reZ" } },
    { INDEX_op_or_i64, { "r", "0", "re" } },
    { INDEX_op_xor_i64, { "r", "0", "re" } },

    { INDEX_op_shl_i64, { "r", "0", "ci" } },
    { INDEX_op_shr_i64, { "r", "0", "ci" } },
    { INDEX_op_sar_i64, { "r", "0", "ci" } },

    { INDEX_op_brcond_i64, { "r", "re" } },

    { INDEX_op_bswap_i32, { "r", "0" } },
    { INDEX_op_bswap_i64, { "r", "0" } },

    { INDEX_op_neg_i32, { "r", "0" } },
    { INDEX_op_neg_i64, { "r", "0" } },

    { INDEX_op_ext8s_i32, { "r", "r"} },
    { INDEX_op_ext16s_i32, { "r", "r"} },
    { INDEX_op_ext8s_i64, { "r", "r"} },
    { INDEX_op_ext16s_i64, { "r", "r"} },
    { INDEX_op_ext32s_i64, { "r", "r"} },

    { INDEX_op_qemu_ld8u, { "r", "L" } },
    { INDEX_op_qemu_ld8s, { "r", "L" } },
    { INDEX_op_qemu_ld16u, { "r", "L" } },
    { INDEX_op_qemu_ld16s, { "r", "L" } },
    { INDEX_op_qemu_ld32u, { "r", "L" } },
    { INDEX_op_qemu_ld32s, { "r", "L" } },
    { INDEX_op_qemu_ld64, { "r", "L" } },

    { INDEX_op_qemu_st8, { "L", "L" } },
    { INDEX_op_qemu_st16, { "L", "L" } },
    { INDEX_op_qemu_st32, { "L", "L" } },
    { INDEX_op_qemu_st64, { "L", "L", "L" } },

    { -1 },
};

void tcg_target_init(TCGContext *s)
{
    /* fail safe */
    if ((1 << CPU_TLB_ENTRY_BITS) != sizeof(CPUTLBEntry))
        tcg_abort();

    tcg_regset_set32(tcg_target_available_regs[TCG_TYPE_I32], 0, 0xffff);
    tcg_regset_set32(tcg_target_available_regs[TCG_TYPE_I64], 0, 0xffff);
    tcg_regset_set32(tcg_target_call_clobber_regs, 0,
                     (1 << TCG_REG_RDI) | 
                     (1 << TCG_REG_RSI) | 
                     (1 << TCG_REG_RDX) |
                     (1 << TCG_REG_RCX) |
                     (1 << TCG_REG_R8) |
                     (1 << TCG_REG_R9) |
                     (1 << TCG_REG_RAX) |
                     (1 << TCG_REG_R10) |
                     (1 << TCG_REG_R11));
    
    tcg_regset_clear(s->reserved_regs);
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_RSP);

    tcg_add_target_add_op_defs(x86_64_op_defs);
}

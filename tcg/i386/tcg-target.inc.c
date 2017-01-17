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

#include "tcg-be-ldst.h"

#ifdef CONFIG_DEBUG_TCG
static const char * const tcg_target_reg_names[TCG_TARGET_NB_REGS] = {
#if TCG_TARGET_REG_BITS == 64
    "%rax", "%rcx", "%rdx", "%rbx", "%rsp", "%rbp", "%rsi", "%rdi",
    "%r8",  "%r9",  "%r10", "%r11", "%r12", "%r13", "%r14", "%r15",
#else
    "%eax", "%ecx", "%edx", "%ebx", "%esp", "%ebp", "%esi", "%edi",
#endif
};
#endif

static const int tcg_target_reg_alloc_order[] = {
#if TCG_TARGET_REG_BITS == 64
    TCG_REG_RBP,
    TCG_REG_RBX,
    TCG_REG_R12,
    TCG_REG_R13,
    TCG_REG_R14,
    TCG_REG_R15,
    TCG_REG_R10,
    TCG_REG_R11,
    TCG_REG_R9,
    TCG_REG_R8,
    TCG_REG_RCX,
    TCG_REG_RDX,
    TCG_REG_RSI,
    TCG_REG_RDI,
    TCG_REG_RAX,
#else
    TCG_REG_EBX,
    TCG_REG_ESI,
    TCG_REG_EDI,
    TCG_REG_EBP,
    TCG_REG_ECX,
    TCG_REG_EDX,
    TCG_REG_EAX,
#endif
};

static const int tcg_target_call_iarg_regs[] = {
#if TCG_TARGET_REG_BITS == 64
#if defined(_WIN64)
    TCG_REG_RCX,
    TCG_REG_RDX,
#else
    TCG_REG_RDI,
    TCG_REG_RSI,
    TCG_REG_RDX,
    TCG_REG_RCX,
#endif
    TCG_REG_R8,
    TCG_REG_R9,
#else
    /* 32 bit mode uses stack based calling convention (GCC default). */
#endif
};

static const int tcg_target_call_oarg_regs[] = {
    TCG_REG_EAX,
#if TCG_TARGET_REG_BITS == 32
    TCG_REG_EDX
#endif
};

/* Constants we accept.  */
#define TCG_CT_CONST_S32 0x100
#define TCG_CT_CONST_U32 0x200
#define TCG_CT_CONST_I32 0x400
#define TCG_CT_CONST_WSZ 0x800

/* Registers used with L constraint, which are the first argument 
   registers on x86_64, and two random call clobbered registers on
   i386. */
#if TCG_TARGET_REG_BITS == 64
# define TCG_REG_L0 tcg_target_call_iarg_regs[0]
# define TCG_REG_L1 tcg_target_call_iarg_regs[1]
#else
# define TCG_REG_L0 TCG_REG_EAX
# define TCG_REG_L1 TCG_REG_EDX
#endif

/* The host compiler should supply <cpuid.h> to enable runtime features
   detection, as we're not going to go so far as our own inline assembly.
   If not available, default values will be assumed.  */
#if defined(CONFIG_CPUID_H)
#include <cpuid.h>
#endif

/* For 32-bit, we are going to attempt to determine at runtime whether cmov
   is available.  */
#if TCG_TARGET_REG_BITS == 64
# define have_cmov 1
#elif defined(CONFIG_CPUID_H) && defined(bit_CMOV)
static bool have_cmov;
#else
# define have_cmov 0
#endif

/* If bit_MOVBE is defined in cpuid.h (added in GCC version 4.6), we are
   going to attempt to determine at runtime whether movbe is available.  */
#if defined(CONFIG_CPUID_H) && defined(bit_MOVBE)
static bool have_movbe;
#else
# define have_movbe 0
#endif

/* We need these symbols in tcg-target.h, and we can't properly conditionalize
   it there.  Therefore we always define the variable.  */
bool have_bmi1;
bool have_popcnt;

#if defined(CONFIG_CPUID_H) && defined(bit_BMI2)
static bool have_bmi2;
#else
# define have_bmi2 0
#endif
#if defined(CONFIG_CPUID_H) && defined(bit_LZCNT)
static bool have_lzcnt;
#else
# define have_lzcnt 0
#endif

static tcg_insn_unit *tb_ret_addr;

static void patch_reloc(tcg_insn_unit *code_ptr, int type,
                        intptr_t value, intptr_t addend)
{
    value += addend;
    switch(type) {
    case R_386_PC32:
        value -= (uintptr_t)code_ptr;
        if (value != (int32_t)value) {
            tcg_abort();
        }
        tcg_patch32(code_ptr, value);
        break;
    case R_386_PC8:
        value -= (uintptr_t)code_ptr;
        if (value != (int8_t)value) {
            tcg_abort();
        }
        tcg_patch8(code_ptr, value);
        break;
    default:
        tcg_abort();
    }
}

/* parse target specific constraints */
static const char *target_parse_constraint(TCGArgConstraint *ct,
                                           const char *ct_str, TCGType type)
{
    switch(*ct_str++) {
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
        if (TCG_TARGET_REG_BITS == 64) {
            tcg_regset_set32(ct->u.regs, 0, 0xffff);
        } else {
            tcg_regset_set32(ct->u.regs, 0, 0xf);
        }
        break;
    case 'Q':
        ct->ct |= TCG_CT_REG;
        tcg_regset_set32(ct->u.regs, 0, 0xf);
        break;
    case 'r':
        ct->ct |= TCG_CT_REG;
        if (TCG_TARGET_REG_BITS == 64) {
            tcg_regset_set32(ct->u.regs, 0, 0xffff);
        } else {
            tcg_regset_set32(ct->u.regs, 0, 0xff);
        }
        break;
    case 'W':
        /* With TZCNT/LZCNT, we can have operand-size as an input.  */
        ct->ct |= TCG_CT_CONST_WSZ;
        break;

        /* qemu_ld/st address constraint */
    case 'L':
        ct->ct |= TCG_CT_REG;
        if (TCG_TARGET_REG_BITS == 64) {
            tcg_regset_set32(ct->u.regs, 0, 0xffff);
        } else {
            tcg_regset_set32(ct->u.regs, 0, 0xff);
        }
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_L0);
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_L1);
        break;

    case 'e':
        ct->ct |= (type == TCG_TYPE_I32 ? TCG_CT_CONST : TCG_CT_CONST_S32);
        break;
    case 'Z':
        ct->ct |= (type == TCG_TYPE_I32 ? TCG_CT_CONST : TCG_CT_CONST_U32);
        break;
    case 'I':
        ct->ct |= (type == TCG_TYPE_I32 ? TCG_CT_CONST : TCG_CT_CONST_I32);
        break;

    default:
        return NULL;
    }
    return ct_str;
}

/* test if a constant matches the constraint */
static inline int tcg_target_const_match(tcg_target_long val, TCGType type,
                                         const TCGArgConstraint *arg_ct)
{
    int ct = arg_ct->ct;
    if (ct & TCG_CT_CONST) {
        return 1;
    }
    if ((ct & TCG_CT_CONST_S32) && val == (int32_t)val) {
        return 1;
    }
    if ((ct & TCG_CT_CONST_U32) && val == (uint32_t)val) {
        return 1;
    }
    if ((ct & TCG_CT_CONST_I32) && ~val == (int32_t)~val) {
        return 1;
    }
    if ((ct & TCG_CT_CONST_WSZ) && val == (type == TCG_TYPE_I32 ? 32 : 64)) {
        return 1;
    }
    return 0;
}

#if TCG_TARGET_REG_BITS == 64
# define LOWREGMASK(x)	((x) & 7)
#else
# define LOWREGMASK(x)	(x)
#endif

#define P_EXT		0x100		/* 0x0f opcode prefix */
#define P_EXT38         0x200           /* 0x0f 0x38 opcode prefix */
#define P_DATA16        0x400           /* 0x66 opcode prefix */
#if TCG_TARGET_REG_BITS == 64
# define P_ADDR32       0x800           /* 0x67 opcode prefix */
# define P_REXW         0x1000          /* Set REX.W = 1 */
# define P_REXB_R       0x2000          /* REG field as byte register */
# define P_REXB_RM      0x4000          /* R/M field as byte register */
# define P_GS           0x8000          /* gs segment override */
#else
# define P_ADDR32	0
# define P_REXW		0
# define P_REXB_R	0
# define P_REXB_RM	0
# define P_GS           0
#endif
#define P_SIMDF3        0x10000         /* 0xf3 opcode prefix */
#define P_SIMDF2        0x20000         /* 0xf2 opcode prefix */

#define OPC_ARITH_EvIz	(0x81)
#define OPC_ARITH_EvIb	(0x83)
#define OPC_ARITH_GvEv	(0x03)		/* ... plus (ARITH_FOO << 3) */
#define OPC_ANDN        (0xf2 | P_EXT38)
#define OPC_ADD_GvEv	(OPC_ARITH_GvEv | (ARITH_ADD << 3))
#define OPC_BSF         (0xbc | P_EXT)
#define OPC_BSR         (0xbd | P_EXT)
#define OPC_BSWAP	(0xc8 | P_EXT)
#define OPC_CALL_Jz	(0xe8)
#define OPC_CMOVCC      (0x40 | P_EXT)  /* ... plus condition code */
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
#define OPC_LZCNT       (0xbd | P_EXT | P_SIMDF3)
#define OPC_MOVB_EvGv	(0x88)		/* stores, more or less */
#define OPC_MOVL_EvGv	(0x89)		/* stores, more or less */
#define OPC_MOVL_GvEv	(0x8b)		/* loads, more or less */
#define OPC_MOVB_EvIz   (0xc6)
#define OPC_MOVL_EvIz	(0xc7)
#define OPC_MOVL_Iv     (0xb8)
#define OPC_MOVBE_GyMy  (0xf0 | P_EXT38)
#define OPC_MOVBE_MyGy  (0xf1 | P_EXT38)
#define OPC_MOVSBL	(0xbe | P_EXT)
#define OPC_MOVSWL	(0xbf | P_EXT)
#define OPC_MOVSLQ	(0x63 | P_REXW)
#define OPC_MOVZBL	(0xb6 | P_EXT)
#define OPC_MOVZWL	(0xb7 | P_EXT)
#define OPC_POP_r32	(0x58)
#define OPC_POPCNT      (0xb8 | P_EXT | P_SIMDF3)
#define OPC_PUSH_r32	(0x50)
#define OPC_PUSH_Iv	(0x68)
#define OPC_PUSH_Ib	(0x6a)
#define OPC_RET		(0xc3)
#define OPC_SETCC	(0x90 | P_EXT | P_REXB_RM) /* ... plus cc */
#define OPC_SHIFT_1	(0xd1)
#define OPC_SHIFT_Ib	(0xc1)
#define OPC_SHIFT_cl	(0xd3)
#define OPC_SARX        (0xf7 | P_EXT38 | P_SIMDF3)
#define OPC_SHLX        (0xf7 | P_EXT38 | P_DATA16)
#define OPC_SHRX        (0xf7 | P_EXT38 | P_SIMDF2)
#define OPC_TESTL	(0x85)
#define OPC_TZCNT       (0xbc | P_EXT | P_SIMDF3)
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
#define EXT5_INC_Ev	0
#define EXT5_DEC_Ev	1
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

static const uint8_t tcg_cond_to_jcc[] = {
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

#if TCG_TARGET_REG_BITS == 64
static void tcg_out_opc(TCGContext *s, int opc, int r, int rm, int x)
{
    int rex;

    if (opc & P_GS) {
        tcg_out8(s, 0x65);
    }
    if (opc & P_DATA16) {
        /* We should never be asking for both 16 and 64-bit operation.  */
        tcg_debug_assert((opc & P_REXW) == 0);
        tcg_out8(s, 0x66);
    }
    if (opc & P_ADDR32) {
        tcg_out8(s, 0x67);
    }
    if (opc & P_SIMDF3) {
        tcg_out8(s, 0xf3);
    } else if (opc & P_SIMDF2) {
        tcg_out8(s, 0xf2);
    }

    rex = 0;
    rex |= (opc & P_REXW) ? 0x8 : 0x0;  /* REX.W */
    rex |= (r & 8) >> 1;                /* REX.R */
    rex |= (x & 8) >> 2;                /* REX.X */
    rex |= (rm & 8) >> 3;               /* REX.B */

    /* P_REXB_{R,RM} indicates that the given register is the low byte.
       For %[abcd]l we need no REX prefix, but for %{si,di,bp,sp}l we do,
       as otherwise the encoding indicates %[abcd]h.  Note that the values
       that are ORed in merely indicate that the REX byte must be present;
       those bits get discarded in output.  */
    rex |= opc & (r >= 4 ? P_REXB_R : 0);
    rex |= opc & (rm >= 4 ? P_REXB_RM : 0);

    if (rex) {
        tcg_out8(s, (uint8_t)(rex | 0x40));
    }

    if (opc & (P_EXT | P_EXT38)) {
        tcg_out8(s, 0x0f);
        if (opc & P_EXT38) {
            tcg_out8(s, 0x38);
        }
    }

    tcg_out8(s, opc);
}
#else
static void tcg_out_opc(TCGContext *s, int opc)
{
    if (opc & P_DATA16) {
        tcg_out8(s, 0x66);
    }
    if (opc & P_SIMDF3) {
        tcg_out8(s, 0xf3);
    } else if (opc & P_SIMDF2) {
        tcg_out8(s, 0xf2);
    }
    if (opc & (P_EXT | P_EXT38)) {
        tcg_out8(s, 0x0f);
        if (opc & P_EXT38) {
            tcg_out8(s, 0x38);
        }
    }
    tcg_out8(s, opc);
}
/* Discard the register arguments to tcg_out_opc early, so as not to penalize
   the 32-bit compilation paths.  This method works with all versions of gcc,
   whereas relying on optimization may not be able to exclude them.  */
#define tcg_out_opc(s, opc, r, rm, x)  (tcg_out_opc)(s, opc)
#endif

static void tcg_out_modrm(TCGContext *s, int opc, int r, int rm)
{
    tcg_out_opc(s, opc, r, rm, 0);
    tcg_out8(s, 0xc0 | (LOWREGMASK(r) << 3) | LOWREGMASK(rm));
}

static void tcg_out_vex_modrm(TCGContext *s, int opc, int r, int v, int rm)
{
    int tmp;

    if ((opc & (P_REXW | P_EXT | P_EXT38)) || (rm & 8)) {
        /* Three byte VEX prefix.  */
        tcg_out8(s, 0xc4);

        /* VEX.m-mmmm */
        if (opc & P_EXT38) {
            tmp = 2;
        } else if (opc & P_EXT) {
            tmp = 1;
        } else {
            tcg_abort();
        }
        tmp |= 0x40;                       /* VEX.X */
        tmp |= (r & 8 ? 0 : 0x80);         /* VEX.R */
        tmp |= (rm & 8 ? 0 : 0x20);        /* VEX.B */
        tcg_out8(s, tmp);

        tmp = (opc & P_REXW ? 0x80 : 0);   /* VEX.W */
    } else {
        /* Two byte VEX prefix.  */
        tcg_out8(s, 0xc5);

        tmp = (r & 8 ? 0 : 0x80);          /* VEX.R */
    }
    /* VEX.pp */
    if (opc & P_DATA16) {
        tmp |= 1;                          /* 0x66 */
    } else if (opc & P_SIMDF3) {
        tmp |= 2;                          /* 0xf3 */
    } else if (opc & P_SIMDF2) {
        tmp |= 3;                          /* 0xf2 */
    }
    tmp |= (~v & 15) << 3;                 /* VEX.vvvv */
    tcg_out8(s, tmp);
    tcg_out8(s, opc);
    tcg_out8(s, 0xc0 | (LOWREGMASK(r) << 3) | LOWREGMASK(rm));
}

/* Output an opcode with a full "rm + (index<<shift) + offset" address mode.
   We handle either RM and INDEX missing with a negative value.  In 64-bit
   mode for absolute addresses, ~RM is the size of the immediate operand
   that will follow the instruction.  */

static void tcg_out_modrm_sib_offset(TCGContext *s, int opc, int r, int rm,
                                     int index, int shift, intptr_t offset)
{
    int mod, len;

    if (index < 0 && rm < 0) {
        if (TCG_TARGET_REG_BITS == 64) {
            /* Try for a rip-relative addressing mode.  This has replaced
               the 32-bit-mode absolute addressing encoding.  */
            intptr_t pc = (intptr_t)s->code_ptr + 5 + ~rm;
            intptr_t disp = offset - pc;
            if (disp == (int32_t)disp) {
                tcg_out_opc(s, opc, r, 0, 0);
                tcg_out8(s, (LOWREGMASK(r) << 3) | 5);
                tcg_out32(s, disp);
                return;
            }

            /* Try for an absolute address encoding.  This requires the
               use of the MODRM+SIB encoding and is therefore larger than
               rip-relative addressing.  */
            if (offset == (int32_t)offset) {
                tcg_out_opc(s, opc, r, 0, 0);
                tcg_out8(s, (LOWREGMASK(r) << 3) | 4);
                tcg_out8(s, (4 << 3) | 5);
                tcg_out32(s, offset);
                return;
            }

            /* ??? The memory isn't directly addressable.  */
            tcg_abort();
        } else {
            /* Absolute address.  */
            tcg_out_opc(s, opc, r, 0, 0);
            tcg_out8(s, (r << 3) | 5);
            tcg_out32(s, offset);
            return;
        }
    }

    /* Find the length of the immediate addend.  Note that the encoding
       that would be used for (%ebp) indicates absolute addressing.  */
    if (rm < 0) {
        mod = 0, len = 4, rm = 5;
    } else if (offset == 0 && LOWREGMASK(rm) != TCG_REG_EBP) {
        mod = 0, len = 0;
    } else if (offset == (int8_t)offset) {
        mod = 0x40, len = 1;
    } else {
        mod = 0x80, len = 4;
    }

    /* Use a single byte MODRM format if possible.  Note that the encoding
       that would be used for %esp is the escape to the two byte form.  */
    if (index < 0 && LOWREGMASK(rm) != TCG_REG_ESP) {
        /* Single byte MODRM format.  */
        tcg_out_opc(s, opc, r, rm, 0);
        tcg_out8(s, mod | (LOWREGMASK(r) << 3) | LOWREGMASK(rm));
    } else {
        /* Two byte MODRM+SIB format.  */

        /* Note that the encoding that would place %esp into the index
           field indicates no index register.  In 64-bit mode, the REX.X
           bit counts, so %r12 can be used as the index.  */
        if (index < 0) {
            index = 4;
        } else {
            tcg_debug_assert(index != TCG_REG_ESP);
        }

        tcg_out_opc(s, opc, r, rm, index);
        tcg_out8(s, mod | (LOWREGMASK(r) << 3) | 4);
        tcg_out8(s, (shift << 6) | (LOWREGMASK(index) << 3) | LOWREGMASK(rm));
    }

    if (len == 1) {
        tcg_out8(s, offset);
    } else if (len == 4) {
        tcg_out32(s, offset);
    }
}

/* A simplification of the above with no index or shift.  */
static inline void tcg_out_modrm_offset(TCGContext *s, int opc, int r,
                                        int rm, intptr_t offset)
{
    tcg_out_modrm_sib_offset(s, opc, r, rm, -1, 0, offset);
}

/* Generate dest op= src.  Uses the same ARITH_* codes as tgen_arithi.  */
static inline void tgen_arithr(TCGContext *s, int subop, int dest, int src)
{
    /* Propagate an opcode prefix, such as P_REXW.  */
    int ext = subop & ~0x7;
    subop &= 0x7;

    tcg_out_modrm(s, OPC_ARITH_GvEv + (subop << 3) + ext, dest, src);
}

static inline void tcg_out_mov(TCGContext *s, TCGType type,
                               TCGReg ret, TCGReg arg)
{
    if (arg != ret) {
        int opc = OPC_MOVL_GvEv + (type == TCG_TYPE_I64 ? P_REXW : 0);
        tcg_out_modrm(s, opc, ret, arg);
    }
}

static void tcg_out_movi(TCGContext *s, TCGType type,
                         TCGReg ret, tcg_target_long arg)
{
    tcg_target_long diff;

    if (arg == 0) {
        tgen_arithr(s, ARITH_XOR, ret, ret);
        return;
    }
    if (arg == (uint32_t)arg || type == TCG_TYPE_I32) {
        tcg_out_opc(s, OPC_MOVL_Iv + LOWREGMASK(ret), 0, ret, 0);
        tcg_out32(s, arg);
        return;
    }
    if (arg == (int32_t)arg) {
        tcg_out_modrm(s, OPC_MOVL_EvIz + P_REXW, 0, ret);
        tcg_out32(s, arg);
        return;
    }

    /* Try a 7 byte pc-relative lea before the 10 byte movq.  */
    diff = arg - ((uintptr_t)s->code_ptr + 7);
    if (diff == (int32_t)diff) {
        tcg_out_opc(s, OPC_LEA | P_REXW, ret, 0, 0);
        tcg_out8(s, (LOWREGMASK(ret) << 3) | 5);
        tcg_out32(s, diff);
        return;
    }

    tcg_out_opc(s, OPC_MOVL_Iv + P_REXW + LOWREGMASK(ret), 0, ret, 0);
    tcg_out64(s, arg);
}

static inline void tcg_out_pushi(TCGContext *s, tcg_target_long val)
{
    if (val == (int8_t)val) {
        tcg_out_opc(s, OPC_PUSH_Ib, 0, 0, 0);
        tcg_out8(s, val);
    } else if (val == (int32_t)val) {
        tcg_out_opc(s, OPC_PUSH_Iv, 0, 0, 0);
        tcg_out32(s, val);
    } else {
        tcg_abort();
    }
}

static inline void tcg_out_mb(TCGContext *s, TCGArg a0)
{
    /* Given the strength of x86 memory ordering, we only need care for
       store-load ordering.  Experimentally, "lock orl $0,0(%esp)" is
       faster than "mfence", so don't bother with the sse insn.  */
    if (a0 & TCG_MO_ST_LD) {
        tcg_out8(s, 0xf0);
        tcg_out_modrm_offset(s, OPC_ARITH_EvIb, ARITH_OR, TCG_REG_ESP, 0);
        tcg_out8(s, 0);
    }
}

static inline void tcg_out_push(TCGContext *s, int reg)
{
    tcg_out_opc(s, OPC_PUSH_r32 + LOWREGMASK(reg), 0, reg, 0);
}

static inline void tcg_out_pop(TCGContext *s, int reg)
{
    tcg_out_opc(s, OPC_POP_r32 + LOWREGMASK(reg), 0, reg, 0);
}

static inline void tcg_out_ld(TCGContext *s, TCGType type, TCGReg ret,
                              TCGReg arg1, intptr_t arg2)
{
    int opc = OPC_MOVL_GvEv + (type == TCG_TYPE_I64 ? P_REXW : 0);
    tcg_out_modrm_offset(s, opc, ret, arg1, arg2);
}

static inline void tcg_out_st(TCGContext *s, TCGType type, TCGReg arg,
                              TCGReg arg1, intptr_t arg2)
{
    int opc = OPC_MOVL_EvGv + (type == TCG_TYPE_I64 ? P_REXW : 0);
    tcg_out_modrm_offset(s, opc, arg, arg1, arg2);
}

static bool tcg_out_sti(TCGContext *s, TCGType type, TCGArg val,
                        TCGReg base, intptr_t ofs)
{
    int rexw = 0;
    if (TCG_TARGET_REG_BITS == 64 && type == TCG_TYPE_I64) {
        if (val != (int32_t)val) {
            return false;
        }
        rexw = P_REXW;
    }
    tcg_out_modrm_offset(s, OPC_MOVL_EvIz | rexw, 0, base, ofs);
    tcg_out32(s, val);
    return true;
}

static void tcg_out_shifti(TCGContext *s, int subopc, int reg, int count)
{
    /* Propagate an opcode prefix, such as P_DATA16.  */
    int ext = subopc & ~0x7;
    subopc &= 0x7;

    if (count == 1) {
        tcg_out_modrm(s, OPC_SHIFT_1 + ext, subopc, reg);
    } else {
        tcg_out_modrm(s, OPC_SHIFT_Ib + ext, subopc, reg);
        tcg_out8(s, count);
    }
}

static inline void tcg_out_bswap32(TCGContext *s, int reg)
{
    tcg_out_opc(s, OPC_BSWAP + LOWREGMASK(reg), 0, reg, 0);
}

static inline void tcg_out_rolw_8(TCGContext *s, int reg)
{
    tcg_out_shifti(s, SHIFT_ROL + P_DATA16, reg, 8);
}

static inline void tcg_out_ext8u(TCGContext *s, int dest, int src)
{
    /* movzbl */
    tcg_debug_assert(src < 4 || TCG_TARGET_REG_BITS == 64);
    tcg_out_modrm(s, OPC_MOVZBL + P_REXB_RM, dest, src);
}

static void tcg_out_ext8s(TCGContext *s, int dest, int src, int rexw)
{
    /* movsbl */
    tcg_debug_assert(src < 4 || TCG_TARGET_REG_BITS == 64);
    tcg_out_modrm(s, OPC_MOVSBL + P_REXB_RM + rexw, dest, src);
}

static inline void tcg_out_ext16u(TCGContext *s, int dest, int src)
{
    /* movzwl */
    tcg_out_modrm(s, OPC_MOVZWL, dest, src);
}

static inline void tcg_out_ext16s(TCGContext *s, int dest, int src, int rexw)
{
    /* movsw[lq] */
    tcg_out_modrm(s, OPC_MOVSWL + rexw, dest, src);
}

static inline void tcg_out_ext32u(TCGContext *s, int dest, int src)
{
    /* 32-bit mov zero extends.  */
    tcg_out_modrm(s, OPC_MOVL_GvEv, dest, src);
}

static inline void tcg_out_ext32s(TCGContext *s, int dest, int src)
{
    tcg_out_modrm(s, OPC_MOVSLQ, dest, src);
}

static inline void tcg_out_bswap64(TCGContext *s, int reg)
{
    tcg_out_opc(s, OPC_BSWAP + P_REXW + LOWREGMASK(reg), 0, reg, 0);
}

static void tgen_arithi(TCGContext *s, int c, int r0,
                        tcg_target_long val, int cf)
{
    int rexw = 0;

    if (TCG_TARGET_REG_BITS == 64) {
        rexw = c & -8;
        c &= 7;
    }

    /* ??? While INC is 2 bytes shorter than ADDL $1, they also induce
       partial flags update stalls on Pentium4 and are not recommended
       by current Intel optimization manuals.  */
    if (!cf && (c == ARITH_ADD || c == ARITH_SUB) && (val == 1 || val == -1)) {
        int is_inc = (c == ARITH_ADD) ^ (val < 0);
        if (TCG_TARGET_REG_BITS == 64) {
            /* The single-byte increment encodings are re-tasked as the
               REX prefixes.  Use the MODRM encoding.  */
            tcg_out_modrm(s, OPC_GRP5 + rexw,
                          (is_inc ? EXT5_INC_Ev : EXT5_DEC_Ev), r0);
        } else {
            tcg_out8(s, (is_inc ? OPC_INC_r32 : OPC_DEC_r32) + r0);
        }
        return;
    }

    if (c == ARITH_AND) {
        if (TCG_TARGET_REG_BITS == 64) {
            if (val == 0xffffffffu) {
                tcg_out_ext32u(s, r0, r0);
                return;
            }
            if (val == (uint32_t)val) {
                /* AND with no high bits set can use a 32-bit operation.  */
                rexw = 0;
            }
        }
        if (val == 0xffu && (r0 < 4 || TCG_TARGET_REG_BITS == 64)) {
            tcg_out_ext8u(s, r0, r0);
            return;
        }
        if (val == 0xffffu) {
            tcg_out_ext16u(s, r0, r0);
            return;
        }
    }

    if (val == (int8_t)val) {
        tcg_out_modrm(s, OPC_ARITH_EvIb + rexw, c, r0);
        tcg_out8(s, val);
        return;
    }
    if (rexw == 0 || val == (int32_t)val) {
        tcg_out_modrm(s, OPC_ARITH_EvIz + rexw, c, r0);
        tcg_out32(s, val);
        return;
    }

    tcg_abort();
}

static void tcg_out_addi(TCGContext *s, int reg, tcg_target_long val)
{
    if (val != 0) {
        tgen_arithi(s, ARITH_ADD + P_REXW, reg, val, 0);
    }
}

/* Use SMALL != 0 to force a short forward branch.  */
static void tcg_out_jxx(TCGContext *s, int opc, TCGLabel *l, int small)
{
    int32_t val, val1;

    if (l->has_value) {
        val = tcg_pcrel_diff(s, l->u.value_ptr);
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
                tcg_out_opc(s, OPC_JCC_long + opc, 0, 0, 0);
                tcg_out32(s, val - 6);
            }
        }
    } else if (small) {
        if (opc == -1) {
            tcg_out8(s, OPC_JMP_short);
        } else {
            tcg_out8(s, OPC_JCC_short + opc);
        }
        tcg_out_reloc(s, s->code_ptr, R_386_PC8, l, -1);
        s->code_ptr += 1;
    } else {
        if (opc == -1) {
            tcg_out8(s, OPC_JMP_long);
        } else {
            tcg_out_opc(s, OPC_JCC_long + opc, 0, 0, 0);
        }
        tcg_out_reloc(s, s->code_ptr, R_386_PC32, l, -4);
        s->code_ptr += 4;
    }
}

static void tcg_out_cmp(TCGContext *s, TCGArg arg1, TCGArg arg2,
                        int const_arg2, int rexw)
{
    if (const_arg2) {
        if (arg2 == 0) {
            /* test r, r */
            tcg_out_modrm(s, OPC_TESTL + rexw, arg1, arg1);
        } else {
            tgen_arithi(s, ARITH_CMP + rexw, arg1, arg2, 0);
        }
    } else {
        tgen_arithr(s, ARITH_CMP + rexw, arg1, arg2);
    }
}

static void tcg_out_brcond32(TCGContext *s, TCGCond cond,
                             TCGArg arg1, TCGArg arg2, int const_arg2,
                             TCGLabel *label, int small)
{
    tcg_out_cmp(s, arg1, arg2, const_arg2, 0);
    tcg_out_jxx(s, tcg_cond_to_jcc[cond], label, small);
}

#if TCG_TARGET_REG_BITS == 64
static void tcg_out_brcond64(TCGContext *s, TCGCond cond,
                             TCGArg arg1, TCGArg arg2, int const_arg2,
                             TCGLabel *label, int small)
{
    tcg_out_cmp(s, arg1, arg2, const_arg2, P_REXW);
    tcg_out_jxx(s, tcg_cond_to_jcc[cond], label, small);
}
#else
/* XXX: we implement it at the target level to avoid having to
   handle cross basic blocks temporaries */
static void tcg_out_brcond2(TCGContext *s, const TCGArg *args,
                            const int *const_args, int small)
{
    TCGLabel *label_next = gen_new_label();
    TCGLabel *label_this = arg_label(args[5]);

    switch(args[4]) {
    case TCG_COND_EQ:
        tcg_out_brcond32(s, TCG_COND_NE, args[0], args[2], const_args[2],
                         label_next, 1);
        tcg_out_brcond32(s, TCG_COND_EQ, args[1], args[3], const_args[3],
                         label_this, small);
        break;
    case TCG_COND_NE:
        tcg_out_brcond32(s, TCG_COND_NE, args[0], args[2], const_args[2],
                         label_this, small);
        tcg_out_brcond32(s, TCG_COND_NE, args[1], args[3], const_args[3],
                         label_this, small);
        break;
    case TCG_COND_LT:
        tcg_out_brcond32(s, TCG_COND_LT, args[1], args[3], const_args[3],
                         label_this, small);
        tcg_out_jxx(s, JCC_JNE, label_next, 1);
        tcg_out_brcond32(s, TCG_COND_LTU, args[0], args[2], const_args[2],
                         label_this, small);
        break;
    case TCG_COND_LE:
        tcg_out_brcond32(s, TCG_COND_LT, args[1], args[3], const_args[3],
                         label_this, small);
        tcg_out_jxx(s, JCC_JNE, label_next, 1);
        tcg_out_brcond32(s, TCG_COND_LEU, args[0], args[2], const_args[2],
                         label_this, small);
        break;
    case TCG_COND_GT:
        tcg_out_brcond32(s, TCG_COND_GT, args[1], args[3], const_args[3],
                         label_this, small);
        tcg_out_jxx(s, JCC_JNE, label_next, 1);
        tcg_out_brcond32(s, TCG_COND_GTU, args[0], args[2], const_args[2],
                         label_this, small);
        break;
    case TCG_COND_GE:
        tcg_out_brcond32(s, TCG_COND_GT, args[1], args[3], const_args[3],
                         label_this, small);
        tcg_out_jxx(s, JCC_JNE, label_next, 1);
        tcg_out_brcond32(s, TCG_COND_GEU, args[0], args[2], const_args[2],
                         label_this, small);
        break;
    case TCG_COND_LTU:
        tcg_out_brcond32(s, TCG_COND_LTU, args[1], args[3], const_args[3],
                         label_this, small);
        tcg_out_jxx(s, JCC_JNE, label_next, 1);
        tcg_out_brcond32(s, TCG_COND_LTU, args[0], args[2], const_args[2],
                         label_this, small);
        break;
    case TCG_COND_LEU:
        tcg_out_brcond32(s, TCG_COND_LTU, args[1], args[3], const_args[3],
                         label_this, small);
        tcg_out_jxx(s, JCC_JNE, label_next, 1);
        tcg_out_brcond32(s, TCG_COND_LEU, args[0], args[2], const_args[2],
                         label_this, small);
        break;
    case TCG_COND_GTU:
        tcg_out_brcond32(s, TCG_COND_GTU, args[1], args[3], const_args[3],
                         label_this, small);
        tcg_out_jxx(s, JCC_JNE, label_next, 1);
        tcg_out_brcond32(s, TCG_COND_GTU, args[0], args[2], const_args[2],
                         label_this, small);
        break;
    case TCG_COND_GEU:
        tcg_out_brcond32(s, TCG_COND_GTU, args[1], args[3], const_args[3],
                         label_this, small);
        tcg_out_jxx(s, JCC_JNE, label_next, 1);
        tcg_out_brcond32(s, TCG_COND_GEU, args[0], args[2], const_args[2],
                         label_this, small);
        break;
    default:
        tcg_abort();
    }
    tcg_out_label(s, label_next, s->code_ptr);
}
#endif

static void tcg_out_setcond32(TCGContext *s, TCGCond cond, TCGArg dest,
                              TCGArg arg1, TCGArg arg2, int const_arg2)
{
    tcg_out_cmp(s, arg1, arg2, const_arg2, 0);
    tcg_out_modrm(s, OPC_SETCC | tcg_cond_to_jcc[cond], 0, dest);
    tcg_out_ext8u(s, dest, dest);
}

#if TCG_TARGET_REG_BITS == 64
static void tcg_out_setcond64(TCGContext *s, TCGCond cond, TCGArg dest,
                              TCGArg arg1, TCGArg arg2, int const_arg2)
{
    tcg_out_cmp(s, arg1, arg2, const_arg2, P_REXW);
    tcg_out_modrm(s, OPC_SETCC | tcg_cond_to_jcc[cond], 0, dest);
    tcg_out_ext8u(s, dest, dest);
}
#else
static void tcg_out_setcond2(TCGContext *s, const TCGArg *args,
                             const int *const_args)
{
    TCGArg new_args[6];
    TCGLabel *label_true, *label_over;

    memcpy(new_args, args+1, 5*sizeof(TCGArg));

    if (args[0] == args[1] || args[0] == args[2]
        || (!const_args[3] && args[0] == args[3])
        || (!const_args[4] && args[0] == args[4])) {
        /* When the destination overlaps with one of the argument
           registers, don't do anything tricky.  */
        label_true = gen_new_label();
        label_over = gen_new_label();

        new_args[5] = label_arg(label_true);
        tcg_out_brcond2(s, new_args, const_args+1, 1);

        tcg_out_movi(s, TCG_TYPE_I32, args[0], 0);
        tcg_out_jxx(s, JCC_JMP, label_over, 1);
        tcg_out_label(s, label_true, s->code_ptr);

        tcg_out_movi(s, TCG_TYPE_I32, args[0], 1);
        tcg_out_label(s, label_over, s->code_ptr);
    } else {
        /* When the destination does not overlap one of the arguments,
           clear the destination first, jump if cond false, and emit an
           increment in the true case.  This results in smaller code.  */

        tcg_out_movi(s, TCG_TYPE_I32, args[0], 0);

        label_over = gen_new_label();
        new_args[4] = tcg_invert_cond(new_args[4]);
        new_args[5] = label_arg(label_over);
        tcg_out_brcond2(s, new_args, const_args+1, 1);

        tgen_arithi(s, ARITH_ADD, args[0], 1, 0);
        tcg_out_label(s, label_over, s->code_ptr);
    }
}
#endif

static void tcg_out_cmov(TCGContext *s, TCGCond cond, int rexw,
                         TCGReg dest, TCGReg v1)
{
    if (have_cmov) {
        tcg_out_modrm(s, OPC_CMOVCC | tcg_cond_to_jcc[cond] | rexw, dest, v1);
    } else {
        TCGLabel *over = gen_new_label();
        tcg_out_jxx(s, tcg_cond_to_jcc[tcg_invert_cond(cond)], over, 1);
        tcg_out_mov(s, TCG_TYPE_I32, dest, v1);
        tcg_out_label(s, over, s->code_ptr);
    }
}

static void tcg_out_movcond32(TCGContext *s, TCGCond cond, TCGReg dest,
                              TCGReg c1, TCGArg c2, int const_c2,
                              TCGReg v1)
{
    tcg_out_cmp(s, c1, c2, const_c2, 0);
    tcg_out_cmov(s, cond, 0, dest, v1);
}

#if TCG_TARGET_REG_BITS == 64
static void tcg_out_movcond64(TCGContext *s, TCGCond cond, TCGReg dest,
                              TCGReg c1, TCGArg c2, int const_c2,
                              TCGReg v1)
{
    tcg_out_cmp(s, c1, c2, const_c2, P_REXW);
    tcg_out_cmov(s, cond, P_REXW, dest, v1);
}
#endif

static void tcg_out_ctz(TCGContext *s, int rexw, TCGReg dest, TCGReg arg1,
                        TCGArg arg2, bool const_a2)
{
    if (have_bmi1) {
        tcg_out_modrm(s, OPC_TZCNT + rexw, dest, arg1);
        if (const_a2) {
            tcg_debug_assert(arg2 == (rexw ? 64 : 32));
        } else {
            tcg_debug_assert(dest != arg2);
            tcg_out_cmov(s, TCG_COND_LTU, rexw, dest, arg2);
        }
    } else {
        tcg_debug_assert(dest != arg2);
        tcg_out_modrm(s, OPC_BSF + rexw, dest, arg1);
        tcg_out_cmov(s, TCG_COND_EQ, rexw, dest, arg2);
    }
}

static void tcg_out_clz(TCGContext *s, int rexw, TCGReg dest, TCGReg arg1,
                        TCGArg arg2, bool const_a2)
{
    if (have_lzcnt) {
        tcg_out_modrm(s, OPC_LZCNT + rexw, dest, arg1);
        if (const_a2) {
            tcg_debug_assert(arg2 == (rexw ? 64 : 32));
        } else {
            tcg_debug_assert(dest != arg2);
            tcg_out_cmov(s, TCG_COND_LTU, rexw, dest, arg2);
        }
    } else {
        tcg_debug_assert(!const_a2);
        tcg_debug_assert(dest != arg1);
        tcg_debug_assert(dest != arg2);

        /* Recall that the output of BSR is the index not the count.  */
        tcg_out_modrm(s, OPC_BSR + rexw, dest, arg1);
        tgen_arithi(s, ARITH_XOR + rexw, dest, rexw ? 63 : 31, 0);

        /* Since we have destroyed the flags from BSR, we have to re-test.  */
        tcg_out_cmp(s, arg1, 0, 1, rexw);
        tcg_out_cmov(s, TCG_COND_EQ, rexw, dest, arg2);
    }
}

static void tcg_out_branch(TCGContext *s, int call, tcg_insn_unit *dest)
{
    intptr_t disp = tcg_pcrel_diff(s, dest) - 5;

    if (disp == (int32_t)disp) {
        tcg_out_opc(s, call ? OPC_CALL_Jz : OPC_JMP_long, 0, 0, 0);
        tcg_out32(s, disp);
    } else {
        tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_R10, (uintptr_t)dest);
        tcg_out_modrm(s, OPC_GRP5,
                      call ? EXT5_CALLN_Ev : EXT5_JMPN_Ev, TCG_REG_R10);
    }
}

static inline void tcg_out_call(TCGContext *s, tcg_insn_unit *dest)
{
    tcg_out_branch(s, 1, dest);
}

static void tcg_out_jmp(TCGContext *s, tcg_insn_unit *dest)
{
    tcg_out_branch(s, 0, dest);
}

static void tcg_out_nopn(TCGContext *s, int n)
{
    int i;
    /* Emit 1 or 2 operand size prefixes for the standard one byte nop,
     * "xchg %eax,%eax", forming "xchg %ax,%ax". All cores accept the
     * duplicate prefix, and all of the interesting recent cores can
     * decode and discard the duplicates in a single cycle.
     */
    tcg_debug_assert(n >= 1);
    for (i = 1; i < n; ++i) {
        tcg_out8(s, 0x66);
    }
    tcg_out8(s, 0x90);
}

#if defined(CONFIG_SOFTMMU)
/* helper signature: helper_ret_ld_mmu(CPUState *env, target_ulong addr,
 *                                     int mmu_idx, uintptr_t ra)
 */
static void * const qemu_ld_helpers[16] = {
    [MO_UB]   = helper_ret_ldub_mmu,
    [MO_LEUW] = helper_le_lduw_mmu,
    [MO_LEUL] = helper_le_ldul_mmu,
    [MO_LEQ]  = helper_le_ldq_mmu,
    [MO_BEUW] = helper_be_lduw_mmu,
    [MO_BEUL] = helper_be_ldul_mmu,
    [MO_BEQ]  = helper_be_ldq_mmu,
};

/* helper signature: helper_ret_st_mmu(CPUState *env, target_ulong addr,
 *                                     uintxx_t val, int mmu_idx, uintptr_t ra)
 */
static void * const qemu_st_helpers[16] = {
    [MO_UB]   = helper_ret_stb_mmu,
    [MO_LEUW] = helper_le_stw_mmu,
    [MO_LEUL] = helper_le_stl_mmu,
    [MO_LEQ]  = helper_le_stq_mmu,
    [MO_BEUW] = helper_be_stw_mmu,
    [MO_BEUL] = helper_be_stl_mmu,
    [MO_BEQ]  = helper_be_stq_mmu,
};

/* Perform the TLB load and compare.

   Inputs:
   ADDRLO and ADDRHI contain the low and high part of the address.

   MEM_INDEX and S_BITS are the memory context and log2 size of the load.

   WHICH is the offset into the CPUTLBEntry structure of the slot to read.
   This should be offsetof addr_read or addr_write.

   Outputs:
   LABEL_PTRS is filled with 1 (32-bit addresses) or 2 (64-bit addresses)
   positions of the displacements of forward jumps to the TLB miss case.

   Second argument register is loaded with the low part of the address.
   In the TLB hit case, it has been adjusted as indicated by the TLB
   and so is a host address.  In the TLB miss case, it continues to
   hold a guest address.

   First argument register is clobbered.  */

static inline void tcg_out_tlb_load(TCGContext *s, TCGReg addrlo, TCGReg addrhi,
                                    int mem_index, TCGMemOp opc,
                                    tcg_insn_unit **label_ptr, int which)
{
    const TCGReg r0 = TCG_REG_L0;
    const TCGReg r1 = TCG_REG_L1;
    TCGType ttype = TCG_TYPE_I32;
    TCGType tlbtype = TCG_TYPE_I32;
    int trexw = 0, hrexw = 0, tlbrexw = 0;
    unsigned a_bits = get_alignment_bits(opc);
    unsigned s_bits = opc & MO_SIZE;
    unsigned a_mask = (1 << a_bits) - 1;
    unsigned s_mask = (1 << s_bits) - 1;
    target_ulong tlb_mask;

    if (TCG_TARGET_REG_BITS == 64) {
        if (TARGET_LONG_BITS == 64) {
            ttype = TCG_TYPE_I64;
            trexw = P_REXW;
        }
        if (TCG_TYPE_PTR == TCG_TYPE_I64) {
            hrexw = P_REXW;
            if (TARGET_PAGE_BITS + CPU_TLB_BITS > 32) {
                tlbtype = TCG_TYPE_I64;
                tlbrexw = P_REXW;
            }
        }
    }

    tcg_out_mov(s, tlbtype, r0, addrlo);
    /* If the required alignment is at least as large as the access, simply
       copy the address and mask.  For lesser alignments, check that we don't
       cross pages for the complete access.  */
    if (a_bits >= s_bits) {
        tcg_out_mov(s, ttype, r1, addrlo);
    } else {
        tcg_out_modrm_offset(s, OPC_LEA + trexw, r1, addrlo, s_mask - a_mask);
    }
    tlb_mask = (target_ulong)TARGET_PAGE_MASK | a_mask;

    tcg_out_shifti(s, SHIFT_SHR + tlbrexw, r0,
                   TARGET_PAGE_BITS - CPU_TLB_ENTRY_BITS);

    tgen_arithi(s, ARITH_AND + trexw, r1, tlb_mask, 0);
    tgen_arithi(s, ARITH_AND + tlbrexw, r0,
                (CPU_TLB_SIZE - 1) << CPU_TLB_ENTRY_BITS, 0);

    tcg_out_modrm_sib_offset(s, OPC_LEA + hrexw, r0, TCG_AREG0, r0, 0,
                             offsetof(CPUArchState, tlb_table[mem_index][0])
                             + which);

    /* cmp 0(r0), r1 */
    tcg_out_modrm_offset(s, OPC_CMP_GvEv + trexw, r1, r0, 0);

    /* Prepare for both the fast path add of the tlb addend, and the slow
       path function argument setup.  There are two cases worth note:
       For 32-bit guest and x86_64 host, MOVL zero-extends the guest address
       before the fastpath ADDQ below.  For 64-bit guest and x32 host, MOVQ
       copies the entire guest address for the slow path, while truncation
       for the 32-bit host happens with the fastpath ADDL below.  */
    tcg_out_mov(s, ttype, r1, addrlo);

    /* jne slow_path */
    tcg_out_opc(s, OPC_JCC_long + JCC_JNE, 0, 0, 0);
    label_ptr[0] = s->code_ptr;
    s->code_ptr += 4;

    if (TARGET_LONG_BITS > TCG_TARGET_REG_BITS) {
        /* cmp 4(r0), addrhi */
        tcg_out_modrm_offset(s, OPC_CMP_GvEv, addrhi, r0, 4);

        /* jne slow_path */
        tcg_out_opc(s, OPC_JCC_long + JCC_JNE, 0, 0, 0);
        label_ptr[1] = s->code_ptr;
        s->code_ptr += 4;
    }

    /* TLB Hit.  */

    /* add addend(r0), r1 */
    tcg_out_modrm_offset(s, OPC_ADD_GvEv + hrexw, r1, r0,
                         offsetof(CPUTLBEntry, addend) - which);
}

/*
 * Record the context of a call to the out of line helper code for the slow path
 * for a load or store, so that we can later generate the correct helper code
 */
static void add_qemu_ldst_label(TCGContext *s, bool is_ld, TCGMemOpIdx oi,
                                TCGReg datalo, TCGReg datahi,
                                TCGReg addrlo, TCGReg addrhi,
                                tcg_insn_unit *raddr,
                                tcg_insn_unit **label_ptr)
{
    TCGLabelQemuLdst *label = new_ldst_label(s);

    label->is_ld = is_ld;
    label->oi = oi;
    label->datalo_reg = datalo;
    label->datahi_reg = datahi;
    label->addrlo_reg = addrlo;
    label->addrhi_reg = addrhi;
    label->raddr = raddr;
    label->label_ptr[0] = label_ptr[0];
    if (TARGET_LONG_BITS > TCG_TARGET_REG_BITS) {
        label->label_ptr[1] = label_ptr[1];
    }
}

/*
 * Generate code for the slow path for a load at the end of block
 */
static void tcg_out_qemu_ld_slow_path(TCGContext *s, TCGLabelQemuLdst *l)
{
    TCGMemOpIdx oi = l->oi;
    TCGMemOp opc = get_memop(oi);
    TCGReg data_reg;
    tcg_insn_unit **label_ptr = &l->label_ptr[0];

    /* resolve label address */
    tcg_patch32(label_ptr[0], s->code_ptr - label_ptr[0] - 4);
    if (TARGET_LONG_BITS > TCG_TARGET_REG_BITS) {
        tcg_patch32(label_ptr[1], s->code_ptr - label_ptr[1] - 4);
    }

    if (TCG_TARGET_REG_BITS == 32) {
        int ofs = 0;

        tcg_out_st(s, TCG_TYPE_PTR, TCG_AREG0, TCG_REG_ESP, ofs);
        ofs += 4;

        tcg_out_st(s, TCG_TYPE_I32, l->addrlo_reg, TCG_REG_ESP, ofs);
        ofs += 4;

        if (TARGET_LONG_BITS == 64) {
            tcg_out_st(s, TCG_TYPE_I32, l->addrhi_reg, TCG_REG_ESP, ofs);
            ofs += 4;
        }

        tcg_out_sti(s, TCG_TYPE_I32, oi, TCG_REG_ESP, ofs);
        ofs += 4;

        tcg_out_sti(s, TCG_TYPE_PTR, (uintptr_t)l->raddr, TCG_REG_ESP, ofs);
    } else {
        tcg_out_mov(s, TCG_TYPE_PTR, tcg_target_call_iarg_regs[0], TCG_AREG0);
        /* The second argument is already loaded with addrlo.  */
        tcg_out_movi(s, TCG_TYPE_I32, tcg_target_call_iarg_regs[2], oi);
        tcg_out_movi(s, TCG_TYPE_PTR, tcg_target_call_iarg_regs[3],
                     (uintptr_t)l->raddr);
    }

    tcg_out_call(s, qemu_ld_helpers[opc & (MO_BSWAP | MO_SIZE)]);

    data_reg = l->datalo_reg;
    switch (opc & MO_SSIZE) {
    case MO_SB:
        tcg_out_ext8s(s, data_reg, TCG_REG_EAX, P_REXW);
        break;
    case MO_SW:
        tcg_out_ext16s(s, data_reg, TCG_REG_EAX, P_REXW);
        break;
#if TCG_TARGET_REG_BITS == 64
    case MO_SL:
        tcg_out_ext32s(s, data_reg, TCG_REG_EAX);
        break;
#endif
    case MO_UB:
    case MO_UW:
        /* Note that the helpers have zero-extended to tcg_target_long.  */
    case MO_UL:
        tcg_out_mov(s, TCG_TYPE_I32, data_reg, TCG_REG_EAX);
        break;
    case MO_Q:
        if (TCG_TARGET_REG_BITS == 64) {
            tcg_out_mov(s, TCG_TYPE_I64, data_reg, TCG_REG_RAX);
        } else if (data_reg == TCG_REG_EDX) {
            /* xchg %edx, %eax */
            tcg_out_opc(s, OPC_XCHG_ax_r32 + TCG_REG_EDX, 0, 0, 0);
            tcg_out_mov(s, TCG_TYPE_I32, l->datahi_reg, TCG_REG_EAX);
        } else {
            tcg_out_mov(s, TCG_TYPE_I32, data_reg, TCG_REG_EAX);
            tcg_out_mov(s, TCG_TYPE_I32, l->datahi_reg, TCG_REG_EDX);
        }
        break;
    default:
        tcg_abort();
    }

    /* Jump to the code corresponding to next IR of qemu_st */
    tcg_out_jmp(s, l->raddr);
}

/*
 * Generate code for the slow path for a store at the end of block
 */
static void tcg_out_qemu_st_slow_path(TCGContext *s, TCGLabelQemuLdst *l)
{
    TCGMemOpIdx oi = l->oi;
    TCGMemOp opc = get_memop(oi);
    TCGMemOp s_bits = opc & MO_SIZE;
    tcg_insn_unit **label_ptr = &l->label_ptr[0];
    TCGReg retaddr;

    /* resolve label address */
    tcg_patch32(label_ptr[0], s->code_ptr - label_ptr[0] - 4);
    if (TARGET_LONG_BITS > TCG_TARGET_REG_BITS) {
        tcg_patch32(label_ptr[1], s->code_ptr - label_ptr[1] - 4);
    }

    if (TCG_TARGET_REG_BITS == 32) {
        int ofs = 0;

        tcg_out_st(s, TCG_TYPE_PTR, TCG_AREG0, TCG_REG_ESP, ofs);
        ofs += 4;

        tcg_out_st(s, TCG_TYPE_I32, l->addrlo_reg, TCG_REG_ESP, ofs);
        ofs += 4;

        if (TARGET_LONG_BITS == 64) {
            tcg_out_st(s, TCG_TYPE_I32, l->addrhi_reg, TCG_REG_ESP, ofs);
            ofs += 4;
        }

        tcg_out_st(s, TCG_TYPE_I32, l->datalo_reg, TCG_REG_ESP, ofs);
        ofs += 4;

        if (s_bits == MO_64) {
            tcg_out_st(s, TCG_TYPE_I32, l->datahi_reg, TCG_REG_ESP, ofs);
            ofs += 4;
        }

        tcg_out_sti(s, TCG_TYPE_I32, oi, TCG_REG_ESP, ofs);
        ofs += 4;

        retaddr = TCG_REG_EAX;
        tcg_out_movi(s, TCG_TYPE_PTR, retaddr, (uintptr_t)l->raddr);
        tcg_out_st(s, TCG_TYPE_PTR, retaddr, TCG_REG_ESP, ofs);
    } else {
        tcg_out_mov(s, TCG_TYPE_PTR, tcg_target_call_iarg_regs[0], TCG_AREG0);
        /* The second argument is already loaded with addrlo.  */
        tcg_out_mov(s, (s_bits == MO_64 ? TCG_TYPE_I64 : TCG_TYPE_I32),
                    tcg_target_call_iarg_regs[2], l->datalo_reg);
        tcg_out_movi(s, TCG_TYPE_I32, tcg_target_call_iarg_regs[3], oi);

        if (ARRAY_SIZE(tcg_target_call_iarg_regs) > 4) {
            retaddr = tcg_target_call_iarg_regs[4];
            tcg_out_movi(s, TCG_TYPE_PTR, retaddr, (uintptr_t)l->raddr);
        } else {
            retaddr = TCG_REG_RAX;
            tcg_out_movi(s, TCG_TYPE_PTR, retaddr, (uintptr_t)l->raddr);
            tcg_out_st(s, TCG_TYPE_PTR, retaddr, TCG_REG_ESP,
                       TCG_TARGET_CALL_STACK_OFFSET);
        }
    }

    /* "Tail call" to the helper, with the return address back inline.  */
    tcg_out_push(s, retaddr);
    tcg_out_jmp(s, qemu_st_helpers[opc & (MO_BSWAP | MO_SIZE)]);
}
#elif defined(__x86_64__) && defined(__linux__)
# include <asm/prctl.h>
# include <sys/prctl.h>

int arch_prctl(int code, unsigned long addr);

static int guest_base_flags;
static inline void setup_guest_base_seg(void)
{
    if (arch_prctl(ARCH_SET_GS, guest_base) == 0) {
        guest_base_flags = P_GS;
    }
}
#else
# define guest_base_flags 0
static inline void setup_guest_base_seg(void) { }
#endif /* SOFTMMU */

static void tcg_out_qemu_ld_direct(TCGContext *s, TCGReg datalo, TCGReg datahi,
                                   TCGReg base, int index, intptr_t ofs,
                                   int seg, TCGMemOp memop)
{
    const TCGMemOp real_bswap = memop & MO_BSWAP;
    TCGMemOp bswap = real_bswap;
    int movop = OPC_MOVL_GvEv;

    if (have_movbe && real_bswap) {
        bswap = 0;
        movop = OPC_MOVBE_GyMy;
    }

    switch (memop & MO_SSIZE) {
    case MO_UB:
        tcg_out_modrm_sib_offset(s, OPC_MOVZBL + seg, datalo,
                                 base, index, 0, ofs);
        break;
    case MO_SB:
        tcg_out_modrm_sib_offset(s, OPC_MOVSBL + P_REXW + seg, datalo,
                                 base, index, 0, ofs);
        break;
    case MO_UW:
        tcg_out_modrm_sib_offset(s, OPC_MOVZWL + seg, datalo,
                                 base, index, 0, ofs);
        if (real_bswap) {
            tcg_out_rolw_8(s, datalo);
        }
        break;
    case MO_SW:
        if (real_bswap) {
            if (have_movbe) {
                tcg_out_modrm_sib_offset(s, OPC_MOVBE_GyMy + P_DATA16 + seg,
                                         datalo, base, index, 0, ofs);
            } else {
                tcg_out_modrm_sib_offset(s, OPC_MOVZWL + seg, datalo,
                                         base, index, 0, ofs);
                tcg_out_rolw_8(s, datalo);
            }
            tcg_out_modrm(s, OPC_MOVSWL + P_REXW, datalo, datalo);
        } else {
            tcg_out_modrm_sib_offset(s, OPC_MOVSWL + P_REXW + seg,
                                     datalo, base, index, 0, ofs);
        }
        break;
    case MO_UL:
        tcg_out_modrm_sib_offset(s, movop + seg, datalo, base, index, 0, ofs);
        if (bswap) {
            tcg_out_bswap32(s, datalo);
        }
        break;
#if TCG_TARGET_REG_BITS == 64
    case MO_SL:
        if (real_bswap) {
            tcg_out_modrm_sib_offset(s, movop + seg, datalo,
                                     base, index, 0, ofs);
            if (bswap) {
                tcg_out_bswap32(s, datalo);
            }
            tcg_out_ext32s(s, datalo, datalo);
        } else {
            tcg_out_modrm_sib_offset(s, OPC_MOVSLQ + seg, datalo,
                                     base, index, 0, ofs);
        }
        break;
#endif
    case MO_Q:
        if (TCG_TARGET_REG_BITS == 64) {
            tcg_out_modrm_sib_offset(s, movop + P_REXW + seg, datalo,
                                     base, index, 0, ofs);
            if (bswap) {
                tcg_out_bswap64(s, datalo);
            }
        } else {
            if (real_bswap) {
                int t = datalo;
                datalo = datahi;
                datahi = t;
            }
            if (base != datalo) {
                tcg_out_modrm_sib_offset(s, movop + seg, datalo,
                                         base, index, 0, ofs);
                tcg_out_modrm_sib_offset(s, movop + seg, datahi,
                                         base, index, 0, ofs + 4);
            } else {
                tcg_out_modrm_sib_offset(s, movop + seg, datahi,
                                         base, index, 0, ofs + 4);
                tcg_out_modrm_sib_offset(s, movop + seg, datalo,
                                         base, index, 0, ofs);
            }
            if (bswap) {
                tcg_out_bswap32(s, datalo);
                tcg_out_bswap32(s, datahi);
            }
        }
        break;
    default:
        tcg_abort();
    }
}

/* XXX: qemu_ld and qemu_st could be modified to clobber only EDX and
   EAX. It will be useful once fixed registers globals are less
   common. */
static void tcg_out_qemu_ld(TCGContext *s, const TCGArg *args, bool is64)
{
    TCGReg datalo, datahi, addrlo;
    TCGReg addrhi __attribute__((unused));
    TCGMemOpIdx oi;
    TCGMemOp opc;
#if defined(CONFIG_SOFTMMU)
    int mem_index;
    tcg_insn_unit *label_ptr[2];
#endif

    datalo = *args++;
    datahi = (TCG_TARGET_REG_BITS == 32 && is64 ? *args++ : 0);
    addrlo = *args++;
    addrhi = (TARGET_LONG_BITS > TCG_TARGET_REG_BITS ? *args++ : 0);
    oi = *args++;
    opc = get_memop(oi);

#if defined(CONFIG_SOFTMMU)
    mem_index = get_mmuidx(oi);

    tcg_out_tlb_load(s, addrlo, addrhi, mem_index, opc,
                     label_ptr, offsetof(CPUTLBEntry, addr_read));

    /* TLB Hit.  */
    tcg_out_qemu_ld_direct(s, datalo, datahi, TCG_REG_L1, -1, 0, 0, opc);

    /* Record the current context of a load into ldst label */
    add_qemu_ldst_label(s, true, oi, datalo, datahi, addrlo, addrhi,
                        s->code_ptr, label_ptr);
#else
    {
        int32_t offset = guest_base;
        TCGReg base = addrlo;
        int index = -1;
        int seg = 0;

        /* For a 32-bit guest, the high 32 bits may contain garbage.
           We can do this with the ADDR32 prefix if we're not using
           a guest base, or when using segmentation.  Otherwise we
           need to zero-extend manually.  */
        if (guest_base == 0 || guest_base_flags) {
            seg = guest_base_flags;
            offset = 0;
            if (TCG_TARGET_REG_BITS > TARGET_LONG_BITS) {
                seg |= P_ADDR32;
            }
        } else if (TCG_TARGET_REG_BITS == 64) {
            if (TARGET_LONG_BITS == 32) {
                tcg_out_ext32u(s, TCG_REG_L0, base);
                base = TCG_REG_L0;
            }
            if (offset != guest_base) {
                tcg_out_movi(s, TCG_TYPE_I64, TCG_REG_L1, guest_base);
                index = TCG_REG_L1;
                offset = 0;
            }
        }

        tcg_out_qemu_ld_direct(s, datalo, datahi,
                               base, index, offset, seg, opc);
    }
#endif
}

static void tcg_out_qemu_st_direct(TCGContext *s, TCGReg datalo, TCGReg datahi,
                                   TCGReg base, intptr_t ofs, int seg,
                                   TCGMemOp memop)
{
    /* ??? Ideally we wouldn't need a scratch register.  For user-only,
       we could perform the bswap twice to restore the original value
       instead of moving to the scratch.  But as it is, the L constraint
       means that TCG_REG_L0 is definitely free here.  */
    const TCGReg scratch = TCG_REG_L0;
    const TCGMemOp real_bswap = memop & MO_BSWAP;
    TCGMemOp bswap = real_bswap;
    int movop = OPC_MOVL_EvGv;

    if (have_movbe && real_bswap) {
        bswap = 0;
        movop = OPC_MOVBE_MyGy;
    }

    switch (memop & MO_SIZE) {
    case MO_8:
        /* In 32-bit mode, 8-bit stores can only happen from [abcd]x.
           Use the scratch register if necessary.  */
        if (TCG_TARGET_REG_BITS == 32 && datalo >= 4) {
            tcg_out_mov(s, TCG_TYPE_I32, scratch, datalo);
            datalo = scratch;
        }
        tcg_out_modrm_offset(s, OPC_MOVB_EvGv + P_REXB_R + seg,
                             datalo, base, ofs);
        break;
    case MO_16:
        if (bswap) {
            tcg_out_mov(s, TCG_TYPE_I32, scratch, datalo);
            tcg_out_rolw_8(s, scratch);
            datalo = scratch;
        }
        tcg_out_modrm_offset(s, movop + P_DATA16 + seg, datalo, base, ofs);
        break;
    case MO_32:
        if (bswap) {
            tcg_out_mov(s, TCG_TYPE_I32, scratch, datalo);
            tcg_out_bswap32(s, scratch);
            datalo = scratch;
        }
        tcg_out_modrm_offset(s, movop + seg, datalo, base, ofs);
        break;
    case MO_64:
        if (TCG_TARGET_REG_BITS == 64) {
            if (bswap) {
                tcg_out_mov(s, TCG_TYPE_I64, scratch, datalo);
                tcg_out_bswap64(s, scratch);
                datalo = scratch;
            }
            tcg_out_modrm_offset(s, movop + P_REXW + seg, datalo, base, ofs);
        } else if (bswap) {
            tcg_out_mov(s, TCG_TYPE_I32, scratch, datahi);
            tcg_out_bswap32(s, scratch);
            tcg_out_modrm_offset(s, OPC_MOVL_EvGv + seg, scratch, base, ofs);
            tcg_out_mov(s, TCG_TYPE_I32, scratch, datalo);
            tcg_out_bswap32(s, scratch);
            tcg_out_modrm_offset(s, OPC_MOVL_EvGv + seg, scratch, base, ofs+4);
        } else {
            if (real_bswap) {
                int t = datalo;
                datalo = datahi;
                datahi = t;
            }
            tcg_out_modrm_offset(s, movop + seg, datalo, base, ofs);
            tcg_out_modrm_offset(s, movop + seg, datahi, base, ofs+4);
        }
        break;
    default:
        tcg_abort();
    }
}

static void tcg_out_qemu_st(TCGContext *s, const TCGArg *args, bool is64)
{
    TCGReg datalo, datahi, addrlo;
    TCGReg addrhi __attribute__((unused));
    TCGMemOpIdx oi;
    TCGMemOp opc;
#if defined(CONFIG_SOFTMMU)
    int mem_index;
    tcg_insn_unit *label_ptr[2];
#endif

    datalo = *args++;
    datahi = (TCG_TARGET_REG_BITS == 32 && is64 ? *args++ : 0);
    addrlo = *args++;
    addrhi = (TARGET_LONG_BITS > TCG_TARGET_REG_BITS ? *args++ : 0);
    oi = *args++;
    opc = get_memop(oi);

#if defined(CONFIG_SOFTMMU)
    mem_index = get_mmuidx(oi);

    tcg_out_tlb_load(s, addrlo, addrhi, mem_index, opc,
                     label_ptr, offsetof(CPUTLBEntry, addr_write));

    /* TLB Hit.  */
    tcg_out_qemu_st_direct(s, datalo, datahi, TCG_REG_L1, 0, 0, opc);

    /* Record the current context of a store into ldst label */
    add_qemu_ldst_label(s, false, oi, datalo, datahi, addrlo, addrhi,
                        s->code_ptr, label_ptr);
#else
    {
        int32_t offset = guest_base;
        TCGReg base = addrlo;
        int seg = 0;

        /* See comment in tcg_out_qemu_ld re zero-extension of addrlo.  */
        if (guest_base == 0 || guest_base_flags) {
            seg = guest_base_flags;
            offset = 0;
            if (TCG_TARGET_REG_BITS > TARGET_LONG_BITS) {
                seg |= P_ADDR32;
            }
        } else if (TCG_TARGET_REG_BITS == 64) {
            /* ??? Note that we can't use the same SIB addressing scheme
               as for loads, since we require L0 free for bswap.  */
            if (offset != guest_base) {
                if (TARGET_LONG_BITS == 32) {
                    tcg_out_ext32u(s, TCG_REG_L0, base);
                    base = TCG_REG_L0;
                }
                tcg_out_movi(s, TCG_TYPE_I64, TCG_REG_L1, guest_base);
                tgen_arithr(s, ARITH_ADD + P_REXW, TCG_REG_L1, base);
                base = TCG_REG_L1;
                offset = 0;
            } else if (TARGET_LONG_BITS == 32) {
                tcg_out_ext32u(s, TCG_REG_L1, base);
                base = TCG_REG_L1;
            }
        }

        tcg_out_qemu_st_direct(s, datalo, datahi, base, offset, seg, opc);
    }
#endif
}

static inline void tcg_out_op(TCGContext *s, TCGOpcode opc,
                              const TCGArg *args, const int *const_args)
{
    TCGArg a0, a1, a2;
    int c, const_a2, vexop, rexw = 0;

#if TCG_TARGET_REG_BITS == 64
# define OP_32_64(x) \
        case glue(glue(INDEX_op_, x), _i64): \
            rexw = P_REXW; /* FALLTHRU */    \
        case glue(glue(INDEX_op_, x), _i32)
#else
# define OP_32_64(x) \
        case glue(glue(INDEX_op_, x), _i32)
#endif

    /* Hoist the loads of the most common arguments.  */
    a0 = args[0];
    a1 = args[1];
    a2 = args[2];
    const_a2 = const_args[2];

    switch (opc) {
    case INDEX_op_exit_tb:
        tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_EAX, a0);
        tcg_out_jmp(s, tb_ret_addr);
        break;
    case INDEX_op_goto_tb:
        if (s->tb_jmp_insn_offset) {
            /* direct jump method */
            int gap;
            /* jump displacement must be aligned for atomic patching;
             * see if we need to add extra nops before jump
             */
            gap = tcg_pcrel_diff(s, QEMU_ALIGN_PTR_UP(s->code_ptr + 1, 4));
            if (gap != 1) {
                tcg_out_nopn(s, gap - 1);
            }
            tcg_out8(s, OPC_JMP_long); /* jmp im */
            s->tb_jmp_insn_offset[a0] = tcg_current_code_size(s);
            tcg_out32(s, 0);
        } else {
            /* indirect jump method */
            tcg_out_modrm_offset(s, OPC_GRP5, EXT5_JMPN_Ev, -1,
                                 (intptr_t)(s->tb_jmp_target_addr + a0));
        }
        s->tb_jmp_reset_offset[a0] = tcg_current_code_size(s);
        break;
    case INDEX_op_br:
        tcg_out_jxx(s, JCC_JMP, arg_label(a0), 0);
        break;
    OP_32_64(ld8u):
        /* Note that we can ignore REXW for the zero-extend to 64-bit.  */
        tcg_out_modrm_offset(s, OPC_MOVZBL, a0, a1, a2);
        break;
    OP_32_64(ld8s):
        tcg_out_modrm_offset(s, OPC_MOVSBL + rexw, a0, a1, a2);
        break;
    OP_32_64(ld16u):
        /* Note that we can ignore REXW for the zero-extend to 64-bit.  */
        tcg_out_modrm_offset(s, OPC_MOVZWL, a0, a1, a2);
        break;
    OP_32_64(ld16s):
        tcg_out_modrm_offset(s, OPC_MOVSWL + rexw, a0, a1, a2);
        break;
#if TCG_TARGET_REG_BITS == 64
    case INDEX_op_ld32u_i64:
#endif
    case INDEX_op_ld_i32:
        tcg_out_ld(s, TCG_TYPE_I32, a0, a1, a2);
        break;

    OP_32_64(st8):
        if (const_args[0]) {
            tcg_out_modrm_offset(s, OPC_MOVB_EvIz, 0, a1, a2);
            tcg_out8(s, a0);
        } else {
            tcg_out_modrm_offset(s, OPC_MOVB_EvGv | P_REXB_R, a0, a1, a2);
        }
        break;
    OP_32_64(st16):
        if (const_args[0]) {
            tcg_out_modrm_offset(s, OPC_MOVL_EvIz | P_DATA16, 0, a1, a2);
            tcg_out16(s, a0);
        } else {
            tcg_out_modrm_offset(s, OPC_MOVL_EvGv | P_DATA16, a0, a1, a2);
        }
        break;
#if TCG_TARGET_REG_BITS == 64
    case INDEX_op_st32_i64:
#endif
    case INDEX_op_st_i32:
        if (const_args[0]) {
            tcg_out_modrm_offset(s, OPC_MOVL_EvIz, 0, a1, a2);
            tcg_out32(s, a0);
        } else {
            tcg_out_st(s, TCG_TYPE_I32, a0, a1, a2);
        }
        break;

    OP_32_64(add):
        /* For 3-operand addition, use LEA.  */
        if (a0 != a1) {
            TCGArg c3 = 0;
            if (const_a2) {
                c3 = a2, a2 = -1;
            } else if (a0 == a2) {
                /* Watch out for dest = src + dest, since we've removed
                   the matching constraint on the add.  */
                tgen_arithr(s, ARITH_ADD + rexw, a0, a1);
                break;
            }

            tcg_out_modrm_sib_offset(s, OPC_LEA + rexw, a0, a1, a2, 0, c3);
            break;
        }
        c = ARITH_ADD;
        goto gen_arith;
    OP_32_64(sub):
        c = ARITH_SUB;
        goto gen_arith;
    OP_32_64(and):
        c = ARITH_AND;
        goto gen_arith;
    OP_32_64(or):
        c = ARITH_OR;
        goto gen_arith;
    OP_32_64(xor):
        c = ARITH_XOR;
        goto gen_arith;
    gen_arith:
        if (const_a2) {
            tgen_arithi(s, c + rexw, a0, a2, 0);
        } else {
            tgen_arithr(s, c + rexw, a0, a2);
        }
        break;

    OP_32_64(andc):
        if (const_a2) {
            tcg_out_mov(s, rexw ? TCG_TYPE_I64 : TCG_TYPE_I32, a0, a1);
            tgen_arithi(s, ARITH_AND + rexw, a0, ~a2, 0);
        } else {
            tcg_out_vex_modrm(s, OPC_ANDN + rexw, a0, a2, a1);
        }
        break;

    OP_32_64(mul):
        if (const_a2) {
            int32_t val;
            val = a2;
            if (val == (int8_t)val) {
                tcg_out_modrm(s, OPC_IMUL_GvEvIb + rexw, a0, a0);
                tcg_out8(s, val);
            } else {
                tcg_out_modrm(s, OPC_IMUL_GvEvIz + rexw, a0, a0);
                tcg_out32(s, val);
            }
        } else {
            tcg_out_modrm(s, OPC_IMUL_GvEv + rexw, a0, a2);
        }
        break;

    OP_32_64(div2):
        tcg_out_modrm(s, OPC_GRP3_Ev + rexw, EXT3_IDIV, args[4]);
        break;
    OP_32_64(divu2):
        tcg_out_modrm(s, OPC_GRP3_Ev + rexw, EXT3_DIV, args[4]);
        break;

    OP_32_64(shl):
        /* For small constant 3-operand shift, use LEA.  */
        if (const_a2 && a0 != a1 && (a2 - 1) < 3) {
            if (a2 - 1 == 0) {
                /* shl $1,a1,a0 -> lea (a1,a1),a0 */
                tcg_out_modrm_sib_offset(s, OPC_LEA + rexw, a0, a1, a1, 0, 0);
            } else {
                /* shl $n,a1,a0 -> lea 0(,a1,n),a0 */
                tcg_out_modrm_sib_offset(s, OPC_LEA + rexw, a0, -1, a1, a2, 0);
            }
            break;
        }
        c = SHIFT_SHL;
        vexop = OPC_SHLX;
        goto gen_shift_maybe_vex;
    OP_32_64(shr):
        c = SHIFT_SHR;
        vexop = OPC_SHRX;
        goto gen_shift_maybe_vex;
    OP_32_64(sar):
        c = SHIFT_SAR;
        vexop = OPC_SARX;
        goto gen_shift_maybe_vex;
    OP_32_64(rotl):
        c = SHIFT_ROL;
        goto gen_shift;
    OP_32_64(rotr):
        c = SHIFT_ROR;
        goto gen_shift;
    gen_shift_maybe_vex:
        if (have_bmi2) {
            if (!const_a2) {
                tcg_out_vex_modrm(s, vexop + rexw, a0, a2, a1);
                break;
            }
            tcg_out_mov(s, rexw ? TCG_TYPE_I64 : TCG_TYPE_I32, a0, a1);
        }
        /* FALLTHRU */
    gen_shift:
        if (const_a2) {
            tcg_out_shifti(s, c + rexw, a0, a2);
        } else {
            tcg_out_modrm(s, OPC_SHIFT_cl + rexw, c, a0);
        }
        break;

    OP_32_64(ctz):
        tcg_out_ctz(s, rexw, args[0], args[1], args[2], const_args[2]);
        break;
    OP_32_64(clz):
        tcg_out_clz(s, rexw, args[0], args[1], args[2], const_args[2]);
        break;
    OP_32_64(ctpop):
        tcg_out_modrm(s, OPC_POPCNT + rexw, a0, a1);
        break;

    case INDEX_op_brcond_i32:
        tcg_out_brcond32(s, a2, a0, a1, const_args[1], arg_label(args[3]), 0);
        break;
    case INDEX_op_setcond_i32:
        tcg_out_setcond32(s, args[3], a0, a1, a2, const_a2);
        break;
    case INDEX_op_movcond_i32:
        tcg_out_movcond32(s, args[5], a0, a1, a2, const_a2, args[3]);
        break;

    OP_32_64(bswap16):
        tcg_out_rolw_8(s, a0);
        break;
    OP_32_64(bswap32):
        tcg_out_bswap32(s, a0);
        break;

    OP_32_64(neg):
        tcg_out_modrm(s, OPC_GRP3_Ev + rexw, EXT3_NEG, a0);
        break;
    OP_32_64(not):
        tcg_out_modrm(s, OPC_GRP3_Ev + rexw, EXT3_NOT, a0);
        break;

    OP_32_64(ext8s):
        tcg_out_ext8s(s, a0, a1, rexw);
        break;
    OP_32_64(ext16s):
        tcg_out_ext16s(s, a0, a1, rexw);
        break;
    OP_32_64(ext8u):
        tcg_out_ext8u(s, a0, a1);
        break;
    OP_32_64(ext16u):
        tcg_out_ext16u(s, a0, a1);
        break;

    case INDEX_op_qemu_ld_i32:
        tcg_out_qemu_ld(s, args, 0);
        break;
    case INDEX_op_qemu_ld_i64:
        tcg_out_qemu_ld(s, args, 1);
        break;
    case INDEX_op_qemu_st_i32:
        tcg_out_qemu_st(s, args, 0);
        break;
    case INDEX_op_qemu_st_i64:
        tcg_out_qemu_st(s, args, 1);
        break;

    OP_32_64(mulu2):
        tcg_out_modrm(s, OPC_GRP3_Ev + rexw, EXT3_MUL, args[3]);
        break;
    OP_32_64(muls2):
        tcg_out_modrm(s, OPC_GRP3_Ev + rexw, EXT3_IMUL, args[3]);
        break;
    OP_32_64(add2):
        if (const_args[4]) {
            tgen_arithi(s, ARITH_ADD + rexw, a0, args[4], 1);
        } else {
            tgen_arithr(s, ARITH_ADD + rexw, a0, args[4]);
        }
        if (const_args[5]) {
            tgen_arithi(s, ARITH_ADC + rexw, a1, args[5], 1);
        } else {
            tgen_arithr(s, ARITH_ADC + rexw, a1, args[5]);
        }
        break;
    OP_32_64(sub2):
        if (const_args[4]) {
            tgen_arithi(s, ARITH_SUB + rexw, a0, args[4], 1);
        } else {
            tgen_arithr(s, ARITH_SUB + rexw, a0, args[4]);
        }
        if (const_args[5]) {
            tgen_arithi(s, ARITH_SBB + rexw, a1, args[5], 1);
        } else {
            tgen_arithr(s, ARITH_SBB + rexw, a1, args[5]);
        }
        break;

#if TCG_TARGET_REG_BITS == 32
    case INDEX_op_brcond2_i32:
        tcg_out_brcond2(s, args, const_args, 0);
        break;
    case INDEX_op_setcond2_i32:
        tcg_out_setcond2(s, args, const_args);
        break;
#else /* TCG_TARGET_REG_BITS == 64 */
    case INDEX_op_ld32s_i64:
        tcg_out_modrm_offset(s, OPC_MOVSLQ, a0, a1, a2);
        break;
    case INDEX_op_ld_i64:
        tcg_out_ld(s, TCG_TYPE_I64, a0, a1, a2);
        break;
    case INDEX_op_st_i64:
        if (const_args[0]) {
            tcg_out_modrm_offset(s, OPC_MOVL_EvIz | P_REXW, 0, a1, a2);
            tcg_out32(s, a0);
        } else {
            tcg_out_st(s, TCG_TYPE_I64, a0, a1, a2);
        }
        break;

    case INDEX_op_brcond_i64:
        tcg_out_brcond64(s, a2, a0, a1, const_args[1], arg_label(args[3]), 0);
        break;
    case INDEX_op_setcond_i64:
        tcg_out_setcond64(s, args[3], a0, a1, a2, const_a2);
        break;
    case INDEX_op_movcond_i64:
        tcg_out_movcond64(s, args[5], a0, a1, a2, const_a2, args[3]);
        break;

    case INDEX_op_bswap64_i64:
        tcg_out_bswap64(s, a0);
        break;
    case INDEX_op_extu_i32_i64:
    case INDEX_op_ext32u_i64:
        tcg_out_ext32u(s, a0, a1);
        break;
    case INDEX_op_ext_i32_i64:
    case INDEX_op_ext32s_i64:
        tcg_out_ext32s(s, a0, a1);
        break;
#endif

    OP_32_64(deposit):
        if (args[3] == 0 && args[4] == 8) {
            /* load bits 0..7 */
            tcg_out_modrm(s, OPC_MOVB_EvGv | P_REXB_R | P_REXB_RM, a2, a0);
        } else if (args[3] == 8 && args[4] == 8) {
            /* load bits 8..15 */
            tcg_out_modrm(s, OPC_MOVB_EvGv, a2, a0 + 4);
        } else if (args[3] == 0 && args[4] == 16) {
            /* load bits 0..15 */
            tcg_out_modrm(s, OPC_MOVL_EvGv | P_DATA16, a2, a0);
        } else {
            tcg_abort();
        }
        break;

    case INDEX_op_extract_i64:
        if (a2 + args[3] == 32) {
            /* This is a 32-bit zero-extending right shift.  */
            tcg_out_mov(s, TCG_TYPE_I32, a0, a1);
            tcg_out_shifti(s, SHIFT_SHR, a0, a2);
            break;
        }
        /* FALLTHRU */
    case INDEX_op_extract_i32:
        /* On the off-chance that we can use the high-byte registers.
           Otherwise we emit the same ext16 + shift pattern that we
           would have gotten from the normal tcg-op.c expansion.  */
        tcg_debug_assert(a2 == 8 && args[3] == 8);
        if (a1 < 4 && a0 < 8) {
            tcg_out_modrm(s, OPC_MOVZBL, a0, a1 + 4);
        } else {
            tcg_out_ext16u(s, a0, a1);
            tcg_out_shifti(s, SHIFT_SHR, a0, 8);
        }
        break;

    case INDEX_op_sextract_i32:
        /* We don't implement sextract_i64, as we cannot sign-extend to
           64-bits without using the REX prefix that explicitly excludes
           access to the high-byte registers.  */
        tcg_debug_assert(a2 == 8 && args[3] == 8);
        if (a1 < 4 && a0 < 8) {
            tcg_out_modrm(s, OPC_MOVSBL, a0, a1 + 4);
        } else {
            tcg_out_ext16s(s, a0, a1, 0);
            tcg_out_shifti(s, SHIFT_SAR, a0, 8);
        }
        break;

    case INDEX_op_mb:
        tcg_out_mb(s, a0);
        break;
    case INDEX_op_mov_i32:  /* Always emitted via tcg_out_mov.  */
    case INDEX_op_mov_i64:
    case INDEX_op_movi_i32: /* Always emitted via tcg_out_movi.  */
    case INDEX_op_movi_i64:
    case INDEX_op_call:     /* Always emitted via tcg_out_call.  */
    default:
        tcg_abort();
    }

#undef OP_32_64
}

static const TCGTargetOpDef *tcg_target_op_def(TCGOpcode op)
{
    static const TCGTargetOpDef ri_r = { .args_ct_str = { "ri", "r" } };
    static const TCGTargetOpDef re_r = { .args_ct_str = { "re", "r" } };
    static const TCGTargetOpDef qi_r = { .args_ct_str = { "qi", "r" } };
    static const TCGTargetOpDef r_r = { .args_ct_str = { "r", "r" } };
    static const TCGTargetOpDef r_q = { .args_ct_str = { "r", "q" } };
    static const TCGTargetOpDef r_re = { .args_ct_str = { "r", "re" } };
    static const TCGTargetOpDef r_0 = { .args_ct_str = { "r", "0" } };
    static const TCGTargetOpDef r_r_ri = { .args_ct_str = { "r", "r", "ri" } };
    static const TCGTargetOpDef r_r_re = { .args_ct_str = { "r", "r", "re" } };
    static const TCGTargetOpDef r_0_re = { .args_ct_str = { "r", "0", "re" } };
    static const TCGTargetOpDef r_0_ci = { .args_ct_str = { "r", "0", "ci" } };
    static const TCGTargetOpDef r_L = { .args_ct_str = { "r", "L" } };
    static const TCGTargetOpDef L_L = { .args_ct_str = { "L", "L" } };
    static const TCGTargetOpDef r_L_L = { .args_ct_str = { "r", "L", "L" } };
    static const TCGTargetOpDef r_r_L = { .args_ct_str = { "r", "r", "L" } };
    static const TCGTargetOpDef L_L_L = { .args_ct_str = { "L", "L", "L" } };
    static const TCGTargetOpDef r_r_L_L
        = { .args_ct_str = { "r", "r", "L", "L" } };
    static const TCGTargetOpDef L_L_L_L
        = { .args_ct_str = { "L", "L", "L", "L" } };

    switch (op) {
    case INDEX_op_ld8u_i32:
    case INDEX_op_ld8u_i64:
    case INDEX_op_ld8s_i32:
    case INDEX_op_ld8s_i64:
    case INDEX_op_ld16u_i32:
    case INDEX_op_ld16u_i64:
    case INDEX_op_ld16s_i32:
    case INDEX_op_ld16s_i64:
    case INDEX_op_ld_i32:
    case INDEX_op_ld32u_i64:
    case INDEX_op_ld32s_i64:
    case INDEX_op_ld_i64:
        return &r_r;

    case INDEX_op_st8_i32:
    case INDEX_op_st8_i64:
        return &qi_r;
    case INDEX_op_st16_i32:
    case INDEX_op_st16_i64:
    case INDEX_op_st_i32:
    case INDEX_op_st32_i64:
        return &ri_r;
    case INDEX_op_st_i64:
        return &re_r;

    case INDEX_op_add_i32:
    case INDEX_op_add_i64:
        return &r_r_re;
    case INDEX_op_sub_i32:
    case INDEX_op_sub_i64:
    case INDEX_op_mul_i32:
    case INDEX_op_mul_i64:
    case INDEX_op_or_i32:
    case INDEX_op_or_i64:
    case INDEX_op_xor_i32:
    case INDEX_op_xor_i64:
        return &r_0_re;

    case INDEX_op_and_i32:
    case INDEX_op_and_i64:
        {
            static const TCGTargetOpDef and
                = { .args_ct_str = { "r", "0", "reZ" } };
            return &and;
        }
        break;
    case INDEX_op_andc_i32:
    case INDEX_op_andc_i64:
        {
            static const TCGTargetOpDef andc
                = { .args_ct_str = { "r", "r", "rI" } };
            return &andc;
        }
        break;

    case INDEX_op_shl_i32:
    case INDEX_op_shl_i64:
    case INDEX_op_shr_i32:
    case INDEX_op_shr_i64:
    case INDEX_op_sar_i32:
    case INDEX_op_sar_i64:
        return have_bmi2 ? &r_r_ri : &r_0_ci;
    case INDEX_op_rotl_i32:
    case INDEX_op_rotl_i64:
    case INDEX_op_rotr_i32:
    case INDEX_op_rotr_i64:
        return &r_0_ci;

    case INDEX_op_brcond_i32:
    case INDEX_op_brcond_i64:
        return &r_re;

    case INDEX_op_bswap16_i32:
    case INDEX_op_bswap16_i64:
    case INDEX_op_bswap32_i32:
    case INDEX_op_bswap32_i64:
    case INDEX_op_bswap64_i64:
    case INDEX_op_neg_i32:
    case INDEX_op_neg_i64:
    case INDEX_op_not_i32:
    case INDEX_op_not_i64:
        return &r_0;

    case INDEX_op_ext8s_i32:
    case INDEX_op_ext8s_i64:
    case INDEX_op_ext8u_i32:
    case INDEX_op_ext8u_i64:
        return &r_q;
    case INDEX_op_ext16s_i32:
    case INDEX_op_ext16s_i64:
    case INDEX_op_ext16u_i32:
    case INDEX_op_ext16u_i64:
    case INDEX_op_ext32s_i64:
    case INDEX_op_ext32u_i64:
    case INDEX_op_ext_i32_i64:
    case INDEX_op_extu_i32_i64:
    case INDEX_op_extract_i32:
    case INDEX_op_extract_i64:
    case INDEX_op_sextract_i32:
    case INDEX_op_ctpop_i32:
    case INDEX_op_ctpop_i64:
        return &r_r;

    case INDEX_op_deposit_i32:
    case INDEX_op_deposit_i64:
        {
            static const TCGTargetOpDef dep
                = { .args_ct_str = { "Q", "0", "Q" } };
            return &dep;
        }
    case INDEX_op_setcond_i32:
    case INDEX_op_setcond_i64:
        {
            static const TCGTargetOpDef setc
                = { .args_ct_str = { "q", "r", "re" } };
            return &setc;
        }
    case INDEX_op_movcond_i32:
    case INDEX_op_movcond_i64:
        {
            static const TCGTargetOpDef movc
                = { .args_ct_str = { "r", "r", "re", "r", "0" } };
            return &movc;
        }
    case INDEX_op_div2_i32:
    case INDEX_op_div2_i64:
    case INDEX_op_divu2_i32:
    case INDEX_op_divu2_i64:
        {
            static const TCGTargetOpDef div2
                = { .args_ct_str = { "a", "d", "0", "1", "r" } };
            return &div2;
        }
    case INDEX_op_mulu2_i32:
    case INDEX_op_mulu2_i64:
    case INDEX_op_muls2_i32:
    case INDEX_op_muls2_i64:
        {
            static const TCGTargetOpDef mul2
                = { .args_ct_str = { "a", "d", "a", "r" } };
            return &mul2;
        }
    case INDEX_op_add2_i32:
    case INDEX_op_add2_i64:
    case INDEX_op_sub2_i32:
    case INDEX_op_sub2_i64:
        {
            static const TCGTargetOpDef arith2
                = { .args_ct_str = { "r", "r", "0", "1", "re", "re" } };
            return &arith2;
        }
    case INDEX_op_ctz_i32:
    case INDEX_op_ctz_i64:
        {
            static const TCGTargetOpDef ctz[2] = {
                { .args_ct_str = { "&r", "r", "r" } },
                { .args_ct_str = { "&r", "r", "rW" } },
            };
            return &ctz[have_bmi1];
        }
    case INDEX_op_clz_i32:
    case INDEX_op_clz_i64:
        {
            static const TCGTargetOpDef clz[2] = {
                { .args_ct_str = { "&r", "r", "r" } },
                { .args_ct_str = { "&r", "r", "rW" } },
            };
            return &clz[have_lzcnt];
        }

    case INDEX_op_qemu_ld_i32:
        return TARGET_LONG_BITS <= TCG_TARGET_REG_BITS ? &r_L : &r_L_L;
    case INDEX_op_qemu_st_i32:
        return TARGET_LONG_BITS <= TCG_TARGET_REG_BITS ? &L_L : &L_L_L;
    case INDEX_op_qemu_ld_i64:
        return (TCG_TARGET_REG_BITS == 64 ? &r_L
                : TARGET_LONG_BITS <= TCG_TARGET_REG_BITS ? &r_r_L
                : &r_r_L_L);
    case INDEX_op_qemu_st_i64:
        return (TCG_TARGET_REG_BITS == 64 ? &L_L
                : TARGET_LONG_BITS <= TCG_TARGET_REG_BITS ? &L_L_L
                : &L_L_L_L);

    case INDEX_op_brcond2_i32:
        {
            static const TCGTargetOpDef b2
                = { .args_ct_str = { "r", "r", "ri", "ri" } };
            return &b2;
        }
    case INDEX_op_setcond2_i32:
        {
            static const TCGTargetOpDef s2
                = { .args_ct_str = { "r", "r", "r", "ri", "ri" } };
            return &s2;
        }

    default:
        break;
    }
    return NULL;
}

static int tcg_target_callee_save_regs[] = {
#if TCG_TARGET_REG_BITS == 64
    TCG_REG_RBP,
    TCG_REG_RBX,
#if defined(_WIN64)
    TCG_REG_RDI,
    TCG_REG_RSI,
#endif
    TCG_REG_R12,
    TCG_REG_R13,
    TCG_REG_R14, /* Currently used for the global env. */
    TCG_REG_R15,
#else
    TCG_REG_EBP, /* Currently used for the global env. */
    TCG_REG_EBX,
    TCG_REG_ESI,
    TCG_REG_EDI,
#endif
};

/* Compute frame size via macros, to share between tcg_target_qemu_prologue
   and tcg_register_jit.  */

#define PUSH_SIZE \
    ((1 + ARRAY_SIZE(tcg_target_callee_save_regs)) \
     * (TCG_TARGET_REG_BITS / 8))

#define FRAME_SIZE \
    ((PUSH_SIZE \
      + TCG_STATIC_CALL_ARGS_SIZE \
      + CPU_TEMP_BUF_NLONGS * sizeof(long) \
      + TCG_TARGET_STACK_ALIGN - 1) \
     & ~(TCG_TARGET_STACK_ALIGN - 1))

/* Generate global QEMU prologue and epilogue code */
static void tcg_target_qemu_prologue(TCGContext *s)
{
    int i, stack_addend;

    /* TB prologue */

    /* Reserve some stack space, also for TCG temps.  */
    stack_addend = FRAME_SIZE - PUSH_SIZE;
    tcg_set_frame(s, TCG_REG_CALL_STACK, TCG_STATIC_CALL_ARGS_SIZE,
                  CPU_TEMP_BUF_NLONGS * sizeof(long));

    /* Save all callee saved registers.  */
    for (i = 0; i < ARRAY_SIZE(tcg_target_callee_save_regs); i++) {
        tcg_out_push(s, tcg_target_callee_save_regs[i]);
    }

#if TCG_TARGET_REG_BITS == 32
    tcg_out_ld(s, TCG_TYPE_PTR, TCG_AREG0, TCG_REG_ESP,
               (ARRAY_SIZE(tcg_target_callee_save_regs) + 1) * 4);
    tcg_out_addi(s, TCG_REG_ESP, -stack_addend);
    /* jmp *tb.  */
    tcg_out_modrm_offset(s, OPC_GRP5, EXT5_JMPN_Ev, TCG_REG_ESP,
		         (ARRAY_SIZE(tcg_target_callee_save_regs) + 2) * 4
			 + stack_addend);
#else
    tcg_out_mov(s, TCG_TYPE_PTR, TCG_AREG0, tcg_target_call_iarg_regs[0]);
    tcg_out_addi(s, TCG_REG_ESP, -stack_addend);
    /* jmp *tb.  */
    tcg_out_modrm(s, OPC_GRP5, EXT5_JMPN_Ev, tcg_target_call_iarg_regs[1]);
#endif

    /* TB epilogue */
    tb_ret_addr = s->code_ptr;

    tcg_out_addi(s, TCG_REG_CALL_STACK, stack_addend);

    for (i = ARRAY_SIZE(tcg_target_callee_save_regs) - 1; i >= 0; i--) {
        tcg_out_pop(s, tcg_target_callee_save_regs[i]);
    }
    tcg_out_opc(s, OPC_RET, 0, 0, 0);

#if !defined(CONFIG_SOFTMMU)
    /* Try to set up a segment register to point to guest_base.  */
    if (guest_base) {
        setup_guest_base_seg();
    }
#endif
}

static void tcg_target_init(TCGContext *s)
{
#ifdef CONFIG_CPUID_H
    unsigned a, b, c, d;
    int max = __get_cpuid_max(0, 0);

    if (max >= 1) {
        __cpuid(1, a, b, c, d);
#ifndef have_cmov
        /* For 32-bit, 99% certainty that we're running on hardware that
           supports cmov, but we still need to check.  In case cmov is not
           available, we'll use a small forward branch.  */
        have_cmov = (d & bit_CMOV) != 0;
#endif
#ifndef have_movbe
        /* MOVBE is only available on Intel Atom and Haswell CPUs, so we
           need to probe for it.  */
        have_movbe = (c & bit_MOVBE) != 0;
#endif
#ifdef bit_POPCNT
        have_popcnt = (c & bit_POPCNT) != 0;
#endif
    }

    if (max >= 7) {
        /* BMI1 is available on AMD Piledriver and Intel Haswell CPUs.  */
        __cpuid_count(7, 0, a, b, c, d);
#ifdef bit_BMI
        have_bmi1 = (b & bit_BMI) != 0;
#endif
#ifndef have_bmi2
        have_bmi2 = (b & bit_BMI2) != 0;
#endif
    }
#endif

#ifndef have_lzcnt
    max = __get_cpuid_max(0x8000000, 0);
    if (max >= 1) {
        __cpuid(0x80000001, a, b, c, d);
        /* LZCNT was introduced with AMD Barcelona and Intel Haswell CPUs.  */
        have_lzcnt = (c & bit_LZCNT) != 0;
    }
#endif

    if (TCG_TARGET_REG_BITS == 64) {
        tcg_regset_set32(tcg_target_available_regs[TCG_TYPE_I32], 0, 0xffff);
        tcg_regset_set32(tcg_target_available_regs[TCG_TYPE_I64], 0, 0xffff);
    } else {
        tcg_regset_set32(tcg_target_available_regs[TCG_TYPE_I32], 0, 0xff);
    }

    tcg_regset_clear(tcg_target_call_clobber_regs);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_EAX);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_EDX);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_ECX);
    if (TCG_TARGET_REG_BITS == 64) {
#if !defined(_WIN64)
        tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_RDI);
        tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_RSI);
#endif
        tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_R8);
        tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_R9);
        tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_R10);
        tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_R11);
    }

    tcg_regset_clear(s->reserved_regs);
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_CALL_STACK);
}

typedef struct {
    DebugFrameHeader h;
    uint8_t fde_def_cfa[4];
    uint8_t fde_reg_ofs[14];
} DebugFrame;

/* We're expecting a 2 byte uleb128 encoded value.  */
QEMU_BUILD_BUG_ON(FRAME_SIZE >= (1 << 14));

#if !defined(__ELF__)
    /* Host machine without ELF. */
#elif TCG_TARGET_REG_BITS == 64
#define ELF_HOST_MACHINE EM_X86_64
static const DebugFrame debug_frame = {
    .h.cie.len = sizeof(DebugFrameCIE)-4, /* length after .len member */
    .h.cie.id = -1,
    .h.cie.version = 1,
    .h.cie.code_align = 1,
    .h.cie.data_align = 0x78,             /* sleb128 -8 */
    .h.cie.return_column = 16,

    /* Total FDE size does not include the "len" member.  */
    .h.fde.len = sizeof(DebugFrame) - offsetof(DebugFrame, h.fde.cie_offset),

    .fde_def_cfa = {
        12, 7,                          /* DW_CFA_def_cfa %rsp, ... */
        (FRAME_SIZE & 0x7f) | 0x80,     /* ... uleb128 FRAME_SIZE */
        (FRAME_SIZE >> 7)
    },
    .fde_reg_ofs = {
        0x90, 1,                        /* DW_CFA_offset, %rip, -8 */
        /* The following ordering must match tcg_target_callee_save_regs.  */
        0x86, 2,                        /* DW_CFA_offset, %rbp, -16 */
        0x83, 3,                        /* DW_CFA_offset, %rbx, -24 */
        0x8c, 4,                        /* DW_CFA_offset, %r12, -32 */
        0x8d, 5,                        /* DW_CFA_offset, %r13, -40 */
        0x8e, 6,                        /* DW_CFA_offset, %r14, -48 */
        0x8f, 7,                        /* DW_CFA_offset, %r15, -56 */
    }
};
#else
#define ELF_HOST_MACHINE EM_386
static const DebugFrame debug_frame = {
    .h.cie.len = sizeof(DebugFrameCIE)-4, /* length after .len member */
    .h.cie.id = -1,
    .h.cie.version = 1,
    .h.cie.code_align = 1,
    .h.cie.data_align = 0x7c,             /* sleb128 -4 */
    .h.cie.return_column = 8,

    /* Total FDE size does not include the "len" member.  */
    .h.fde.len = sizeof(DebugFrame) - offsetof(DebugFrame, h.fde.cie_offset),

    .fde_def_cfa = {
        12, 4,                          /* DW_CFA_def_cfa %esp, ... */
        (FRAME_SIZE & 0x7f) | 0x80,     /* ... uleb128 FRAME_SIZE */
        (FRAME_SIZE >> 7)
    },
    .fde_reg_ofs = {
        0x88, 1,                        /* DW_CFA_offset, %eip, -4 */
        /* The following ordering must match tcg_target_callee_save_regs.  */
        0x85, 2,                        /* DW_CFA_offset, %ebp, -8 */
        0x83, 3,                        /* DW_CFA_offset, %ebx, -12 */
        0x86, 4,                        /* DW_CFA_offset, %esi, -16 */
        0x87, 5,                        /* DW_CFA_offset, %edi, -20 */
    }
};
#endif

#if defined(ELF_HOST_MACHINE)
void tcg_register_jit(void *buf, size_t buf_size)
{
    tcg_register_jit_int(buf, buf_size, &debug_frame, sizeof(debug_frame));
}
#endif

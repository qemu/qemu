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

#ifdef HOST_WORDS_BIGENDIAN
# define MIPS_BE  1
#else
# define MIPS_BE  0
#endif

#if TCG_TARGET_REG_BITS == 32
# define LO_OFF  (MIPS_BE * 4)
# define HI_OFF  (4 - LO_OFF)
#else
/* To assert at compile-time that these values are never used
   for TCG_TARGET_REG_BITS == 64.  */
int link_error(void);
# define LO_OFF  link_error()
# define HI_OFF  link_error()
#endif

#ifdef CONFIG_DEBUG_TCG
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
    "s8",
    "ra",
};
#endif

#define TCG_TMP0  TCG_REG_AT
#define TCG_TMP1  TCG_REG_T9
#define TCG_TMP2  TCG_REG_T8
#define TCG_TMP3  TCG_REG_T7

#ifndef CONFIG_SOFTMMU
#define TCG_GUEST_BASE_REG TCG_REG_S1
#endif

/* check if we really need so many registers :P */
static const int tcg_target_reg_alloc_order[] = {
    /* Call saved registers.  */
    TCG_REG_S0,
    TCG_REG_S1,
    TCG_REG_S2,
    TCG_REG_S3,
    TCG_REG_S4,
    TCG_REG_S5,
    TCG_REG_S6,
    TCG_REG_S7,
    TCG_REG_S8,

    /* Call clobbered registers.  */
    TCG_REG_T4,
    TCG_REG_T5,
    TCG_REG_T6,
    TCG_REG_T7,
    TCG_REG_T8,
    TCG_REG_T9,
    TCG_REG_V1,
    TCG_REG_V0,

    /* Argument registers, opposite order of allocation.  */
    TCG_REG_T3,
    TCG_REG_T2,
    TCG_REG_T1,
    TCG_REG_T0,
    TCG_REG_A3,
    TCG_REG_A2,
    TCG_REG_A1,
    TCG_REG_A0,
};

static const TCGReg tcg_target_call_iarg_regs[] = {
    TCG_REG_A0,
    TCG_REG_A1,
    TCG_REG_A2,
    TCG_REG_A3,
#if _MIPS_SIM == _ABIN32 || _MIPS_SIM == _ABI64
    TCG_REG_T0,
    TCG_REG_T1,
    TCG_REG_T2,
    TCG_REG_T3,
#endif
};

static const TCGReg tcg_target_call_oarg_regs[2] = {
    TCG_REG_V0,
    TCG_REG_V1
};

static tcg_insn_unit *tb_ret_addr;
static tcg_insn_unit *bswap32_addr;
static tcg_insn_unit *bswap32u_addr;
static tcg_insn_unit *bswap64_addr;

static inline uint32_t reloc_pc16_val(tcg_insn_unit *pc, tcg_insn_unit *target)
{
    /* Let the compiler perform the right-shift as part of the arithmetic.  */
    ptrdiff_t disp = target - (pc + 1);
    tcg_debug_assert(disp == (int16_t)disp);
    return disp & 0xffff;
}

static inline void reloc_pc16(tcg_insn_unit *pc, tcg_insn_unit *target)
{
    *pc = deposit32(*pc, 0, 16, reloc_pc16_val(pc, target));
}

static inline uint32_t reloc_26_val(tcg_insn_unit *pc, tcg_insn_unit *target)
{
    tcg_debug_assert((((uintptr_t)pc ^ (uintptr_t)target) & 0xf0000000) == 0);
    return ((uintptr_t)target >> 2) & 0x3ffffff;
}

static inline void reloc_26(tcg_insn_unit *pc, tcg_insn_unit *target)
{
    *pc = deposit32(*pc, 0, 26, reloc_26_val(pc, target));
}

static bool patch_reloc(tcg_insn_unit *code_ptr, int type,
                        intptr_t value, intptr_t addend)
{
    tcg_debug_assert(type == R_MIPS_PC16);
    tcg_debug_assert(addend == 0);
    reloc_pc16(code_ptr, (tcg_insn_unit *)value);
    return true;
}

#define TCG_CT_CONST_ZERO 0x100
#define TCG_CT_CONST_U16  0x200    /* Unsigned 16-bit: 0 - 0xffff.  */
#define TCG_CT_CONST_S16  0x400    /* Signed 16-bit: -32768 - 32767 */
#define TCG_CT_CONST_P2M1 0x800    /* Power of 2 minus 1.  */
#define TCG_CT_CONST_N16  0x1000   /* "Negatable" 16-bit: -32767 - 32767 */
#define TCG_CT_CONST_WSZ  0x2000   /* word size */

static inline bool is_p2m1(tcg_target_long val)
{
    return val && ((val + 1) & val) == 0;
}

/* parse target specific constraints */
static const char *target_parse_constraint(TCGArgConstraint *ct,
                                           const char *ct_str, TCGType type)
{
    switch(*ct_str++) {
    case 'r':
        ct->ct |= TCG_CT_REG;
        ct->u.regs = 0xffffffff;
        break;
    case 'L': /* qemu_ld input arg constraint */
        ct->ct |= TCG_CT_REG;
        ct->u.regs = 0xffffffff;
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_A0);
#if defined(CONFIG_SOFTMMU)
        if (TCG_TARGET_REG_BITS < TARGET_LONG_BITS) {
            tcg_regset_reset_reg(ct->u.regs, TCG_REG_A2);
        }
#endif
        break;
    case 'S': /* qemu_st constraint */
        ct->ct |= TCG_CT_REG;
        ct->u.regs = 0xffffffff;
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_A0);
#if defined(CONFIG_SOFTMMU)
        if (TCG_TARGET_REG_BITS < TARGET_LONG_BITS) {
            tcg_regset_reset_reg(ct->u.regs, TCG_REG_A2);
            tcg_regset_reset_reg(ct->u.regs, TCG_REG_A3);
        } else {
            tcg_regset_reset_reg(ct->u.regs, TCG_REG_A1);
        }
#endif
        break;
    case 'I':
        ct->ct |= TCG_CT_CONST_U16;
        break;
    case 'J':
        ct->ct |= TCG_CT_CONST_S16;
        break;
    case 'K':
        ct->ct |= TCG_CT_CONST_P2M1;
        break;
    case 'N':
        ct->ct |= TCG_CT_CONST_N16;
        break;
    case 'W':
        ct->ct |= TCG_CT_CONST_WSZ;
        break;
    case 'Z':
        /* We are cheating a bit here, using the fact that the register
           ZERO is also the register number 0. Hence there is no need
           to check for const_args in each instruction. */
        ct->ct |= TCG_CT_CONST_ZERO;
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
    int ct;
    ct = arg_ct->ct;
    if (ct & TCG_CT_CONST) {
        return 1;
    } else if ((ct & TCG_CT_CONST_ZERO) && val == 0) {
        return 1;
    } else if ((ct & TCG_CT_CONST_U16) && val == (uint16_t)val) {
        return 1;
    } else if ((ct & TCG_CT_CONST_S16) && val == (int16_t)val) {
        return 1;
    } else if ((ct & TCG_CT_CONST_N16) && val >= -32767 && val <= 32767) {
        return 1;
    } else if ((ct & TCG_CT_CONST_P2M1)
               && use_mips32r2_instructions && is_p2m1(val)) {
        return 1;
    } else if ((ct & TCG_CT_CONST_WSZ)
               && val == (type == TCG_TYPE_I32 ? 32 : 64)) {
        return 1;
    }
    return 0;
}

/* instruction opcodes */
typedef enum {
    OPC_J        = 002 << 26,
    OPC_JAL      = 003 << 26,
    OPC_BEQ      = 004 << 26,
    OPC_BNE      = 005 << 26,
    OPC_BLEZ     = 006 << 26,
    OPC_BGTZ     = 007 << 26,
    OPC_ADDIU    = 011 << 26,
    OPC_SLTI     = 012 << 26,
    OPC_SLTIU    = 013 << 26,
    OPC_ANDI     = 014 << 26,
    OPC_ORI      = 015 << 26,
    OPC_XORI     = 016 << 26,
    OPC_LUI      = 017 << 26,
    OPC_DADDIU   = 031 << 26,
    OPC_LB       = 040 << 26,
    OPC_LH       = 041 << 26,
    OPC_LW       = 043 << 26,
    OPC_LBU      = 044 << 26,
    OPC_LHU      = 045 << 26,
    OPC_LWU      = 047 << 26,
    OPC_SB       = 050 << 26,
    OPC_SH       = 051 << 26,
    OPC_SW       = 053 << 26,
    OPC_LD       = 067 << 26,
    OPC_SD       = 077 << 26,

    OPC_SPECIAL  = 000 << 26,
    OPC_SLL      = OPC_SPECIAL | 000,
    OPC_SRL      = OPC_SPECIAL | 002,
    OPC_ROTR     = OPC_SPECIAL | 002 | (1 << 21),
    OPC_SRA      = OPC_SPECIAL | 003,
    OPC_SLLV     = OPC_SPECIAL | 004,
    OPC_SRLV     = OPC_SPECIAL | 006,
    OPC_ROTRV    = OPC_SPECIAL | 006 | 0100,
    OPC_SRAV     = OPC_SPECIAL | 007,
    OPC_JR_R5    = OPC_SPECIAL | 010,
    OPC_JALR     = OPC_SPECIAL | 011,
    OPC_MOVZ     = OPC_SPECIAL | 012,
    OPC_MOVN     = OPC_SPECIAL | 013,
    OPC_SYNC     = OPC_SPECIAL | 017,
    OPC_MFHI     = OPC_SPECIAL | 020,
    OPC_MFLO     = OPC_SPECIAL | 022,
    OPC_DSLLV    = OPC_SPECIAL | 024,
    OPC_DSRLV    = OPC_SPECIAL | 026,
    OPC_DROTRV   = OPC_SPECIAL | 026 | 0100,
    OPC_DSRAV    = OPC_SPECIAL | 027,
    OPC_MULT     = OPC_SPECIAL | 030,
    OPC_MUL_R6   = OPC_SPECIAL | 030 | 0200,
    OPC_MUH      = OPC_SPECIAL | 030 | 0300,
    OPC_MULTU    = OPC_SPECIAL | 031,
    OPC_MULU     = OPC_SPECIAL | 031 | 0200,
    OPC_MUHU     = OPC_SPECIAL | 031 | 0300,
    OPC_DIV      = OPC_SPECIAL | 032,
    OPC_DIV_R6   = OPC_SPECIAL | 032 | 0200,
    OPC_MOD      = OPC_SPECIAL | 032 | 0300,
    OPC_DIVU     = OPC_SPECIAL | 033,
    OPC_DIVU_R6  = OPC_SPECIAL | 033 | 0200,
    OPC_MODU     = OPC_SPECIAL | 033 | 0300,
    OPC_DMULT    = OPC_SPECIAL | 034,
    OPC_DMUL     = OPC_SPECIAL | 034 | 0200,
    OPC_DMUH     = OPC_SPECIAL | 034 | 0300,
    OPC_DMULTU   = OPC_SPECIAL | 035,
    OPC_DMULU    = OPC_SPECIAL | 035 | 0200,
    OPC_DMUHU    = OPC_SPECIAL | 035 | 0300,
    OPC_DDIV     = OPC_SPECIAL | 036,
    OPC_DDIV_R6  = OPC_SPECIAL | 036 | 0200,
    OPC_DMOD     = OPC_SPECIAL | 036 | 0300,
    OPC_DDIVU    = OPC_SPECIAL | 037,
    OPC_DDIVU_R6 = OPC_SPECIAL | 037 | 0200,
    OPC_DMODU    = OPC_SPECIAL | 037 | 0300,
    OPC_ADDU     = OPC_SPECIAL | 041,
    OPC_SUBU     = OPC_SPECIAL | 043,
    OPC_AND      = OPC_SPECIAL | 044,
    OPC_OR       = OPC_SPECIAL | 045,
    OPC_XOR      = OPC_SPECIAL | 046,
    OPC_NOR      = OPC_SPECIAL | 047,
    OPC_SLT      = OPC_SPECIAL | 052,
    OPC_SLTU     = OPC_SPECIAL | 053,
    OPC_DADDU    = OPC_SPECIAL | 055,
    OPC_DSUBU    = OPC_SPECIAL | 057,
    OPC_SELEQZ   = OPC_SPECIAL | 065,
    OPC_SELNEZ   = OPC_SPECIAL | 067,
    OPC_DSLL     = OPC_SPECIAL | 070,
    OPC_DSRL     = OPC_SPECIAL | 072,
    OPC_DROTR    = OPC_SPECIAL | 072 | (1 << 21),
    OPC_DSRA     = OPC_SPECIAL | 073,
    OPC_DSLL32   = OPC_SPECIAL | 074,
    OPC_DSRL32   = OPC_SPECIAL | 076,
    OPC_DROTR32  = OPC_SPECIAL | 076 | (1 << 21),
    OPC_DSRA32   = OPC_SPECIAL | 077,
    OPC_CLZ_R6   = OPC_SPECIAL | 0120,
    OPC_DCLZ_R6  = OPC_SPECIAL | 0122,

    OPC_REGIMM   = 001 << 26,
    OPC_BLTZ     = OPC_REGIMM | (000 << 16),
    OPC_BGEZ     = OPC_REGIMM | (001 << 16),

    OPC_SPECIAL2 = 034 << 26,
    OPC_MUL_R5   = OPC_SPECIAL2 | 002,
    OPC_CLZ      = OPC_SPECIAL2 | 040,
    OPC_DCLZ     = OPC_SPECIAL2 | 044,

    OPC_SPECIAL3 = 037 << 26,
    OPC_EXT      = OPC_SPECIAL3 | 000,
    OPC_DEXTM    = OPC_SPECIAL3 | 001,
    OPC_DEXTU    = OPC_SPECIAL3 | 002,
    OPC_DEXT     = OPC_SPECIAL3 | 003,
    OPC_INS      = OPC_SPECIAL3 | 004,
    OPC_DINSM    = OPC_SPECIAL3 | 005,
    OPC_DINSU    = OPC_SPECIAL3 | 006,
    OPC_DINS     = OPC_SPECIAL3 | 007,
    OPC_WSBH     = OPC_SPECIAL3 | 00240,
    OPC_DSBH     = OPC_SPECIAL3 | 00244,
    OPC_DSHD     = OPC_SPECIAL3 | 00544,
    OPC_SEB      = OPC_SPECIAL3 | 02040,
    OPC_SEH      = OPC_SPECIAL3 | 03040,

    /* MIPS r6 doesn't have JR, JALR should be used instead */
    OPC_JR       = use_mips32r6_instructions ? OPC_JALR : OPC_JR_R5,

    /*
     * MIPS r6 replaces MUL with an alternative encoding which is
     * backwards-compatible at the assembly level.
     */
    OPC_MUL      = use_mips32r6_instructions ? OPC_MUL_R6 : OPC_MUL_R5,

    /* MIPS r6 introduced names for weaker variants of SYNC.  These are
       backward compatible to previous architecture revisions.  */
    OPC_SYNC_WMB     = OPC_SYNC | 0x04 << 5,
    OPC_SYNC_MB      = OPC_SYNC | 0x10 << 5,
    OPC_SYNC_ACQUIRE = OPC_SYNC | 0x11 << 5,
    OPC_SYNC_RELEASE = OPC_SYNC | 0x12 << 5,
    OPC_SYNC_RMB     = OPC_SYNC | 0x13 << 5,

    /* Aliases for convenience.  */
    ALIAS_PADD     = sizeof(void *) == 4 ? OPC_ADDU : OPC_DADDU,
    ALIAS_PADDI    = sizeof(void *) == 4 ? OPC_ADDIU : OPC_DADDIU,
    ALIAS_TSRL     = TARGET_LONG_BITS == 32 || TCG_TARGET_REG_BITS == 32
                     ? OPC_SRL : OPC_DSRL,
} MIPSInsn;

/*
 * Type reg
 */
static inline void tcg_out_opc_reg(TCGContext *s, MIPSInsn opc,
                                   TCGReg rd, TCGReg rs, TCGReg rt)
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
static inline void tcg_out_opc_imm(TCGContext *s, MIPSInsn opc,
                                   TCGReg rt, TCGReg rs, TCGArg imm)
{
    int32_t inst;

    inst = opc;
    inst |= (rs & 0x1F) << 21;
    inst |= (rt & 0x1F) << 16;
    inst |= (imm & 0xffff);
    tcg_out32(s, inst);
}

/*
 * Type bitfield
 */
static inline void tcg_out_opc_bf(TCGContext *s, MIPSInsn opc, TCGReg rt,
                                  TCGReg rs, int msb, int lsb)
{
    int32_t inst;

    inst = opc;
    inst |= (rs & 0x1F) << 21;
    inst |= (rt & 0x1F) << 16;
    inst |= (msb & 0x1F) << 11;
    inst |= (lsb & 0x1F) << 6;
    tcg_out32(s, inst);
}

static inline void tcg_out_opc_bf64(TCGContext *s, MIPSInsn opc, MIPSInsn opm,
                                    MIPSInsn oph, TCGReg rt, TCGReg rs,
                                    int msb, int lsb)
{
    if (lsb >= 32) {
        opc = oph;
        msb -= 32;
        lsb -= 32;
    } else if (msb >= 32) {
        opc = opm;
        msb -= 32;
    }
    tcg_out_opc_bf(s, opc, rt, rs, msb, lsb);
}

/*
 * Type branch
 */
static inline void tcg_out_opc_br(TCGContext *s, MIPSInsn opc,
                                  TCGReg rt, TCGReg rs)
{
    tcg_out_opc_imm(s, opc, rt, rs, 0);
}

/*
 * Type sa
 */
static inline void tcg_out_opc_sa(TCGContext *s, MIPSInsn opc,
                                  TCGReg rd, TCGReg rt, TCGArg sa)
{
    int32_t inst;

    inst = opc;
    inst |= (rt & 0x1F) << 16;
    inst |= (rd & 0x1F) << 11;
    inst |= (sa & 0x1F) <<  6;
    tcg_out32(s, inst);

}

static void tcg_out_opc_sa64(TCGContext *s, MIPSInsn opc1, MIPSInsn opc2,
                             TCGReg rd, TCGReg rt, TCGArg sa)
{
    int32_t inst;

    inst = (sa & 32 ? opc2 : opc1);
    inst |= (rt & 0x1F) << 16;
    inst |= (rd & 0x1F) << 11;
    inst |= (sa & 0x1F) <<  6;
    tcg_out32(s, inst);
}

/*
 * Type jump.
 * Returns true if the branch was in range and the insn was emitted.
 */
static bool tcg_out_opc_jmp(TCGContext *s, MIPSInsn opc, void *target)
{
    uintptr_t dest = (uintptr_t)target;
    uintptr_t from = (uintptr_t)s->code_ptr + 4;
    int32_t inst;

    /* The pc-region branch happens within the 256MB region of
       the delay slot (thus the +4).  */
    if ((from ^ dest) & -(1 << 28)) {
        return false;
    }
    tcg_debug_assert((dest & 3) == 0);

    inst = opc;
    inst |= (dest >> 2) & 0x3ffffff;
    tcg_out32(s, inst);
    return true;
}

static inline void tcg_out_nop(TCGContext *s)
{
    tcg_out32(s, 0);
}

static inline void tcg_out_dsll(TCGContext *s, TCGReg rd, TCGReg rt, TCGArg sa)
{
    tcg_out_opc_sa64(s, OPC_DSLL, OPC_DSLL32, rd, rt, sa);
}

static inline void tcg_out_dsrl(TCGContext *s, TCGReg rd, TCGReg rt, TCGArg sa)
{
    tcg_out_opc_sa64(s, OPC_DSRL, OPC_DSRL32, rd, rt, sa);
}

static inline void tcg_out_dsra(TCGContext *s, TCGReg rd, TCGReg rt, TCGArg sa)
{
    tcg_out_opc_sa64(s, OPC_DSRA, OPC_DSRA32, rd, rt, sa);
}

static inline bool tcg_out_mov(TCGContext *s, TCGType type,
                               TCGReg ret, TCGReg arg)
{
    /* Simple reg-reg move, optimising out the 'do nothing' case */
    if (ret != arg) {
        tcg_out_opc_reg(s, OPC_OR, ret, arg, TCG_REG_ZERO);
    }
    return true;
}

static void tcg_out_movi(TCGContext *s, TCGType type,
                         TCGReg ret, tcg_target_long arg)
{
    if (TCG_TARGET_REG_BITS == 64 && type == TCG_TYPE_I32) {
        arg = (int32_t)arg;
    }
    if (arg == (int16_t)arg) {
        tcg_out_opc_imm(s, OPC_ADDIU, ret, TCG_REG_ZERO, arg);
        return;
    }
    if (arg == (uint16_t)arg) {
        tcg_out_opc_imm(s, OPC_ORI, ret, TCG_REG_ZERO, arg);
        return;
    }
    if (TCG_TARGET_REG_BITS == 32 || arg == (int32_t)arg) {
        tcg_out_opc_imm(s, OPC_LUI, ret, TCG_REG_ZERO, arg >> 16);
    } else {
        tcg_out_movi(s, TCG_TYPE_I32, ret, arg >> 31 >> 1);
        if (arg & 0xffff0000ull) {
            tcg_out_dsll(s, ret, ret, 16);
            tcg_out_opc_imm(s, OPC_ORI, ret, ret, arg >> 16);
            tcg_out_dsll(s, ret, ret, 16);
        } else {
            tcg_out_dsll(s, ret, ret, 32);
        }
    }
    if (arg & 0xffff) {
        tcg_out_opc_imm(s, OPC_ORI, ret, ret, arg & 0xffff);
    }
}

static inline void tcg_out_bswap16(TCGContext *s, TCGReg ret, TCGReg arg)
{
    if (use_mips32r2_instructions) {
        tcg_out_opc_reg(s, OPC_WSBH, ret, 0, arg);
    } else {
        /* ret and arg can't be register at */
        if (ret == TCG_TMP0 || arg == TCG_TMP0) {
            tcg_abort();
        }

        tcg_out_opc_sa(s, OPC_SRL, TCG_TMP0, arg, 8);
        tcg_out_opc_sa(s, OPC_SLL, ret, arg, 8);
        tcg_out_opc_imm(s, OPC_ANDI, ret, ret, 0xff00);
        tcg_out_opc_reg(s, OPC_OR, ret, ret, TCG_TMP0);
    }
}

static inline void tcg_out_bswap16s(TCGContext *s, TCGReg ret, TCGReg arg)
{
    if (use_mips32r2_instructions) {
        tcg_out_opc_reg(s, OPC_WSBH, ret, 0, arg);
        tcg_out_opc_reg(s, OPC_SEH, ret, 0, ret);
    } else {
        /* ret and arg can't be register at */
        if (ret == TCG_TMP0 || arg == TCG_TMP0) {
            tcg_abort();
        }

        tcg_out_opc_sa(s, OPC_SRL, TCG_TMP0, arg, 8);
        tcg_out_opc_sa(s, OPC_SLL, ret, arg, 24);
        tcg_out_opc_sa(s, OPC_SRA, ret, ret, 16);
        tcg_out_opc_reg(s, OPC_OR, ret, ret, TCG_TMP0);
    }
}

static void tcg_out_bswap_subr(TCGContext *s, tcg_insn_unit *sub)
{
    bool ok = tcg_out_opc_jmp(s, OPC_JAL, sub);
    tcg_debug_assert(ok);
}

static void tcg_out_bswap32(TCGContext *s, TCGReg ret, TCGReg arg)
{
    if (use_mips32r2_instructions) {
        tcg_out_opc_reg(s, OPC_WSBH, ret, 0, arg);
        tcg_out_opc_sa(s, OPC_ROTR, ret, ret, 16);
    } else {
        tcg_out_bswap_subr(s, bswap32_addr);
        /* delay slot -- never omit the insn, like tcg_out_mov might.  */
        tcg_out_opc_reg(s, OPC_OR, TCG_TMP0, arg, TCG_REG_ZERO);
        tcg_out_mov(s, TCG_TYPE_I32, ret, TCG_TMP3);
    }
}

static void tcg_out_bswap32u(TCGContext *s, TCGReg ret, TCGReg arg)
{
    if (use_mips32r2_instructions) {
        tcg_out_opc_reg(s, OPC_DSBH, ret, 0, arg);
        tcg_out_opc_reg(s, OPC_DSHD, ret, 0, ret);
        tcg_out_dsrl(s, ret, ret, 32);
    } else {
        tcg_out_bswap_subr(s, bswap32u_addr);
        /* delay slot -- never omit the insn, like tcg_out_mov might.  */
        tcg_out_opc_reg(s, OPC_OR, TCG_TMP0, arg, TCG_REG_ZERO);
        tcg_out_mov(s, TCG_TYPE_I32, ret, TCG_TMP3);
    }
}

static void tcg_out_bswap64(TCGContext *s, TCGReg ret, TCGReg arg)
{
    if (use_mips32r2_instructions) {
        tcg_out_opc_reg(s, OPC_DSBH, ret, 0, arg);
        tcg_out_opc_reg(s, OPC_DSHD, ret, 0, ret);
    } else {
        tcg_out_bswap_subr(s, bswap64_addr);
        /* delay slot -- never omit the insn, like tcg_out_mov might.  */
        tcg_out_opc_reg(s, OPC_OR, TCG_TMP0, arg, TCG_REG_ZERO);
        tcg_out_mov(s, TCG_TYPE_I32, ret, TCG_TMP3);
    }
}

static inline void tcg_out_ext8s(TCGContext *s, TCGReg ret, TCGReg arg)
{
    if (use_mips32r2_instructions) {
        tcg_out_opc_reg(s, OPC_SEB, ret, 0, arg);
    } else {
        tcg_out_opc_sa(s, OPC_SLL, ret, arg, 24);
        tcg_out_opc_sa(s, OPC_SRA, ret, ret, 24);
    }
}

static inline void tcg_out_ext16s(TCGContext *s, TCGReg ret, TCGReg arg)
{
    if (use_mips32r2_instructions) {
        tcg_out_opc_reg(s, OPC_SEH, ret, 0, arg);
    } else {
        tcg_out_opc_sa(s, OPC_SLL, ret, arg, 16);
        tcg_out_opc_sa(s, OPC_SRA, ret, ret, 16);
    }
}

static inline void tcg_out_ext32u(TCGContext *s, TCGReg ret, TCGReg arg)
{
    if (use_mips32r2_instructions) {
        tcg_out_opc_bf(s, OPC_DEXT, ret, arg, 31, 0);
    } else {
        tcg_out_dsll(s, ret, arg, 32);
        tcg_out_dsrl(s, ret, ret, 32);
    }
}

static void tcg_out_ldst(TCGContext *s, MIPSInsn opc, TCGReg data,
                         TCGReg addr, intptr_t ofs)
{
    int16_t lo = ofs;
    if (ofs != lo) {
        tcg_out_movi(s, TCG_TYPE_PTR, TCG_TMP0, ofs - lo);
        if (addr != TCG_REG_ZERO) {
            tcg_out_opc_reg(s, ALIAS_PADD, TCG_TMP0, TCG_TMP0, addr);
        }
        addr = TCG_TMP0;
    }
    tcg_out_opc_imm(s, opc, data, addr, lo);
}

static inline void tcg_out_ld(TCGContext *s, TCGType type, TCGReg arg,
                              TCGReg arg1, intptr_t arg2)
{
    MIPSInsn opc = OPC_LD;
    if (TCG_TARGET_REG_BITS == 32 || type == TCG_TYPE_I32) {
        opc = OPC_LW;
    }
    tcg_out_ldst(s, opc, arg, arg1, arg2);
}

static inline void tcg_out_st(TCGContext *s, TCGType type, TCGReg arg,
                              TCGReg arg1, intptr_t arg2)
{
    MIPSInsn opc = OPC_SD;
    if (TCG_TARGET_REG_BITS == 32 || type == TCG_TYPE_I32) {
        opc = OPC_SW;
    }
    tcg_out_ldst(s, opc, arg, arg1, arg2);
}

static inline bool tcg_out_sti(TCGContext *s, TCGType type, TCGArg val,
                               TCGReg base, intptr_t ofs)
{
    if (val == 0) {
        tcg_out_st(s, type, TCG_REG_ZERO, base, ofs);
        return true;
    }
    return false;
}

static void tcg_out_addsub2(TCGContext *s, TCGReg rl, TCGReg rh, TCGReg al,
                            TCGReg ah, TCGArg bl, TCGArg bh, bool cbl,
                            bool cbh, bool is_sub)
{
    TCGReg th = TCG_TMP1;

    /* If we have a negative constant such that negating it would
       make the high part zero, we can (usually) eliminate one insn.  */
    if (cbl && cbh && bh == -1 && bl != 0) {
        bl = -bl;
        bh = 0;
        is_sub = !is_sub;
    }

    /* By operating on the high part first, we get to use the final
       carry operation to move back from the temporary.  */
    if (!cbh) {
        tcg_out_opc_reg(s, (is_sub ? OPC_SUBU : OPC_ADDU), th, ah, bh);
    } else if (bh != 0 || ah == rl) {
        tcg_out_opc_imm(s, OPC_ADDIU, th, ah, (is_sub ? -bh : bh));
    } else {
        th = ah;
    }

    /* Note that tcg optimization should eliminate the bl == 0 case.  */
    if (is_sub) {
        if (cbl) {
            tcg_out_opc_imm(s, OPC_SLTIU, TCG_TMP0, al, bl);
            tcg_out_opc_imm(s, OPC_ADDIU, rl, al, -bl);
        } else {
            tcg_out_opc_reg(s, OPC_SLTU, TCG_TMP0, al, bl);
            tcg_out_opc_reg(s, OPC_SUBU, rl, al, bl);
        }
        tcg_out_opc_reg(s, OPC_SUBU, rh, th, TCG_TMP0);
    } else {
        if (cbl) {
            tcg_out_opc_imm(s, OPC_ADDIU, rl, al, bl);
            tcg_out_opc_imm(s, OPC_SLTIU, TCG_TMP0, rl, bl);
        } else if (rl == al && rl == bl) {
            tcg_out_opc_sa(s, OPC_SRL, TCG_TMP0, al, TCG_TARGET_REG_BITS - 1);
            tcg_out_opc_reg(s, OPC_ADDU, rl, al, bl);
        } else {
            tcg_out_opc_reg(s, OPC_ADDU, rl, al, bl);
            tcg_out_opc_reg(s, OPC_SLTU, TCG_TMP0, rl, (rl == bl ? al : bl));
        }
        tcg_out_opc_reg(s, OPC_ADDU, rh, th, TCG_TMP0);
    }
}

/* Bit 0 set if inversion required; bit 1 set if swapping required.  */
#define MIPS_CMP_INV  1
#define MIPS_CMP_SWAP 2

static const uint8_t mips_cmp_map[16] = {
    [TCG_COND_LT]  = 0,
    [TCG_COND_LTU] = 0,
    [TCG_COND_GE]  = MIPS_CMP_INV,
    [TCG_COND_GEU] = MIPS_CMP_INV,
    [TCG_COND_LE]  = MIPS_CMP_INV | MIPS_CMP_SWAP,
    [TCG_COND_LEU] = MIPS_CMP_INV | MIPS_CMP_SWAP,
    [TCG_COND_GT]  = MIPS_CMP_SWAP,
    [TCG_COND_GTU] = MIPS_CMP_SWAP,
};

static void tcg_out_setcond(TCGContext *s, TCGCond cond, TCGReg ret,
                            TCGReg arg1, TCGReg arg2)
{
    MIPSInsn s_opc = OPC_SLTU;
    int cmp_map;

    switch (cond) {
    case TCG_COND_EQ:
        if (arg2 != 0) {
            tcg_out_opc_reg(s, OPC_XOR, ret, arg1, arg2);
            arg1 = ret;
        }
        tcg_out_opc_imm(s, OPC_SLTIU, ret, arg1, 1);
        break;

    case TCG_COND_NE:
        if (arg2 != 0) {
            tcg_out_opc_reg(s, OPC_XOR, ret, arg1, arg2);
            arg1 = ret;
        }
        tcg_out_opc_reg(s, OPC_SLTU, ret, TCG_REG_ZERO, arg1);
        break;

    case TCG_COND_LT:
    case TCG_COND_GE:
    case TCG_COND_LE:
    case TCG_COND_GT:
        s_opc = OPC_SLT;
        /* FALLTHRU */

    case TCG_COND_LTU:
    case TCG_COND_GEU:
    case TCG_COND_LEU:
    case TCG_COND_GTU:
        cmp_map = mips_cmp_map[cond];
        if (cmp_map & MIPS_CMP_SWAP) {
            TCGReg t = arg1;
            arg1 = arg2;
            arg2 = t;
        }
        tcg_out_opc_reg(s, s_opc, ret, arg1, arg2);
        if (cmp_map & MIPS_CMP_INV) {
            tcg_out_opc_imm(s, OPC_XORI, ret, ret, 1);
        }
        break;

     default:
         tcg_abort();
         break;
     }
}

static void tcg_out_brcond(TCGContext *s, TCGCond cond, TCGReg arg1,
                           TCGReg arg2, TCGLabel *l)
{
    static const MIPSInsn b_zero[16] = {
        [TCG_COND_LT] = OPC_BLTZ,
        [TCG_COND_GT] = OPC_BGTZ,
        [TCG_COND_LE] = OPC_BLEZ,
        [TCG_COND_GE] = OPC_BGEZ,
    };

    MIPSInsn s_opc = OPC_SLTU;
    MIPSInsn b_opc;
    int cmp_map;

    switch (cond) {
    case TCG_COND_EQ:
        b_opc = OPC_BEQ;
        break;
    case TCG_COND_NE:
        b_opc = OPC_BNE;
        break;

    case TCG_COND_LT:
    case TCG_COND_GT:
    case TCG_COND_LE:
    case TCG_COND_GE:
        if (arg2 == 0) {
            b_opc = b_zero[cond];
            arg2 = arg1;
            arg1 = 0;
            break;
        }
        s_opc = OPC_SLT;
        /* FALLTHRU */

    case TCG_COND_LTU:
    case TCG_COND_GTU:
    case TCG_COND_LEU:
    case TCG_COND_GEU:
        cmp_map = mips_cmp_map[cond];
        if (cmp_map & MIPS_CMP_SWAP) {
            TCGReg t = arg1;
            arg1 = arg2;
            arg2 = t;
        }
        tcg_out_opc_reg(s, s_opc, TCG_TMP0, arg1, arg2);
        b_opc = (cmp_map & MIPS_CMP_INV ? OPC_BEQ : OPC_BNE);
        arg1 = TCG_TMP0;
        arg2 = TCG_REG_ZERO;
        break;

    default:
        tcg_abort();
        break;
    }

    tcg_out_opc_br(s, b_opc, arg1, arg2);
    if (l->has_value) {
        reloc_pc16(s->code_ptr - 1, l->u.value_ptr);
    } else {
        tcg_out_reloc(s, s->code_ptr - 1, R_MIPS_PC16, l, 0);
    }
    tcg_out_nop(s);
}

static TCGReg tcg_out_reduce_eq2(TCGContext *s, TCGReg tmp0, TCGReg tmp1,
                                 TCGReg al, TCGReg ah,
                                 TCGReg bl, TCGReg bh)
{
    /* Merge highpart comparison into AH.  */
    if (bh != 0) {
        if (ah != 0) {
            tcg_out_opc_reg(s, OPC_XOR, tmp0, ah, bh);
            ah = tmp0;
        } else {
            ah = bh;
        }
    }
    /* Merge lowpart comparison into AL.  */
    if (bl != 0) {
        if (al != 0) {
            tcg_out_opc_reg(s, OPC_XOR, tmp1, al, bl);
            al = tmp1;
        } else {
            al = bl;
        }
    }
    /* Merge high and low part comparisons into AL.  */
    if (ah != 0) {
        if (al != 0) {
            tcg_out_opc_reg(s, OPC_OR, tmp0, ah, al);
            al = tmp0;
        } else {
            al = ah;
        }
    }
    return al;
}

static void tcg_out_setcond2(TCGContext *s, TCGCond cond, TCGReg ret,
                             TCGReg al, TCGReg ah, TCGReg bl, TCGReg bh)
{
    TCGReg tmp0 = TCG_TMP0;
    TCGReg tmp1 = ret;

    tcg_debug_assert(ret != TCG_TMP0);
    if (ret == ah || ret == bh) {
        tcg_debug_assert(ret != TCG_TMP1);
        tmp1 = TCG_TMP1;
    }

    switch (cond) {
    case TCG_COND_EQ:
    case TCG_COND_NE:
        tmp1 = tcg_out_reduce_eq2(s, tmp0, tmp1, al, ah, bl, bh);
        tcg_out_setcond(s, cond, ret, tmp1, TCG_REG_ZERO);
        break;

    default:
        tcg_out_setcond(s, TCG_COND_EQ, tmp0, ah, bh);
        tcg_out_setcond(s, tcg_unsigned_cond(cond), tmp1, al, bl);
        tcg_out_opc_reg(s, OPC_AND, tmp1, tmp1, tmp0);
        tcg_out_setcond(s, tcg_high_cond(cond), tmp0, ah, bh);
        tcg_out_opc_reg(s, OPC_OR, ret, tmp1, tmp0);
        break;
    }
}

static void tcg_out_brcond2(TCGContext *s, TCGCond cond, TCGReg al, TCGReg ah,
                            TCGReg bl, TCGReg bh, TCGLabel *l)
{
    TCGCond b_cond = TCG_COND_NE;
    TCGReg tmp = TCG_TMP1;

    /* With branches, we emit between 4 and 9 insns with 2 or 3 branches.
       With setcond, we emit between 3 and 10 insns and only 1 branch,
       which ought to get better branch prediction.  */
     switch (cond) {
     case TCG_COND_EQ:
     case TCG_COND_NE:
        b_cond = cond;
        tmp = tcg_out_reduce_eq2(s, TCG_TMP0, TCG_TMP1, al, ah, bl, bh);
        break;

    default:
        /* Minimize code size by preferring a compare not requiring INV.  */
        if (mips_cmp_map[cond] & MIPS_CMP_INV) {
            cond = tcg_invert_cond(cond);
            b_cond = TCG_COND_EQ;
        }
        tcg_out_setcond2(s, cond, tmp, al, ah, bl, bh);
        break;
    }

    tcg_out_brcond(s, b_cond, tmp, TCG_REG_ZERO, l);
}

static void tcg_out_movcond(TCGContext *s, TCGCond cond, TCGReg ret,
                            TCGReg c1, TCGReg c2, TCGReg v1, TCGReg v2)
{
    bool eqz = false;

    /* If one of the values is zero, put it last to match SEL*Z instructions */
    if (use_mips32r6_instructions && v1 == 0) {
        v1 = v2;
        v2 = 0;
        cond = tcg_invert_cond(cond);
    }

    switch (cond) {
    case TCG_COND_EQ:
        eqz = true;
        /* FALLTHRU */
    case TCG_COND_NE:
        if (c2 != 0) {
            tcg_out_opc_reg(s, OPC_XOR, TCG_TMP0, c1, c2);
            c1 = TCG_TMP0;
        }
        break;

    default:
        /* Minimize code size by preferring a compare not requiring INV.  */
        if (mips_cmp_map[cond] & MIPS_CMP_INV) {
            cond = tcg_invert_cond(cond);
            eqz = true;
        }
        tcg_out_setcond(s, cond, TCG_TMP0, c1, c2);
        c1 = TCG_TMP0;
        break;
    }

    if (use_mips32r6_instructions) {
        MIPSInsn m_opc_t = eqz ? OPC_SELEQZ : OPC_SELNEZ;
        MIPSInsn m_opc_f = eqz ? OPC_SELNEZ : OPC_SELEQZ;

        if (v2 != 0) {
            tcg_out_opc_reg(s, m_opc_f, TCG_TMP1, v2, c1);
        }
        tcg_out_opc_reg(s, m_opc_t, ret, v1, c1);
        if (v2 != 0) {
            tcg_out_opc_reg(s, OPC_OR, ret, ret, TCG_TMP1);
        }
    } else {
        MIPSInsn m_opc = eqz ? OPC_MOVZ : OPC_MOVN;

        tcg_out_opc_reg(s, m_opc, ret, v1, c1);

        /* This should be guaranteed via constraints */
        tcg_debug_assert(v2 == ret);
    }
}

static void tcg_out_call_int(TCGContext *s, tcg_insn_unit *arg, bool tail)
{
    /* Note that the ABI requires the called function's address to be
       loaded into T9, even if a direct branch is in range.  */
    tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_T9, (uintptr_t)arg);

    /* But do try a direct branch, allowing the cpu better insn prefetch.  */
    if (tail) {
        if (!tcg_out_opc_jmp(s, OPC_J, arg)) {
            tcg_out_opc_reg(s, OPC_JR, 0, TCG_REG_T9, 0);
        }
    } else {
        if (!tcg_out_opc_jmp(s, OPC_JAL, arg)) {
            tcg_out_opc_reg(s, OPC_JALR, TCG_REG_RA, TCG_REG_T9, 0);
        }
    }
}

static void tcg_out_call(TCGContext *s, tcg_insn_unit *arg)
{
    tcg_out_call_int(s, arg, false);
    tcg_out_nop(s);
}

#if defined(CONFIG_SOFTMMU)
#include "tcg-ldst.inc.c"

static void * const qemu_ld_helpers[16] = {
    [MO_UB]   = helper_ret_ldub_mmu,
    [MO_SB]   = helper_ret_ldsb_mmu,
    [MO_LEUW] = helper_le_lduw_mmu,
    [MO_LESW] = helper_le_ldsw_mmu,
    [MO_LEUL] = helper_le_ldul_mmu,
    [MO_LEQ]  = helper_le_ldq_mmu,
    [MO_BEUW] = helper_be_lduw_mmu,
    [MO_BESW] = helper_be_ldsw_mmu,
    [MO_BEUL] = helper_be_ldul_mmu,
    [MO_BEQ]  = helper_be_ldq_mmu,
#if TCG_TARGET_REG_BITS == 64
    [MO_LESL] = helper_le_ldsl_mmu,
    [MO_BESL] = helper_be_ldsl_mmu,
#endif
};

static void * const qemu_st_helpers[16] = {
    [MO_UB]   = helper_ret_stb_mmu,
    [MO_LEUW] = helper_le_stw_mmu,
    [MO_LEUL] = helper_le_stl_mmu,
    [MO_LEQ]  = helper_le_stq_mmu,
    [MO_BEUW] = helper_be_stw_mmu,
    [MO_BEUL] = helper_be_stl_mmu,
    [MO_BEQ]  = helper_be_stq_mmu,
};

/* Helper routines for marshalling helper function arguments into
 * the correct registers and stack.
 * I is where we want to put this argument, and is updated and returned
 * for the next call. ARG is the argument itself.
 *
 * We provide routines for arguments which are: immediate, 32 bit
 * value in register, 16 and 8 bit values in register (which must be zero
 * extended before use) and 64 bit value in a lo:hi register pair.
 */

static int tcg_out_call_iarg_reg(TCGContext *s, int i, TCGReg arg)
{
    if (i < ARRAY_SIZE(tcg_target_call_iarg_regs)) {
        tcg_out_mov(s, TCG_TYPE_REG, tcg_target_call_iarg_regs[i], arg);
    } else {
        /* For N32 and N64, the initial offset is different.  But there
           we also have 8 argument register so we don't run out here.  */
        tcg_debug_assert(TCG_TARGET_REG_BITS == 32);
        tcg_out_st(s, TCG_TYPE_REG, arg, TCG_REG_SP, 4 * i);
    }
    return i + 1;
}

static int tcg_out_call_iarg_reg8(TCGContext *s, int i, TCGReg arg)
{
    TCGReg tmp = TCG_TMP0;
    if (i < ARRAY_SIZE(tcg_target_call_iarg_regs)) {
        tmp = tcg_target_call_iarg_regs[i];
    }
    tcg_out_opc_imm(s, OPC_ANDI, tmp, arg, 0xff);
    return tcg_out_call_iarg_reg(s, i, tmp);
}

static int tcg_out_call_iarg_reg16(TCGContext *s, int i, TCGReg arg)
{
    TCGReg tmp = TCG_TMP0;
    if (i < ARRAY_SIZE(tcg_target_call_iarg_regs)) {
        tmp = tcg_target_call_iarg_regs[i];
    }
    tcg_out_opc_imm(s, OPC_ANDI, tmp, arg, 0xffff);
    return tcg_out_call_iarg_reg(s, i, tmp);
}

static int tcg_out_call_iarg_imm(TCGContext *s, int i, TCGArg arg)
{
    TCGReg tmp = TCG_TMP0;
    if (arg == 0) {
        tmp = TCG_REG_ZERO;
    } else {
        if (i < ARRAY_SIZE(tcg_target_call_iarg_regs)) {
            tmp = tcg_target_call_iarg_regs[i];
        }
        tcg_out_movi(s, TCG_TYPE_REG, tmp, arg);
    }
    return tcg_out_call_iarg_reg(s, i, tmp);
}

static int tcg_out_call_iarg_reg2(TCGContext *s, int i, TCGReg al, TCGReg ah)
{
    tcg_debug_assert(TCG_TARGET_REG_BITS == 32);
    i = (i + 1) & ~1;
    i = tcg_out_call_iarg_reg(s, i, (MIPS_BE ? ah : al));
    i = tcg_out_call_iarg_reg(s, i, (MIPS_BE ? al : ah));
    return i;
}

/* We expect to use a 16-bit negative offset from ENV.  */
QEMU_BUILD_BUG_ON(TLB_MASK_TABLE_OFS(0) > 0);
QEMU_BUILD_BUG_ON(TLB_MASK_TABLE_OFS(0) < -32768);

/*
 * Perform the tlb comparison operation.
 * The complete host address is placed in BASE.
 * Clobbers TMP0, TMP1, TMP2, TMP3.
 */
static void tcg_out_tlb_load(TCGContext *s, TCGReg base, TCGReg addrl,
                             TCGReg addrh, TCGMemOpIdx oi,
                             tcg_insn_unit *label_ptr[2], bool is_load)
{
    MemOp opc = get_memop(oi);
    unsigned s_bits = opc & MO_SIZE;
    unsigned a_bits = get_alignment_bits(opc);
    int mem_index = get_mmuidx(oi);
    int fast_off = TLB_MASK_TABLE_OFS(mem_index);
    int mask_off = fast_off + offsetof(CPUTLBDescFast, mask);
    int table_off = fast_off + offsetof(CPUTLBDescFast, table);
    int add_off = offsetof(CPUTLBEntry, addend);
    int cmp_off = (is_load ? offsetof(CPUTLBEntry, addr_read)
                   : offsetof(CPUTLBEntry, addr_write));
    target_ulong mask;

    /* Load tlb_mask[mmu_idx] and tlb_table[mmu_idx].  */
    tcg_out_ld(s, TCG_TYPE_PTR, TCG_TMP0, TCG_AREG0, mask_off);
    tcg_out_ld(s, TCG_TYPE_PTR, TCG_TMP1, TCG_AREG0, table_off);

    /* Extract the TLB index from the address into TMP3.  */
    tcg_out_opc_sa(s, ALIAS_TSRL, TCG_TMP3, addrl,
                   TARGET_PAGE_BITS - CPU_TLB_ENTRY_BITS);
    tcg_out_opc_reg(s, OPC_AND, TCG_TMP3, TCG_TMP3, TCG_TMP0);

    /* Add the tlb_table pointer, creating the CPUTLBEntry address in TMP3.  */
    tcg_out_opc_reg(s, ALIAS_PADD, TCG_TMP3, TCG_TMP3, TCG_TMP1);

    /* We don't currently support unaligned accesses.
       We could do so with mips32r6.  */
    if (a_bits < s_bits) {
        a_bits = s_bits;
    }

    /* Mask the page bits, keeping the alignment bits to compare against.  */
    mask = (target_ulong)TARGET_PAGE_MASK | ((1 << a_bits) - 1);

    /* Load the (low-half) tlb comparator.  */
    if (TCG_TARGET_REG_BITS < TARGET_LONG_BITS) {
        tcg_out_ld(s, TCG_TYPE_I32, TCG_TMP0, TCG_TMP3, cmp_off + LO_OFF);
        tcg_out_movi(s, TCG_TYPE_I32, TCG_TMP1, mask);
    } else {
        tcg_out_ldst(s, (TARGET_LONG_BITS == 64 ? OPC_LD
                         : TCG_TARGET_REG_BITS == 64 ? OPC_LWU : OPC_LW),
                     TCG_TMP0, TCG_TMP3, cmp_off);
        tcg_out_movi(s, TCG_TYPE_TL, TCG_TMP1, mask);
        /* No second compare is required here;
           load the tlb addend for the fast path.  */
        tcg_out_ld(s, TCG_TYPE_PTR, TCG_TMP2, TCG_TMP3, add_off);
    }
    tcg_out_opc_reg(s, OPC_AND, TCG_TMP1, TCG_TMP1, addrl);

    /* Zero extend a 32-bit guest address for a 64-bit host. */
    if (TCG_TARGET_REG_BITS > TARGET_LONG_BITS) {
        tcg_out_ext32u(s, base, addrl);
        addrl = base;
    }

    label_ptr[0] = s->code_ptr;
    tcg_out_opc_br(s, OPC_BNE, TCG_TMP1, TCG_TMP0);

    /* Load and test the high half tlb comparator.  */
    if (TCG_TARGET_REG_BITS < TARGET_LONG_BITS) {
        /* delay slot */
        tcg_out_ld(s, TCG_TYPE_I32, TCG_TMP0, TCG_TMP3, cmp_off + HI_OFF);

        /* Load the tlb addend for the fast path.  */
        tcg_out_ld(s, TCG_TYPE_PTR, TCG_TMP2, TCG_TMP3, add_off);

        label_ptr[1] = s->code_ptr;
        tcg_out_opc_br(s, OPC_BNE, addrh, TCG_TMP0);
    }

    /* delay slot */
    tcg_out_opc_reg(s, ALIAS_PADD, base, TCG_TMP2, addrl);
}

static void add_qemu_ldst_label(TCGContext *s, int is_ld, TCGMemOpIdx oi,
                                TCGType ext,
                                TCGReg datalo, TCGReg datahi,
                                TCGReg addrlo, TCGReg addrhi,
                                void *raddr, tcg_insn_unit *label_ptr[2])
{
    TCGLabelQemuLdst *label = new_ldst_label(s);

    label->is_ld = is_ld;
    label->oi = oi;
    label->type = ext;
    label->datalo_reg = datalo;
    label->datahi_reg = datahi;
    label->addrlo_reg = addrlo;
    label->addrhi_reg = addrhi;
    label->raddr = raddr;
    label->label_ptr[0] = label_ptr[0];
    if (TCG_TARGET_REG_BITS < TARGET_LONG_BITS) {
        label->label_ptr[1] = label_ptr[1];
    }
}

static bool tcg_out_qemu_ld_slow_path(TCGContext *s, TCGLabelQemuLdst *l)
{
    TCGMemOpIdx oi = l->oi;
    MemOp opc = get_memop(oi);
    TCGReg v0;
    int i;

    /* resolve label address */
    reloc_pc16(l->label_ptr[0], s->code_ptr);
    if (TCG_TARGET_REG_BITS < TARGET_LONG_BITS) {
        reloc_pc16(l->label_ptr[1], s->code_ptr);
    }

    i = 1;
    if (TCG_TARGET_REG_BITS < TARGET_LONG_BITS) {
        i = tcg_out_call_iarg_reg2(s, i, l->addrlo_reg, l->addrhi_reg);
    } else {
        i = tcg_out_call_iarg_reg(s, i, l->addrlo_reg);
    }
    i = tcg_out_call_iarg_imm(s, i, oi);
    i = tcg_out_call_iarg_imm(s, i, (intptr_t)l->raddr);
    tcg_out_call_int(s, qemu_ld_helpers[opc & (MO_BSWAP | MO_SSIZE)], false);
    /* delay slot */
    tcg_out_mov(s, TCG_TYPE_PTR, tcg_target_call_iarg_regs[0], TCG_AREG0);

    v0 = l->datalo_reg;
    if (TCG_TARGET_REG_BITS == 32 && (opc & MO_SIZE) == MO_64) {
        /* We eliminated V0 from the possible output registers, so it
           cannot be clobbered here.  So we must move V1 first.  */
        if (MIPS_BE) {
            tcg_out_mov(s, TCG_TYPE_I32, v0, TCG_REG_V1);
            v0 = l->datahi_reg;
        } else {
            tcg_out_mov(s, TCG_TYPE_I32, l->datahi_reg, TCG_REG_V1);
        }
    }

    tcg_out_opc_br(s, OPC_BEQ, TCG_REG_ZERO, TCG_REG_ZERO);
    reloc_pc16(s->code_ptr - 1, l->raddr);

    /* delay slot */
    if (TCG_TARGET_REG_BITS == 64 && l->type == TCG_TYPE_I32) {
        /* we always sign-extend 32-bit loads */
        tcg_out_opc_sa(s, OPC_SLL, v0, TCG_REG_V0, 0);
    } else {
        tcg_out_opc_reg(s, OPC_OR, v0, TCG_REG_V0, TCG_REG_ZERO);
    }
    return true;
}

static bool tcg_out_qemu_st_slow_path(TCGContext *s, TCGLabelQemuLdst *l)
{
    TCGMemOpIdx oi = l->oi;
    MemOp opc = get_memop(oi);
    MemOp s_bits = opc & MO_SIZE;
    int i;

    /* resolve label address */
    reloc_pc16(l->label_ptr[0], s->code_ptr);
    if (TCG_TARGET_REG_BITS < TARGET_LONG_BITS) {
        reloc_pc16(l->label_ptr[1], s->code_ptr);
    }

    i = 1;
    if (TCG_TARGET_REG_BITS < TARGET_LONG_BITS) {
        i = tcg_out_call_iarg_reg2(s, i, l->addrlo_reg, l->addrhi_reg);
    } else {
        i = tcg_out_call_iarg_reg(s, i, l->addrlo_reg);
    }
    switch (s_bits) {
    case MO_8:
        i = tcg_out_call_iarg_reg8(s, i, l->datalo_reg);
        break;
    case MO_16:
        i = tcg_out_call_iarg_reg16(s, i, l->datalo_reg);
        break;
    case MO_32:
        i = tcg_out_call_iarg_reg(s, i, l->datalo_reg);
        break;
    case MO_64:
        if (TCG_TARGET_REG_BITS == 32) {
            i = tcg_out_call_iarg_reg2(s, i, l->datalo_reg, l->datahi_reg);
        } else {
            i = tcg_out_call_iarg_reg(s, i, l->datalo_reg);
        }
        break;
    default:
        tcg_abort();
    }
    i = tcg_out_call_iarg_imm(s, i, oi);

    /* Tail call to the store helper.  Thus force the return address
       computation to take place in the return address register.  */
    tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_RA, (intptr_t)l->raddr);
    i = tcg_out_call_iarg_reg(s, i, TCG_REG_RA);
    tcg_out_call_int(s, qemu_st_helpers[opc & (MO_BSWAP | MO_SIZE)], true);
    /* delay slot */
    tcg_out_mov(s, TCG_TYPE_PTR, tcg_target_call_iarg_regs[0], TCG_AREG0);
    return true;
}
#endif

static void tcg_out_qemu_ld_direct(TCGContext *s, TCGReg lo, TCGReg hi,
                                   TCGReg base, MemOp opc, bool is_64)
{
    switch (opc & (MO_SSIZE | MO_BSWAP)) {
    case MO_UB:
        tcg_out_opc_imm(s, OPC_LBU, lo, base, 0);
        break;
    case MO_SB:
        tcg_out_opc_imm(s, OPC_LB, lo, base, 0);
        break;
    case MO_UW | MO_BSWAP:
        tcg_out_opc_imm(s, OPC_LHU, TCG_TMP1, base, 0);
        tcg_out_bswap16(s, lo, TCG_TMP1);
        break;
    case MO_UW:
        tcg_out_opc_imm(s, OPC_LHU, lo, base, 0);
        break;
    case MO_SW | MO_BSWAP:
        tcg_out_opc_imm(s, OPC_LHU, TCG_TMP1, base, 0);
        tcg_out_bswap16s(s, lo, TCG_TMP1);
        break;
    case MO_SW:
        tcg_out_opc_imm(s, OPC_LH, lo, base, 0);
        break;
    case MO_UL | MO_BSWAP:
        if (TCG_TARGET_REG_BITS == 64 && is_64) {
            if (use_mips32r2_instructions) {
                tcg_out_opc_imm(s, OPC_LWU, lo, base, 0);
                tcg_out_bswap32u(s, lo, lo);
            } else {
                tcg_out_bswap_subr(s, bswap32u_addr);
                /* delay slot */
                tcg_out_opc_imm(s, OPC_LWU, TCG_TMP0, base, 0);
                tcg_out_mov(s, TCG_TYPE_I64, lo, TCG_TMP3);
            }
            break;
        }
        /* FALLTHRU */
    case MO_SL | MO_BSWAP:
        if (use_mips32r2_instructions) {
            tcg_out_opc_imm(s, OPC_LW, lo, base, 0);
            tcg_out_bswap32(s, lo, lo);
        } else {
            tcg_out_bswap_subr(s, bswap32_addr);
            /* delay slot */
            tcg_out_opc_imm(s, OPC_LW, TCG_TMP0, base, 0);
            tcg_out_mov(s, TCG_TYPE_I32, lo, TCG_TMP3);
        }
        break;
    case MO_UL:
        if (TCG_TARGET_REG_BITS == 64 && is_64) {
            tcg_out_opc_imm(s, OPC_LWU, lo, base, 0);
            break;
        }
        /* FALLTHRU */
    case MO_SL:
        tcg_out_opc_imm(s, OPC_LW, lo, base, 0);
        break;
    case MO_Q | MO_BSWAP:
        if (TCG_TARGET_REG_BITS == 64) {
            if (use_mips32r2_instructions) {
                tcg_out_opc_imm(s, OPC_LD, lo, base, 0);
                tcg_out_bswap64(s, lo, lo);
            } else {
                tcg_out_bswap_subr(s, bswap64_addr);
                /* delay slot */
                tcg_out_opc_imm(s, OPC_LD, TCG_TMP0, base, 0);
                tcg_out_mov(s, TCG_TYPE_I64, lo, TCG_TMP3);
            }
        } else if (use_mips32r2_instructions) {
            tcg_out_opc_imm(s, OPC_LW, TCG_TMP0, base, 0);
            tcg_out_opc_imm(s, OPC_LW, TCG_TMP1, base, 4);
            tcg_out_opc_reg(s, OPC_WSBH, TCG_TMP0, 0, TCG_TMP0);
            tcg_out_opc_reg(s, OPC_WSBH, TCG_TMP1, 0, TCG_TMP1);
            tcg_out_opc_sa(s, OPC_ROTR, MIPS_BE ? lo : hi, TCG_TMP0, 16);
            tcg_out_opc_sa(s, OPC_ROTR, MIPS_BE ? hi : lo, TCG_TMP1, 16);
        } else {
            tcg_out_bswap_subr(s, bswap32_addr);
            /* delay slot */
            tcg_out_opc_imm(s, OPC_LW, TCG_TMP0, base, 0);
            tcg_out_opc_imm(s, OPC_LW, TCG_TMP0, base, 4);
            tcg_out_bswap_subr(s, bswap32_addr);
            /* delay slot */
            tcg_out_mov(s, TCG_TYPE_I32, MIPS_BE ? lo : hi, TCG_TMP3);
            tcg_out_mov(s, TCG_TYPE_I32, MIPS_BE ? hi : lo, TCG_TMP3);
        }
        break;
    case MO_Q:
        /* Prefer to load from offset 0 first, but allow for overlap.  */
        if (TCG_TARGET_REG_BITS == 64) {
            tcg_out_opc_imm(s, OPC_LD, lo, base, 0);
        } else if (MIPS_BE ? hi != base : lo == base) {
            tcg_out_opc_imm(s, OPC_LW, hi, base, HI_OFF);
            tcg_out_opc_imm(s, OPC_LW, lo, base, LO_OFF);
        } else {
            tcg_out_opc_imm(s, OPC_LW, lo, base, LO_OFF);
            tcg_out_opc_imm(s, OPC_LW, hi, base, HI_OFF);
        }
        break;
    default:
        tcg_abort();
    }
}

static void tcg_out_qemu_ld(TCGContext *s, const TCGArg *args, bool is_64)
{
    TCGReg addr_regl, addr_regh __attribute__((unused));
    TCGReg data_regl, data_regh;
    TCGMemOpIdx oi;
    MemOp opc;
#if defined(CONFIG_SOFTMMU)
    tcg_insn_unit *label_ptr[2];
#endif
    TCGReg base = TCG_REG_A0;

    data_regl = *args++;
    data_regh = (TCG_TARGET_REG_BITS == 32 && is_64 ? *args++ : 0);
    addr_regl = *args++;
    addr_regh = (TCG_TARGET_REG_BITS < TARGET_LONG_BITS ? *args++ : 0);
    oi = *args++;
    opc = get_memop(oi);

#if defined(CONFIG_SOFTMMU)
    tcg_out_tlb_load(s, base, addr_regl, addr_regh, oi, label_ptr, 1);
    tcg_out_qemu_ld_direct(s, data_regl, data_regh, base, opc, is_64);
    add_qemu_ldst_label(s, 1, oi,
                        (is_64 ? TCG_TYPE_I64 : TCG_TYPE_I32),
                        data_regl, data_regh, addr_regl, addr_regh,
                        s->code_ptr, label_ptr);
#else
    if (TCG_TARGET_REG_BITS > TARGET_LONG_BITS) {
        tcg_out_ext32u(s, base, addr_regl);
        addr_regl = base;
    }
    if (guest_base == 0 && data_regl != addr_regl) {
        base = addr_regl;
    } else if (guest_base == (int16_t)guest_base) {
        tcg_out_opc_imm(s, ALIAS_PADDI, base, addr_regl, guest_base);
    } else {
        tcg_out_opc_reg(s, ALIAS_PADD, base, TCG_GUEST_BASE_REG, addr_regl);
    }
    tcg_out_qemu_ld_direct(s, data_regl, data_regh, base, opc, is_64);
#endif
}

static void tcg_out_qemu_st_direct(TCGContext *s, TCGReg lo, TCGReg hi,
                                   TCGReg base, MemOp opc)
{
    /* Don't clutter the code below with checks to avoid bswapping ZERO.  */
    if ((lo | hi) == 0) {
        opc &= ~MO_BSWAP;
    }

    switch (opc & (MO_SIZE | MO_BSWAP)) {
    case MO_8:
        tcg_out_opc_imm(s, OPC_SB, lo, base, 0);
        break;

    case MO_16 | MO_BSWAP:
        tcg_out_opc_imm(s, OPC_ANDI, TCG_TMP1, lo, 0xffff);
        tcg_out_bswap16(s, TCG_TMP1, TCG_TMP1);
        lo = TCG_TMP1;
        /* FALLTHRU */
    case MO_16:
        tcg_out_opc_imm(s, OPC_SH, lo, base, 0);
        break;

    case MO_32 | MO_BSWAP:
        tcg_out_bswap32(s, TCG_TMP3, lo);
        lo = TCG_TMP3;
        /* FALLTHRU */
    case MO_32:
        tcg_out_opc_imm(s, OPC_SW, lo, base, 0);
        break;

    case MO_64 | MO_BSWAP:
        if (TCG_TARGET_REG_BITS == 64) {
            tcg_out_bswap64(s, TCG_TMP3, lo);
            tcg_out_opc_imm(s, OPC_SD, TCG_TMP3, base, 0);
        } else if (use_mips32r2_instructions) {
            tcg_out_opc_reg(s, OPC_WSBH, TCG_TMP0, 0, MIPS_BE ? lo : hi);
            tcg_out_opc_reg(s, OPC_WSBH, TCG_TMP1, 0, MIPS_BE ? hi : lo);
            tcg_out_opc_sa(s, OPC_ROTR, TCG_TMP0, TCG_TMP0, 16);
            tcg_out_opc_sa(s, OPC_ROTR, TCG_TMP1, TCG_TMP1, 16);
            tcg_out_opc_imm(s, OPC_SW, TCG_TMP0, base, 0);
            tcg_out_opc_imm(s, OPC_SW, TCG_TMP1, base, 4);
        } else {
            tcg_out_bswap32(s, TCG_TMP3, MIPS_BE ? lo : hi);
            tcg_out_opc_imm(s, OPC_SW, TCG_TMP3, base, 0);
            tcg_out_bswap32(s, TCG_TMP3, MIPS_BE ? hi : lo);
            tcg_out_opc_imm(s, OPC_SW, TCG_TMP3, base, 4);
        }
        break;
    case MO_64:
        if (TCG_TARGET_REG_BITS == 64) {
            tcg_out_opc_imm(s, OPC_SD, lo, base, 0);
        } else {
            tcg_out_opc_imm(s, OPC_SW, MIPS_BE ? hi : lo, base, 0);
            tcg_out_opc_imm(s, OPC_SW, MIPS_BE ? lo : hi, base, 4);
        }
        break;

    default:
        tcg_abort();
    }
}

static void tcg_out_qemu_st(TCGContext *s, const TCGArg *args, bool is_64)
{
    TCGReg addr_regl, addr_regh __attribute__((unused));
    TCGReg data_regl, data_regh;
    TCGMemOpIdx oi;
    MemOp opc;
#if defined(CONFIG_SOFTMMU)
    tcg_insn_unit *label_ptr[2];
#endif
    TCGReg base = TCG_REG_A0;

    data_regl = *args++;
    data_regh = (TCG_TARGET_REG_BITS == 32 && is_64 ? *args++ : 0);
    addr_regl = *args++;
    addr_regh = (TCG_TARGET_REG_BITS < TARGET_LONG_BITS ? *args++ : 0);
    oi = *args++;
    opc = get_memop(oi);

#if defined(CONFIG_SOFTMMU)
    tcg_out_tlb_load(s, base, addr_regl, addr_regh, oi, label_ptr, 0);
    tcg_out_qemu_st_direct(s, data_regl, data_regh, base, opc);
    add_qemu_ldst_label(s, 0, oi,
                        (is_64 ? TCG_TYPE_I64 : TCG_TYPE_I32),
                        data_regl, data_regh, addr_regl, addr_regh,
                        s->code_ptr, label_ptr);
#else
    base = TCG_REG_A0;
    if (TCG_TARGET_REG_BITS > TARGET_LONG_BITS) {
        tcg_out_ext32u(s, base, addr_regl);
        addr_regl = base;
    }
    if (guest_base == 0) {
        base = addr_regl;
    } else if (guest_base == (int16_t)guest_base) {
        tcg_out_opc_imm(s, ALIAS_PADDI, base, addr_regl, guest_base);
    } else {
        tcg_out_opc_reg(s, ALIAS_PADD, base, TCG_GUEST_BASE_REG, addr_regl);
    }
    tcg_out_qemu_st_direct(s, data_regl, data_regh, base, opc);
#endif
}

static void tcg_out_mb(TCGContext *s, TCGArg a0)
{
    static const MIPSInsn sync[] = {
        /* Note that SYNC_MB is a slightly weaker than SYNC 0,
           as the former is an ordering barrier and the latter
           is a completion barrier.  */
        [0 ... TCG_MO_ALL]            = OPC_SYNC_MB,
        [TCG_MO_LD_LD]                = OPC_SYNC_RMB,
        [TCG_MO_ST_ST]                = OPC_SYNC_WMB,
        [TCG_MO_LD_ST]                = OPC_SYNC_RELEASE,
        [TCG_MO_LD_ST | TCG_MO_ST_ST] = OPC_SYNC_RELEASE,
        [TCG_MO_LD_ST | TCG_MO_LD_LD] = OPC_SYNC_ACQUIRE,
    };
    tcg_out32(s, sync[a0 & TCG_MO_ALL]);
}

static void tcg_out_clz(TCGContext *s, MIPSInsn opcv2, MIPSInsn opcv6,
                        int width, TCGReg a0, TCGReg a1, TCGArg a2)
{
    if (use_mips32r6_instructions) {
        if (a2 == width) {
            tcg_out_opc_reg(s, opcv6, a0, a1, 0);
        } else {
            tcg_out_opc_reg(s, opcv6, TCG_TMP0, a1, 0);
            tcg_out_movcond(s, TCG_COND_EQ, a0, a1, 0, a2, TCG_TMP0);
        }
    } else {
        if (a2 == width) {
            tcg_out_opc_reg(s, opcv2, a0, a1, a1);
        } else if (a0 == a2) {
            tcg_out_opc_reg(s, opcv2, TCG_TMP0, a1, a1);
            tcg_out_opc_reg(s, OPC_MOVN, a0, TCG_TMP0, a1);
        } else if (a0 != a1) {
            tcg_out_opc_reg(s, opcv2, a0, a1, a1);
            tcg_out_opc_reg(s, OPC_MOVZ, a0, a2, a1);
        } else {
            tcg_out_opc_reg(s, opcv2, TCG_TMP0, a1, a1);
            tcg_out_opc_reg(s, OPC_MOVZ, TCG_TMP0, a2, a1);
            tcg_out_mov(s, TCG_TYPE_REG, a0, TCG_TMP0);
        }
    }
}

static inline void tcg_out_op(TCGContext *s, TCGOpcode opc,
                              const TCGArg *args, const int *const_args)
{
    MIPSInsn i1, i2;
    TCGArg a0, a1, a2;
    int c2;

    a0 = args[0];
    a1 = args[1];
    a2 = args[2];
    c2 = const_args[2];

    switch (opc) {
    case INDEX_op_exit_tb:
        {
            TCGReg b0 = TCG_REG_ZERO;

            a0 = (intptr_t)a0;
            if (a0 & ~0xffff) {
                tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_V0, a0 & ~0xffff);
                b0 = TCG_REG_V0;
            }
            if (!tcg_out_opc_jmp(s, OPC_J, tb_ret_addr)) {
                tcg_out_movi(s, TCG_TYPE_PTR, TCG_TMP0,
                             (uintptr_t)tb_ret_addr);
                tcg_out_opc_reg(s, OPC_JR, 0, TCG_TMP0, 0);
            }
            tcg_out_opc_imm(s, OPC_ORI, TCG_REG_V0, b0, a0 & 0xffff);
        }
        break;
    case INDEX_op_goto_tb:
        if (s->tb_jmp_insn_offset) {
            /* direct jump method */
            s->tb_jmp_insn_offset[a0] = tcg_current_code_size(s);
            /* Avoid clobbering the address during retranslation.  */
            tcg_out32(s, OPC_J | (*(uint32_t *)s->code_ptr & 0x3ffffff));
        } else {
            /* indirect jump method */
            tcg_out_ld(s, TCG_TYPE_PTR, TCG_TMP0, TCG_REG_ZERO,
                       (uintptr_t)(s->tb_jmp_target_addr + a0));
            tcg_out_opc_reg(s, OPC_JR, 0, TCG_TMP0, 0);
        }
        tcg_out_nop(s);
        set_jmp_reset_offset(s, a0);
        break;
    case INDEX_op_goto_ptr:
        /* jmp to the given host address (could be epilogue) */
        tcg_out_opc_reg(s, OPC_JR, 0, a0, 0);
        tcg_out_nop(s);
        break;
    case INDEX_op_br:
        tcg_out_brcond(s, TCG_COND_EQ, TCG_REG_ZERO, TCG_REG_ZERO,
                       arg_label(a0));
        break;

    case INDEX_op_ld8u_i32:
    case INDEX_op_ld8u_i64:
        i1 = OPC_LBU;
        goto do_ldst;
    case INDEX_op_ld8s_i32:
    case INDEX_op_ld8s_i64:
        i1 = OPC_LB;
        goto do_ldst;
    case INDEX_op_ld16u_i32:
    case INDEX_op_ld16u_i64:
        i1 = OPC_LHU;
        goto do_ldst;
    case INDEX_op_ld16s_i32:
    case INDEX_op_ld16s_i64:
        i1 = OPC_LH;
        goto do_ldst;
    case INDEX_op_ld_i32:
    case INDEX_op_ld32s_i64:
        i1 = OPC_LW;
        goto do_ldst;
    case INDEX_op_ld32u_i64:
        i1 = OPC_LWU;
        goto do_ldst;
    case INDEX_op_ld_i64:
        i1 = OPC_LD;
        goto do_ldst;
    case INDEX_op_st8_i32:
    case INDEX_op_st8_i64:
        i1 = OPC_SB;
        goto do_ldst;
    case INDEX_op_st16_i32:
    case INDEX_op_st16_i64:
        i1 = OPC_SH;
        goto do_ldst;
    case INDEX_op_st_i32:
    case INDEX_op_st32_i64:
        i1 = OPC_SW;
        goto do_ldst;
    case INDEX_op_st_i64:
        i1 = OPC_SD;
    do_ldst:
        tcg_out_ldst(s, i1, a0, a1, a2);
        break;

    case INDEX_op_add_i32:
        i1 = OPC_ADDU, i2 = OPC_ADDIU;
        goto do_binary;
    case INDEX_op_add_i64:
        i1 = OPC_DADDU, i2 = OPC_DADDIU;
        goto do_binary;
    case INDEX_op_or_i32:
    case INDEX_op_or_i64:
        i1 = OPC_OR, i2 = OPC_ORI;
        goto do_binary;
    case INDEX_op_xor_i32:
    case INDEX_op_xor_i64:
        i1 = OPC_XOR, i2 = OPC_XORI;
    do_binary:
        if (c2) {
            tcg_out_opc_imm(s, i2, a0, a1, a2);
            break;
        }
    do_binaryv:
        tcg_out_opc_reg(s, i1, a0, a1, a2);
        break;

    case INDEX_op_sub_i32:
        i1 = OPC_SUBU, i2 = OPC_ADDIU;
        goto do_subtract;
    case INDEX_op_sub_i64:
        i1 = OPC_DSUBU, i2 = OPC_DADDIU;
    do_subtract:
        if (c2) {
            tcg_out_opc_imm(s, i2, a0, a1, -a2);
            break;
        }
        goto do_binaryv;
    case INDEX_op_and_i32:
        if (c2 && a2 != (uint16_t)a2) {
            int msb = ctz32(~a2) - 1;
            tcg_debug_assert(use_mips32r2_instructions);
            tcg_debug_assert(is_p2m1(a2));
            tcg_out_opc_bf(s, OPC_EXT, a0, a1, msb, 0);
            break;
        }
        i1 = OPC_AND, i2 = OPC_ANDI;
        goto do_binary;
    case INDEX_op_and_i64:
        if (c2 && a2 != (uint16_t)a2) {
            int msb = ctz64(~a2) - 1;
            tcg_debug_assert(use_mips32r2_instructions);
            tcg_debug_assert(is_p2m1(a2));
            tcg_out_opc_bf64(s, OPC_DEXT, OPC_DEXTM, OPC_DEXTU, a0, a1, msb, 0);
            break;
        }
        i1 = OPC_AND, i2 = OPC_ANDI;
        goto do_binary;
    case INDEX_op_nor_i32:
    case INDEX_op_nor_i64:
        i1 = OPC_NOR;
        goto do_binaryv;

    case INDEX_op_mul_i32:
        if (use_mips32_instructions) {
            tcg_out_opc_reg(s, OPC_MUL, a0, a1, a2);
            break;
        }
        i1 = OPC_MULT, i2 = OPC_MFLO;
        goto do_hilo1;
    case INDEX_op_mulsh_i32:
        if (use_mips32r6_instructions) {
            tcg_out_opc_reg(s, OPC_MUH, a0, a1, a2);
            break;
        }
        i1 = OPC_MULT, i2 = OPC_MFHI;
        goto do_hilo1;
    case INDEX_op_muluh_i32:
        if (use_mips32r6_instructions) {
            tcg_out_opc_reg(s, OPC_MUHU, a0, a1, a2);
            break;
        }
        i1 = OPC_MULTU, i2 = OPC_MFHI;
        goto do_hilo1;
    case INDEX_op_div_i32:
        if (use_mips32r6_instructions) {
            tcg_out_opc_reg(s, OPC_DIV_R6, a0, a1, a2);
            break;
        }
        i1 = OPC_DIV, i2 = OPC_MFLO;
        goto do_hilo1;
    case INDEX_op_divu_i32:
        if (use_mips32r6_instructions) {
            tcg_out_opc_reg(s, OPC_DIVU_R6, a0, a1, a2);
            break;
        }
        i1 = OPC_DIVU, i2 = OPC_MFLO;
        goto do_hilo1;
    case INDEX_op_rem_i32:
        if (use_mips32r6_instructions) {
            tcg_out_opc_reg(s, OPC_MOD, a0, a1, a2);
            break;
        }
        i1 = OPC_DIV, i2 = OPC_MFHI;
        goto do_hilo1;
    case INDEX_op_remu_i32:
        if (use_mips32r6_instructions) {
            tcg_out_opc_reg(s, OPC_MODU, a0, a1, a2);
            break;
        }
        i1 = OPC_DIVU, i2 = OPC_MFHI;
        goto do_hilo1;
    case INDEX_op_mul_i64:
        if (use_mips32r6_instructions) {
            tcg_out_opc_reg(s, OPC_DMUL, a0, a1, a2);
            break;
        }
        i1 = OPC_DMULT, i2 = OPC_MFLO;
        goto do_hilo1;
    case INDEX_op_mulsh_i64:
        if (use_mips32r6_instructions) {
            tcg_out_opc_reg(s, OPC_DMUH, a0, a1, a2);
            break;
        }
        i1 = OPC_DMULT, i2 = OPC_MFHI;
        goto do_hilo1;
    case INDEX_op_muluh_i64:
        if (use_mips32r6_instructions) {
            tcg_out_opc_reg(s, OPC_DMUHU, a0, a1, a2);
            break;
        }
        i1 = OPC_DMULTU, i2 = OPC_MFHI;
        goto do_hilo1;
    case INDEX_op_div_i64:
        if (use_mips32r6_instructions) {
            tcg_out_opc_reg(s, OPC_DDIV_R6, a0, a1, a2);
            break;
        }
        i1 = OPC_DDIV, i2 = OPC_MFLO;
        goto do_hilo1;
    case INDEX_op_divu_i64:
        if (use_mips32r6_instructions) {
            tcg_out_opc_reg(s, OPC_DDIVU_R6, a0, a1, a2);
            break;
        }
        i1 = OPC_DDIVU, i2 = OPC_MFLO;
        goto do_hilo1;
    case INDEX_op_rem_i64:
        if (use_mips32r6_instructions) {
            tcg_out_opc_reg(s, OPC_DMOD, a0, a1, a2);
            break;
        }
        i1 = OPC_DDIV, i2 = OPC_MFHI;
        goto do_hilo1;
    case INDEX_op_remu_i64:
        if (use_mips32r6_instructions) {
            tcg_out_opc_reg(s, OPC_DMODU, a0, a1, a2);
            break;
        }
        i1 = OPC_DDIVU, i2 = OPC_MFHI;
    do_hilo1:
        tcg_out_opc_reg(s, i1, 0, a1, a2);
        tcg_out_opc_reg(s, i2, a0, 0, 0);
        break;

    case INDEX_op_muls2_i32:
        i1 = OPC_MULT;
        goto do_hilo2;
    case INDEX_op_mulu2_i32:
        i1 = OPC_MULTU;
        goto do_hilo2;
    case INDEX_op_muls2_i64:
        i1 = OPC_DMULT;
        goto do_hilo2;
    case INDEX_op_mulu2_i64:
        i1 = OPC_DMULTU;
    do_hilo2:
        tcg_out_opc_reg(s, i1, 0, a2, args[3]);
        tcg_out_opc_reg(s, OPC_MFLO, a0, 0, 0);
        tcg_out_opc_reg(s, OPC_MFHI, a1, 0, 0);
        break;

    case INDEX_op_not_i32:
    case INDEX_op_not_i64:
        i1 = OPC_NOR;
        goto do_unary;
    case INDEX_op_bswap16_i32:
    case INDEX_op_bswap16_i64:
        i1 = OPC_WSBH;
        goto do_unary;
    case INDEX_op_ext8s_i32:
    case INDEX_op_ext8s_i64:
        i1 = OPC_SEB;
        goto do_unary;
    case INDEX_op_ext16s_i32:
    case INDEX_op_ext16s_i64:
        i1 = OPC_SEH;
    do_unary:
        tcg_out_opc_reg(s, i1, a0, TCG_REG_ZERO, a1);
        break;

    case INDEX_op_bswap32_i32:
        tcg_out_bswap32(s, a0, a1);
        break;
    case INDEX_op_bswap32_i64:
        tcg_out_bswap32u(s, a0, a1);
        break;
    case INDEX_op_bswap64_i64:
        tcg_out_bswap64(s, a0, a1);
        break;
    case INDEX_op_extrh_i64_i32:
        tcg_out_dsra(s, a0, a1, 32);
        break;
    case INDEX_op_ext32s_i64:
    case INDEX_op_ext_i32_i64:
    case INDEX_op_extrl_i64_i32:
        tcg_out_opc_sa(s, OPC_SLL, a0, a1, 0);
        break;
    case INDEX_op_ext32u_i64:
    case INDEX_op_extu_i32_i64:
        tcg_out_ext32u(s, a0, a1);
        break;

    case INDEX_op_sar_i32:
        i1 = OPC_SRAV, i2 = OPC_SRA;
        goto do_shift;
    case INDEX_op_shl_i32:
        i1 = OPC_SLLV, i2 = OPC_SLL;
        goto do_shift;
    case INDEX_op_shr_i32:
        i1 = OPC_SRLV, i2 = OPC_SRL;
        goto do_shift;
    case INDEX_op_rotr_i32:
        i1 = OPC_ROTRV, i2 = OPC_ROTR;
    do_shift:
        if (c2) {
            tcg_out_opc_sa(s, i2, a0, a1, a2);
            break;
        }
    do_shiftv:
        tcg_out_opc_reg(s, i1, a0, a2, a1);
        break;
    case INDEX_op_rotl_i32:
        if (c2) {
            tcg_out_opc_sa(s, OPC_ROTR, a0, a1, 32 - a2);
        } else {
            tcg_out_opc_reg(s, OPC_SUBU, TCG_TMP0, TCG_REG_ZERO, a2);
            tcg_out_opc_reg(s, OPC_ROTRV, a0, TCG_TMP0, a1);
        }
        break;
    case INDEX_op_sar_i64:
        if (c2) {
            tcg_out_dsra(s, a0, a1, a2);
            break;
        }
        i1 = OPC_DSRAV;
        goto do_shiftv;
    case INDEX_op_shl_i64:
        if (c2) {
            tcg_out_dsll(s, a0, a1, a2);
            break;
        }
        i1 = OPC_DSLLV;
        goto do_shiftv;
    case INDEX_op_shr_i64:
        if (c2) {
            tcg_out_dsrl(s, a0, a1, a2);
            break;
        }
        i1 = OPC_DSRLV;
        goto do_shiftv;
    case INDEX_op_rotr_i64:
        if (c2) {
            tcg_out_opc_sa64(s, OPC_DROTR, OPC_DROTR32, a0, a1, a2);
            break;
        }
        i1 = OPC_DROTRV;
        goto do_shiftv;
    case INDEX_op_rotl_i64:
        if (c2) {
            tcg_out_opc_sa64(s, OPC_DROTR, OPC_DROTR32, a0, a1, 64 - a2);
        } else {
            tcg_out_opc_reg(s, OPC_DSUBU, TCG_TMP0, TCG_REG_ZERO, a2);
            tcg_out_opc_reg(s, OPC_DROTRV, a0, TCG_TMP0, a1);
        }
        break;

    case INDEX_op_clz_i32:
        tcg_out_clz(s, OPC_CLZ, OPC_CLZ_R6, 32, a0, a1, a2);
        break;
    case INDEX_op_clz_i64:
        tcg_out_clz(s, OPC_DCLZ, OPC_DCLZ_R6, 64, a0, a1, a2);
        break;

    case INDEX_op_deposit_i32:
        tcg_out_opc_bf(s, OPC_INS, a0, a2, args[3] + args[4] - 1, args[3]);
        break;
    case INDEX_op_deposit_i64:
        tcg_out_opc_bf64(s, OPC_DINS, OPC_DINSM, OPC_DINSU, a0, a2,
                         args[3] + args[4] - 1, args[3]);
        break;
    case INDEX_op_extract_i32:
        tcg_out_opc_bf(s, OPC_EXT, a0, a1, args[3] - 1, a2);
        break;
    case INDEX_op_extract_i64:
        tcg_out_opc_bf64(s, OPC_DEXT, OPC_DEXTM, OPC_DEXTU, a0, a1,
                         args[3] - 1, a2);
        break;

    case INDEX_op_brcond_i32:
    case INDEX_op_brcond_i64:
        tcg_out_brcond(s, a2, a0, a1, arg_label(args[3]));
        break;
    case INDEX_op_brcond2_i32:
        tcg_out_brcond2(s, args[4], a0, a1, a2, args[3], arg_label(args[5]));
        break;

    case INDEX_op_movcond_i32:
    case INDEX_op_movcond_i64:
        tcg_out_movcond(s, args[5], a0, a1, a2, args[3], args[4]);
        break;

    case INDEX_op_setcond_i32:
    case INDEX_op_setcond_i64:
        tcg_out_setcond(s, args[3], a0, a1, a2);
        break;
    case INDEX_op_setcond2_i32:
        tcg_out_setcond2(s, args[5], a0, a1, a2, args[3], args[4]);
        break;

    case INDEX_op_qemu_ld_i32:
        tcg_out_qemu_ld(s, args, false);
        break;
    case INDEX_op_qemu_ld_i64:
        tcg_out_qemu_ld(s, args, true);
        break;
    case INDEX_op_qemu_st_i32:
        tcg_out_qemu_st(s, args, false);
        break;
    case INDEX_op_qemu_st_i64:
        tcg_out_qemu_st(s, args, true);
        break;

    case INDEX_op_add2_i32:
        tcg_out_addsub2(s, a0, a1, a2, args[3], args[4], args[5],
                        const_args[4], const_args[5], false);
        break;
    case INDEX_op_sub2_i32:
        tcg_out_addsub2(s, a0, a1, a2, args[3], args[4], args[5],
                        const_args[4], const_args[5], true);
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
}

static const TCGTargetOpDef *tcg_target_op_def(TCGOpcode op)
{
    static const TCGTargetOpDef r = { .args_ct_str = { "r" } };
    static const TCGTargetOpDef r_r = { .args_ct_str = { "r", "r" } };
    static const TCGTargetOpDef r_L = { .args_ct_str = { "r", "L" } };
    static const TCGTargetOpDef rZ_r = { .args_ct_str = { "rZ", "r" } };
    static const TCGTargetOpDef SZ_S = { .args_ct_str = { "SZ", "S" } };
    static const TCGTargetOpDef rZ_rZ = { .args_ct_str = { "rZ", "rZ" } };
    static const TCGTargetOpDef r_r_L = { .args_ct_str = { "r", "r", "L" } };
    static const TCGTargetOpDef r_L_L = { .args_ct_str = { "r", "L", "L" } };
    static const TCGTargetOpDef r_r_ri = { .args_ct_str = { "r", "r", "ri" } };
    static const TCGTargetOpDef r_r_rI = { .args_ct_str = { "r", "r", "rI" } };
    static const TCGTargetOpDef r_r_rJ = { .args_ct_str = { "r", "r", "rJ" } };
    static const TCGTargetOpDef SZ_S_S = { .args_ct_str = { "SZ", "S", "S" } };
    static const TCGTargetOpDef SZ_SZ_S
        = { .args_ct_str = { "SZ", "SZ", "S" } };
    static const TCGTargetOpDef SZ_SZ_S_S
        = { .args_ct_str = { "SZ", "SZ", "S", "S" } };
    static const TCGTargetOpDef r_rZ_rN
        = { .args_ct_str = { "r", "rZ", "rN" } };
    static const TCGTargetOpDef r_rZ_rZ
        = { .args_ct_str = { "r", "rZ", "rZ" } };
    static const TCGTargetOpDef r_r_rIK
        = { .args_ct_str = { "r", "r", "rIK" } };
    static const TCGTargetOpDef r_r_rWZ
        = { .args_ct_str = { "r", "r", "rWZ" } };
    static const TCGTargetOpDef r_r_r_r
        = { .args_ct_str = { "r", "r", "r", "r" } };
    static const TCGTargetOpDef r_r_L_L
        = { .args_ct_str = { "r", "r", "L", "L" } };
    static const TCGTargetOpDef dep
        = { .args_ct_str = { "r", "0", "rZ" } };
    static const TCGTargetOpDef movc
        = { .args_ct_str = { "r", "rZ", "rZ", "rZ", "0" } };
    static const TCGTargetOpDef movc_r6
        = { .args_ct_str = { "r", "rZ", "rZ", "rZ", "rZ" } };
    static const TCGTargetOpDef add2
        = { .args_ct_str = { "r", "r", "rZ", "rZ", "rN", "rN" } };
    static const TCGTargetOpDef br2
        = { .args_ct_str = { "rZ", "rZ", "rZ", "rZ" } };
    static const TCGTargetOpDef setc2
        = { .args_ct_str = { "r", "rZ", "rZ", "rZ", "rZ" } };

    switch (op) {
    case INDEX_op_goto_ptr:
        return &r;

    case INDEX_op_ld8u_i32:
    case INDEX_op_ld8s_i32:
    case INDEX_op_ld16u_i32:
    case INDEX_op_ld16s_i32:
    case INDEX_op_ld_i32:
    case INDEX_op_not_i32:
    case INDEX_op_bswap16_i32:
    case INDEX_op_bswap32_i32:
    case INDEX_op_ext8s_i32:
    case INDEX_op_ext16s_i32:
    case INDEX_op_extract_i32:
    case INDEX_op_ld8u_i64:
    case INDEX_op_ld8s_i64:
    case INDEX_op_ld16u_i64:
    case INDEX_op_ld16s_i64:
    case INDEX_op_ld32s_i64:
    case INDEX_op_ld32u_i64:
    case INDEX_op_ld_i64:
    case INDEX_op_not_i64:
    case INDEX_op_bswap16_i64:
    case INDEX_op_bswap32_i64:
    case INDEX_op_bswap64_i64:
    case INDEX_op_ext8s_i64:
    case INDEX_op_ext16s_i64:
    case INDEX_op_ext32s_i64:
    case INDEX_op_ext32u_i64:
    case INDEX_op_ext_i32_i64:
    case INDEX_op_extu_i32_i64:
    case INDEX_op_extrl_i64_i32:
    case INDEX_op_extrh_i64_i32:
    case INDEX_op_extract_i64:
        return &r_r;

    case INDEX_op_st8_i32:
    case INDEX_op_st16_i32:
    case INDEX_op_st_i32:
    case INDEX_op_st8_i64:
    case INDEX_op_st16_i64:
    case INDEX_op_st32_i64:
    case INDEX_op_st_i64:
        return &rZ_r;

    case INDEX_op_add_i32:
    case INDEX_op_add_i64:
        return &r_r_rJ;
    case INDEX_op_sub_i32:
    case INDEX_op_sub_i64:
        return &r_rZ_rN;
    case INDEX_op_mul_i32:
    case INDEX_op_mulsh_i32:
    case INDEX_op_muluh_i32:
    case INDEX_op_div_i32:
    case INDEX_op_divu_i32:
    case INDEX_op_rem_i32:
    case INDEX_op_remu_i32:
    case INDEX_op_nor_i32:
    case INDEX_op_setcond_i32:
    case INDEX_op_mul_i64:
    case INDEX_op_mulsh_i64:
    case INDEX_op_muluh_i64:
    case INDEX_op_div_i64:
    case INDEX_op_divu_i64:
    case INDEX_op_rem_i64:
    case INDEX_op_remu_i64:
    case INDEX_op_nor_i64:
    case INDEX_op_setcond_i64:
        return &r_rZ_rZ;
    case INDEX_op_muls2_i32:
    case INDEX_op_mulu2_i32:
    case INDEX_op_muls2_i64:
    case INDEX_op_mulu2_i64:
        return &r_r_r_r;
    case INDEX_op_and_i32:
    case INDEX_op_and_i64:
        return &r_r_rIK;
    case INDEX_op_or_i32:
    case INDEX_op_xor_i32:
    case INDEX_op_or_i64:
    case INDEX_op_xor_i64:
        return &r_r_rI;
    case INDEX_op_shl_i32:
    case INDEX_op_shr_i32:
    case INDEX_op_sar_i32:
    case INDEX_op_rotr_i32:
    case INDEX_op_rotl_i32:
    case INDEX_op_shl_i64:
    case INDEX_op_shr_i64:
    case INDEX_op_sar_i64:
    case INDEX_op_rotr_i64:
    case INDEX_op_rotl_i64:
        return &r_r_ri;
    case INDEX_op_clz_i32:
    case INDEX_op_clz_i64:
        return &r_r_rWZ;

    case INDEX_op_deposit_i32:
    case INDEX_op_deposit_i64:
        return &dep;
    case INDEX_op_brcond_i32:
    case INDEX_op_brcond_i64:
        return &rZ_rZ;
    case INDEX_op_movcond_i32:
    case INDEX_op_movcond_i64:
        return use_mips32r6_instructions ? &movc_r6 : &movc;

    case INDEX_op_add2_i32:
    case INDEX_op_sub2_i32:
        return &add2;
    case INDEX_op_setcond2_i32:
        return &setc2;
    case INDEX_op_brcond2_i32:
        return &br2;

    case INDEX_op_qemu_ld_i32:
        return (TCG_TARGET_REG_BITS == 64 || TARGET_LONG_BITS == 32
                ? &r_L : &r_L_L);
    case INDEX_op_qemu_st_i32:
        return (TCG_TARGET_REG_BITS == 64 || TARGET_LONG_BITS == 32
                ? &SZ_S : &SZ_S_S);
    case INDEX_op_qemu_ld_i64:
        return (TCG_TARGET_REG_BITS == 64 ? &r_L
                : TARGET_LONG_BITS == 32 ? &r_r_L : &r_r_L_L);
    case INDEX_op_qemu_st_i64:
        return (TCG_TARGET_REG_BITS == 64 ? &SZ_S
                : TARGET_LONG_BITS == 32 ? &SZ_SZ_S : &SZ_SZ_S_S);

    default:
        return NULL;
    }
}

static const int tcg_target_callee_save_regs[] = {
    TCG_REG_S0,       /* used for the global env (TCG_AREG0) */
    TCG_REG_S1,
    TCG_REG_S2,
    TCG_REG_S3,
    TCG_REG_S4,
    TCG_REG_S5,
    TCG_REG_S6,
    TCG_REG_S7,
    TCG_REG_S8,
    TCG_REG_RA,       /* should be last for ABI compliance */
};

/* The Linux kernel doesn't provide any information about the available
   instruction set. Probe it using a signal handler. */


#ifndef use_movnz_instructions
bool use_movnz_instructions = false;
#endif

#ifndef use_mips32_instructions
bool use_mips32_instructions = false;
#endif

#ifndef use_mips32r2_instructions
bool use_mips32r2_instructions = false;
#endif

static volatile sig_atomic_t got_sigill;

static void sigill_handler(int signo, siginfo_t *si, void *data)
{
    /* Skip the faulty instruction */
    ucontext_t *uc = (ucontext_t *)data;
    uc->uc_mcontext.pc += 4;

    got_sigill = 1;
}

static void tcg_target_detect_isa(void)
{
    struct sigaction sa_old, sa_new;

    memset(&sa_new, 0, sizeof(sa_new));
    sa_new.sa_flags = SA_SIGINFO;
    sa_new.sa_sigaction = sigill_handler;
    sigaction(SIGILL, &sa_new, &sa_old);

    /* Probe for movn/movz, necessary to implement movcond. */
#ifndef use_movnz_instructions
    got_sigill = 0;
    asm volatile(".set push\n"
                 ".set mips32\n"
                 "movn $zero, $zero, $zero\n"
                 "movz $zero, $zero, $zero\n"
                 ".set pop\n"
                 : : : );
    use_movnz_instructions = !got_sigill;
#endif

    /* Probe for MIPS32 instructions. As no subsetting is allowed
       by the specification, it is only necessary to probe for one
       of the instructions. */
#ifndef use_mips32_instructions
    got_sigill = 0;
    asm volatile(".set push\n"
                 ".set mips32\n"
                 "mul $zero, $zero\n"
                 ".set pop\n"
                 : : : );
    use_mips32_instructions = !got_sigill;
#endif

    /* Probe for MIPS32r2 instructions if MIPS32 instructions are
       available. As no subsetting is allowed by the specification,
       it is only necessary to probe for one of the instructions. */
#ifndef use_mips32r2_instructions
    if (use_mips32_instructions) {
        got_sigill = 0;
        asm volatile(".set push\n"
                     ".set mips32r2\n"
                     "seb $zero, $zero\n"
                     ".set pop\n"
                     : : : );
        use_mips32r2_instructions = !got_sigill;
    }
#endif

    sigaction(SIGILL, &sa_old, NULL);
}

static tcg_insn_unit *align_code_ptr(TCGContext *s)
{
    uintptr_t p = (uintptr_t)s->code_ptr;
    if (p & 15) {
        p = (p + 15) & -16;
        s->code_ptr = (void *)p;
    }
    return s->code_ptr;
}

/* Stack frame parameters.  */
#define REG_SIZE   (TCG_TARGET_REG_BITS / 8)
#define SAVE_SIZE  ((int)ARRAY_SIZE(tcg_target_callee_save_regs) * REG_SIZE)
#define TEMP_SIZE  (CPU_TEMP_BUF_NLONGS * (int)sizeof(long))

#define FRAME_SIZE ((TCG_STATIC_CALL_ARGS_SIZE + TEMP_SIZE + SAVE_SIZE \
                     + TCG_TARGET_STACK_ALIGN - 1) \
                    & -TCG_TARGET_STACK_ALIGN)
#define SAVE_OFS   (TCG_STATIC_CALL_ARGS_SIZE + TEMP_SIZE)

/* We're expecting to be able to use an immediate for frame allocation.  */
QEMU_BUILD_BUG_ON(FRAME_SIZE > 0x7fff);

/* Generate global QEMU prologue and epilogue code */
static void tcg_target_qemu_prologue(TCGContext *s)
{
    int i;

    tcg_set_frame(s, TCG_REG_SP, TCG_STATIC_CALL_ARGS_SIZE, TEMP_SIZE);

    /* TB prologue */
    tcg_out_opc_imm(s, ALIAS_PADDI, TCG_REG_SP, TCG_REG_SP, -FRAME_SIZE);
    for (i = 0; i < ARRAY_SIZE(tcg_target_callee_save_regs); i++) {
        tcg_out_st(s, TCG_TYPE_REG, tcg_target_callee_save_regs[i],
                   TCG_REG_SP, SAVE_OFS + i * REG_SIZE);
    }

#ifndef CONFIG_SOFTMMU
    if (guest_base) {
        tcg_out_movi(s, TCG_TYPE_PTR, TCG_GUEST_BASE_REG, guest_base);
        tcg_regset_set_reg(s->reserved_regs, TCG_GUEST_BASE_REG);
    }
#endif

    /* Call generated code */
    tcg_out_opc_reg(s, OPC_JR, 0, tcg_target_call_iarg_regs[1], 0);
    /* delay slot */
    tcg_out_mov(s, TCG_TYPE_PTR, TCG_AREG0, tcg_target_call_iarg_regs[0]);

    /*
     * Return path for goto_ptr. Set return value to 0, a-la exit_tb,
     * and fall through to the rest of the epilogue.
     */
    s->code_gen_epilogue = s->code_ptr;
    tcg_out_mov(s, TCG_TYPE_REG, TCG_REG_V0, TCG_REG_ZERO);

    /* TB epilogue */
    tb_ret_addr = s->code_ptr;
    for (i = 0; i < ARRAY_SIZE(tcg_target_callee_save_regs); i++) {
        tcg_out_ld(s, TCG_TYPE_REG, tcg_target_callee_save_regs[i],
                   TCG_REG_SP, SAVE_OFS + i * REG_SIZE);
    }

    tcg_out_opc_reg(s, OPC_JR, 0, TCG_REG_RA, 0);
    /* delay slot */
    tcg_out_opc_imm(s, ALIAS_PADDI, TCG_REG_SP, TCG_REG_SP, FRAME_SIZE);

    if (use_mips32r2_instructions) {
        return;
    }

    /* Bswap subroutines: Input in TCG_TMP0, output in TCG_TMP3;
       clobbers TCG_TMP1, TCG_TMP2.  */

    /*
     * bswap32 -- 32-bit swap (signed result for mips64).  a0 = abcd.
     */
    bswap32_addr = align_code_ptr(s);
    /* t3 = (ssss)d000 */
    tcg_out_opc_sa(s, OPC_SLL, TCG_TMP3, TCG_TMP0, 24);
    /* t1 = 000a */
    tcg_out_opc_sa(s, OPC_SRL, TCG_TMP1, TCG_TMP0, 24);
    /* t2 = 00c0 */
    tcg_out_opc_imm(s, OPC_ANDI, TCG_TMP2, TCG_TMP0, 0xff00);
    /* t3 = d00a */
    tcg_out_opc_reg(s, OPC_OR, TCG_TMP3, TCG_TMP3, TCG_TMP1);
    /* t1 = 0abc */
    tcg_out_opc_sa(s, OPC_SRL, TCG_TMP1, TCG_TMP0, 8);
    /* t2 = 0c00 */
    tcg_out_opc_sa(s, OPC_SLL, TCG_TMP2, TCG_TMP2, 8);
    /* t1 = 00b0 */
    tcg_out_opc_imm(s, OPC_ANDI, TCG_TMP1, TCG_TMP1, 0xff00);
    /* t3 = dc0a */
    tcg_out_opc_reg(s, OPC_OR, TCG_TMP3, TCG_TMP3, TCG_TMP2);
    tcg_out_opc_reg(s, OPC_JR, 0, TCG_REG_RA, 0);
    /* t3 = dcba -- delay slot */
    tcg_out_opc_reg(s, OPC_OR, TCG_TMP3, TCG_TMP3, TCG_TMP1);

    if (TCG_TARGET_REG_BITS == 32) {
        return;
    }

    /*
     * bswap32u -- unsigned 32-bit swap.  a0 = ....abcd.
     */
    bswap32u_addr = align_code_ptr(s);
    /* t1 = (0000)000d */
    tcg_out_opc_imm(s, OPC_ANDI, TCG_TMP1, TCG_TMP0, 0xff);
    /* t3 = 000a */
    tcg_out_opc_sa(s, OPC_SRL, TCG_TMP3, TCG_TMP0, 24);
    /* t1 = (0000)d000 */
    tcg_out_dsll(s, TCG_TMP1, TCG_TMP1, 24);
    /* t2 = 00c0 */
    tcg_out_opc_imm(s, OPC_ANDI, TCG_TMP2, TCG_TMP0, 0xff00);
    /* t3 = d00a */
    tcg_out_opc_reg(s, OPC_OR, TCG_TMP3, TCG_TMP3, TCG_TMP1);
    /* t1 = 0abc */
    tcg_out_opc_sa(s, OPC_SRL, TCG_TMP1, TCG_TMP0, 8);
    /* t2 = 0c00 */
    tcg_out_opc_sa(s, OPC_SLL, TCG_TMP2, TCG_TMP2, 8);
    /* t1 = 00b0 */
    tcg_out_opc_imm(s, OPC_ANDI, TCG_TMP1, TCG_TMP1, 0xff00);
    /* t3 = dc0a */
    tcg_out_opc_reg(s, OPC_OR, TCG_TMP3, TCG_TMP3, TCG_TMP2);
    tcg_out_opc_reg(s, OPC_JR, 0, TCG_REG_RA, 0);
    /* t3 = dcba -- delay slot */
    tcg_out_opc_reg(s, OPC_OR, TCG_TMP3, TCG_TMP3, TCG_TMP1);

    /*
     * bswap64 -- 64-bit swap.  a0 = abcdefgh
     */
    bswap64_addr = align_code_ptr(s);
    /* t3 = h0000000 */
    tcg_out_dsll(s, TCG_TMP3, TCG_TMP0, 56);
    /* t1 = 0000000a */
    tcg_out_dsrl(s, TCG_TMP1, TCG_TMP0, 56);

    /* t2 = 000000g0 */
    tcg_out_opc_imm(s, OPC_ANDI, TCG_TMP2, TCG_TMP0, 0xff00);
    /* t3 = h000000a */
    tcg_out_opc_reg(s, OPC_OR, TCG_TMP3, TCG_TMP3, TCG_TMP1);
    /* t1 = 00000abc */
    tcg_out_dsrl(s, TCG_TMP1, TCG_TMP0, 40);
    /* t2 = 0g000000 */
    tcg_out_dsll(s, TCG_TMP2, TCG_TMP2, 40);
    /* t1 = 000000b0 */
    tcg_out_opc_imm(s, OPC_ANDI, TCG_TMP1, TCG_TMP1, 0xff00);

    /* t3 = hg00000a */
    tcg_out_opc_reg(s, OPC_OR, TCG_TMP3, TCG_TMP3, TCG_TMP2);
    /* t2 = 0000abcd */
    tcg_out_dsrl(s, TCG_TMP2, TCG_TMP0, 32);
    /* t3 = hg0000ba */
    tcg_out_opc_reg(s, OPC_OR, TCG_TMP3, TCG_TMP3, TCG_TMP1);

    /* t1 = 000000c0 */
    tcg_out_opc_imm(s, OPC_ANDI, TCG_TMP1, TCG_TMP2, 0xff00);
    /* t2 = 0000000d */
    tcg_out_opc_imm(s, OPC_ANDI, TCG_TMP2, TCG_TMP2, 0x00ff);
    /* t1 = 00000c00 */
    tcg_out_dsll(s, TCG_TMP1, TCG_TMP1, 8);
    /* t2 = 0000d000 */
    tcg_out_dsll(s, TCG_TMP2, TCG_TMP2, 24);

    /* t3 = hg000cba */
    tcg_out_opc_reg(s, OPC_OR, TCG_TMP3, TCG_TMP3, TCG_TMP1);
    /* t1 = 00abcdef */
    tcg_out_dsrl(s, TCG_TMP1, TCG_TMP0, 16);
    /* t3 = hg00dcba */
    tcg_out_opc_reg(s, OPC_OR, TCG_TMP3, TCG_TMP3, TCG_TMP2);

    /* t2 = 0000000f */
    tcg_out_opc_imm(s, OPC_ANDI, TCG_TMP2, TCG_TMP1, 0x00ff);
    /* t1 = 000000e0 */
    tcg_out_opc_imm(s, OPC_ANDI, TCG_TMP1, TCG_TMP1, 0xff00);
    /* t2 = 00f00000 */
    tcg_out_dsll(s, TCG_TMP2, TCG_TMP2, 40);
    /* t1 = 000e0000 */
    tcg_out_dsll(s, TCG_TMP1, TCG_TMP1, 24);

    /* t3 = hgf0dcba */
    tcg_out_opc_reg(s, OPC_OR, TCG_TMP3, TCG_TMP3, TCG_TMP2);
    tcg_out_opc_reg(s, OPC_JR, 0, TCG_REG_RA, 0);
    /* t3 = hgfedcba -- delay slot */
    tcg_out_opc_reg(s, OPC_OR, TCG_TMP3, TCG_TMP3, TCG_TMP1);
}

static void tcg_target_init(TCGContext *s)
{
    tcg_target_detect_isa();
    tcg_target_available_regs[TCG_TYPE_I32] = 0xffffffff;
    if (TCG_TARGET_REG_BITS == 64) {
        tcg_target_available_regs[TCG_TYPE_I64] = 0xffffffff;
    }

    tcg_target_call_clobber_regs = 0;
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_V0);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_V1);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_A0);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_A1);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_A2);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_A3);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_T0);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_T1);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_T2);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_T3);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_T4);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_T5);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_T6);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_T7);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_T8);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_T9);

    s->reserved_regs = 0;
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_ZERO); /* zero register */
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_K0);   /* kernel use only */
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_K1);   /* kernel use only */
    tcg_regset_set_reg(s->reserved_regs, TCG_TMP0);     /* internal use */
    tcg_regset_set_reg(s->reserved_regs, TCG_TMP1);     /* internal use */
    tcg_regset_set_reg(s->reserved_regs, TCG_TMP2);     /* internal use */
    tcg_regset_set_reg(s->reserved_regs, TCG_TMP3);     /* internal use */
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_RA);   /* return address */
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_SP);   /* stack pointer */
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_GP);   /* global pointer */
}

void tb_target_set_jmp_target(uintptr_t tc_ptr, uintptr_t jmp_addr,
                              uintptr_t addr)
{
    atomic_set((uint32_t *)jmp_addr, deposit32(OPC_J, 0, 26, addr >> 2));
    flush_icache_range(jmp_addr, jmp_addr + 4);
}

typedef struct {
    DebugFrameHeader h;
    uint8_t fde_def_cfa[4];
    uint8_t fde_reg_ofs[ARRAY_SIZE(tcg_target_callee_save_regs) * 2];
} DebugFrame;

#define ELF_HOST_MACHINE EM_MIPS
/* GDB doesn't appear to require proper setting of ELF_HOST_FLAGS,
   which is good because they're really quite complicated for MIPS.  */

static const DebugFrame debug_frame = {
    .h.cie.len = sizeof(DebugFrameCIE) - 4, /* length after .len member */
    .h.cie.id = -1,
    .h.cie.version = 1,
    .h.cie.code_align = 1,
    .h.cie.data_align = -(TCG_TARGET_REG_BITS / 8) & 0x7f, /* sleb128 */
    .h.cie.return_column = TCG_REG_RA,

    /* Total FDE size does not include the "len" member.  */
    .h.fde.len = sizeof(DebugFrame) - offsetof(DebugFrame, h.fde.cie_offset),

    .fde_def_cfa = {
        12, TCG_REG_SP,                 /* DW_CFA_def_cfa sp, ... */
        (FRAME_SIZE & 0x7f) | 0x80,     /* ... uleb128 FRAME_SIZE */
        (FRAME_SIZE >> 7)
    },
    .fde_reg_ofs = {
        0x80 + 16, 9,                   /* DW_CFA_offset, s0, -72 */
        0x80 + 17, 8,                   /* DW_CFA_offset, s2, -64 */
        0x80 + 18, 7,                   /* DW_CFA_offset, s3, -56 */
        0x80 + 19, 6,                   /* DW_CFA_offset, s4, -48 */
        0x80 + 20, 5,                   /* DW_CFA_offset, s5, -40 */
        0x80 + 21, 4,                   /* DW_CFA_offset, s6, -32 */
        0x80 + 22, 3,                   /* DW_CFA_offset, s7, -24 */
        0x80 + 30, 2,                   /* DW_CFA_offset, s8, -16 */
        0x80 + 31, 1,                   /* DW_CFA_offset, ra,  -8 */
    }
};

void tcg_register_jit(void *buf, size_t buf_size)
{
    tcg_register_jit_int(buf, buf_size, &debug_frame, sizeof(debug_frame));
}

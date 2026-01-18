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

/* used for function call generation */
#define TCG_TARGET_STACK_ALIGN        16
#define TCG_TARGET_CALL_STACK_OFFSET  0
#define TCG_TARGET_CALL_ARG_I32       TCG_CALL_ARG_NORMAL
#define TCG_TARGET_CALL_ARG_I64       TCG_CALL_ARG_NORMAL
#define TCG_TARGET_CALL_RET_I128      TCG_CALL_RET_NORMAL
#define TCG_TARGET_CALL_ARG_I128      TCG_CALL_ARG_EVEN

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

#define TCG_GUEST_BASE_REG TCG_REG_S7
#define TCG_REG_TB         TCG_REG_S6

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
    TCG_REG_T0,
    TCG_REG_T1,
    TCG_REG_T2,
    TCG_REG_T3,
};

static TCGReg tcg_target_call_oarg_reg(TCGCallReturnKind kind, int slot)
{
    tcg_debug_assert(kind == TCG_CALL_RET_NORMAL);
    tcg_debug_assert(slot >= 0 && slot <= 1);
    return TCG_REG_V0 + slot;
}

static const tcg_insn_unit *tb_ret_addr;
static const tcg_insn_unit *bswap32_addr;
static const tcg_insn_unit *bswap32u_addr;
static const tcg_insn_unit *bswap64_addr;

static bool reloc_pc16(tcg_insn_unit *src_rw, const tcg_insn_unit *target)
{
    /* Let the compiler perform the right-shift as part of the arithmetic.  */
    const tcg_insn_unit *src_rx = tcg_splitwx_to_rx(src_rw);
    ptrdiff_t disp = target - (src_rx + 1);
    if (disp == (int16_t)disp) {
        *src_rw = deposit32(*src_rw, 0, 16, disp);
        return true;
    }
    return false;
}

static bool patch_reloc(tcg_insn_unit *code_ptr, int type,
                        intptr_t value, intptr_t addend)
{
    value += addend;
    switch (type) {
    case R_MIPS_PC16:
        return reloc_pc16(code_ptr, (const tcg_insn_unit *)value);
    case R_MIPS_16:
        if (value != (int16_t)value) {
            return false;
        }
        *code_ptr = deposit32(*code_ptr, 0, 16, value);
        return true;
    }
    g_assert_not_reached();
}

#define TCG_CT_CONST_U16  0x100    /* Unsigned 16-bit: 0 - 0xffff.  */
#define TCG_CT_CONST_S16  0x200    /* Signed 16-bit: -32768 - 32767 */
#define TCG_CT_CONST_P2M1 0x400    /* Power of 2 minus 1.  */
#define TCG_CT_CONST_WSZ  0x800    /* word size */

#define ALL_GENERAL_REGS  0xffffffffu

static bool is_p2m1(tcg_target_long val)
{
    return val && ((val + 1) & val) == 0;
}

/* test if a constant matches the constraint */
static bool tcg_target_const_match(int64_t val, int ct,
                                   TCGType type, TCGCond cond, int vece)
{
    if (ct & TCG_CT_CONST) {
        return 1;
    } else if ((ct & TCG_CT_CONST_U16) && val == (uint16_t)val) {
        return 1;
    } else if ((ct & TCG_CT_CONST_S16) && val == (int16_t)val) {
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
    OPC_BNEL     = 025 << 26,
    OPC_BNEZALC_R6 = 030 << 26,
    OPC_DADDIU   = 031 << 26,
    OPC_LDL      = 032 << 26,
    OPC_LDR      = 033 << 26,
    OPC_LB       = 040 << 26,
    OPC_LH       = 041 << 26,
    OPC_LWL      = 042 << 26,
    OPC_LW       = 043 << 26,
    OPC_LBU      = 044 << 26,
    OPC_LHU      = 045 << 26,
    OPC_LWR      = 046 << 26,
    OPC_LWU      = 047 << 26,
    OPC_SB       = 050 << 26,
    OPC_SH       = 051 << 26,
    OPC_SWL      = 052 << 26,
    OPC_SW       = 053 << 26,
    OPC_SDL      = 054 << 26,
    OPC_SDR      = 055 << 26,
    OPC_SWR      = 056 << 26,
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
    OPC_SYNC_WMB     = OPC_SYNC | 0x04 << 6,
    OPC_SYNC_MB      = OPC_SYNC | 0x10 << 6,
    OPC_SYNC_ACQUIRE = OPC_SYNC | 0x11 << 6,
    OPC_SYNC_RELEASE = OPC_SYNC | 0x12 << 6,
    OPC_SYNC_RMB     = OPC_SYNC | 0x13 << 6,
} MIPSInsn;

/*
 * Type reg
 */
static void tcg_out_opc_reg(TCGContext *s, MIPSInsn opc,
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
static void tcg_out_opc_imm(TCGContext *s, MIPSInsn opc,
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
static void tcg_out_opc_bf(TCGContext *s, MIPSInsn opc, TCGReg rt,
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

static void tcg_out_opc_bf64(TCGContext *s, MIPSInsn opc, MIPSInsn opm,
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
static void tcg_out_opc_br(TCGContext *s, MIPSInsn opc, TCGReg rt, TCGReg rs)
{
    tcg_out_opc_imm(s, opc, rt, rs, 0);
}

/*
 * Type sa
 */
static void tcg_out_opc_sa(TCGContext *s, MIPSInsn opc,
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
static bool tcg_out_opc_jmp(TCGContext *s, MIPSInsn opc, const void *target)
{
    uintptr_t dest = (uintptr_t)target;
    uintptr_t from = (uintptr_t)tcg_splitwx_to_rx(s->code_ptr) + 4;
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

static void tcg_out_nop(TCGContext *s)
{
    tcg_out32(s, 0);
}

static void tcg_out_nop_fill(tcg_insn_unit *p, int count)
{
    memset(p, 0, count * sizeof(tcg_insn_unit));
}

static void tcg_out_dsll(TCGContext *s, TCGReg rd, TCGReg rt, TCGArg sa)
{
    tcg_out_opc_sa64(s, OPC_DSLL, OPC_DSLL32, rd, rt, sa);
}

static void tcg_out_dsrl(TCGContext *s, TCGReg rd, TCGReg rt, TCGArg sa)
{
    tcg_out_opc_sa64(s, OPC_DSRL, OPC_DSRL32, rd, rt, sa);
}

static void tcg_out_dsra(TCGContext *s, TCGReg rd, TCGReg rt, TCGArg sa)
{
    tcg_out_opc_sa64(s, OPC_DSRA, OPC_DSRA32, rd, rt, sa);
}

static bool tcg_out_mov(TCGContext *s, TCGType type, TCGReg ret, TCGReg arg)
{
    /* Simple reg-reg move, optimising out the 'do nothing' case */
    if (ret != arg) {
        tcg_out_opc_reg(s, OPC_OR, ret, arg, TCG_REG_ZERO);
    }
    return true;
}

static bool tcg_out_movi_one(TCGContext *s, TCGReg ret, tcg_target_long arg)
{
    if (arg == (int16_t)arg) {
        tcg_out_opc_imm(s, OPC_ADDIU, ret, TCG_REG_ZERO, arg);
        return true;
    }
    if (arg == (uint16_t)arg) {
        tcg_out_opc_imm(s, OPC_ORI, ret, TCG_REG_ZERO, arg);
        return true;
    }
    if (arg == (int32_t)arg && (arg & 0xffff) == 0) {
        tcg_out_opc_imm(s, OPC_LUI, ret, TCG_REG_ZERO, arg >> 16);
        return true;
    }
    return false;
}

static bool tcg_out_movi_two(TCGContext *s, TCGReg ret, tcg_target_long arg)
{
    /*
     * All signed 32-bit constants are loadable with two immediates,
     * and everything else requires more work.
     */
    if (arg == (int32_t)arg) {
        if (!tcg_out_movi_one(s, ret, arg)) {
            tcg_out_opc_imm(s, OPC_LUI, ret, TCG_REG_ZERO, arg >> 16);
            tcg_out_opc_imm(s, OPC_ORI, ret, ret, arg & 0xffff);
        }
        return true;
    }
    return false;
}

static void tcg_out_movi_pool(TCGContext *s, TCGReg ret,
                              tcg_target_long arg, TCGReg tbreg)
{
    new_pool_label(s, arg, R_MIPS_16, s->code_ptr, tcg_tbrel_diff(s, NULL));
    tcg_out_opc_imm(s, OPC_LD, ret, tbreg, 0);
}

static void tcg_out_movi_int(TCGContext *s, TCGType type, TCGReg ret,
                             tcg_target_long arg, TCGReg tbreg)
{
    tcg_target_long tmp;
    int sh, lo;

    if (type == TCG_TYPE_I32) {
        arg = (int32_t)arg;
    }

    /* Load all 32-bit constants. */
    if (tcg_out_movi_two(s, ret, arg)) {
        return;
    }

    /* Load addresses within 2GB of TB with 1 or 3 insns. */
    tmp = tcg_tbrel_diff(s, (void *)arg);
    if (tmp == (int16_t)tmp) {
        tcg_out_opc_imm(s, OPC_DADDIU, ret, tbreg, tmp);
        return;
    }
    if (tcg_out_movi_two(s, ret, tmp)) {
        tcg_out_opc_reg(s, OPC_DADDU, ret, ret, tbreg);
        return;
    }

    /*
     * Load bitmasks with a right-shift.  This is good for things
     * like 0x0fff_ffff_ffff_fff0: ADDUI r,0,0xff00 + DSRL r,r,4.
     * or similarly using LUI.  For this to work, bit 31 must be set.
     */
    if (arg > 0 && (int32_t)arg < 0) {
        sh = clz64(arg);
        if (tcg_out_movi_one(s, ret, arg << sh)) {
            tcg_out_dsrl(s, ret, ret, sh);
            return;
        }
    }

    /*
     * Load slightly larger constants using left-shift.
     * Limit this sequence to 3 insns to avoid too much expansion.
     */
    sh = ctz64(arg);
    if (sh && tcg_out_movi_two(s, ret, arg >> sh)) {
        tcg_out_dsll(s, ret, ret, sh);
        return;
    }

    /*
     * Load slightly larger constants using left-shift and add/or.
     * Prefer addi with a negative immediate when that would produce
     * a larger shift.  For this to work, bits 15 and 16 must be set.
     */
    lo = arg & 0xffff;
    if (lo) {
        if ((arg & 0x18000) == 0x18000) {
            lo = (int16_t)arg;
        }
        tmp = arg - lo;
        sh = ctz64(tmp);
        tmp >>= sh;
        if (tcg_out_movi_one(s, ret, tmp)) {
            tcg_out_dsll(s, ret, ret, sh);
            tcg_out_opc_imm(s, lo < 0 ? OPC_DADDIU : OPC_ORI, ret, ret, lo);
            return;
        }
    }

    /* Otherwise, put 64-bit constants into the constant pool. */
    tcg_out_movi_pool(s, ret, arg, tbreg);
}

static void tcg_out_movi(TCGContext *s, TCGType type,
                         TCGReg ret, tcg_target_long arg)
{
    tcg_out_movi_int(s, type, ret, arg, TCG_REG_TB);
}

static void tcg_out_ext8s(TCGContext *s, TCGType type, TCGReg rd, TCGReg rs)
{
    tcg_debug_assert(use_mips32r2_instructions);
    tcg_out_opc_reg(s, OPC_SEB, rd, TCG_REG_ZERO, rs);
}

static void tcg_out_ext8u(TCGContext *s, TCGReg rd, TCGReg rs)
{
    tcg_out_opc_imm(s, OPC_ANDI, rd, rs, 0xff);
}

static void tcg_out_ext16s(TCGContext *s, TCGType type, TCGReg rd, TCGReg rs)
{
    tcg_debug_assert(use_mips32r2_instructions);
    tcg_out_opc_reg(s, OPC_SEH, rd, TCG_REG_ZERO, rs);
}

static void tcg_out_ext16u(TCGContext *s, TCGReg rd, TCGReg rs)
{
    tcg_out_opc_imm(s, OPC_ANDI, rd, rs, 0xffff);
}

static void tcg_out_ext32s(TCGContext *s, TCGReg rd, TCGReg rs)
{
    tcg_out_opc_sa(s, OPC_SLL, rd, rs, 0);
}

static void tcg_out_exts_i32_i64(TCGContext *s, TCGReg rd, TCGReg rs)
{
    if (rd != rs) {
        tcg_out_ext32s(s, rd, rs);
    }
}

static void tcg_out_extu_i32_i64(TCGContext *s, TCGReg rd, TCGReg rs)
{
    tcg_out_ext32u(s, rd, rs);
}

static void tcg_out_extrl_i64_i32(TCGContext *s, TCGReg rd, TCGReg rs)
{
    tcg_out_ext32s(s, rd, rs);
}

static bool tcg_out_xchg(TCGContext *s, TCGType type, TCGReg r1, TCGReg r2)
{
    return false;
}

static void tcg_out_addi_ptr(TCGContext *s, TCGReg rd, TCGReg rs,
                             tcg_target_long imm)
{
    /* This function is only used for passing structs by reference. */
    g_assert_not_reached();
}

static void tcg_out_bswap_subr(TCGContext *s, const tcg_insn_unit *sub)
{
    if (!tcg_out_opc_jmp(s, OPC_JAL, sub)) {
        tcg_out_movi(s, TCG_TYPE_PTR, TCG_TMP1, (uintptr_t)sub);
        tcg_out_opc_reg(s, OPC_JALR, TCG_REG_RA, TCG_TMP1, 0);
    }
}

static void tcg_out_ext32u(TCGContext *s, TCGReg ret, TCGReg arg)
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
            tcg_out_opc_reg(s, OPC_DADDU, TCG_TMP0, TCG_TMP0, addr);
        }
        addr = TCG_TMP0;
    }
    tcg_out_opc_imm(s, opc, data, addr, lo);
}

static void tcg_out_ld(TCGContext *s, TCGType type, TCGReg arg,
                       TCGReg arg1, intptr_t arg2)
{
    MIPSInsn opc = type == TCG_TYPE_I32 ? OPC_LW : OPC_LD;
    tcg_out_ldst(s, opc, arg, arg1, arg2);
}

static void tcg_out_st(TCGContext *s, TCGType type, TCGReg arg,
                       TCGReg arg1, intptr_t arg2)
{
    MIPSInsn opc = type == TCG_TYPE_I32 ? OPC_SW : OPC_SD;
    tcg_out_ldst(s, opc, arg, arg1, arg2);
}

static bool tcg_out_sti(TCGContext *s, TCGType type, TCGArg val,
                        TCGReg base, intptr_t ofs)
{
    if (val == 0) {
        tcg_out_st(s, type, TCG_REG_ZERO, base, ofs);
        return true;
    }
    return false;
}

#define SETCOND_INV    TCG_TARGET_NB_REGS
#define SETCOND_NEZ    (SETCOND_INV << 1)
#define SETCOND_FLAGS  (SETCOND_INV | SETCOND_NEZ)

static int tcg_out_setcond_int(TCGContext *s, TCGCond cond, TCGReg ret,
                               TCGReg arg1, TCGReg arg2)
{
    int flags = 0;

    switch (cond) {
    case TCG_COND_EQ:    /* -> NE  */
    case TCG_COND_GE:    /* -> LT  */
    case TCG_COND_GEU:   /* -> LTU */
    case TCG_COND_LE:    /* -> GT  */
    case TCG_COND_LEU:   /* -> GTU */
        cond = tcg_invert_cond(cond);
        flags ^= SETCOND_INV;
        break;
    default:
        break;
    }

    switch (cond) {
    case TCG_COND_NE:
        flags |= SETCOND_NEZ;
        if (arg2 == 0) {
            return arg1 | flags;
        }
        tcg_out_opc_reg(s, OPC_XOR, ret, arg1, arg2);
        break;
    case TCG_COND_LT:
        tcg_out_opc_reg(s, OPC_SLT, ret, arg1, arg2);
        break;
    case TCG_COND_LTU:
        tcg_out_opc_reg(s, OPC_SLTU, ret, arg1, arg2);
        break;
    case TCG_COND_GT:
        tcg_out_opc_reg(s, OPC_SLT, ret, arg2, arg1);
        break;
    case TCG_COND_GTU:
        tcg_out_opc_reg(s, OPC_SLTU, ret, arg2, arg1);
        break;
    default:
        g_assert_not_reached();
    }
    return ret | flags;
}

static void tcg_out_setcond_end(TCGContext *s, TCGReg ret, int tmpflags)
{
    if (tmpflags != ret) {
        TCGReg tmp = tmpflags & ~SETCOND_FLAGS;

        switch (tmpflags & SETCOND_FLAGS) {
        case SETCOND_INV:
            /* Intermediate result is boolean: simply invert. */
            tcg_out_opc_imm(s, OPC_XORI, ret, tmp, 1);
            break;
        case SETCOND_NEZ:
            /* Intermediate result is zero/non-zero: test != 0. */
            tcg_out_opc_reg(s, OPC_SLTU, ret, TCG_REG_ZERO, tmp);
            break;
        case SETCOND_NEZ | SETCOND_INV:
            /* Intermediate result is zero/non-zero: test == 0. */
            tcg_out_opc_imm(s, OPC_SLTIU, ret, tmp, 1);
            break;
        default:
            g_assert_not_reached();
        }
    }
}

static void tgen_setcond(TCGContext *s, TCGType type, TCGCond cond,
                         TCGReg ret, TCGReg arg1, TCGReg arg2)
{
    int tmpflags = tcg_out_setcond_int(s, cond, ret, arg1, arg2);
    tcg_out_setcond_end(s, ret, tmpflags);
}

static const TCGOutOpSetcond outop_setcond = {
    .base.static_constraint = C_O1_I2(r, r, rz),
    .out_rrr = tgen_setcond,
};

static void tgen_negsetcond(TCGContext *s, TCGType type, TCGCond cond,
                            TCGReg ret, TCGReg arg1, TCGReg arg2)
{
    int tmpflags = tcg_out_setcond_int(s, cond, ret, arg1, arg2);
    TCGReg tmp = tmpflags & ~SETCOND_FLAGS;

    /* If intermediate result is zero/non-zero: test != 0. */
    if (tmpflags & SETCOND_NEZ) {
        tcg_out_opc_reg(s, OPC_SLTU, ret, TCG_REG_ZERO, tmp);
        tmp = ret;
    }
    /* Produce the 0/-1 result. */
    if (tmpflags & SETCOND_INV) {
        tcg_out_opc_imm(s, OPC_ADDIU, ret, tmp, -1);
    } else {
        tcg_out_opc_reg(s, OPC_SUBU, ret, TCG_REG_ZERO, tmp);
    }
}

static const TCGOutOpSetcond outop_negsetcond = {
    .base.static_constraint = C_O1_I2(r, r, rz),
    .out_rrr = tgen_negsetcond,
};

static void tgen_brcond(TCGContext *s, TCGType type, TCGCond cond,
                        TCGReg arg1, TCGReg arg2, TCGLabel *l)
{
    static const MIPSInsn b_zero[16] = {
        [TCG_COND_LT] = OPC_BLTZ,
        [TCG_COND_GT] = OPC_BGTZ,
        [TCG_COND_LE] = OPC_BLEZ,
        [TCG_COND_GE] = OPC_BGEZ,
    };

    MIPSInsn b_opc = 0;

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
        }
        break;
    default:
        break;
    }

    if (b_opc == 0) {
        int tmpflags = tcg_out_setcond_int(s, cond, TCG_TMP0, arg1, arg2);

        arg2 = TCG_REG_ZERO;
        arg1 = tmpflags & ~SETCOND_FLAGS;
        b_opc = tmpflags & SETCOND_INV ? OPC_BEQ : OPC_BNE;
    }

    tcg_out_reloc(s, s->code_ptr, R_MIPS_PC16, l, 0);
    tcg_out_opc_br(s, b_opc, arg1, arg2);
    tcg_out_nop(s);
}

static const TCGOutOpBrcond outop_brcond = {
    .base.static_constraint = C_O0_I2(r, rz),
    .out_rr = tgen_brcond,
};

void tcg_out_br(TCGContext *s, TCGLabel *l)
{
    tgen_brcond(s, TCG_TYPE_I32, TCG_COND_EQ, TCG_REG_ZERO, TCG_REG_ZERO, l);
}

static void tgen_movcond(TCGContext *s, TCGType type, TCGCond cond,
                         TCGReg ret, TCGReg c1, TCGArg c2, bool const_c2,
                         TCGArg v1, bool const_v1, TCGArg v2, bool const_v2)
{
    int tmpflags;
    bool eqz;

    /* If one of the values is zero, put it last to match SEL*Z instructions */
    if (use_mips32r6_instructions && v1 == 0) {
        v1 = v2;
        v2 = 0;
        cond = tcg_invert_cond(cond);
    }

    tmpflags = tcg_out_setcond_int(s, cond, TCG_TMP0, c1, c2);
    c1 = tmpflags & ~SETCOND_FLAGS;
    eqz = tmpflags & SETCOND_INV;

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
        return;
    }

    /* This should be guaranteed via constraints */
    tcg_debug_assert(v2 == ret);

    if (use_movnz_instructions) {
        MIPSInsn m_opc = eqz ? OPC_MOVZ : OPC_MOVN;
        tcg_out_opc_reg(s, m_opc, ret, v1, c1);
    } else {
        /* Invert the condition in order to branch over the move. */
        MIPSInsn b_opc = eqz ? OPC_BNE : OPC_BEQ;
        tcg_out_opc_imm(s, b_opc, c1, TCG_REG_ZERO, 2);
        tcg_out_nop(s);
        /* Open-code tcg_out_mov, without the nop-move check. */
        tcg_out_opc_reg(s, OPC_OR, ret, v1, TCG_REG_ZERO);
    }
}

static const TCGOutOpMovcond outop_movcond = {
    .base.static_constraint = (use_mips32r6_instructions
                               ? C_O1_I4(r, r, rz, rz, rz)
                               : C_O1_I4(r, r, rz, rz, 0)),
    .out = tgen_movcond,
};

static void tcg_out_call_int(TCGContext *s, const tcg_insn_unit *arg, bool tail)
{
    /*
     * Note that __mips_abicalls requires the called function's address
     * to be loaded into $25 (t9), even if a direct branch is in range.
     *
     * We can re-use helper addresses often; always drop the pointer
     * into the constant pool.
     */
    tcg_out_movi_pool(s, TCG_REG_T9, (uintptr_t)arg, TCG_REG_TB);

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

static void tcg_out_call(TCGContext *s, const tcg_insn_unit *arg,
                         const TCGHelperInfo *info)
{
    tcg_out_call_int(s, arg, false);
    tcg_out_nop(s);
}

/* We have four temps, we might as well expose three of them. */
static const TCGLdstHelperParam ldst_helper_param = {
    .ntmp = 3, .tmp = { TCG_TMP0, TCG_TMP1, TCG_TMP2 }
};

static bool tcg_out_qemu_ld_slow_path(TCGContext *s, TCGLabelQemuLdst *l)
{
    const tcg_insn_unit *tgt_rx = tcg_splitwx_to_rx(s->code_ptr);
    MemOp opc = get_memop(l->oi);

    /* resolve label address */
    if (!reloc_pc16(l->label_ptr[0], tgt_rx)
        || (l->label_ptr[1] && !reloc_pc16(l->label_ptr[1], tgt_rx))) {
        return false;
    }

    tcg_out_ld_helper_args(s, l, &ldst_helper_param);

    tcg_out_call_int(s, qemu_ld_helpers[opc & MO_SSIZE], false);
    /* delay slot */
    tcg_out_nop(s);

    tcg_out_ld_helper_ret(s, l, true, &ldst_helper_param);

    tcg_out_opc_br(s, OPC_BEQ, TCG_REG_ZERO, TCG_REG_ZERO);
    if (!reloc_pc16(s->code_ptr - 1, l->raddr)) {
        return false;
    }

    /* delay slot */
    tcg_out_nop(s);
    return true;
}

static bool tcg_out_qemu_st_slow_path(TCGContext *s, TCGLabelQemuLdst *l)
{
    const tcg_insn_unit *tgt_rx = tcg_splitwx_to_rx(s->code_ptr);
    MemOp opc = get_memop(l->oi);

    /* resolve label address */
    if (!reloc_pc16(l->label_ptr[0], tgt_rx)
        || (l->label_ptr[1] && !reloc_pc16(l->label_ptr[1], tgt_rx))) {
        return false;
    }

    tcg_out_st_helper_args(s, l, &ldst_helper_param);

    tcg_out_call_int(s, qemu_st_helpers[opc & MO_SIZE], false);
    /* delay slot */
    tcg_out_nop(s);

    tcg_out_opc_br(s, OPC_BEQ, TCG_REG_ZERO, TCG_REG_ZERO);
    if (!reloc_pc16(s->code_ptr - 1, l->raddr)) {
        return false;
    }

    /* delay slot */
    tcg_out_nop(s);
    return true;
}

typedef struct {
    TCGReg base;
    TCGAtomAlign aa;
} HostAddress;

bool tcg_target_has_memory_bswap(MemOp memop)
{
    return false;
}

/* We expect to use a 16-bit negative offset from ENV.  */
#define MIN_TLB_MASK_TABLE_OFS  -32768

/*
 * For system-mode, perform the TLB load and compare.
 * For user-mode, perform any required alignment tests.
 * In both cases, return a TCGLabelQemuLdst structure if the slow path
 * is required and fill in @h with the host address for the fast path.
 */
static TCGLabelQemuLdst *prepare_host_addr(TCGContext *s, HostAddress *h,
                                           TCGReg addr, MemOpIdx oi, bool is_ld)
{
    TCGType addr_type = s->addr_type;
    TCGLabelQemuLdst *ldst = NULL;
    MemOp opc = get_memop(oi);
    MemOp a_bits;
    unsigned s_bits = opc & MO_SIZE;
    unsigned a_mask;
    TCGReg base;

    h->aa = atom_and_align_for_opc(s, opc, MO_ATOM_IFALIGN, false);
    a_bits = h->aa.align;
    a_mask = (1 << a_bits) - 1;

    if (tcg_use_softmmu) {
        unsigned s_mask = (1 << s_bits) - 1;
        int mem_index = get_mmuidx(oi);
        int fast_off = tlb_mask_table_ofs(s, mem_index);
        int mask_off = fast_off + offsetof(CPUTLBDescFast, mask);
        int table_off = fast_off + offsetof(CPUTLBDescFast, table);
        int add_off = offsetof(CPUTLBEntry, addend);
        int cmp_off = is_ld ? offsetof(CPUTLBEntry, addr_read)
                            : offsetof(CPUTLBEntry, addr_write);

        ldst = new_ldst_label(s);
        ldst->is_ld = is_ld;
        ldst->oi = oi;
        ldst->addr_reg = addr;

        /* Load tlb_mask[mmu_idx] and tlb_table[mmu_idx].  */
        tcg_out_ld(s, TCG_TYPE_PTR, TCG_TMP0, TCG_AREG0, mask_off);
        tcg_out_ld(s, TCG_TYPE_PTR, TCG_TMP1, TCG_AREG0, table_off);

        /* Extract the TLB index from the address into TMP3.  */
        if (addr_type == TCG_TYPE_I32) {
            tcg_out_opc_sa(s, OPC_SRL, TCG_TMP3, addr,
                           TARGET_PAGE_BITS - CPU_TLB_ENTRY_BITS);
        } else {
            tcg_out_dsrl(s, TCG_TMP3, addr, TARGET_PAGE_BITS - CPU_TLB_ENTRY_BITS);
        }
        tcg_out_opc_reg(s, OPC_AND, TCG_TMP3, TCG_TMP3, TCG_TMP0);

        /* Add the tlb_table pointer, creating the CPUTLBEntry address.  */
        tcg_out_opc_reg(s, OPC_DADDU, TCG_TMP3, TCG_TMP3, TCG_TMP1);

        /* Load the tlb comparator.  */
        if (addr_type == TCG_TYPE_I32) {
            tcg_out_ld(s, TCG_TYPE_I32, TCG_TMP0, TCG_TMP3,
                       cmp_off + HOST_BIG_ENDIAN * 4);
        } else {
            tcg_out_ld(s, TCG_TYPE_REG, TCG_TMP0, TCG_TMP3, cmp_off);
        }

        /* Load the tlb addend for the fast path.  */
        tcg_out_ld(s, TCG_TYPE_PTR, TCG_TMP3, TCG_TMP3, add_off);

        /*
         * Mask the page bits, keeping the alignment bits to compare against.
         * For unaligned accesses, compare against the end of the access to
         * verify that it does not cross a page boundary.
         */
        tcg_out_movi(s, addr_type, TCG_TMP1, TARGET_PAGE_MASK | a_mask);
        if (a_mask < s_mask) {
            tcg_out_opc_imm(s, (addr_type == TCG_TYPE_I32
                                ? OPC_ADDIU : OPC_DADDIU),
                            TCG_TMP2, addr, s_mask - a_mask);
            tcg_out_opc_reg(s, OPC_AND, TCG_TMP1, TCG_TMP1, TCG_TMP2);
        } else {
            tcg_out_opc_reg(s, OPC_AND, TCG_TMP1, TCG_TMP1, addr);
        }

        /* Zero extend a 32-bit guest address for a 64-bit host. */
        if (addr_type == TCG_TYPE_I32) {
            tcg_out_ext32u(s, TCG_TMP2, addr);
            addr = TCG_TMP2;
        }

        ldst->label_ptr[0] = s->code_ptr;
        tcg_out_opc_br(s, OPC_BNE, TCG_TMP1, TCG_TMP0);

        /* delay slot */
        base = TCG_TMP3;
        tcg_out_opc_reg(s, OPC_DADDU, base, TCG_TMP3, addr);
    } else {
        if (a_mask && (use_mips32r6_instructions || a_bits != s_bits)) {
            ldst = new_ldst_label(s);

            ldst->is_ld = is_ld;
            ldst->oi = oi;
            ldst->addr_reg = addr;

            /* We are expecting a_bits to max out at 7, much lower than ANDI. */
            tcg_debug_assert(a_bits < 16);
            tcg_out_opc_imm(s, OPC_ANDI, TCG_TMP0, addr, a_mask);

            ldst->label_ptr[0] = s->code_ptr;
            if (use_mips32r6_instructions) {
                tcg_out_opc_br(s, OPC_BNEZALC_R6, TCG_REG_ZERO, TCG_TMP0);
            } else {
                tcg_out_opc_br(s, OPC_BNEL, TCG_TMP0, TCG_REG_ZERO);
                tcg_out_nop(s);
            }
        }

        base = addr;
        if (addr_type == TCG_TYPE_I32) {
            tcg_out_ext32u(s, TCG_REG_A0, base);
            base = TCG_REG_A0;
        }
        if (guest_base) {
            if (guest_base == (int16_t)guest_base) {
                tcg_out_opc_imm(s, OPC_DADDIU, TCG_REG_A0, base, guest_base);
            } else {
                tcg_out_opc_reg(s, OPC_DADDU, TCG_REG_A0, base,
                                TCG_GUEST_BASE_REG);
            }
            base = TCG_REG_A0;
        }
    }

    h->base = base;
    return ldst;
}

static void tcg_out_qemu_ld_direct(TCGContext *s, TCGReg lo, TCGReg hi,
                                   TCGReg base, MemOp opc, TCGType type)
{
    switch (opc & MO_SSIZE) {
    case MO_UB:
        tcg_out_opc_imm(s, OPC_LBU, lo, base, 0);
        break;
    case MO_SB:
        tcg_out_opc_imm(s, OPC_LB, lo, base, 0);
        break;
    case MO_UW:
        tcg_out_opc_imm(s, OPC_LHU, lo, base, 0);
        break;
    case MO_SW:
        tcg_out_opc_imm(s, OPC_LH, lo, base, 0);
        break;
    case MO_UL:
        if (type == TCG_TYPE_I64) {
            tcg_out_opc_imm(s, OPC_LWU, lo, base, 0);
            break;
        }
        /* FALLTHRU */
    case MO_SL:
        tcg_out_opc_imm(s, OPC_LW, lo, base, 0);
        break;
    case MO_UQ:
        tcg_out_opc_imm(s, OPC_LD, lo, base, 0);
        break;
    default:
        g_assert_not_reached();
    }
}

static void tcg_out_qemu_ld_unalign(TCGContext *s, TCGReg lo, TCGReg hi,
                                    TCGReg base, MemOp opc, TCGType type)
{
    const MIPSInsn lw1 = HOST_BIG_ENDIAN ? OPC_LWL : OPC_LWR;
    const MIPSInsn lw2 = HOST_BIG_ENDIAN ? OPC_LWR : OPC_LWL;
    const MIPSInsn ld1 = HOST_BIG_ENDIAN ? OPC_LDL : OPC_LDR;
    const MIPSInsn ld2 = HOST_BIG_ENDIAN ? OPC_LDR : OPC_LDL;
    bool sgn = opc & MO_SIGN;

    switch (opc & MO_SIZE) {
    case MO_16:
        if (HOST_BIG_ENDIAN) {
            tcg_out_opc_imm(s, sgn ? OPC_LB : OPC_LBU, TCG_TMP0, base, 0);
            tcg_out_opc_imm(s, OPC_LBU, lo, base, 1);
            if (use_mips32r2_instructions) {
                tcg_out_opc_bf(s, OPC_INS, lo, TCG_TMP0, 31, 8);
            } else {
                tcg_out_opc_sa(s, OPC_SLL, TCG_TMP0, TCG_TMP0, 8);
                tcg_out_opc_reg(s, OPC_OR, lo, lo, TCG_TMP0);
            }
        } else if (use_mips32r2_instructions && lo != base) {
            tcg_out_opc_imm(s, OPC_LBU, lo, base, 0);
            tcg_out_opc_imm(s, sgn ? OPC_LB : OPC_LBU, TCG_TMP0, base, 1);
            tcg_out_opc_bf(s, OPC_INS, lo, TCG_TMP0, 31, 8);
        } else {
            tcg_out_opc_imm(s, OPC_LBU, TCG_TMP0, base, 0);
            tcg_out_opc_imm(s, sgn ? OPC_LB : OPC_LBU, TCG_TMP1, base, 1);
            tcg_out_opc_sa(s, OPC_SLL, TCG_TMP1, TCG_TMP1, 8);
            tcg_out_opc_reg(s, OPC_OR, lo, TCG_TMP0, TCG_TMP1);
        }
        break;

    case MO_32:
        tcg_out_opc_imm(s, lw1, lo, base, 0);
        tcg_out_opc_imm(s, lw2, lo, base, 3);
        if (type == TCG_TYPE_I64 && !sgn) {
            tcg_out_ext32u(s, lo, lo);
        }
        break;

    case MO_64:
        tcg_out_opc_imm(s, ld1, lo, base, 0);
        tcg_out_opc_imm(s, ld2, lo, base, 7);
        break;

    default:
        g_assert_not_reached();
    }
}

static void tgen_qemu_ld(TCGContext *s, TCGType type, TCGReg data,
                         TCGReg addr, MemOpIdx oi)
{
    MemOp opc = get_memop(oi);
    TCGLabelQemuLdst *ldst;
    HostAddress h;

    ldst = prepare_host_addr(s, &h, addr, oi, true);

    if (use_mips32r6_instructions || h.aa.align >= (opc & MO_SIZE)) {
        tcg_out_qemu_ld_direct(s, data, 0, h.base, opc, type);
    } else {
        tcg_out_qemu_ld_unalign(s, data, 0, h.base, opc, type);
    }

    if (ldst) {
        ldst->type = type;
        ldst->datalo_reg = data;
        ldst->datahi_reg = 0;
        ldst->raddr = tcg_splitwx_to_rx(s->code_ptr);
    }
}

static const TCGOutOpQemuLdSt outop_qemu_ld = {
    .base.static_constraint = C_O1_I1(r, r),
    .out = tgen_qemu_ld,
};

static const TCGOutOpQemuLdSt2 outop_qemu_ld2 = {
    .base.static_constraint = C_NotImplemented,
};

static void tcg_out_qemu_st_direct(TCGContext *s, TCGReg lo, TCGReg hi,
                                   TCGReg base, MemOp opc)
{
    switch (opc & MO_SIZE) {
    case MO_8:
        tcg_out_opc_imm(s, OPC_SB, lo, base, 0);
        break;
    case MO_16:
        tcg_out_opc_imm(s, OPC_SH, lo, base, 0);
        break;
    case MO_32:
        tcg_out_opc_imm(s, OPC_SW, lo, base, 0);
        break;
    case MO_64:
        tcg_out_opc_imm(s, OPC_SD, lo, base, 0);
        break;
    default:
        g_assert_not_reached();
    }
}

static void tcg_out_qemu_st_unalign(TCGContext *s, TCGReg lo, TCGReg hi,
                                    TCGReg base, MemOp opc)
{
    const MIPSInsn sw1 = HOST_BIG_ENDIAN ? OPC_SWL : OPC_SWR;
    const MIPSInsn sw2 = HOST_BIG_ENDIAN ? OPC_SWR : OPC_SWL;
    const MIPSInsn sd1 = HOST_BIG_ENDIAN ? OPC_SDL : OPC_SDR;
    const MIPSInsn sd2 = HOST_BIG_ENDIAN ? OPC_SDR : OPC_SDL;

    switch (opc & MO_SIZE) {
    case MO_16:
        tcg_out_opc_sa(s, OPC_SRL, TCG_TMP0, lo, 8);
        tcg_out_opc_imm(s, OPC_SB, HOST_BIG_ENDIAN ? TCG_TMP0 : lo, base, 0);
        tcg_out_opc_imm(s, OPC_SB, HOST_BIG_ENDIAN ? lo : TCG_TMP0, base, 1);
        break;

    case MO_32:
        tcg_out_opc_imm(s, sw1, lo, base, 0);
        tcg_out_opc_imm(s, sw2, lo, base, 3);
        break;

    case MO_64:
        tcg_out_opc_imm(s, sd1, lo, base, 0);
        tcg_out_opc_imm(s, sd2, lo, base, 7);
        break;

    default:
        g_assert_not_reached();
    }
}

static void tgen_qemu_st(TCGContext *s, TCGType type, TCGReg data,
                         TCGReg addr, MemOpIdx oi)
{
    MemOp opc = get_memop(oi);
    TCGLabelQemuLdst *ldst;
    HostAddress h;

    ldst = prepare_host_addr(s, &h, addr, oi, false);

    if (use_mips32r6_instructions || h.aa.align >= (opc & MO_SIZE)) {
        tcg_out_qemu_st_direct(s, data, 0, h.base, opc);
    } else {
        tcg_out_qemu_st_unalign(s, data, 0, h.base, opc);
    }

    if (ldst) {
        ldst->type = type;
        ldst->datalo_reg = data;
        ldst->datahi_reg = 0;
        ldst->raddr = tcg_splitwx_to_rx(s->code_ptr);
    }
}

static const TCGOutOpQemuLdSt outop_qemu_st = {
    .base.static_constraint = C_O0_I2(rz, r),
    .out = tgen_qemu_st,
};

static const TCGOutOpQemuLdSt2 outop_qemu_st2 = {
    .base.static_constraint = C_NotImplemented,
};

static void tcg_out_mb(TCGContext *s, unsigned a0)
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

static void tcg_out_exit_tb(TCGContext *s, uintptr_t a0)
{
    TCGReg base = TCG_REG_ZERO;
    int16_t lo = 0;

    if (a0) {
        intptr_t ofs = tcg_tbrel_diff(s, (void *)a0);
        lo = ofs;
        if (ofs == lo) {
            base = TCG_REG_TB;
        } else {
            base = TCG_REG_V0;
            tcg_out_movi(s, TCG_TYPE_PTR, base, ofs - lo);
            tcg_out_opc_reg(s, OPC_DADDU, base, base, TCG_REG_TB);
        }
    }
    if (!tcg_out_opc_jmp(s, OPC_J, tb_ret_addr)) {
        tcg_out_movi(s, TCG_TYPE_PTR, TCG_TMP0, (uintptr_t)tb_ret_addr);
        tcg_out_opc_reg(s, OPC_JR, 0, TCG_TMP0, 0);
    }
    /* delay slot */
    tcg_out_opc_imm(s, OPC_DADDIU, TCG_REG_V0, base, lo);
}

static void tcg_out_goto_tb(TCGContext *s, int which)
{
    intptr_t ofs = get_jmp_target_addr(s, which);
    TCGReg base, dest;

    /* indirect jump method */
    dest = TCG_REG_TB;
    base = TCG_REG_TB;
    ofs = tcg_tbrel_diff(s, (void *)ofs);
    tcg_out_ld(s, TCG_TYPE_PTR, dest, base, ofs);
    tcg_out_opc_reg(s, OPC_JR, 0, dest, 0);
    /* delay slot */
    tcg_out_nop(s);

    set_jmp_reset_offset(s, which);
    /* For the unlinked case, need to reset TCG_REG_TB. */
    tcg_out_ldst(s, OPC_DADDIU, TCG_REG_TB, TCG_REG_TB,
                 -tcg_current_code_size(s));
}

static void tcg_out_goto_ptr(TCGContext *s, TCGReg a0)
{
    tcg_out_opc_reg(s, OPC_JR, 0, a0, 0);
    tcg_out_mov(s, TCG_TYPE_PTR, TCG_REG_TB, a0);
}

void tb_target_set_jmp_target(const TranslationBlock *tb, int n,
                              uintptr_t jmp_rx, uintptr_t jmp_rw)
{
    /* Always indirect, nothing to do */
}


static void tgen_add(TCGContext *s, TCGType type,
                     TCGReg a0, TCGReg a1, TCGReg a2)
{
    MIPSInsn insn = type == TCG_TYPE_I32 ? OPC_ADDU : OPC_DADDU;
    tcg_out_opc_reg(s, insn, a0, a1, a2);
}

static void tgen_addi(TCGContext *s, TCGType type,
                      TCGReg a0, TCGReg a1, tcg_target_long a2)
{
    MIPSInsn insn = type == TCG_TYPE_I32 ? OPC_ADDIU : OPC_DADDIU;
    tcg_out_opc_imm(s, insn, a0, a1, a2);
}

static const TCGOutOpBinary outop_add = {
    .base.static_constraint = C_O1_I2(r, r, rJ),
    .out_rrr = tgen_add,
    .out_rri = tgen_addi,
};

static const TCGOutOpBinary outop_addco = {
    .base.static_constraint = C_NotImplemented,
};

static const TCGOutOpAddSubCarry outop_addci = {
    .base.static_constraint = C_NotImplemented,
};

static const TCGOutOpBinary outop_addcio = {
    .base.static_constraint = C_NotImplemented,
};

static void tcg_out_set_carry(TCGContext *s)
{
    g_assert_not_reached();
}

static void tgen_and(TCGContext *s, TCGType type,
                     TCGReg a0, TCGReg a1, TCGReg a2)
{
    tcg_out_opc_reg(s, OPC_AND, a0, a1, a2);
}

static void tgen_andi(TCGContext *s, TCGType type,
                      TCGReg a0, TCGReg a1, tcg_target_long a2)
{
    int msb;

    if (a2 == (uint16_t)a2) {
        tcg_out_opc_imm(s, OPC_ANDI, a0, a1, a2);
        return;
    }

    tcg_debug_assert(use_mips32r2_instructions);
    tcg_debug_assert(is_p2m1(a2));
    msb = ctz64(~a2) - 1;
    if (type == TCG_TYPE_I32) {
        tcg_out_opc_bf(s, OPC_EXT, a0, a1, msb, 0);
    } else {
        tcg_out_opc_bf64(s, OPC_DEXT, OPC_DEXTM, OPC_DEXTU, a0, a1, msb, 0);
    }
}

static const TCGOutOpBinary outop_and = {
    .base.static_constraint = C_O1_I2(r, r, rIK),
    .out_rrr = tgen_and,
    .out_rri = tgen_andi,
};

static const TCGOutOpBinary outop_andc = {
    .base.static_constraint = C_NotImplemented,
};

static void tgen_clz(TCGContext *s, TCGType type,
                     TCGReg a0, TCGReg a1, TCGReg a2)
{
    if (use_mips32r6_instructions) {
        MIPSInsn opcv6 = type == TCG_TYPE_I32 ? OPC_CLZ_R6 : OPC_DCLZ_R6;
        tcg_out_opc_reg(s, opcv6, TCG_TMP0, a1, 0);
        tgen_movcond(s, TCG_TYPE_REG, TCG_COND_EQ, a0, a1, a2, false,
                     TCG_TMP0, false, TCG_REG_ZERO, false);
    } else {
        MIPSInsn opcv2 = type == TCG_TYPE_I32 ? OPC_CLZ : OPC_DCLZ;
        if (a0 == a2) {
            tcg_out_opc_reg(s, opcv2, TCG_TMP0, a1, a1);
            tcg_out_opc_reg(s, OPC_MOVN, a0, TCG_TMP0, a1);
        } else if (a0 != a1) {
            tcg_out_opc_reg(s, opcv2, a0, a1, a1);
            tcg_out_opc_reg(s, OPC_MOVZ, a0, a2, a1);
        } else {
            tcg_out_opc_reg(s, opcv2, TCG_TMP0, a1, a1);
            tcg_out_opc_reg(s, OPC_MOVZ, TCG_TMP0, a2, a1);
            tcg_out_mov(s, type, a0, TCG_TMP0);
        }
    }
}

static void tgen_clzi(TCGContext *s, TCGType type,
                      TCGReg a0, TCGReg a1, tcg_target_long a2)
{
    if (a2 == 0) {
        tgen_clz(s, type, a0, a1, TCG_REG_ZERO);
    } else if (use_mips32r6_instructions) {
        MIPSInsn opcv6 = type == TCG_TYPE_I32 ? OPC_CLZ_R6 : OPC_DCLZ_R6;
        tcg_out_opc_reg(s, opcv6, a0, a1, 0);
    } else {
        MIPSInsn opcv2 = type == TCG_TYPE_I32 ? OPC_CLZ : OPC_DCLZ;
        tcg_out_opc_reg(s, opcv2, a0, a1, a1);
    }
}

static TCGConstraintSetIndex cset_clz(TCGType type, unsigned flags)
{
    return use_mips32r2_instructions ? C_O1_I2(r, r, rzW) : C_NotImplemented;
}

static const TCGOutOpBinary outop_clz = {
    .base.static_constraint = C_Dynamic,
    .base.dynamic_constraint = cset_clz,
    .out_rrr = tgen_clz,
    .out_rri = tgen_clzi,
};

static const TCGOutOpUnary outop_ctpop = {
    .base.static_constraint = C_NotImplemented,
};

static const TCGOutOpBinary outop_ctz = {
    .base.static_constraint = C_NotImplemented,
};

static void tgen_divs(TCGContext *s, TCGType type,
                      TCGReg a0, TCGReg a1, TCGReg a2)
{
    if (use_mips32r6_instructions) {
        if (type == TCG_TYPE_I32) {
            tcg_out_opc_reg(s, OPC_DIV_R6, a0, a1, a2);
        } else {
            tcg_out_opc_reg(s, OPC_DDIV_R6, a0, a1, a2);
        }
    } else {
        MIPSInsn insn = type == TCG_TYPE_I32 ? OPC_DIV : OPC_DDIV;
        tcg_out_opc_reg(s, insn, 0, a1, a2);
        tcg_out_opc_reg(s, OPC_MFLO, a0, 0, 0);
    }
}

static const TCGOutOpBinary outop_divs = {
    .base.static_constraint = C_O1_I2(r, r, r),
    .out_rrr = tgen_divs,
};

static const TCGOutOpDivRem outop_divs2 = {
    .base.static_constraint = C_NotImplemented,
};

static void tgen_divu(TCGContext *s, TCGType type,
                      TCGReg a0, TCGReg a1, TCGReg a2)
{
    if (use_mips32r6_instructions) {
        if (type == TCG_TYPE_I32) {
            tcg_out_opc_reg(s, OPC_DIVU_R6, a0, a1, a2);
        } else {
            tcg_out_opc_reg(s, OPC_DDIVU_R6, a0, a1, a2);
        }
    } else {
        MIPSInsn insn = type == TCG_TYPE_I32 ? OPC_DIVU : OPC_DDIVU;
        tcg_out_opc_reg(s, insn, 0, a1, a2);
        tcg_out_opc_reg(s, OPC_MFLO, a0, 0, 0);
    }
}

static const TCGOutOpBinary outop_divu = {
    .base.static_constraint = C_O1_I2(r, r, r),
    .out_rrr = tgen_divu,
};

static const TCGOutOpDivRem outop_divu2 = {
    .base.static_constraint = C_NotImplemented,
};

static const TCGOutOpBinary outop_eqv = {
    .base.static_constraint = C_NotImplemented,
};

static void tgen_extrh_i64_i32(TCGContext *s, TCGType t, TCGReg a0, TCGReg a1)
{
    tcg_out_dsra(s, a0, a1, 32);
}

static const TCGOutOpUnary outop_extrh_i64_i32 = {
    .base.static_constraint = C_O1_I1(r, r),
    .out_rr = tgen_extrh_i64_i32,
};

static void tgen_mul(TCGContext *s, TCGType type,
                     TCGReg a0, TCGReg a1, TCGReg a2)
{
    MIPSInsn insn;

    if (type == TCG_TYPE_I32) {
        if (use_mips32_instructions) {
            tcg_out_opc_reg(s, OPC_MUL, a0, a1, a2);
            return;
        }
        insn = OPC_MULT;
    } else {
        if (use_mips32r6_instructions) {
            tcg_out_opc_reg(s, OPC_DMUL, a0, a1, a2);
            return;
        }
        insn = OPC_DMULT;
    }
    tcg_out_opc_reg(s, insn, 0, a1, a2);
    tcg_out_opc_reg(s, OPC_MFLO, a0, 0, 0);
}

static const TCGOutOpBinary outop_mul = {
    .base.static_constraint = C_O1_I2(r, r, r),
    .out_rrr = tgen_mul,
};

static void tgen_muls2(TCGContext *s, TCGType type,
                       TCGReg a0, TCGReg a1, TCGReg a2, TCGReg a3)
{
    MIPSInsn insn = type == TCG_TYPE_I32 ? OPC_MULT : OPC_DMULT;
    tcg_out_opc_reg(s, insn, 0, a2, a3);
    tcg_out_opc_reg(s, OPC_MFLO, a0, 0, 0);
    tcg_out_opc_reg(s, OPC_MFHI, a1, 0, 0);
}

static TCGConstraintSetIndex cset_mul2(TCGType type, unsigned flags)
{
    return use_mips32r6_instructions ? C_NotImplemented : C_O2_I2(r, r, r, r);
}

static const TCGOutOpMul2 outop_muls2 = {
    .base.static_constraint = C_Dynamic,
    .base.dynamic_constraint = cset_mul2,
    .out_rrrr = tgen_muls2,
};

static void tgen_mulsh(TCGContext *s, TCGType type,
                       TCGReg a0, TCGReg a1, TCGReg a2)
{
    if (use_mips32r6_instructions) {
        MIPSInsn insn = type == TCG_TYPE_I32 ? OPC_MUH : OPC_DMUH;
        tcg_out_opc_reg(s, insn, a0, a1, a2);
    } else {
        MIPSInsn insn = type == TCG_TYPE_I32 ? OPC_MULT : OPC_DMULT;
        tcg_out_opc_reg(s, insn, 0, a1, a2);
        tcg_out_opc_reg(s, OPC_MFHI, a0, 0, 0);
    }
}

static const TCGOutOpBinary outop_mulsh = {
    .base.static_constraint = C_O1_I2(r, r, r),
    .out_rrr = tgen_mulsh,
};

static void tgen_mulu2(TCGContext *s, TCGType type,
                       TCGReg a0, TCGReg a1, TCGReg a2, TCGReg a3)
{
    MIPSInsn insn = type == TCG_TYPE_I32 ? OPC_MULTU : OPC_DMULTU;
    tcg_out_opc_reg(s, insn, 0, a2, a3);
    tcg_out_opc_reg(s, OPC_MFLO, a0, 0, 0);
    tcg_out_opc_reg(s, OPC_MFHI, a1, 0, 0);
}

static const TCGOutOpMul2 outop_mulu2 = {
    .base.static_constraint = C_Dynamic,
    .base.dynamic_constraint = cset_mul2,
    .out_rrrr = tgen_mulu2,
};

static void tgen_muluh(TCGContext *s, TCGType type,
                       TCGReg a0, TCGReg a1, TCGReg a2)
{
    if (use_mips32r6_instructions) {
        MIPSInsn insn = type == TCG_TYPE_I32 ? OPC_MUHU : OPC_DMUHU;
        tcg_out_opc_reg(s, insn, a0, a1, a2);
    } else {
        MIPSInsn insn = type == TCG_TYPE_I32 ? OPC_MULTU : OPC_DMULTU;
        tcg_out_opc_reg(s, insn, 0, a1, a2);
        tcg_out_opc_reg(s, OPC_MFHI, a0, 0, 0);
    }
}

static const TCGOutOpBinary outop_muluh = {
    .base.static_constraint = C_O1_I2(r, r, r),
    .out_rrr = tgen_muluh,
};

static const TCGOutOpBinary outop_nand = {
    .base.static_constraint = C_NotImplemented,
};

static void tgen_nor(TCGContext *s, TCGType type,
                     TCGReg a0, TCGReg a1, TCGReg a2)
{
    tcg_out_opc_reg(s, OPC_NOR, a0, a1, a2);
}

static const TCGOutOpBinary outop_nor = {
    .base.static_constraint = C_O1_I2(r, r, r),
    .out_rrr = tgen_nor,
};

static void tgen_or(TCGContext *s, TCGType type,
                    TCGReg a0, TCGReg a1, TCGReg a2)
{
    tcg_out_opc_reg(s, OPC_OR, a0, a1, a2);
}

static void tgen_ori(TCGContext *s, TCGType type,
                     TCGReg a0, TCGReg a1, tcg_target_long a2)
{
    tcg_out_opc_imm(s, OPC_ORI, a0, a1, a2);
}

static const TCGOutOpBinary outop_or = {
    .base.static_constraint = C_O1_I2(r, r, rI),
    .out_rrr = tgen_or,
    .out_rri = tgen_ori,
};

static const TCGOutOpBinary outop_orc = {
    .base.static_constraint = C_NotImplemented,
};

static void tgen_rems(TCGContext *s, TCGType type,
                      TCGReg a0, TCGReg a1, TCGReg a2)
{
    if (use_mips32r6_instructions) {
        if (type == TCG_TYPE_I32) {
            tcg_out_opc_reg(s, OPC_MOD, a0, a1, a2);
        } else {
            tcg_out_opc_reg(s, OPC_DMOD, a0, a1, a2);
        }
    } else {
        MIPSInsn insn = type == TCG_TYPE_I32 ? OPC_DIV : OPC_DDIV;
        tcg_out_opc_reg(s, insn, 0, a1, a2);
        tcg_out_opc_reg(s, OPC_MFHI, a0, 0, 0);
    }
}

static const TCGOutOpBinary outop_rems = {
    .base.static_constraint = C_O1_I2(r, r, r),
    .out_rrr = tgen_rems,
};

static void tgen_remu(TCGContext *s, TCGType type,
                      TCGReg a0, TCGReg a1, TCGReg a2)
{
    if (use_mips32r6_instructions) {
        if (type == TCG_TYPE_I32) {
            tcg_out_opc_reg(s, OPC_MODU, a0, a1, a2);
        } else {
            tcg_out_opc_reg(s, OPC_DMODU, a0, a1, a2);
        }
    } else {
        MIPSInsn insn = type == TCG_TYPE_I32 ? OPC_DIVU : OPC_DDIVU;
        tcg_out_opc_reg(s, insn, 0, a1, a2);
        tcg_out_opc_reg(s, OPC_MFHI, a0, 0, 0);
    }
}

static const TCGOutOpBinary outop_remu = {
    .base.static_constraint = C_O1_I2(r, r, r),
    .out_rrr = tgen_remu,
};

static const TCGOutOpBinary outop_rotl = {
    .base.static_constraint = C_NotImplemented,
};

static TCGConstraintSetIndex cset_rotr(TCGType type, unsigned flags)
{
    return use_mips32r2_instructions ? C_O1_I2(r, r, ri) : C_NotImplemented;
}

static void tgen_rotr(TCGContext *s, TCGType type,
                     TCGReg a0, TCGReg a1, TCGReg a2)
{
    MIPSInsn insn = type == TCG_TYPE_I32 ? OPC_ROTRV : OPC_DROTRV;
    tcg_out_opc_reg(s, insn, a0, a1, a2);
}

static void tgen_rotri(TCGContext *s, TCGType type,
                      TCGReg a0, TCGReg a1, tcg_target_long a2)
{
    if (type == TCG_TYPE_I32) {
        tcg_out_opc_sa(s, OPC_ROTR, a0, a1, a2);
    } else {
        tcg_out_opc_sa64(s, OPC_DROTR, OPC_DROTR32, a0, a1, a2);
    }
}

static const TCGOutOpBinary outop_rotr = {
    .base.static_constraint = C_Dynamic,
    .base.dynamic_constraint = cset_rotr,
    .out_rrr = tgen_rotr,
    .out_rri = tgen_rotri,
};

static void tgen_sar(TCGContext *s, TCGType type,
                     TCGReg a0, TCGReg a1, TCGReg a2)
{
    MIPSInsn insn = type == TCG_TYPE_I32 ? OPC_SRAV : OPC_DSRAV;
    tcg_out_opc_reg(s, insn, a0, a1, a2);
}

static void tgen_sari(TCGContext *s, TCGType type,
                      TCGReg a0, TCGReg a1, tcg_target_long a2)
{
    if (type == TCG_TYPE_I32) {
        tcg_out_opc_sa(s, OPC_SRA, a0, a1, a2);
    } else {
        tcg_out_dsra(s, a0, a1, a2);
    }
}

static const TCGOutOpBinary outop_sar = {
    .base.static_constraint = C_O1_I2(r, r, ri),
    .out_rrr = tgen_sar,
    .out_rri = tgen_sari,
};

static void tgen_shl(TCGContext *s, TCGType type,
                     TCGReg a0, TCGReg a1, TCGReg a2)
{
    MIPSInsn insn = type == TCG_TYPE_I32 ? OPC_SLLV : OPC_DSLLV;
    tcg_out_opc_reg(s, insn, a0, a1, a2);
}

static void tgen_shli(TCGContext *s, TCGType type,
                      TCGReg a0, TCGReg a1, tcg_target_long a2)
{
    if (type == TCG_TYPE_I32) {
        tcg_out_opc_sa(s, OPC_SLL, a0, a1, a2);
    } else {
        tcg_out_dsll(s, a0, a1, a2);
    }
}

static const TCGOutOpBinary outop_shl = {
    .base.static_constraint = C_O1_I2(r, r, ri),
    .out_rrr = tgen_shl,
    .out_rri = tgen_shli,
};

static void tgen_shr(TCGContext *s, TCGType type,
                     TCGReg a0, TCGReg a1, TCGReg a2)
{
    MIPSInsn insn = type == TCG_TYPE_I32 ? OPC_SRLV : OPC_DSRLV;
    tcg_out_opc_reg(s, insn, a0, a1, a2);
}

static void tgen_shri(TCGContext *s, TCGType type,
                      TCGReg a0, TCGReg a1, tcg_target_long a2)
{
    if (type == TCG_TYPE_I32) {
        tcg_out_opc_sa(s, OPC_SRL, a0, a1, a2);
    } else {
        tcg_out_dsrl(s, a0, a1, a2);
    }
}

static const TCGOutOpBinary outop_shr = {
    .base.static_constraint = C_O1_I2(r, r, ri),
    .out_rrr = tgen_shr,
    .out_rri = tgen_shri,
};

static void tgen_sub(TCGContext *s, TCGType type,
                     TCGReg a0, TCGReg a1, TCGReg a2)
{
    MIPSInsn insn = type == TCG_TYPE_I32 ? OPC_SUBU : OPC_DSUBU;
    tcg_out_opc_reg(s, insn, a0, a1, a2);
}

static const TCGOutOpSubtract outop_sub = {
    .base.static_constraint = C_O1_I2(r, r, r),
    .out_rrr = tgen_sub,
};

static const TCGOutOpAddSubCarry outop_subbo = {
    .base.static_constraint = C_NotImplemented,
};

static const TCGOutOpAddSubCarry outop_subbi = {
    .base.static_constraint = C_NotImplemented,
};

static const TCGOutOpAddSubCarry outop_subbio = {
    .base.static_constraint = C_NotImplemented,
};

static void tcg_out_set_borrow(TCGContext *s)
{
    g_assert_not_reached();
}

static void tgen_xor(TCGContext *s, TCGType type,
                     TCGReg a0, TCGReg a1, TCGReg a2)
{
    tcg_out_opc_reg(s, OPC_XOR, a0, a1, a2);
}

static void tgen_xori(TCGContext *s, TCGType type,
                      TCGReg a0, TCGReg a1, tcg_target_long a2)
{
    tcg_out_opc_imm(s, OPC_XORI, a0, a1, a2);
}

static const TCGOutOpBinary outop_xor = {
    .base.static_constraint = C_O1_I2(r, r, rI),
    .out_rrr = tgen_xor,
    .out_rri = tgen_xori,
};

static void tgen_bswap16(TCGContext *s, TCGType type,
                         TCGReg ret, TCGReg arg, unsigned flags)
{
    /* With arg = abcd: */
    if (use_mips32r2_instructions) {
        tcg_out_opc_reg(s, OPC_WSBH, ret, 0, arg);                 /* badc */
        if (flags & TCG_BSWAP_OS) {
            tcg_out_opc_reg(s, OPC_SEH, ret, 0, ret);              /* ssdc */
        } else if ((flags & (TCG_BSWAP_IZ | TCG_BSWAP_OZ)) == TCG_BSWAP_OZ) {
            tcg_out_opc_imm(s, OPC_ANDI, ret, ret, 0xffff);        /* 00dc */
        }
        return;
    }

    tcg_out_opc_sa(s, OPC_SRL, TCG_TMP0, arg, 8);                  /* 0abc */
    if (!(flags & TCG_BSWAP_IZ)) {
        tcg_out_opc_imm(s, OPC_ANDI, TCG_TMP0, TCG_TMP0, 0x00ff);  /* 000c */
    }
    if (flags & TCG_BSWAP_OS) {
        tcg_out_opc_sa(s, OPC_SLL, ret, arg, 24);                  /* d000 */
        tcg_out_opc_sa(s, OPC_SRA, ret, ret, 16);                  /* ssd0 */
    } else {
        tcg_out_opc_sa(s, OPC_SLL, ret, arg, 8);                   /* bcd0 */
        if (flags & TCG_BSWAP_OZ) {
            tcg_out_opc_imm(s, OPC_ANDI, ret, ret, 0xff00);        /* 00d0 */
        }
    }
    tcg_out_opc_reg(s, OPC_OR, ret, ret, TCG_TMP0);                /* ssdc */
}

static const TCGOutOpBswap outop_bswap16 = {
    .base.static_constraint = C_O1_I1(r, r),
    .out_rr = tgen_bswap16,
};

static void tgen_bswap32(TCGContext *s, TCGType type,
                         TCGReg ret, TCGReg arg, unsigned flags)
{
    if (use_mips32r2_instructions) {
        tcg_out_opc_reg(s, OPC_WSBH, ret, 0, arg);
        tcg_out_opc_sa(s, OPC_ROTR, ret, ret, 16);
        if (flags & TCG_BSWAP_OZ) {
            tcg_out_opc_bf(s, OPC_DEXT, ret, ret, 31, 0);
        }
    } else {
        if (flags & TCG_BSWAP_OZ) {
            tcg_out_bswap_subr(s, bswap32u_addr);
        } else {
            tcg_out_bswap_subr(s, bswap32_addr);
        }
        /* delay slot -- never omit the insn, like tcg_out_mov might.  */
        tcg_out_opc_reg(s, OPC_OR, TCG_TMP0, arg, TCG_REG_ZERO);
        tcg_out_mov(s, TCG_TYPE_I32, ret, TCG_TMP3);
    }
}

static const TCGOutOpBswap outop_bswap32 = {
    .base.static_constraint = C_O1_I1(r, r),
    .out_rr = tgen_bswap32,
};

static void tgen_bswap64(TCGContext *s, TCGType type, TCGReg ret, TCGReg arg)
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

static const TCGOutOpUnary outop_bswap64 = {
    .base.static_constraint = C_O1_I1(r, r),
    .out_rr = tgen_bswap64,
};

static void tgen_neg(TCGContext *s, TCGType type, TCGReg a0, TCGReg a1)
{
    tgen_sub(s, type, a0, TCG_REG_ZERO, a1);
}

static const TCGOutOpUnary outop_neg = {
    .base.static_constraint = C_O1_I1(r, r),
    .out_rr = tgen_neg,
};

static void tgen_not(TCGContext *s, TCGType type, TCGReg a0, TCGReg a1)
{
    tgen_nor(s, type, a0, TCG_REG_ZERO, a1);
}

static const TCGOutOpUnary outop_not = {
    .base.static_constraint = C_O1_I1(r, r),
    .out_rr = tgen_not,
};

static void tgen_deposit(TCGContext *s, TCGType type, TCGReg a0, TCGReg a1,
                         TCGReg a2, unsigned ofs, unsigned len)
{
    if (type == TCG_TYPE_I32) {
        tcg_out_opc_bf(s, OPC_INS, a0, a2, ofs + len - 1, ofs);
    } else {
        tcg_out_opc_bf64(s, OPC_DINS, OPC_DINSM, OPC_DINSU, a0, a2,
                         ofs + len - 1, ofs);
    }
}

static const TCGOutOpDeposit outop_deposit = {
    .base.static_constraint = C_O1_I2(r, 0, rz),
    .out_rrr = tgen_deposit,
};

static void tgen_extract(TCGContext *s, TCGType type, TCGReg a0, TCGReg a1,
                         unsigned ofs, unsigned len)
{
    if (ofs == 0 && len <= 16) {
        tcg_out_opc_imm(s, OPC_ANDI, a0, a1, (1 << len) - 1);
    } else if (type == TCG_TYPE_I32) {
        tcg_out_opc_bf(s, OPC_EXT, a0, a1, len - 1, ofs);
    } else {
        tcg_out_opc_bf64(s, OPC_DEXT, OPC_DEXTM, OPC_DEXTU,
                         a0, a1, len - 1, ofs);
    }
}

static const TCGOutOpExtract outop_extract = {
    .base.static_constraint = C_O1_I1(r, r),
    .out_rr = tgen_extract,
};

static void tgen_sextract(TCGContext *s, TCGType type, TCGReg a0, TCGReg a1,
                          unsigned ofs, unsigned len)
{
    if (ofs == 0) {
        switch (len) {
        case 8:
            tcg_out_ext8s(s, type, a0, a1);
            return;
        case 16:
            tcg_out_ext16s(s, type, a0, a1);
            return;
        case 32:
            tcg_out_ext32s(s, a0, a1);
            return;
        }
    }
    g_assert_not_reached();
}

static const TCGOutOpExtract outop_sextract = {
    .base.static_constraint = C_O1_I1(r, r),
    .out_rr = tgen_sextract,
};

static const TCGOutOpExtract2 outop_extract2 = {
    .base.static_constraint = C_NotImplemented,
};

static void tgen_ld8u(TCGContext *s, TCGType type, TCGReg dest,
                      TCGReg base, ptrdiff_t offset)
{
    tcg_out_ldst(s, OPC_LBU, dest, base, offset);
}

static const TCGOutOpLoad outop_ld8u = {
    .base.static_constraint = C_O1_I1(r, r),
    .out = tgen_ld8u,
};

static void tgen_ld8s(TCGContext *s, TCGType type, TCGReg dest,
                      TCGReg base, ptrdiff_t offset)
{
    tcg_out_ldst(s, OPC_LB, dest, base, offset);
}

static const TCGOutOpLoad outop_ld8s = {
    .base.static_constraint = C_O1_I1(r, r),
    .out = tgen_ld8s,
};

static void tgen_ld16u(TCGContext *s, TCGType type, TCGReg dest,
                       TCGReg base, ptrdiff_t offset)
{
    tcg_out_ldst(s, OPC_LHU, dest, base, offset);
}

static const TCGOutOpLoad outop_ld16u = {
    .base.static_constraint = C_O1_I1(r, r),
    .out = tgen_ld16u,
};

static void tgen_ld16s(TCGContext *s, TCGType type, TCGReg dest,
                       TCGReg base, ptrdiff_t offset)
{
    tcg_out_ldst(s, OPC_LH, dest, base, offset);
}

static const TCGOutOpLoad outop_ld16s = {
    .base.static_constraint = C_O1_I1(r, r),
    .out = tgen_ld16s,
};

static void tgen_ld32u(TCGContext *s, TCGType type, TCGReg dest,
                       TCGReg base, ptrdiff_t offset)
{
    tcg_out_ldst(s, OPC_LWU, dest, base, offset);
}

static const TCGOutOpLoad outop_ld32u = {
    .base.static_constraint = C_O1_I1(r, r),
    .out = tgen_ld32u,
};

static void tgen_ld32s(TCGContext *s, TCGType type, TCGReg dest,
                       TCGReg base, ptrdiff_t offset)
{
    tcg_out_ldst(s, OPC_LW, dest, base, offset);
}

static const TCGOutOpLoad outop_ld32s = {
    .base.static_constraint = C_O1_I1(r, r),
    .out = tgen_ld32s,
};

static void tgen_st8_r(TCGContext *s, TCGType type, TCGReg data,
                       TCGReg base, ptrdiff_t offset)
{
    tcg_out_ldst(s, OPC_SB, data, base, offset);
}

static const TCGOutOpStore outop_st8 = {
    .base.static_constraint = C_O0_I2(rz, r),
    .out_r = tgen_st8_r,
};

static void tgen_st16_r(TCGContext *s, TCGType type, TCGReg data,
                        TCGReg base, ptrdiff_t offset)
{
    tcg_out_ldst(s, OPC_SH, data, base, offset);
}

static const TCGOutOpStore outop_st16 = {
    .base.static_constraint = C_O0_I2(rz, r),
    .out_r = tgen_st16_r,
};

static const TCGOutOpStore outop_st = {
    .base.static_constraint = C_O0_I2(rz, r),
    .out_r = tcg_out_st,
};


static TCGConstraintSetIndex
tcg_target_op_def(TCGOpcode op, TCGType type, unsigned flags)
{
    return C_NotImplemented;
}

static const int tcg_target_callee_save_regs[] = {
    TCG_REG_S0,
    TCG_REG_S1,
    TCG_REG_S2,
    TCG_REG_S3,
    TCG_REG_S4,
    TCG_REG_S5,
    TCG_REG_S6,       /* used for the tb base (TCG_REG_TB) */
    TCG_REG_S7,       /* used for guest_base */
    TCG_REG_S8,       /* used for the global env (TCG_AREG0) */
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
#define REG_SIZE   8
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
    tcg_out_opc_imm(s, OPC_DADDIU, TCG_REG_SP, TCG_REG_SP, -FRAME_SIZE);
    for (i = 0; i < ARRAY_SIZE(tcg_target_callee_save_regs); i++) {
        tcg_out_st(s, TCG_TYPE_REG, tcg_target_callee_save_regs[i],
                   TCG_REG_SP, SAVE_OFS + i * REG_SIZE);
    }

    if (!tcg_use_softmmu && guest_base != (int16_t)guest_base) {
        /*
         * The function call abi for n32 and n64 will have loaded $25 (t9)
         * with the address of the prologue, so we can use that instead
         * of TCG_REG_TB.
         */
#if !defined(__mips_abicalls)
# error "Unknown mips abi"
#endif
        tcg_out_movi_int(s, TCG_TYPE_PTR, TCG_GUEST_BASE_REG, guest_base,
                         TCG_REG_T9);
        tcg_regset_set_reg(s->reserved_regs, TCG_GUEST_BASE_REG);
    }

    tcg_out_mov(s, TCG_TYPE_PTR, TCG_REG_TB, tcg_target_call_iarg_regs[1]);

    /* Call generated code */
    tcg_out_opc_reg(s, OPC_JR, 0, tcg_target_call_iarg_regs[1], 0);
    /* delay slot */
    tcg_out_mov(s, TCG_TYPE_PTR, TCG_AREG0, tcg_target_call_iarg_regs[0]);

    /*
     * Return path for goto_ptr. Set return value to 0, a-la exit_tb,
     * and fall through to the rest of the epilogue.
     */
    tcg_code_gen_epilogue = tcg_splitwx_to_rx(s->code_ptr);
    tcg_out_mov(s, TCG_TYPE_REG, TCG_REG_V0, TCG_REG_ZERO);

    /* TB epilogue */
    tb_ret_addr = tcg_splitwx_to_rx(s->code_ptr);
    for (i = 0; i < ARRAY_SIZE(tcg_target_callee_save_regs); i++) {
        tcg_out_ld(s, TCG_TYPE_REG, tcg_target_callee_save_regs[i],
                   TCG_REG_SP, SAVE_OFS + i * REG_SIZE);
    }

    tcg_out_opc_reg(s, OPC_JR, 0, TCG_REG_RA, 0);
    /* delay slot */
    tcg_out_opc_imm(s, OPC_DADDIU, TCG_REG_SP, TCG_REG_SP, FRAME_SIZE);

    if (use_mips32r2_instructions) {
        return;
    }

    /* Bswap subroutines: Input in TCG_TMP0, output in TCG_TMP3;
       clobbers TCG_TMP1, TCG_TMP2.  */

    /*
     * bswap32 -- 32-bit swap (signed result for mips64).  a0 = abcd.
     */
    bswap32_addr = tcg_splitwx_to_rx(align_code_ptr(s));
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

    /*
     * bswap32u -- unsigned 32-bit swap.  a0 = ....abcd.
     */
    bswap32u_addr = tcg_splitwx_to_rx(align_code_ptr(s));
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
    bswap64_addr = tcg_splitwx_to_rx(align_code_ptr(s));
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

static void tcg_out_tb_start(TCGContext *s)
{
    /* nothing to do */
}

static void tcg_target_init(TCGContext *s)
{
    tcg_target_detect_isa();
    tcg_target_available_regs[TCG_TYPE_I32] = 0xffffffff;
    tcg_target_available_regs[TCG_TYPE_I64] = 0xffffffff;

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
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_TB);   /* tc->tc_ptr */
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
    .h.cie.data_align = -REG_SIZE & 0x7f, /* sleb128 */
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

void tcg_register_jit(const void *buf, size_t buf_size)
{
    tcg_register_jit_int(buf, buf_size, &debug_frame, sizeof(debug_frame));
}

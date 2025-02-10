/*
 * Tiny Code Generator for QEMU
 *
 * Copyright (c) 2018 SiFive, Inc
 * Copyright (c) 2008-2009 Arnaud Patard <arnaud.patard@rtp-net.org>
 * Copyright (c) 2009 Aurelien Jarno <aurelien@aurel32.net>
 * Copyright (c) 2008 Fabrice Bellard
 *
 * Based on i386/tcg-target.c and mips/tcg-target.c
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

/* Used for function call generation. */
#define TCG_REG_CALL_STACK              TCG_REG_SP
#define TCG_TARGET_STACK_ALIGN          16
#define TCG_TARGET_CALL_STACK_OFFSET    0
#define TCG_TARGET_CALL_ARG_I32         TCG_CALL_ARG_NORMAL
#define TCG_TARGET_CALL_ARG_I64         TCG_CALL_ARG_NORMAL
#define TCG_TARGET_CALL_ARG_I128        TCG_CALL_ARG_NORMAL
#define TCG_TARGET_CALL_RET_I128        TCG_CALL_RET_NORMAL

#ifdef CONFIG_DEBUG_TCG
static const char * const tcg_target_reg_names[TCG_TARGET_NB_REGS] = {
    "zero", "ra",  "sp",  "gp",  "tp",  "t0",  "t1",  "t2",
    "s0",   "s1",  "a0",  "a1",  "a2",  "a3",  "a4",  "a5",
    "a6",   "a7",  "s2",  "s3",  "s4",  "s5",  "s6",  "s7",
    "s8",   "s9",  "s10", "s11", "t3",  "t4",  "t5",  "t6",
    "v0",   "v1",  "v2",  "v3",  "v4",  "v5",  "v6",  "v7",
    "v8",   "v9",  "v10", "v11", "v12", "v13", "v14", "v15",
    "v16",  "v17", "v18", "v19", "v20", "v21", "v22", "v23",
    "v24",  "v25", "v26", "v27", "v28", "v29", "v30", "v31",
};
#endif

static const int tcg_target_reg_alloc_order[] = {
    /* Call saved registers */
    /* TCG_REG_S0 reserved for TCG_AREG0 */
    TCG_REG_S1,
    TCG_REG_S2,
    TCG_REG_S3,
    TCG_REG_S4,
    TCG_REG_S5,
    TCG_REG_S6,
    TCG_REG_S7,
    TCG_REG_S8,
    TCG_REG_S9,
    TCG_REG_S10,
    TCG_REG_S11,

    /* Call clobbered registers */
    TCG_REG_T0,
    TCG_REG_T1,
    TCG_REG_T2,
    TCG_REG_T3,
    TCG_REG_T4,
    TCG_REG_T5,
    TCG_REG_T6,

    /* Argument registers */
    TCG_REG_A0,
    TCG_REG_A1,
    TCG_REG_A2,
    TCG_REG_A3,
    TCG_REG_A4,
    TCG_REG_A5,
    TCG_REG_A6,
    TCG_REG_A7,

    /* Vector registers and TCG_REG_V0 reserved for mask. */
    TCG_REG_V1,  TCG_REG_V2,  TCG_REG_V3,  TCG_REG_V4,
    TCG_REG_V5,  TCG_REG_V6,  TCG_REG_V7,  TCG_REG_V8,
    TCG_REG_V9,  TCG_REG_V10, TCG_REG_V11, TCG_REG_V12,
    TCG_REG_V13, TCG_REG_V14, TCG_REG_V15, TCG_REG_V16,
    TCG_REG_V17, TCG_REG_V18, TCG_REG_V19, TCG_REG_V20,
    TCG_REG_V21, TCG_REG_V22, TCG_REG_V23, TCG_REG_V24,
    TCG_REG_V25, TCG_REG_V26, TCG_REG_V27, TCG_REG_V28,
    TCG_REG_V29, TCG_REG_V30, TCG_REG_V31,
};

static const int tcg_target_call_iarg_regs[] = {
    TCG_REG_A0,
    TCG_REG_A1,
    TCG_REG_A2,
    TCG_REG_A3,
    TCG_REG_A4,
    TCG_REG_A5,
    TCG_REG_A6,
    TCG_REG_A7,
};

static TCGReg tcg_target_call_oarg_reg(TCGCallReturnKind kind, int slot)
{
    tcg_debug_assert(kind == TCG_CALL_RET_NORMAL);
    tcg_debug_assert(slot >= 0 && slot <= 1);
    return TCG_REG_A0 + slot;
}

#define TCG_CT_CONST_S12     0x100
#define TCG_CT_CONST_N12     0x200
#define TCG_CT_CONST_M12     0x400
#define TCG_CT_CONST_J12     0x800
#define TCG_CT_CONST_S5     0x1000
#define TCG_CT_CONST_CMP_VI 0x2000

#define ALL_GENERAL_REGS   MAKE_64BIT_MASK(0, 32)
#define ALL_VECTOR_REGS    MAKE_64BIT_MASK(32, 32)
#define ALL_DVECTOR_REG_GROUPS 0x5555555500000000
#define ALL_QVECTOR_REG_GROUPS 0x1111111100000000

#define sextreg  sextract64

/*
 * RISC-V Base ISA opcodes (IM)
 */

#define V_OPIVV (0x0 << 12)
#define V_OPFVV (0x1 << 12)
#define V_OPMVV (0x2 << 12)
#define V_OPIVI (0x3 << 12)
#define V_OPIVX (0x4 << 12)
#define V_OPFVF (0x5 << 12)
#define V_OPMVX (0x6 << 12)
#define V_OPCFG (0x7 << 12)

/* NF <= 7 && NF >= 0 */
#define V_NF(x) (x << 29)
#define V_UNIT_STRIDE (0x0 << 20)
#define V_UNIT_STRIDE_WHOLE_REG (0x8 << 20)

typedef enum {
    VLMUL_M1 = 0, /* LMUL=1 */
    VLMUL_M2,     /* LMUL=2 */
    VLMUL_M4,     /* LMUL=4 */
    VLMUL_M8,     /* LMUL=8 */
    VLMUL_RESERVED,
    VLMUL_MF8,    /* LMUL=1/8 */
    VLMUL_MF4,    /* LMUL=1/4 */
    VLMUL_MF2,    /* LMUL=1/2 */
} RISCVVlmul;

typedef enum {
    OPC_ADD = 0x33,
    OPC_ADDI = 0x13,
    OPC_AND = 0x7033,
    OPC_ANDI = 0x7013,
    OPC_AUIPC = 0x17,
    OPC_BEQ = 0x63,
    OPC_BEXTI = 0x48005013,
    OPC_BGE = 0x5063,
    OPC_BGEU = 0x7063,
    OPC_BLT = 0x4063,
    OPC_BLTU = 0x6063,
    OPC_BNE = 0x1063,
    OPC_DIV = 0x2004033,
    OPC_DIVU = 0x2005033,
    OPC_JAL = 0x6f,
    OPC_JALR = 0x67,
    OPC_LB = 0x3,
    OPC_LBU = 0x4003,
    OPC_LD = 0x3003,
    OPC_LH = 0x1003,
    OPC_LHU = 0x5003,
    OPC_LUI = 0x37,
    OPC_LW = 0x2003,
    OPC_LWU = 0x6003,
    OPC_MUL = 0x2000033,
    OPC_MULH = 0x2001033,
    OPC_MULHSU = 0x2002033,
    OPC_MULHU = 0x2003033,
    OPC_OR = 0x6033,
    OPC_ORI = 0x6013,
    OPC_REM = 0x2006033,
    OPC_REMU = 0x2007033,
    OPC_SB = 0x23,
    OPC_SD = 0x3023,
    OPC_SH = 0x1023,
    OPC_SLL = 0x1033,
    OPC_SLLI = 0x1013,
    OPC_SLT = 0x2033,
    OPC_SLTI = 0x2013,
    OPC_SLTIU = 0x3013,
    OPC_SLTU = 0x3033,
    OPC_SRA = 0x40005033,
    OPC_SRAI = 0x40005013,
    OPC_SRL = 0x5033,
    OPC_SRLI = 0x5013,
    OPC_SUB = 0x40000033,
    OPC_SW = 0x2023,
    OPC_XOR = 0x4033,
    OPC_XORI = 0x4013,

    OPC_ADDIW = 0x1b,
    OPC_ADDW = 0x3b,
    OPC_DIVUW = 0x200503b,
    OPC_DIVW = 0x200403b,
    OPC_MULW = 0x200003b,
    OPC_REMUW = 0x200703b,
    OPC_REMW = 0x200603b,
    OPC_SLLIW = 0x101b,
    OPC_SLLW = 0x103b,
    OPC_SRAIW = 0x4000501b,
    OPC_SRAW = 0x4000503b,
    OPC_SRLIW = 0x501b,
    OPC_SRLW = 0x503b,
    OPC_SUBW = 0x4000003b,

    OPC_FENCE = 0x0000000f,
    OPC_NOP   = OPC_ADDI,   /* nop = addi r0,r0,0 */

    /* Zba: Bit manipulation extension, address generation */
    OPC_ADD_UW = 0x0800003b,

    /* Zbb: Bit manipulation extension, basic bit manipulation */
    OPC_ANDN   = 0x40007033,
    OPC_CLZ    = 0x60001013,
    OPC_CLZW   = 0x6000101b,
    OPC_CPOP   = 0x60201013,
    OPC_CPOPW  = 0x6020101b,
    OPC_CTZ    = 0x60101013,
    OPC_CTZW   = 0x6010101b,
    OPC_ORN    = 0x40006033,
    OPC_REV8   = 0x6b805013,
    OPC_ROL    = 0x60001033,
    OPC_ROLW   = 0x6000103b,
    OPC_ROR    = 0x60005033,
    OPC_RORW   = 0x6000503b,
    OPC_RORI   = 0x60005013,
    OPC_RORIW  = 0x6000501b,
    OPC_SEXT_B = 0x60401013,
    OPC_SEXT_H = 0x60501013,
    OPC_XNOR   = 0x40004033,
    OPC_ZEXT_H = 0x0800403b,

    /* Zicond: integer conditional operations */
    OPC_CZERO_EQZ = 0x0e005033,
    OPC_CZERO_NEZ = 0x0e007033,

    /* V: Vector extension 1.0 */
    OPC_VSETVLI  = 0x57 | V_OPCFG,
    OPC_VSETIVLI = 0xc0000057 | V_OPCFG,
    OPC_VSETVL   = 0x80000057 | V_OPCFG,

    OPC_VLE8_V  = 0x7 | V_UNIT_STRIDE,
    OPC_VLE16_V = 0x5007 | V_UNIT_STRIDE,
    OPC_VLE32_V = 0x6007 | V_UNIT_STRIDE,
    OPC_VLE64_V = 0x7007 | V_UNIT_STRIDE,
    OPC_VSE8_V  = 0x27 | V_UNIT_STRIDE,
    OPC_VSE16_V = 0x5027 | V_UNIT_STRIDE,
    OPC_VSE32_V = 0x6027 | V_UNIT_STRIDE,
    OPC_VSE64_V = 0x7027 | V_UNIT_STRIDE,

    OPC_VL1RE64_V = 0x2007007 | V_UNIT_STRIDE_WHOLE_REG | V_NF(0),
    OPC_VL2RE64_V = 0x2007007 | V_UNIT_STRIDE_WHOLE_REG | V_NF(1),
    OPC_VL4RE64_V = 0x2007007 | V_UNIT_STRIDE_WHOLE_REG | V_NF(3),
    OPC_VL8RE64_V = 0x2007007 | V_UNIT_STRIDE_WHOLE_REG | V_NF(7),

    OPC_VS1R_V = 0x2000027 | V_UNIT_STRIDE_WHOLE_REG | V_NF(0),
    OPC_VS2R_V = 0x2000027 | V_UNIT_STRIDE_WHOLE_REG | V_NF(1),
    OPC_VS4R_V = 0x2000027 | V_UNIT_STRIDE_WHOLE_REG | V_NF(3),
    OPC_VS8R_V = 0x2000027 | V_UNIT_STRIDE_WHOLE_REG | V_NF(7),

    OPC_VMERGE_VIM = 0x5c000057 | V_OPIVI,
    OPC_VMERGE_VVM = 0x5c000057 | V_OPIVV,

    OPC_VADD_VV = 0x57 | V_OPIVV,
    OPC_VADD_VI = 0x57 | V_OPIVI,
    OPC_VSUB_VV = 0x8000057 | V_OPIVV,
    OPC_VRSUB_VI = 0xc000057 | V_OPIVI,
    OPC_VAND_VV = 0x24000057 | V_OPIVV,
    OPC_VAND_VI = 0x24000057 | V_OPIVI,
    OPC_VOR_VV = 0x28000057 | V_OPIVV,
    OPC_VOR_VI = 0x28000057 | V_OPIVI,
    OPC_VXOR_VV = 0x2c000057 | V_OPIVV,
    OPC_VXOR_VI = 0x2c000057 | V_OPIVI,

    OPC_VMUL_VV = 0x94000057 | V_OPMVV,
    OPC_VSADD_VV = 0x84000057 | V_OPIVV,
    OPC_VSADD_VI = 0x84000057 | V_OPIVI,
    OPC_VSSUB_VV = 0x8c000057 | V_OPIVV,
    OPC_VSSUB_VI = 0x8c000057 | V_OPIVI,
    OPC_VSADDU_VV = 0x80000057 | V_OPIVV,
    OPC_VSADDU_VI = 0x80000057 | V_OPIVI,
    OPC_VSSUBU_VV = 0x88000057 | V_OPIVV,
    OPC_VSSUBU_VI = 0x88000057 | V_OPIVI,

    OPC_VMAX_VV = 0x1c000057 | V_OPIVV,
    OPC_VMAX_VI = 0x1c000057 | V_OPIVI,
    OPC_VMAXU_VV = 0x18000057 | V_OPIVV,
    OPC_VMAXU_VI = 0x18000057 | V_OPIVI,
    OPC_VMIN_VV = 0x14000057 | V_OPIVV,
    OPC_VMIN_VI = 0x14000057 | V_OPIVI,
    OPC_VMINU_VV = 0x10000057 | V_OPIVV,
    OPC_VMINU_VI = 0x10000057 | V_OPIVI,

    OPC_VMSEQ_VV = 0x60000057 | V_OPIVV,
    OPC_VMSEQ_VI = 0x60000057 | V_OPIVI,
    OPC_VMSEQ_VX = 0x60000057 | V_OPIVX,
    OPC_VMSNE_VV = 0x64000057 | V_OPIVV,
    OPC_VMSNE_VI = 0x64000057 | V_OPIVI,
    OPC_VMSNE_VX = 0x64000057 | V_OPIVX,

    OPC_VMSLTU_VV = 0x68000057 | V_OPIVV,
    OPC_VMSLTU_VX = 0x68000057 | V_OPIVX,
    OPC_VMSLT_VV = 0x6c000057 | V_OPIVV,
    OPC_VMSLT_VX = 0x6c000057 | V_OPIVX,
    OPC_VMSLEU_VV = 0x70000057 | V_OPIVV,
    OPC_VMSLEU_VX = 0x70000057 | V_OPIVX,
    OPC_VMSLE_VV = 0x74000057 | V_OPIVV,
    OPC_VMSLE_VX = 0x74000057 | V_OPIVX,

    OPC_VMSLEU_VI = 0x70000057 | V_OPIVI,
    OPC_VMSLE_VI = 0x74000057 | V_OPIVI,
    OPC_VMSGTU_VI = 0x78000057 | V_OPIVI,
    OPC_VMSGTU_VX = 0x78000057 | V_OPIVX,
    OPC_VMSGT_VI = 0x7c000057 | V_OPIVI,
    OPC_VMSGT_VX = 0x7c000057 | V_OPIVX,

    OPC_VSLL_VV = 0x94000057 | V_OPIVV,
    OPC_VSLL_VI = 0x94000057 | V_OPIVI,
    OPC_VSLL_VX = 0x94000057 | V_OPIVX,
    OPC_VSRL_VV = 0xa0000057 | V_OPIVV,
    OPC_VSRL_VI = 0xa0000057 | V_OPIVI,
    OPC_VSRL_VX = 0xa0000057 | V_OPIVX,
    OPC_VSRA_VV = 0xa4000057 | V_OPIVV,
    OPC_VSRA_VI = 0xa4000057 | V_OPIVI,
    OPC_VSRA_VX = 0xa4000057 | V_OPIVX,

    OPC_VMV_V_V = 0x5e000057 | V_OPIVV,
    OPC_VMV_V_I = 0x5e000057 | V_OPIVI,
    OPC_VMV_V_X = 0x5e000057 | V_OPIVX,

    OPC_VMVNR_V = 0x9e000057 | V_OPIVI,
} RISCVInsn;

static const struct {
    RISCVInsn op;
    bool swap;
} tcg_cmpcond_to_rvv_vv[] = {
    [TCG_COND_EQ] =  { OPC_VMSEQ_VV,  false },
    [TCG_COND_NE] =  { OPC_VMSNE_VV,  false },
    [TCG_COND_LT] =  { OPC_VMSLT_VV,  false },
    [TCG_COND_GE] =  { OPC_VMSLE_VV,  true  },
    [TCG_COND_GT] =  { OPC_VMSLT_VV,  true  },
    [TCG_COND_LE] =  { OPC_VMSLE_VV,  false },
    [TCG_COND_LTU] = { OPC_VMSLTU_VV, false },
    [TCG_COND_GEU] = { OPC_VMSLEU_VV, true  },
    [TCG_COND_GTU] = { OPC_VMSLTU_VV, true  },
    [TCG_COND_LEU] = { OPC_VMSLEU_VV, false }
};

static const struct {
    RISCVInsn op;
    int min;
    int max;
    bool adjust;
}  tcg_cmpcond_to_rvv_vi[] = {
    [TCG_COND_EQ]  = { OPC_VMSEQ_VI,  -16, 15, false },
    [TCG_COND_NE]  = { OPC_VMSNE_VI,  -16, 15, false },
    [TCG_COND_GT]  = { OPC_VMSGT_VI,  -16, 15, false },
    [TCG_COND_LE]  = { OPC_VMSLE_VI,  -16, 15, false },
    [TCG_COND_LT]  = { OPC_VMSLE_VI,  -15, 16, true  },
    [TCG_COND_GE]  = { OPC_VMSGT_VI,  -15, 16, true  },
    [TCG_COND_LEU] = { OPC_VMSLEU_VI,   0, 15, false },
    [TCG_COND_GTU] = { OPC_VMSGTU_VI,   0, 15, false },
    [TCG_COND_LTU] = { OPC_VMSLEU_VI,   1, 16, true  },
    [TCG_COND_GEU] = { OPC_VMSGTU_VI,   1, 16, true  },
};

/* test if a constant matches the constraint */
static bool tcg_target_const_match(int64_t val, int ct,
                                   TCGType type, TCGCond cond, int vece)
{
    if (ct & TCG_CT_CONST) {
        return 1;
    }
    if (type >= TCG_TYPE_V64) {
        /* Val is replicated by VECE; extract the highest element. */
        val >>= (-8 << vece) & 63;
    }
    /*
     * Sign extended from 12 bits: [-0x800, 0x7ff].
     * Used for most arithmetic, as this is the isa field.
     */
    if ((ct & TCG_CT_CONST_S12) && val >= -0x800 && val <= 0x7ff) {
        return 1;
    }
    /*
     * Sign extended from 12 bits, negated: [-0x7ff, 0x800].
     * Used for subtraction, where a constant must be handled by ADDI.
     */
    if ((ct & TCG_CT_CONST_N12) && val >= -0x7ff && val <= 0x800) {
        return 1;
    }
    /*
     * Sign extended from 12 bits, +/- matching: [-0x7ff, 0x7ff].
     * Used by addsub2 and movcond, which may need the negative value,
     * and requires the modified constant to be representable.
     */
    if ((ct & TCG_CT_CONST_M12) && val >= -0x7ff && val <= 0x7ff) {
        return 1;
    }
    /*
     * Inverse of sign extended from 12 bits: ~[-0x800, 0x7ff].
     * Used to map ANDN back to ANDI, etc.
     */
    if ((ct & TCG_CT_CONST_J12) && ~val >= -0x800 && ~val <= 0x7ff) {
        return 1;
    }
    /*
     * Sign extended from 5 bits: [-0x10, 0x0f].
     * Used for vector-immediate.
     */
    if ((ct & TCG_CT_CONST_S5) && val >= -0x10 && val <= 0x0f) {
        return 1;
    }
    /*
     * Used for vector compare OPIVI instructions.
     */
    if ((ct & TCG_CT_CONST_CMP_VI) &&
        val >= tcg_cmpcond_to_rvv_vi[cond].min &&
        val <= tcg_cmpcond_to_rvv_vi[cond].max) {
        return true;
     }
    return 0;
}

/*
 * RISC-V immediate and instruction encoders (excludes 16-bit RVC)
 */

/* Type-R */

static int32_t encode_r(RISCVInsn opc, TCGReg rd, TCGReg rs1, TCGReg rs2)
{
    return opc | (rd & 0x1f) << 7 | (rs1 & 0x1f) << 15 | (rs2 & 0x1f) << 20;
}

/* Type-I */

static int32_t encode_imm12(uint32_t imm)
{
    return (imm & 0xfff) << 20;
}

static int32_t encode_i(RISCVInsn opc, TCGReg rd, TCGReg rs1, uint32_t imm)
{
    return opc | (rd & 0x1f) << 7 | (rs1 & 0x1f) << 15 | encode_imm12(imm);
}

/* Type-S */

static int32_t encode_simm12(uint32_t imm)
{
    int32_t ret = 0;

    ret |= (imm & 0xFE0) << 20;
    ret |= (imm & 0x1F) << 7;

    return ret;
}

static int32_t encode_s(RISCVInsn opc, TCGReg rs1, TCGReg rs2, uint32_t imm)
{
    return opc | (rs1 & 0x1f) << 15 | (rs2 & 0x1f) << 20 | encode_simm12(imm);
}

/* Type-SB */

static int32_t encode_sbimm12(uint32_t imm)
{
    int32_t ret = 0;

    ret |= (imm & 0x1000) << 19;
    ret |= (imm & 0x7e0) << 20;
    ret |= (imm & 0x1e) << 7;
    ret |= (imm & 0x800) >> 4;

    return ret;
}

static int32_t encode_sb(RISCVInsn opc, TCGReg rs1, TCGReg rs2, uint32_t imm)
{
    return opc | (rs1 & 0x1f) << 15 | (rs2 & 0x1f) << 20 | encode_sbimm12(imm);
}

/* Type-U */

static int32_t encode_uimm20(uint32_t imm)
{
    return imm & 0xfffff000;
}

static int32_t encode_u(RISCVInsn opc, TCGReg rd, uint32_t imm)
{
    return opc | (rd & 0x1f) << 7 | encode_uimm20(imm);
}

/* Type-UJ */

static int32_t encode_ujimm20(uint32_t imm)
{
    int32_t ret = 0;

    ret |= (imm & 0x0007fe) << (21 - 1);
    ret |= (imm & 0x000800) << (20 - 11);
    ret |= (imm & 0x0ff000) << (12 - 12);
    ret |= (imm & 0x100000) << (31 - 20);

    return ret;
}

static int32_t encode_uj(RISCVInsn opc, TCGReg rd, uint32_t imm)
{
    return opc | (rd & 0x1f) << 7 | encode_ujimm20(imm);
}


/* Type-OPIVI */

static int32_t encode_vi(RISCVInsn opc, TCGReg rd, int32_t imm,
                         TCGReg vs2, bool vm)
{
    return opc | (rd & 0x1f) << 7 | (imm & 0x1f) << 15 |
           (vs2 & 0x1f) << 20 | (vm << 25);
}

/* Type-OPIVV/OPMVV/OPIVX/OPMVX, Vector load and store */

static int32_t encode_v(RISCVInsn opc, TCGReg d, TCGReg s1,
                        TCGReg s2, bool vm)
{
    return opc | (d & 0x1f) << 7 | (s1 & 0x1f) << 15 |
           (s2 & 0x1f) << 20 | (vm << 25);
}

/* Vector vtype */

static uint32_t encode_vtype(bool vta, bool vma,
                            MemOp vsew, RISCVVlmul vlmul)
{
    return vma << 7 | vta << 6 | vsew << 3 | vlmul;
}

static int32_t encode_vset(RISCVInsn opc, TCGReg rd,
                           TCGArg rs1, uint32_t vtype)
{
    return opc | (rd & 0x1f) << 7 | (rs1 & 0x1f) << 15 | (vtype & 0x7ff) << 20;
}

static int32_t encode_vseti(RISCVInsn opc, TCGReg rd,
                            uint32_t uimm, uint32_t vtype)
{
    return opc | (rd & 0x1f) << 7 | (uimm & 0x1f) << 15 | (vtype & 0x3ff) << 20;
}

/*
 * RISC-V instruction emitters
 */

static void tcg_out_opc_reg(TCGContext *s, RISCVInsn opc,
                            TCGReg rd, TCGReg rs1, TCGReg rs2)
{
    tcg_out32(s, encode_r(opc, rd, rs1, rs2));
}

static void tcg_out_opc_imm(TCGContext *s, RISCVInsn opc,
                            TCGReg rd, TCGReg rs1, TCGArg imm)
{
    tcg_out32(s, encode_i(opc, rd, rs1, imm));
}

static void tcg_out_opc_store(TCGContext *s, RISCVInsn opc,
                              TCGReg rs1, TCGReg rs2, uint32_t imm)
{
    tcg_out32(s, encode_s(opc, rs1, rs2, imm));
}

static void tcg_out_opc_branch(TCGContext *s, RISCVInsn opc,
                               TCGReg rs1, TCGReg rs2, uint32_t imm)
{
    tcg_out32(s, encode_sb(opc, rs1, rs2, imm));
}

static void tcg_out_opc_upper(TCGContext *s, RISCVInsn opc,
                              TCGReg rd, uint32_t imm)
{
    tcg_out32(s, encode_u(opc, rd, imm));
}

static void tcg_out_opc_jump(TCGContext *s, RISCVInsn opc,
                             TCGReg rd, uint32_t imm)
{
    tcg_out32(s, encode_uj(opc, rd, imm));
}

static void tcg_out_nop_fill(tcg_insn_unit *p, int count)
{
    int i;
    for (i = 0; i < count; ++i) {
        p[i] = OPC_NOP;
    }
}

/*
 * Relocations
 */

static bool reloc_sbimm12(tcg_insn_unit *src_rw, const tcg_insn_unit *target)
{
    const tcg_insn_unit *src_rx = tcg_splitwx_to_rx(src_rw);
    intptr_t offset = (intptr_t)target - (intptr_t)src_rx;

    tcg_debug_assert((offset & 1) == 0);
    if (offset == sextreg(offset, 0, 12)) {
        *src_rw |= encode_sbimm12(offset);
        return true;
    }

    return false;
}

static bool reloc_jimm20(tcg_insn_unit *src_rw, const tcg_insn_unit *target)
{
    const tcg_insn_unit *src_rx = tcg_splitwx_to_rx(src_rw);
    intptr_t offset = (intptr_t)target - (intptr_t)src_rx;

    tcg_debug_assert((offset & 1) == 0);
    if (offset == sextreg(offset, 0, 20)) {
        *src_rw |= encode_ujimm20(offset);
        return true;
    }

    return false;
}

static bool reloc_call(tcg_insn_unit *src_rw, const tcg_insn_unit *target)
{
    const tcg_insn_unit *src_rx = tcg_splitwx_to_rx(src_rw);
    intptr_t offset = (intptr_t)target - (intptr_t)src_rx;
    int32_t lo = sextreg(offset, 0, 12);
    int32_t hi = offset - lo;

    if (offset == hi + lo) {
        src_rw[0] |= encode_uimm20(hi);
        src_rw[1] |= encode_imm12(lo);
        return true;
    }

    return false;
}

static bool patch_reloc(tcg_insn_unit *code_ptr, int type,
                        intptr_t value, intptr_t addend)
{
    tcg_debug_assert(addend == 0);
    switch (type) {
    case R_RISCV_BRANCH:
        return reloc_sbimm12(code_ptr, (tcg_insn_unit *)value);
    case R_RISCV_JAL:
        return reloc_jimm20(code_ptr, (tcg_insn_unit *)value);
    case R_RISCV_CALL:
        return reloc_call(code_ptr, (tcg_insn_unit *)value);
    default:
        g_assert_not_reached();
    }
}

/*
 * RISC-V vector instruction emitters
 */

/*
 * Vector registers uses the same 5 lower bits as GPR registers,
 * and vm=0 (vm = false) means vector masking ENABLED.
 * With RVV 1.0, vs2 is the first operand, while rs1/imm is the
 * second operand.
 */
static void tcg_out_opc_vv(TCGContext *s, RISCVInsn opc,
                           TCGReg vd, TCGReg vs2, TCGReg vs1)
{
    tcg_out32(s, encode_v(opc, vd, vs1, vs2, true));
}

static void tcg_out_opc_vx(TCGContext *s, RISCVInsn opc,
                           TCGReg vd, TCGReg vs2, TCGReg rs1)
{
    tcg_out32(s, encode_v(opc, vd, rs1, vs2, true));
}

static void tcg_out_opc_vi(TCGContext *s, RISCVInsn opc,
                           TCGReg vd, TCGReg vs2, int32_t imm)
{
    tcg_out32(s, encode_vi(opc, vd, imm, vs2, true));
}

static void tcg_out_opc_vv_vi(TCGContext *s, RISCVInsn o_vv, RISCVInsn o_vi,
                              TCGReg vd, TCGReg vs2, TCGArg vi1, int c_vi1)
{
    if (c_vi1) {
        tcg_out_opc_vi(s, o_vi, vd, vs2, vi1);
    } else {
        tcg_out_opc_vv(s, o_vv, vd, vs2, vi1);
    }
}

static void tcg_out_opc_vim_mask(TCGContext *s, RISCVInsn opc, TCGReg vd,
                                 TCGReg vs2, int32_t imm)
{
    tcg_out32(s, encode_vi(opc, vd, imm, vs2, false));
}

static void tcg_out_opc_vvm_mask(TCGContext *s, RISCVInsn opc, TCGReg vd,
                                 TCGReg vs2, TCGReg vs1)
{
    tcg_out32(s, encode_v(opc, vd, vs1, vs2, false));
}

typedef struct VsetCache {
    uint32_t movi_insn;
    uint32_t vset_insn;
} VsetCache;

static VsetCache riscv_vset_cache[3][4];

static void set_vtype(TCGContext *s, TCGType type, MemOp vsew)
{
    const VsetCache *p = &riscv_vset_cache[type - TCG_TYPE_V64][vsew];

    s->riscv_cur_type = type;
    s->riscv_cur_vsew = vsew;

    if (p->movi_insn) {
        tcg_out32(s, p->movi_insn);
    }
    tcg_out32(s, p->vset_insn);
}

static MemOp set_vtype_len(TCGContext *s, TCGType type)
{
    if (type != s->riscv_cur_type) {
        set_vtype(s, type, MO_64);
    }
    return s->riscv_cur_vsew;
}

static void set_vtype_len_sew(TCGContext *s, TCGType type, MemOp vsew)
{
    if (type != s->riscv_cur_type || vsew != s->riscv_cur_vsew) {
        set_vtype(s, type, vsew);
    }
}

/*
 * TCG intrinsics
 */

static bool tcg_out_mov(TCGContext *s, TCGType type, TCGReg ret, TCGReg arg)
{
    if (ret == arg) {
        return true;
    }
    switch (type) {
    case TCG_TYPE_I32:
    case TCG_TYPE_I64:
        tcg_out_opc_imm(s, OPC_ADDI, ret, arg, 0);
        break;
    case TCG_TYPE_V64:
    case TCG_TYPE_V128:
    case TCG_TYPE_V256:
        {
            int lmul = type - riscv_lg2_vlenb;
            int nf = 1 << MAX(lmul, 0);
            tcg_out_opc_vi(s, OPC_VMVNR_V, ret, arg, nf - 1);
        }
        break;
    default:
        g_assert_not_reached();
    }
    return true;
}

static void tcg_out_movi(TCGContext *s, TCGType type, TCGReg rd,
                         tcg_target_long val)
{
    tcg_target_long lo, hi, tmp;
    int shift, ret;

    if (type == TCG_TYPE_I32) {
        val = (int32_t)val;
    }

    lo = sextreg(val, 0, 12);
    if (val == lo) {
        tcg_out_opc_imm(s, OPC_ADDI, rd, TCG_REG_ZERO, lo);
        return;
    }

    hi = val - lo;
    if (val == (int32_t)val) {
        tcg_out_opc_upper(s, OPC_LUI, rd, hi);
        if (lo != 0) {
            tcg_out_opc_imm(s, OPC_ADDIW, rd, rd, lo);
        }
        return;
    }

    tmp = tcg_pcrel_diff(s, (void *)val);
    if (tmp == (int32_t)tmp) {
        tcg_out_opc_upper(s, OPC_AUIPC, rd, 0);
        tcg_out_opc_imm(s, OPC_ADDI, rd, rd, 0);
        ret = reloc_call(s->code_ptr - 2, (const tcg_insn_unit *)val);
        tcg_debug_assert(ret == true);
        return;
    }

    /* Look for a single 20-bit section.  */
    shift = ctz64(val);
    tmp = val >> shift;
    if (tmp == sextreg(tmp, 0, 20)) {
        tcg_out_opc_upper(s, OPC_LUI, rd, tmp << 12);
        if (shift > 12) {
            tcg_out_opc_imm(s, OPC_SLLI, rd, rd, shift - 12);
        } else {
            tcg_out_opc_imm(s, OPC_SRAI, rd, rd, 12 - shift);
        }
        return;
    }

    /* Look for a few high zero bits, with lots of bits set in the middle.  */
    shift = clz64(val);
    tmp = val << shift;
    if (tmp == sextreg(tmp, 12, 20) << 12) {
        tcg_out_opc_upper(s, OPC_LUI, rd, tmp);
        tcg_out_opc_imm(s, OPC_SRLI, rd, rd, shift);
        return;
    } else if (tmp == sextreg(tmp, 0, 12)) {
        tcg_out_opc_imm(s, OPC_ADDI, rd, TCG_REG_ZERO, tmp);
        tcg_out_opc_imm(s, OPC_SRLI, rd, rd, shift);
        return;
    }

    /* Drop into the constant pool.  */
    new_pool_label(s, val, R_RISCV_CALL, s->code_ptr, 0);
    tcg_out_opc_upper(s, OPC_AUIPC, rd, 0);
    tcg_out_opc_imm(s, OPC_LD, rd, rd, 0);
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

static void tcg_out_ext8u(TCGContext *s, TCGReg ret, TCGReg arg)
{
    tcg_out_opc_imm(s, OPC_ANDI, ret, arg, 0xff);
}

static void tcg_out_ext16u(TCGContext *s, TCGReg ret, TCGReg arg)
{
    if (cpuinfo & CPUINFO_ZBB) {
        tcg_out_opc_reg(s, OPC_ZEXT_H, ret, arg, TCG_REG_ZERO);
    } else {
        tcg_out_opc_imm(s, OPC_SLLIW, ret, arg, 16);
        tcg_out_opc_imm(s, OPC_SRLIW, ret, ret, 16);
    }
}

static void tcg_out_ext32u(TCGContext *s, TCGReg ret, TCGReg arg)
{
    if (cpuinfo & CPUINFO_ZBA) {
        tcg_out_opc_reg(s, OPC_ADD_UW, ret, arg, TCG_REG_ZERO);
    } else {
        tcg_out_opc_imm(s, OPC_SLLI, ret, arg, 32);
        tcg_out_opc_imm(s, OPC_SRLI, ret, ret, 32);
    }
}

static void tcg_out_ext8s(TCGContext *s, TCGType type, TCGReg ret, TCGReg arg)
{
    if (cpuinfo & CPUINFO_ZBB) {
        tcg_out_opc_imm(s, OPC_SEXT_B, ret, arg, 0);
    } else {
        tcg_out_opc_imm(s, OPC_SLLIW, ret, arg, 24);
        tcg_out_opc_imm(s, OPC_SRAIW, ret, ret, 24);
    }
}

static void tcg_out_ext16s(TCGContext *s, TCGType type, TCGReg ret, TCGReg arg)
{
    if (cpuinfo & CPUINFO_ZBB) {
        tcg_out_opc_imm(s, OPC_SEXT_H, ret, arg, 0);
    } else {
        tcg_out_opc_imm(s, OPC_SLLIW, ret, arg, 16);
        tcg_out_opc_imm(s, OPC_SRAIW, ret, ret, 16);
    }
}

static void tcg_out_ext32s(TCGContext *s, TCGReg ret, TCGReg arg)
{
    tcg_out_opc_imm(s, OPC_ADDIW, ret, arg, 0);
}

static void tcg_out_exts_i32_i64(TCGContext *s, TCGReg ret, TCGReg arg)
{
    if (ret != arg) {
        tcg_out_ext32s(s, ret, arg);
    }
}

static void tcg_out_extu_i32_i64(TCGContext *s, TCGReg ret, TCGReg arg)
{
    tcg_out_ext32u(s, ret, arg);
}

static void tcg_out_extrl_i64_i32(TCGContext *s, TCGReg ret, TCGReg arg)
{
    tcg_out_ext32s(s, ret, arg);
}

static void tcg_out_ldst(TCGContext *s, RISCVInsn opc, TCGReg data,
                         TCGReg addr, intptr_t offset)
{
    intptr_t imm12 = sextreg(offset, 0, 12);

    if (offset != imm12) {
        intptr_t diff = tcg_pcrel_diff(s, (void *)offset);

        if (addr == TCG_REG_ZERO && diff == (int32_t)diff) {
            imm12 = sextreg(diff, 0, 12);
            tcg_out_opc_upper(s, OPC_AUIPC, TCG_REG_TMP2, diff - imm12);
        } else {
            tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_TMP2, offset - imm12);
            if (addr != TCG_REG_ZERO) {
                tcg_out_opc_reg(s, OPC_ADD, TCG_REG_TMP2, TCG_REG_TMP2, addr);
            }
        }
        addr = TCG_REG_TMP2;
    }

    switch (opc) {
    case OPC_SB:
    case OPC_SH:
    case OPC_SW:
    case OPC_SD:
        tcg_out_opc_store(s, opc, addr, data, imm12);
        break;
    case OPC_LB:
    case OPC_LBU:
    case OPC_LH:
    case OPC_LHU:
    case OPC_LW:
    case OPC_LWU:
    case OPC_LD:
        tcg_out_opc_imm(s, opc, data, addr, imm12);
        break;
    default:
        g_assert_not_reached();
    }
}

static void tcg_out_vec_ldst(TCGContext *s, RISCVInsn opc, TCGReg data,
                             TCGReg addr, intptr_t offset)
{
    tcg_debug_assert(data >= TCG_REG_V0);
    tcg_debug_assert(addr < TCG_REG_V0);

    if (offset) {
        tcg_debug_assert(addr != TCG_REG_ZERO);
        if (offset == sextreg(offset, 0, 12)) {
            tcg_out_opc_imm(s, OPC_ADDI, TCG_REG_TMP0, addr, offset);
        } else {
            tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_TMP0, offset);
            tcg_out_opc_reg(s, OPC_ADD, TCG_REG_TMP0, TCG_REG_TMP0, addr);
        }
        addr = TCG_REG_TMP0;
    }
    tcg_out32(s, encode_v(opc, data, addr, 0, true));
}

static void tcg_out_ld(TCGContext *s, TCGType type, TCGReg arg,
                       TCGReg arg1, intptr_t arg2)
{
    RISCVInsn insn;

    switch (type) {
    case TCG_TYPE_I32:
        tcg_out_ldst(s, OPC_LW, arg, arg1, arg2);
        break;
    case TCG_TYPE_I64:
        tcg_out_ldst(s, OPC_LD, arg, arg1, arg2);
        break;
    case TCG_TYPE_V64:
    case TCG_TYPE_V128:
    case TCG_TYPE_V256:
        if (type >= riscv_lg2_vlenb) {
            static const RISCVInsn whole_reg_ld[] = {
                OPC_VL1RE64_V, OPC_VL2RE64_V, OPC_VL4RE64_V, OPC_VL8RE64_V
            };
            unsigned idx = type - riscv_lg2_vlenb;

            tcg_debug_assert(idx < ARRAY_SIZE(whole_reg_ld));
            insn = whole_reg_ld[idx];
        } else {
            static const RISCVInsn unit_stride_ld[] = {
                OPC_VLE8_V, OPC_VLE16_V, OPC_VLE32_V, OPC_VLE64_V
            };
            MemOp prev_vsew = set_vtype_len(s, type);

            tcg_debug_assert(prev_vsew < ARRAY_SIZE(unit_stride_ld));
            insn = unit_stride_ld[prev_vsew];
        }
        tcg_out_vec_ldst(s, insn, arg, arg1, arg2);
        break;
    default:
        g_assert_not_reached();
    }
}

static void tcg_out_st(TCGContext *s, TCGType type, TCGReg arg,
                       TCGReg arg1, intptr_t arg2)
{
    RISCVInsn insn;

    switch (type) {
    case TCG_TYPE_I32:
        tcg_out_ldst(s, OPC_SW, arg, arg1, arg2);
        break;
    case TCG_TYPE_I64:
        tcg_out_ldst(s, OPC_SD, arg, arg1, arg2);
        break;
    case TCG_TYPE_V64:
    case TCG_TYPE_V128:
    case TCG_TYPE_V256:
        if (type >= riscv_lg2_vlenb) {
            static const RISCVInsn whole_reg_st[] = {
                OPC_VS1R_V, OPC_VS2R_V, OPC_VS4R_V, OPC_VS8R_V
            };
            unsigned idx = type - riscv_lg2_vlenb;

            tcg_debug_assert(idx < ARRAY_SIZE(whole_reg_st));
            insn = whole_reg_st[idx];
        } else {
            static const RISCVInsn unit_stride_st[] = {
                OPC_VSE8_V, OPC_VSE16_V, OPC_VSE32_V, OPC_VSE64_V
            };
            MemOp prev_vsew = set_vtype_len(s, type);

            tcg_debug_assert(prev_vsew < ARRAY_SIZE(unit_stride_st));
            insn = unit_stride_st[prev_vsew];
        }
        tcg_out_vec_ldst(s, insn, arg, arg1, arg2);
        break;
    default:
        g_assert_not_reached();
    }
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

static void tcg_out_addsub2(TCGContext *s,
                            TCGReg rl, TCGReg rh,
                            TCGReg al, TCGReg ah,
                            TCGArg bl, TCGArg bh,
                            bool cbl, bool cbh, bool is_sub, bool is32bit)
{
    const RISCVInsn opc_add = is32bit ? OPC_ADDW : OPC_ADD;
    const RISCVInsn opc_addi = is32bit ? OPC_ADDIW : OPC_ADDI;
    const RISCVInsn opc_sub = is32bit ? OPC_SUBW : OPC_SUB;
    TCGReg th = TCG_REG_TMP1;

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
        tcg_out_opc_reg(s, (is_sub ? opc_sub : opc_add), th, ah, bh);
    } else if (bh != 0 || ah == rl) {
        tcg_out_opc_imm(s, opc_addi, th, ah, (is_sub ? -bh : bh));
    } else {
        th = ah;
    }

    /* Note that tcg optimization should eliminate the bl == 0 case.  */
    if (is_sub) {
        if (cbl) {
            tcg_out_opc_imm(s, OPC_SLTIU, TCG_REG_TMP0, al, bl);
            tcg_out_opc_imm(s, opc_addi, rl, al, -bl);
        } else {
            tcg_out_opc_reg(s, OPC_SLTU, TCG_REG_TMP0, al, bl);
            tcg_out_opc_reg(s, opc_sub, rl, al, bl);
        }
        tcg_out_opc_reg(s, opc_sub, rh, th, TCG_REG_TMP0);
    } else {
        if (cbl) {
            tcg_out_opc_imm(s, opc_addi, rl, al, bl);
            tcg_out_opc_imm(s, OPC_SLTIU, TCG_REG_TMP0, rl, bl);
        } else if (al == bl) {
            /*
             * If the input regs overlap, this is a simple doubling
             * and carry-out is the input msb.  This special case is
             * required when the output reg overlaps the input,
             * but we might as well use it always.
             */
            tcg_out_opc_imm(s, OPC_SLTI, TCG_REG_TMP0, al, 0);
            tcg_out_opc_reg(s, opc_add, rl, al, al);
        } else {
            tcg_out_opc_reg(s, opc_add, rl, al, bl);
            tcg_out_opc_reg(s, OPC_SLTU, TCG_REG_TMP0,
                            rl, (rl == bl ? al : bl));
        }
        tcg_out_opc_reg(s, opc_add, rh, th, TCG_REG_TMP0);
    }
}

static bool tcg_out_dup_vec(TCGContext *s, TCGType type, unsigned vece,
                                   TCGReg dst, TCGReg src)
{
    set_vtype_len_sew(s, type, vece);
    tcg_out_opc_vx(s, OPC_VMV_V_X, dst, 0, src);
    return true;
}

static bool tcg_out_dupm_vec(TCGContext *s, TCGType type, unsigned vece,
                                    TCGReg dst, TCGReg base, intptr_t offset)
{
    tcg_out_ld(s, TCG_TYPE_REG, TCG_REG_TMP0, base, offset);
    return tcg_out_dup_vec(s, type, vece, dst, TCG_REG_TMP0);
}

static void tcg_out_dupi_vec(TCGContext *s, TCGType type, unsigned vece,
                                    TCGReg dst, int64_t arg)
{
    /* Arg is replicated by VECE; extract the highest element. */
    arg >>= (-8 << vece) & 63;

    if (arg >= -16 && arg < 16) {
        if (arg == 0 || arg == -1) {
            set_vtype_len(s, type);
        } else {
            set_vtype_len_sew(s, type, vece);
        }
        tcg_out_opc_vi(s, OPC_VMV_V_I, dst, 0, arg);
        return;
    }
    tcg_out_movi(s, TCG_TYPE_I64, TCG_REG_TMP0, arg);
    tcg_out_dup_vec(s, type, vece, dst, TCG_REG_TMP0);
}

static const struct {
    RISCVInsn op;
    bool swap;
} tcg_brcond_to_riscv[] = {
    [TCG_COND_EQ] =  { OPC_BEQ,  false },
    [TCG_COND_NE] =  { OPC_BNE,  false },
    [TCG_COND_LT] =  { OPC_BLT,  false },
    [TCG_COND_GE] =  { OPC_BGE,  false },
    [TCG_COND_LE] =  { OPC_BGE,  true  },
    [TCG_COND_GT] =  { OPC_BLT,  true  },
    [TCG_COND_LTU] = { OPC_BLTU, false },
    [TCG_COND_GEU] = { OPC_BGEU, false },
    [TCG_COND_LEU] = { OPC_BGEU, true  },
    [TCG_COND_GTU] = { OPC_BLTU, true  }
};

static void tcg_out_brcond(TCGContext *s, TCGCond cond, TCGReg arg1,
                           TCGReg arg2, TCGLabel *l)
{
    RISCVInsn op = tcg_brcond_to_riscv[cond].op;

    tcg_debug_assert(op != 0);

    if (tcg_brcond_to_riscv[cond].swap) {
        TCGReg t = arg1;
        arg1 = arg2;
        arg2 = t;
    }

    tcg_out_reloc(s, s->code_ptr, R_RISCV_BRANCH, l, 0);
    tcg_out_opc_branch(s, op, arg1, arg2, 0);
}

#define SETCOND_INV    TCG_TARGET_NB_REGS
#define SETCOND_NEZ    (SETCOND_INV << 1)
#define SETCOND_FLAGS  (SETCOND_INV | SETCOND_NEZ)

static int tcg_out_setcond_int(TCGContext *s, TCGCond cond, TCGReg ret,
                               TCGReg arg1, tcg_target_long arg2, bool c2)
{
    int flags = 0;

    switch (cond) {
    case TCG_COND_EQ:    /* -> NE  */
    case TCG_COND_GE:    /* -> LT  */
    case TCG_COND_GEU:   /* -> LTU */
    case TCG_COND_GT:    /* -> LE  */
    case TCG_COND_GTU:   /* -> LEU */
        cond = tcg_invert_cond(cond);
        flags ^= SETCOND_INV;
        break;
    default:
        break;
    }

    switch (cond) {
    case TCG_COND_LE:
    case TCG_COND_LEU:
        /*
         * If we have a constant input, the most efficient way to implement
         * LE is by adding 1 and using LT.  Watch out for wrap around for LEU.
         * We don't need to care for this for LE because the constant input
         * is constrained to signed 12-bit, and 0x800 is representable in the
         * temporary register.
         */
        if (c2) {
            if (cond == TCG_COND_LEU) {
                /* unsigned <= -1 is true */
                if (arg2 == -1) {
                    tcg_out_movi(s, TCG_TYPE_REG, ret, !(flags & SETCOND_INV));
                    return ret;
                }
                cond = TCG_COND_LTU;
            } else {
                cond = TCG_COND_LT;
            }
            tcg_debug_assert(arg2 <= 0x7ff);
            if (++arg2 == 0x800) {
                tcg_out_movi(s, TCG_TYPE_REG, TCG_REG_TMP0, arg2);
                arg2 = TCG_REG_TMP0;
                c2 = false;
            }
        } else {
            TCGReg tmp = arg2;
            arg2 = arg1;
            arg1 = tmp;
            cond = tcg_swap_cond(cond);    /* LE -> GE */
            cond = tcg_invert_cond(cond);  /* GE -> LT */
            flags ^= SETCOND_INV;
        }
        break;
    default:
        break;
    }

    switch (cond) {
    case TCG_COND_NE:
        flags |= SETCOND_NEZ;
        if (!c2) {
            tcg_out_opc_reg(s, OPC_XOR, ret, arg1, arg2);
        } else if (arg2 == 0) {
            ret = arg1;
        } else {
            tcg_out_opc_imm(s, OPC_XORI, ret, arg1, arg2);
        }
        break;

    case TCG_COND_LT:
        if (c2) {
            tcg_out_opc_imm(s, OPC_SLTI, ret, arg1, arg2);
        } else {
            tcg_out_opc_reg(s, OPC_SLT, ret, arg1, arg2);
        }
        break;

    case TCG_COND_LTU:
        if (c2) {
            tcg_out_opc_imm(s, OPC_SLTIU, ret, arg1, arg2);
        } else {
            tcg_out_opc_reg(s, OPC_SLTU, ret, arg1, arg2);
        }
        break;

    default:
        g_assert_not_reached();
    }

    return ret | flags;
}

static void tcg_out_setcond(TCGContext *s, TCGCond cond, TCGReg ret,
                            TCGReg arg1, tcg_target_long arg2, bool c2)
{
    int tmpflags = tcg_out_setcond_int(s, cond, ret, arg1, arg2, c2);

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

static void tcg_out_negsetcond(TCGContext *s, TCGCond cond, TCGReg ret,
                               TCGReg arg1, tcg_target_long arg2, bool c2)
{
    int tmpflags;
    TCGReg tmp;

    /* For LT/GE comparison against 0, replicate the sign bit. */
    if (c2 && arg2 == 0) {
        switch (cond) {
        case TCG_COND_GE:
            tcg_out_opc_imm(s, OPC_XORI, ret, arg1, -1);
            arg1 = ret;
            /* fall through */
        case TCG_COND_LT:
            tcg_out_opc_imm(s, OPC_SRAI, ret, arg1, TCG_TARGET_REG_BITS - 1);
            return;
        default:
            break;
        }
    }

    tmpflags = tcg_out_setcond_int(s, cond, ret, arg1, arg2, c2);
    tmp = tmpflags & ~SETCOND_FLAGS;

    /* If intermediate result is zero/non-zero: test != 0. */
    if (tmpflags & SETCOND_NEZ) {
        tcg_out_opc_reg(s, OPC_SLTU, ret, TCG_REG_ZERO, tmp);
        tmp = ret;
    }

    /* Produce the 0/-1 result. */
    if (tmpflags & SETCOND_INV) {
        tcg_out_opc_imm(s, OPC_ADDI, ret, tmp, -1);
    } else {
        tcg_out_opc_reg(s, OPC_SUB, ret, TCG_REG_ZERO, tmp);
    }
}

static void tcg_out_movcond_zicond(TCGContext *s, TCGReg ret, TCGReg test_ne,
                                   int val1, bool c_val1,
                                   int val2, bool c_val2)
{
    if (val1 == 0) {
        if (c_val2) {
            tcg_out_movi(s, TCG_TYPE_REG, TCG_REG_TMP1, val2);
            val2 = TCG_REG_TMP1;
        }
        tcg_out_opc_reg(s, OPC_CZERO_NEZ, ret, val2, test_ne);
        return;
    }

    if (val2 == 0) {
        if (c_val1) {
            tcg_out_movi(s, TCG_TYPE_REG, TCG_REG_TMP1, val1);
            val1 = TCG_REG_TMP1;
        }
        tcg_out_opc_reg(s, OPC_CZERO_EQZ, ret, val1, test_ne);
        return;
    }

    if (c_val2) {
        if (c_val1) {
            tcg_out_movi(s, TCG_TYPE_REG, TCG_REG_TMP1, val1 - val2);
        } else {
            tcg_out_opc_imm(s, OPC_ADDI, TCG_REG_TMP1, val1, -val2);
        }
        tcg_out_opc_reg(s, OPC_CZERO_EQZ, ret, TCG_REG_TMP1, test_ne);
        tcg_out_opc_imm(s, OPC_ADDI, ret, ret, val2);
        return;
    }

    if (c_val1) {
        tcg_out_opc_imm(s, OPC_ADDI, TCG_REG_TMP1, val2, -val1);
        tcg_out_opc_reg(s, OPC_CZERO_NEZ, ret, TCG_REG_TMP1, test_ne);
        tcg_out_opc_imm(s, OPC_ADDI, ret, ret, val1);
        return;
    }

    tcg_out_opc_reg(s, OPC_CZERO_NEZ, TCG_REG_TMP1, val2, test_ne);
    tcg_out_opc_reg(s, OPC_CZERO_EQZ, TCG_REG_TMP0, val1, test_ne);
    tcg_out_opc_reg(s, OPC_OR, ret, TCG_REG_TMP0, TCG_REG_TMP1);
}

static void tcg_out_movcond_br1(TCGContext *s, TCGCond cond, TCGReg ret,
                                TCGReg cmp1, TCGReg cmp2,
                                int val, bool c_val)
{
    RISCVInsn op;
    int disp = 8;

    tcg_debug_assert((unsigned)cond < ARRAY_SIZE(tcg_brcond_to_riscv));
    op = tcg_brcond_to_riscv[cond].op;
    tcg_debug_assert(op != 0);

    if (tcg_brcond_to_riscv[cond].swap) {
        tcg_out_opc_branch(s, op, cmp2, cmp1, disp);
    } else {
        tcg_out_opc_branch(s, op, cmp1, cmp2, disp);
    }
    if (c_val) {
        tcg_out_opc_imm(s, OPC_ADDI, ret, TCG_REG_ZERO, val);
    } else {
        tcg_out_opc_imm(s, OPC_ADDI, ret, val, 0);
    }
}

static void tcg_out_movcond_br2(TCGContext *s, TCGCond cond, TCGReg ret,
                                TCGReg cmp1, TCGReg cmp2,
                                int val1, bool c_val1,
                                int val2, bool c_val2)
{
    TCGReg tmp;

    /* TCG optimizer reorders to prefer ret matching val2. */
    if (!c_val2 && ret == val2) {
        cond = tcg_invert_cond(cond);
        tcg_out_movcond_br1(s, cond, ret, cmp1, cmp2, val1, c_val1);
        return;
    }

    if (!c_val1 && ret == val1) {
        tcg_out_movcond_br1(s, cond, ret, cmp1, cmp2, val2, c_val2);
        return;
    }

    tmp = (ret == cmp1 || ret == cmp2 ? TCG_REG_TMP1 : ret);
    if (c_val1) {
        tcg_out_movi(s, TCG_TYPE_REG, tmp, val1);
    } else {
        tcg_out_mov(s, TCG_TYPE_REG, tmp, val1);
    }
    tcg_out_movcond_br1(s, cond, tmp, cmp1, cmp2, val2, c_val2);
    tcg_out_mov(s, TCG_TYPE_REG, ret, tmp);
}

static void tcg_out_movcond(TCGContext *s, TCGCond cond, TCGReg ret,
                            TCGReg cmp1, int cmp2, bool c_cmp2,
                            TCGReg val1, bool c_val1,
                            TCGReg val2, bool c_val2)
{
    int tmpflags;
    TCGReg t;

    if (!(cpuinfo & CPUINFO_ZICOND) && (!c_cmp2 || cmp2 == 0)) {
        tcg_out_movcond_br2(s, cond, ret, cmp1, cmp2,
                            val1, c_val1, val2, c_val2);
        return;
    }

    tmpflags = tcg_out_setcond_int(s, cond, TCG_REG_TMP0, cmp1, cmp2, c_cmp2);
    t = tmpflags & ~SETCOND_FLAGS;

    if (cpuinfo & CPUINFO_ZICOND) {
        if (tmpflags & SETCOND_INV) {
            tcg_out_movcond_zicond(s, ret, t, val2, c_val2, val1, c_val1);
        } else {
            tcg_out_movcond_zicond(s, ret, t, val1, c_val1, val2, c_val2);
        }
    } else {
        cond = tmpflags & SETCOND_INV ? TCG_COND_EQ : TCG_COND_NE;
        tcg_out_movcond_br2(s, cond, ret, t, TCG_REG_ZERO,
                            val1, c_val1, val2, c_val2);
    }
}

static void tcg_out_cltz(TCGContext *s, TCGType type, RISCVInsn insn,
                         TCGReg ret, TCGReg src1, int src2, bool c_src2)
{
    tcg_out_opc_imm(s, insn, ret, src1, 0);

    if (!c_src2 || src2 != (type == TCG_TYPE_I32 ? 32 : 64)) {
        /*
         * The requested zero result does not match the insn, so adjust.
         * Note that constraints put 'ret' in a new register, so the
         * computation above did not clobber either 'src1' or 'src2'.
         */
        tcg_out_movcond(s, TCG_COND_EQ, ret, src1, 0, true,
                        src2, c_src2, ret, false);
    }
}

static void tcg_out_cmpsel(TCGContext *s, TCGType type, unsigned vece,
                           TCGCond cond, TCGReg ret,
                           TCGReg cmp1, TCGReg cmp2, bool c_cmp2,
                           TCGReg val1, bool c_val1,
                           TCGReg val2, bool c_val2)
{
    set_vtype_len_sew(s, type, vece);

    /* Use only vmerge_vim if possible, by inverting the test. */
    if (c_val2 && !c_val1) {
        TCGArg temp = val1;
        cond = tcg_invert_cond(cond);
        val1 = val2;
        val2 = temp;
        c_val1 = true;
        c_val2 = false;
    }

    /* Perform the comparison into V0 mask. */
    if (c_cmp2) {
        tcg_out_opc_vi(s, tcg_cmpcond_to_rvv_vi[cond].op, TCG_REG_V0, cmp1,
                       cmp2 - tcg_cmpcond_to_rvv_vi[cond].adjust);
    } else if (tcg_cmpcond_to_rvv_vv[cond].swap) {
        tcg_out_opc_vv(s, tcg_cmpcond_to_rvv_vv[cond].op,
                       TCG_REG_V0, cmp2, cmp1);
    } else {
        tcg_out_opc_vv(s, tcg_cmpcond_to_rvv_vv[cond].op,
                       TCG_REG_V0, cmp1, cmp2);
    }
    if (c_val1) {
        if (c_val2) {
            tcg_out_opc_vi(s, OPC_VMV_V_I, ret, 0, val2);
            val2 = ret;
        }
        /* vd[i] == v0.mask[i] ? imm : vs2[i] */
        tcg_out_opc_vim_mask(s, OPC_VMERGE_VIM, ret, val2, val1);
    } else {
        /* vd[i] == v0.mask[i] ? vs1[i] : vs2[i] */
        tcg_out_opc_vvm_mask(s, OPC_VMERGE_VVM, ret, val2, val1);
    }
}

static void tcg_out_vshifti(TCGContext *s, RISCVInsn opc_vi, RISCVInsn opc_vx,
                             TCGReg dst, TCGReg src, unsigned imm)
{
    if (imm < 32) {
        tcg_out_opc_vi(s, opc_vi, dst, src, imm);
    } else {
        tcg_out_movi(s, TCG_TYPE_I32, TCG_REG_TMP0, imm);
        tcg_out_opc_vx(s, opc_vx, dst, src, TCG_REG_TMP0);
    }
}

static void init_setting_vtype(TCGContext *s)
{
    s->riscv_cur_type = TCG_TYPE_COUNT;
}

static void tcg_out_call_int(TCGContext *s, const tcg_insn_unit *arg, bool tail)
{
    TCGReg link = tail ? TCG_REG_ZERO : TCG_REG_RA;
    ptrdiff_t offset = tcg_pcrel_diff(s, arg);
    int ret;

    init_setting_vtype(s);

    tcg_debug_assert((offset & 1) == 0);
    if (offset == sextreg(offset, 0, 20)) {
        /* short jump: -2097150 to 2097152 */
        tcg_out_opc_jump(s, OPC_JAL, link, offset);
    } else if (offset == (int32_t)offset) {
        /* long jump: -2147483646 to 2147483648 */
        tcg_out_opc_upper(s, OPC_AUIPC, TCG_REG_TMP0, 0);
        tcg_out_opc_imm(s, OPC_JALR, link, TCG_REG_TMP0, 0);
        ret = reloc_call(s->code_ptr - 2, arg);
        tcg_debug_assert(ret == true);
    } else {
        /* far jump: 64-bit */
        tcg_target_long imm = sextreg((tcg_target_long)arg, 0, 12);
        tcg_target_long base = (tcg_target_long)arg - imm;
        tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_TMP0, base);
        tcg_out_opc_imm(s, OPC_JALR, link, TCG_REG_TMP0, imm);
    }
}

static void tcg_out_call(TCGContext *s, const tcg_insn_unit *arg,
                         const TCGHelperInfo *info)
{
    tcg_out_call_int(s, arg, false);
}

static void tcg_out_mb(TCGContext *s, TCGArg a0)
{
    tcg_insn_unit insn = OPC_FENCE;

    if (a0 & TCG_MO_LD_LD) {
        insn |= 0x02200000;
    }
    if (a0 & TCG_MO_ST_LD) {
        insn |= 0x01200000;
    }
    if (a0 & TCG_MO_LD_ST) {
        insn |= 0x02100000;
    }
    if (a0 & TCG_MO_ST_ST) {
        insn |= 0x01100000;
    }
    tcg_out32(s, insn);
}

/*
 * Load/store and TLB
 */

static void tcg_out_goto(TCGContext *s, const tcg_insn_unit *target)
{
    tcg_out_opc_jump(s, OPC_JAL, TCG_REG_ZERO, 0);
    bool ok = reloc_jimm20(s->code_ptr - 1, target);
    tcg_debug_assert(ok);
}

bool tcg_target_has_memory_bswap(MemOp memop)
{
    return false;
}

/* We have three temps, we might as well expose them. */
static const TCGLdstHelperParam ldst_helper_param = {
    .ntmp = 3, .tmp = { TCG_REG_TMP0, TCG_REG_TMP1, TCG_REG_TMP2 }
};

static bool tcg_out_qemu_ld_slow_path(TCGContext *s, TCGLabelQemuLdst *l)
{
    MemOp opc = get_memop(l->oi);

    /* resolve label address */
    if (!reloc_sbimm12(l->label_ptr[0], tcg_splitwx_to_rx(s->code_ptr))) {
        return false;
    }

    /* call load helper */
    tcg_out_ld_helper_args(s, l, &ldst_helper_param);
    tcg_out_call_int(s, qemu_ld_helpers[opc & MO_SSIZE], false);
    tcg_out_ld_helper_ret(s, l, true, &ldst_helper_param);

    tcg_out_goto(s, l->raddr);
    return true;
}

static bool tcg_out_qemu_st_slow_path(TCGContext *s, TCGLabelQemuLdst *l)
{
    MemOp opc = get_memop(l->oi);

    /* resolve label address */
    if (!reloc_sbimm12(l->label_ptr[0], tcg_splitwx_to_rx(s->code_ptr))) {
        return false;
    }

    /* call store helper */
    tcg_out_st_helper_args(s, l, &ldst_helper_param);
    tcg_out_call_int(s, qemu_st_helpers[opc & MO_SIZE], false);

    tcg_out_goto(s, l->raddr);
    return true;
}

/* We expect to use a 12-bit negative offset from ENV.  */
#define MIN_TLB_MASK_TABLE_OFS  -(1 << 11)

/*
 * For system-mode, perform the TLB load and compare.
 * For user-mode, perform any required alignment tests.
 * In both cases, return a TCGLabelQemuLdst structure if the slow path
 * is required and fill in @h with the host address for the fast path.
 */
static TCGLabelQemuLdst *prepare_host_addr(TCGContext *s, TCGReg *pbase,
                                           TCGReg addr_reg, MemOpIdx oi,
                                           bool is_ld)
{
    TCGType addr_type = s->addr_type;
    TCGLabelQemuLdst *ldst = NULL;
    MemOp opc = get_memop(oi);
    TCGAtomAlign aa;
    unsigned a_mask;

    aa = atom_and_align_for_opc(s, opc, MO_ATOM_IFALIGN, false);
    a_mask = (1u << aa.align) - 1;

    if (tcg_use_softmmu) {
        unsigned s_bits = opc & MO_SIZE;
        unsigned s_mask = (1u << s_bits) - 1;
        int mem_index = get_mmuidx(oi);
        int fast_ofs = tlb_mask_table_ofs(s, mem_index);
        int mask_ofs = fast_ofs + offsetof(CPUTLBDescFast, mask);
        int table_ofs = fast_ofs + offsetof(CPUTLBDescFast, table);
        int compare_mask;
        TCGReg addr_adj;

        ldst = new_ldst_label(s);
        ldst->is_ld = is_ld;
        ldst->oi = oi;
        ldst->addr_reg = addr_reg;

        init_setting_vtype(s);

        tcg_out_ld(s, TCG_TYPE_PTR, TCG_REG_TMP0, TCG_AREG0, mask_ofs);
        tcg_out_ld(s, TCG_TYPE_PTR, TCG_REG_TMP1, TCG_AREG0, table_ofs);

        tcg_out_opc_imm(s, OPC_SRLI, TCG_REG_TMP2, addr_reg,
                        s->page_bits - CPU_TLB_ENTRY_BITS);
        tcg_out_opc_reg(s, OPC_AND, TCG_REG_TMP2, TCG_REG_TMP2, TCG_REG_TMP0);
        tcg_out_opc_reg(s, OPC_ADD, TCG_REG_TMP2, TCG_REG_TMP2, TCG_REG_TMP1);

        /*
         * For aligned accesses, we check the first byte and include the
         * alignment bits within the address.  For unaligned access, we
         * check that we don't cross pages using the address of the last
         * byte of the access.
         */
        addr_adj = addr_reg;
        if (a_mask < s_mask) {
            addr_adj = TCG_REG_TMP0;
            tcg_out_opc_imm(s, addr_type == TCG_TYPE_I32 ? OPC_ADDIW : OPC_ADDI,
                            addr_adj, addr_reg, s_mask - a_mask);
        }
        compare_mask = s->page_mask | a_mask;
        if (compare_mask == sextreg(compare_mask, 0, 12)) {
            tcg_out_opc_imm(s, OPC_ANDI, TCG_REG_TMP1, addr_adj, compare_mask);
        } else {
            tcg_out_movi(s, addr_type, TCG_REG_TMP1, compare_mask);
            tcg_out_opc_reg(s, OPC_AND, TCG_REG_TMP1, TCG_REG_TMP1, addr_adj);
        }

        /* Load the tlb comparator and the addend.  */
        QEMU_BUILD_BUG_ON(HOST_BIG_ENDIAN);
        tcg_out_ld(s, addr_type, TCG_REG_TMP0, TCG_REG_TMP2,
                   is_ld ? offsetof(CPUTLBEntry, addr_read)
                         : offsetof(CPUTLBEntry, addr_write));
        tcg_out_ld(s, TCG_TYPE_PTR, TCG_REG_TMP2, TCG_REG_TMP2,
                   offsetof(CPUTLBEntry, addend));

        /* Compare masked address with the TLB entry. */
        ldst->label_ptr[0] = s->code_ptr;
        tcg_out_opc_branch(s, OPC_BNE, TCG_REG_TMP0, TCG_REG_TMP1, 0);

        /* TLB Hit - translate address using addend.  */
        if (addr_type != TCG_TYPE_I32) {
            tcg_out_opc_reg(s, OPC_ADD, TCG_REG_TMP0, addr_reg, TCG_REG_TMP2);
        } else if (cpuinfo & CPUINFO_ZBA) {
            tcg_out_opc_reg(s, OPC_ADD_UW, TCG_REG_TMP0,
                            addr_reg, TCG_REG_TMP2);
        } else {
            tcg_out_ext32u(s, TCG_REG_TMP0, addr_reg);
            tcg_out_opc_reg(s, OPC_ADD, TCG_REG_TMP0,
                            TCG_REG_TMP0, TCG_REG_TMP2);
        }
        *pbase = TCG_REG_TMP0;
    } else {
        TCGReg base;

        if (a_mask) {
            ldst = new_ldst_label(s);
            ldst->is_ld = is_ld;
            ldst->oi = oi;
            ldst->addr_reg = addr_reg;

            init_setting_vtype(s);

            /* We are expecting alignment max 7, so we can always use andi. */
            tcg_debug_assert(a_mask == sextreg(a_mask, 0, 12));
            tcg_out_opc_imm(s, OPC_ANDI, TCG_REG_TMP1, addr_reg, a_mask);

            ldst->label_ptr[0] = s->code_ptr;
            tcg_out_opc_branch(s, OPC_BNE, TCG_REG_TMP1, TCG_REG_ZERO, 0);
        }

        if (guest_base != 0) {
            base = TCG_REG_TMP0;
            if (addr_type != TCG_TYPE_I32) {
                tcg_out_opc_reg(s, OPC_ADD, base, addr_reg,
                                TCG_GUEST_BASE_REG);
            } else if (cpuinfo & CPUINFO_ZBA) {
                tcg_out_opc_reg(s, OPC_ADD_UW, base, addr_reg,
                                TCG_GUEST_BASE_REG);
            } else {
                tcg_out_ext32u(s, base, addr_reg);
                tcg_out_opc_reg(s, OPC_ADD, base, base, TCG_GUEST_BASE_REG);
            }
        } else if (addr_type != TCG_TYPE_I32) {
            base = addr_reg;
        } else {
            base = TCG_REG_TMP0;
            tcg_out_ext32u(s, base, addr_reg);
        }
        *pbase = base;
    }

    return ldst;
}

static void tcg_out_qemu_ld_direct(TCGContext *s, TCGReg val,
                                   TCGReg base, MemOp opc, TCGType type)
{
    /* Byte swapping is left to middle-end expansion. */
    tcg_debug_assert((opc & MO_BSWAP) == 0);

    switch (opc & (MO_SSIZE)) {
    case MO_UB:
        tcg_out_opc_imm(s, OPC_LBU, val, base, 0);
        break;
    case MO_SB:
        tcg_out_opc_imm(s, OPC_LB, val, base, 0);
        break;
    case MO_UW:
        tcg_out_opc_imm(s, OPC_LHU, val, base, 0);
        break;
    case MO_SW:
        tcg_out_opc_imm(s, OPC_LH, val, base, 0);
        break;
    case MO_UL:
        if (type == TCG_TYPE_I64) {
            tcg_out_opc_imm(s, OPC_LWU, val, base, 0);
            break;
        }
        /* FALLTHRU */
    case MO_SL:
        tcg_out_opc_imm(s, OPC_LW, val, base, 0);
        break;
    case MO_UQ:
        tcg_out_opc_imm(s, OPC_LD, val, base, 0);
        break;
    default:
        g_assert_not_reached();
    }
}

static void tcg_out_qemu_ld(TCGContext *s, TCGReg data_reg, TCGReg addr_reg,
                            MemOpIdx oi, TCGType data_type)
{
    TCGLabelQemuLdst *ldst;
    TCGReg base;

    ldst = prepare_host_addr(s, &base, addr_reg, oi, true);
    tcg_out_qemu_ld_direct(s, data_reg, base, get_memop(oi), data_type);

    if (ldst) {
        ldst->type = data_type;
        ldst->datalo_reg = data_reg;
        ldst->raddr = tcg_splitwx_to_rx(s->code_ptr);
    }
}

static void tcg_out_qemu_st_direct(TCGContext *s, TCGReg val,
                                   TCGReg base, MemOp opc)
{
    /* Byte swapping is left to middle-end expansion. */
    tcg_debug_assert((opc & MO_BSWAP) == 0);

    switch (opc & (MO_SSIZE)) {
    case MO_8:
        tcg_out_opc_store(s, OPC_SB, base, val, 0);
        break;
    case MO_16:
        tcg_out_opc_store(s, OPC_SH, base, val, 0);
        break;
    case MO_32:
        tcg_out_opc_store(s, OPC_SW, base, val, 0);
        break;
    case MO_64:
        tcg_out_opc_store(s, OPC_SD, base, val, 0);
        break;
    default:
        g_assert_not_reached();
    }
}

static void tcg_out_qemu_st(TCGContext *s, TCGReg data_reg, TCGReg addr_reg,
                            MemOpIdx oi, TCGType data_type)
{
    TCGLabelQemuLdst *ldst;
    TCGReg base;

    ldst = prepare_host_addr(s, &base, addr_reg, oi, false);
    tcg_out_qemu_st_direct(s, data_reg, base, get_memop(oi));

    if (ldst) {
        ldst->type = data_type;
        ldst->datalo_reg = data_reg;
        ldst->raddr = tcg_splitwx_to_rx(s->code_ptr);
    }
}

static const tcg_insn_unit *tb_ret_addr;

static void tcg_out_exit_tb(TCGContext *s, uintptr_t a0)
{
    /* Reuse the zeroing that exists for goto_ptr.  */
    if (a0 == 0) {
        tcg_out_call_int(s, tcg_code_gen_epilogue, true);
    } else {
        tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_A0, a0);
        tcg_out_call_int(s, tb_ret_addr, true);
    }
}

static void tcg_out_goto_tb(TCGContext *s, int which)
{
    /* Direct branch will be patched by tb_target_set_jmp_target. */
    set_jmp_insn_offset(s, which);
    tcg_out32(s, OPC_JAL);

    /* When branch is out of range, fall through to indirect. */
    tcg_out_ld(s, TCG_TYPE_PTR, TCG_REG_TMP0, TCG_REG_ZERO,
               get_jmp_target_addr(s, which));
    tcg_out_opc_imm(s, OPC_JALR, TCG_REG_ZERO, TCG_REG_TMP0, 0);
    set_jmp_reset_offset(s, which);
}

void tb_target_set_jmp_target(const TranslationBlock *tb, int n,
                              uintptr_t jmp_rx, uintptr_t jmp_rw)
{
    uintptr_t addr = tb->jmp_target_addr[n];
    ptrdiff_t offset = addr - jmp_rx;
    tcg_insn_unit insn;

    /* Either directly branch, or fall through to indirect branch. */
    if (offset == sextreg(offset, 0, 20)) {
        insn = encode_uj(OPC_JAL, TCG_REG_ZERO, offset);
    } else {
        insn = OPC_NOP;
    }
    qatomic_set((uint32_t *)jmp_rw, insn);
    flush_idcache_range(jmp_rx, jmp_rw, 4);
}

static void tcg_out_op(TCGContext *s, TCGOpcode opc, TCGType type,
                       const TCGArg args[TCG_MAX_OP_ARGS],
                       const int const_args[TCG_MAX_OP_ARGS])
{
    TCGArg a0 = args[0];
    TCGArg a1 = args[1];
    TCGArg a2 = args[2];
    int c2 = const_args[2];

    switch (opc) {
    case INDEX_op_goto_ptr:
        tcg_out_opc_imm(s, OPC_JALR, TCG_REG_ZERO, a0, 0);
        break;

    case INDEX_op_br:
        tcg_out_reloc(s, s->code_ptr, R_RISCV_JAL, arg_label(a0), 0);
        tcg_out_opc_jump(s, OPC_JAL, TCG_REG_ZERO, 0);
        break;

    case INDEX_op_ld8u_i32:
    case INDEX_op_ld8u_i64:
        tcg_out_ldst(s, OPC_LBU, a0, a1, a2);
        break;
    case INDEX_op_ld8s_i32:
    case INDEX_op_ld8s_i64:
        tcg_out_ldst(s, OPC_LB, a0, a1, a2);
        break;
    case INDEX_op_ld16u_i32:
    case INDEX_op_ld16u_i64:
        tcg_out_ldst(s, OPC_LHU, a0, a1, a2);
        break;
    case INDEX_op_ld16s_i32:
    case INDEX_op_ld16s_i64:
        tcg_out_ldst(s, OPC_LH, a0, a1, a2);
        break;
    case INDEX_op_ld32u_i64:
        tcg_out_ldst(s, OPC_LWU, a0, a1, a2);
        break;
    case INDEX_op_ld_i32:
    case INDEX_op_ld32s_i64:
        tcg_out_ldst(s, OPC_LW, a0, a1, a2);
        break;
    case INDEX_op_ld_i64:
        tcg_out_ldst(s, OPC_LD, a0, a1, a2);
        break;

    case INDEX_op_st8_i32:
    case INDEX_op_st8_i64:
        tcg_out_ldst(s, OPC_SB, a0, a1, a2);
        break;
    case INDEX_op_st16_i32:
    case INDEX_op_st16_i64:
        tcg_out_ldst(s, OPC_SH, a0, a1, a2);
        break;
    case INDEX_op_st_i32:
    case INDEX_op_st32_i64:
        tcg_out_ldst(s, OPC_SW, a0, a1, a2);
        break;
    case INDEX_op_st_i64:
        tcg_out_ldst(s, OPC_SD, a0, a1, a2);
        break;

    case INDEX_op_add_i32:
        if (c2) {
            tcg_out_opc_imm(s, OPC_ADDIW, a0, a1, a2);
        } else {
            tcg_out_opc_reg(s, OPC_ADDW, a0, a1, a2);
        }
        break;
    case INDEX_op_add_i64:
        if (c2) {
            tcg_out_opc_imm(s, OPC_ADDI, a0, a1, a2);
        } else {
            tcg_out_opc_reg(s, OPC_ADD, a0, a1, a2);
        }
        break;

    case INDEX_op_sub_i32:
        if (c2) {
            tcg_out_opc_imm(s, OPC_ADDIW, a0, a1, -a2);
        } else {
            tcg_out_opc_reg(s, OPC_SUBW, a0, a1, a2);
        }
        break;
    case INDEX_op_sub_i64:
        if (c2) {
            tcg_out_opc_imm(s, OPC_ADDI, a0, a1, -a2);
        } else {
            tcg_out_opc_reg(s, OPC_SUB, a0, a1, a2);
        }
        break;

    case INDEX_op_and_i32:
    case INDEX_op_and_i64:
        if (c2) {
            tcg_out_opc_imm(s, OPC_ANDI, a0, a1, a2);
        } else {
            tcg_out_opc_reg(s, OPC_AND, a0, a1, a2);
        }
        break;

    case INDEX_op_or_i32:
    case INDEX_op_or_i64:
        if (c2) {
            tcg_out_opc_imm(s, OPC_ORI, a0, a1, a2);
        } else {
            tcg_out_opc_reg(s, OPC_OR, a0, a1, a2);
        }
        break;

    case INDEX_op_xor_i32:
    case INDEX_op_xor_i64:
        if (c2) {
            tcg_out_opc_imm(s, OPC_XORI, a0, a1, a2);
        } else {
            tcg_out_opc_reg(s, OPC_XOR, a0, a1, a2);
        }
        break;

    case INDEX_op_andc_i32:
    case INDEX_op_andc_i64:
        if (c2) {
            tcg_out_opc_imm(s, OPC_ANDI, a0, a1, ~a2);
        } else {
            tcg_out_opc_reg(s, OPC_ANDN, a0, a1, a2);
        }
        break;
    case INDEX_op_orc_i32:
    case INDEX_op_orc_i64:
        if (c2) {
            tcg_out_opc_imm(s, OPC_ORI, a0, a1, ~a2);
        } else {
            tcg_out_opc_reg(s, OPC_ORN, a0, a1, a2);
        }
        break;
    case INDEX_op_eqv_i32:
    case INDEX_op_eqv_i64:
        if (c2) {
            tcg_out_opc_imm(s, OPC_XORI, a0, a1, ~a2);
        } else {
            tcg_out_opc_reg(s, OPC_XNOR, a0, a1, a2);
        }
        break;

    case INDEX_op_not_i32:
    case INDEX_op_not_i64:
        tcg_out_opc_imm(s, OPC_XORI, a0, a1, -1);
        break;

    case INDEX_op_neg_i32:
        tcg_out_opc_reg(s, OPC_SUBW, a0, TCG_REG_ZERO, a1);
        break;
    case INDEX_op_neg_i64:
        tcg_out_opc_reg(s, OPC_SUB, a0, TCG_REG_ZERO, a1);
        break;

    case INDEX_op_mul_i32:
        tcg_out_opc_reg(s, OPC_MULW, a0, a1, a2);
        break;
    case INDEX_op_mul_i64:
        tcg_out_opc_reg(s, OPC_MUL, a0, a1, a2);
        break;

    case INDEX_op_div_i32:
        tcg_out_opc_reg(s, OPC_DIVW, a0, a1, a2);
        break;
    case INDEX_op_div_i64:
        tcg_out_opc_reg(s, OPC_DIV, a0, a1, a2);
        break;

    case INDEX_op_divu_i32:
        tcg_out_opc_reg(s, OPC_DIVUW, a0, a1, a2);
        break;
    case INDEX_op_divu_i64:
        tcg_out_opc_reg(s, OPC_DIVU, a0, a1, a2);
        break;

    case INDEX_op_rem_i32:
        tcg_out_opc_reg(s, OPC_REMW, a0, a1, a2);
        break;
    case INDEX_op_rem_i64:
        tcg_out_opc_reg(s, OPC_REM, a0, a1, a2);
        break;

    case INDEX_op_remu_i32:
        tcg_out_opc_reg(s, OPC_REMUW, a0, a1, a2);
        break;
    case INDEX_op_remu_i64:
        tcg_out_opc_reg(s, OPC_REMU, a0, a1, a2);
        break;

    case INDEX_op_shl_i32:
        if (c2) {
            tcg_out_opc_imm(s, OPC_SLLIW, a0, a1, a2 & 0x1f);
        } else {
            tcg_out_opc_reg(s, OPC_SLLW, a0, a1, a2);
        }
        break;
    case INDEX_op_shl_i64:
        if (c2) {
            tcg_out_opc_imm(s, OPC_SLLI, a0, a1, a2 & 0x3f);
        } else {
            tcg_out_opc_reg(s, OPC_SLL, a0, a1, a2);
        }
        break;

    case INDEX_op_shr_i32:
        if (c2) {
            tcg_out_opc_imm(s, OPC_SRLIW, a0, a1, a2 & 0x1f);
        } else {
            tcg_out_opc_reg(s, OPC_SRLW, a0, a1, a2);
        }
        break;
    case INDEX_op_shr_i64:
        if (c2) {
            tcg_out_opc_imm(s, OPC_SRLI, a0, a1, a2 & 0x3f);
        } else {
            tcg_out_opc_reg(s, OPC_SRL, a0, a1, a2);
        }
        break;

    case INDEX_op_sar_i32:
        if (c2) {
            tcg_out_opc_imm(s, OPC_SRAIW, a0, a1, a2 & 0x1f);
        } else {
            tcg_out_opc_reg(s, OPC_SRAW, a0, a1, a2);
        }
        break;
    case INDEX_op_sar_i64:
        if (c2) {
            tcg_out_opc_imm(s, OPC_SRAI, a0, a1, a2 & 0x3f);
        } else {
            tcg_out_opc_reg(s, OPC_SRA, a0, a1, a2);
        }
        break;

    case INDEX_op_rotl_i32:
        if (c2) {
            tcg_out_opc_imm(s, OPC_RORIW, a0, a1, -a2 & 0x1f);
        } else {
            tcg_out_opc_reg(s, OPC_ROLW, a0, a1, a2);
        }
        break;
    case INDEX_op_rotl_i64:
        if (c2) {
            tcg_out_opc_imm(s, OPC_RORI, a0, a1, -a2 & 0x3f);
        } else {
            tcg_out_opc_reg(s, OPC_ROL, a0, a1, a2);
        }
        break;

    case INDEX_op_rotr_i32:
        if (c2) {
            tcg_out_opc_imm(s, OPC_RORIW, a0, a1, a2 & 0x1f);
        } else {
            tcg_out_opc_reg(s, OPC_RORW, a0, a1, a2);
        }
        break;
    case INDEX_op_rotr_i64:
        if (c2) {
            tcg_out_opc_imm(s, OPC_RORI, a0, a1, a2 & 0x3f);
        } else {
            tcg_out_opc_reg(s, OPC_ROR, a0, a1, a2);
        }
        break;

    case INDEX_op_bswap64_i64:
        tcg_out_opc_imm(s, OPC_REV8, a0, a1, 0);
        break;
    case INDEX_op_bswap32_i32:
        a2 = 0;
        /* fall through */
    case INDEX_op_bswap32_i64:
        tcg_out_opc_imm(s, OPC_REV8, a0, a1, 0);
        if (a2 & TCG_BSWAP_OZ) {
            tcg_out_opc_imm(s, OPC_SRLI, a0, a0, 32);
        } else {
            tcg_out_opc_imm(s, OPC_SRAI, a0, a0, 32);
        }
        break;
    case INDEX_op_bswap16_i64:
    case INDEX_op_bswap16_i32:
        tcg_out_opc_imm(s, OPC_REV8, a0, a1, 0);
        if (a2 & TCG_BSWAP_OZ) {
            tcg_out_opc_imm(s, OPC_SRLI, a0, a0, 48);
        } else {
            tcg_out_opc_imm(s, OPC_SRAI, a0, a0, 48);
        }
        break;

    case INDEX_op_ctpop_i32:
        tcg_out_opc_imm(s, OPC_CPOPW, a0, a1, 0);
        break;
    case INDEX_op_ctpop_i64:
        tcg_out_opc_imm(s, OPC_CPOP, a0, a1, 0);
        break;

    case INDEX_op_clz_i32:
        tcg_out_cltz(s, TCG_TYPE_I32, OPC_CLZW, a0, a1, a2, c2);
        break;
    case INDEX_op_clz_i64:
        tcg_out_cltz(s, TCG_TYPE_I64, OPC_CLZ, a0, a1, a2, c2);
        break;
    case INDEX_op_ctz_i32:
        tcg_out_cltz(s, TCG_TYPE_I32, OPC_CTZW, a0, a1, a2, c2);
        break;
    case INDEX_op_ctz_i64:
        tcg_out_cltz(s, TCG_TYPE_I64, OPC_CTZ, a0, a1, a2, c2);
        break;

    case INDEX_op_add2_i32:
        tcg_out_addsub2(s, a0, a1, a2, args[3], args[4], args[5],
                        const_args[4], const_args[5], false, true);
        break;
    case INDEX_op_add2_i64:
        tcg_out_addsub2(s, a0, a1, a2, args[3], args[4], args[5],
                        const_args[4], const_args[5], false, false);
        break;
    case INDEX_op_sub2_i32:
        tcg_out_addsub2(s, a0, a1, a2, args[3], args[4], args[5],
                        const_args[4], const_args[5], true, true);
        break;
    case INDEX_op_sub2_i64:
        tcg_out_addsub2(s, a0, a1, a2, args[3], args[4], args[5],
                        const_args[4], const_args[5], true, false);
        break;

    case INDEX_op_brcond_i32:
    case INDEX_op_brcond_i64:
        tcg_out_brcond(s, a2, a0, a1, arg_label(args[3]));
        break;

    case INDEX_op_setcond_i32:
    case INDEX_op_setcond_i64:
        tcg_out_setcond(s, args[3], a0, a1, a2, c2);
        break;

    case INDEX_op_negsetcond_i32:
    case INDEX_op_negsetcond_i64:
        tcg_out_negsetcond(s, args[3], a0, a1, a2, c2);
        break;

    case INDEX_op_movcond_i32:
    case INDEX_op_movcond_i64:
        tcg_out_movcond(s, args[5], a0, a1, a2, c2,
                        args[3], const_args[3], args[4], const_args[4]);
        break;

    case INDEX_op_qemu_ld_i32:
        tcg_out_qemu_ld(s, a0, a1, a2, TCG_TYPE_I32);
        break;
    case INDEX_op_qemu_ld_i64:
        tcg_out_qemu_ld(s, a0, a1, a2, TCG_TYPE_I64);
        break;
    case INDEX_op_qemu_st_i32:
        tcg_out_qemu_st(s, a0, a1, a2, TCG_TYPE_I32);
        break;
    case INDEX_op_qemu_st_i64:
        tcg_out_qemu_st(s, a0, a1, a2, TCG_TYPE_I64);
        break;

    case INDEX_op_extrh_i64_i32:
        tcg_out_opc_imm(s, OPC_SRAI, a0, a1, 32);
        break;

    case INDEX_op_mulsh_i32:
    case INDEX_op_mulsh_i64:
        tcg_out_opc_reg(s, OPC_MULH, a0, a1, a2);
        break;

    case INDEX_op_muluh_i32:
    case INDEX_op_muluh_i64:
        tcg_out_opc_reg(s, OPC_MULHU, a0, a1, a2);
        break;

    case INDEX_op_mb:
        tcg_out_mb(s, a0);
        break;

    case INDEX_op_extract_i64:
        if (a2 + args[3] == 32) {
            if (a2 == 0) {
                tcg_out_ext32u(s, a0, a1);
            } else {
                tcg_out_opc_imm(s, OPC_SRLIW, a0, a1, a2);
            }
            break;
        }
        /* FALLTHRU */
    case INDEX_op_extract_i32:
        switch (args[3]) {
        case 1:
            tcg_out_opc_imm(s, OPC_BEXTI, a0, a1, a2);
            break;
        case 16:
            tcg_debug_assert(a2 == 0);
            tcg_out_ext16u(s, a0, a1);
            break;
        default:
            g_assert_not_reached();
        }
        break;

    case INDEX_op_sextract_i64:
        if (a2 + args[3] == 32) {
            if (a2 == 0) {
                tcg_out_ext32s(s, a0, a1);
            } else {
                tcg_out_opc_imm(s, OPC_SRAIW, a0, a1, a2);
            }
            break;
        }
        /* FALLTHRU */
    case INDEX_op_sextract_i32:
        if (a2 == 0 && args[3] == 8) {
            tcg_out_ext8s(s, TCG_TYPE_REG, a0, a1);
        } else if (a2 == 0 && args[3] == 16) {
            tcg_out_ext16s(s, TCG_TYPE_REG, a0, a1);
        } else {
            g_assert_not_reached();
        }
        break;

    case INDEX_op_mov_i32:  /* Always emitted via tcg_out_mov.  */
    case INDEX_op_mov_i64:
    case INDEX_op_call:     /* Always emitted via tcg_out_call.  */
    case INDEX_op_exit_tb:  /* Always emitted via tcg_out_exit_tb.  */
    case INDEX_op_goto_tb:  /* Always emitted via tcg_out_goto_tb.  */
    case INDEX_op_ext8s_i32:  /* Always emitted via tcg_reg_alloc_op.  */
    case INDEX_op_ext8s_i64:
    case INDEX_op_ext8u_i32:
    case INDEX_op_ext8u_i64:
    case INDEX_op_ext16s_i32:
    case INDEX_op_ext16s_i64:
    case INDEX_op_ext16u_i32:
    case INDEX_op_ext16u_i64:
    case INDEX_op_ext32s_i64:
    case INDEX_op_ext32u_i64:
    case INDEX_op_ext_i32_i64:
    case INDEX_op_extu_i32_i64:
    case INDEX_op_extrl_i64_i32:
    default:
        g_assert_not_reached();
    }
}

static void tcg_out_vec_op(TCGContext *s, TCGOpcode opc,
                           unsigned vecl, unsigned vece,
                           const TCGArg args[TCG_MAX_OP_ARGS],
                           const int const_args[TCG_MAX_OP_ARGS])
{
    TCGType type = vecl + TCG_TYPE_V64;
    TCGArg a0, a1, a2;
    int c2;

    a0 = args[0];
    a1 = args[1];
    a2 = args[2];
    c2 = const_args[2];

    switch (opc) {
    case INDEX_op_dupm_vec:
        tcg_out_dupm_vec(s, type, vece, a0, a1, a2);
        break;
    case INDEX_op_ld_vec:
        tcg_out_ld(s, type, a0, a1, a2);
        break;
    case INDEX_op_st_vec:
        tcg_out_st(s, type, a0, a1, a2);
        break;
    case INDEX_op_add_vec:
        set_vtype_len_sew(s, type, vece);
        tcg_out_opc_vv_vi(s, OPC_VADD_VV, OPC_VADD_VI, a0, a1, a2, c2);
        break;
    case INDEX_op_sub_vec:
        set_vtype_len_sew(s, type, vece);
        if (const_args[1]) {
            tcg_out_opc_vi(s, OPC_VRSUB_VI, a0, a2, a1);
        } else {
            tcg_out_opc_vv(s, OPC_VSUB_VV, a0, a1, a2);
        }
        break;
    case INDEX_op_and_vec:
        set_vtype_len(s, type);
        tcg_out_opc_vv_vi(s, OPC_VAND_VV, OPC_VAND_VI, a0, a1, a2, c2);
        break;
    case INDEX_op_or_vec:
        set_vtype_len(s, type);
        tcg_out_opc_vv_vi(s, OPC_VOR_VV, OPC_VOR_VI, a0, a1, a2, c2);
        break;
    case INDEX_op_xor_vec:
        set_vtype_len(s, type);
        tcg_out_opc_vv_vi(s, OPC_VXOR_VV, OPC_VXOR_VI, a0, a1, a2, c2);
        break;
    case INDEX_op_not_vec:
        set_vtype_len(s, type);
        tcg_out_opc_vi(s, OPC_VXOR_VI, a0, a1, -1);
        break;
    case INDEX_op_neg_vec:
        set_vtype_len_sew(s, type, vece);
        tcg_out_opc_vi(s, OPC_VRSUB_VI, a0, a1, 0);
        break;
    case INDEX_op_mul_vec:
        set_vtype_len_sew(s, type, vece);
        tcg_out_opc_vv(s, OPC_VMUL_VV, a0, a1, a2);
        break;
    case INDEX_op_ssadd_vec:
        set_vtype_len_sew(s, type, vece);
        tcg_out_opc_vv_vi(s, OPC_VSADD_VV, OPC_VSADD_VI, a0, a1, a2, c2);
        break;
    case INDEX_op_sssub_vec:
        set_vtype_len_sew(s, type, vece);
        tcg_out_opc_vv_vi(s, OPC_VSSUB_VV, OPC_VSSUB_VI, a0, a1, a2, c2);
        break;
    case INDEX_op_usadd_vec:
        set_vtype_len_sew(s, type, vece);
        tcg_out_opc_vv_vi(s, OPC_VSADDU_VV, OPC_VSADDU_VI, a0, a1, a2, c2);
        break;
    case INDEX_op_ussub_vec:
        set_vtype_len_sew(s, type, vece);
        tcg_out_opc_vv_vi(s, OPC_VSSUBU_VV, OPC_VSSUBU_VI, a0, a1, a2, c2);
        break;
    case INDEX_op_smax_vec:
        set_vtype_len_sew(s, type, vece);
        tcg_out_opc_vv_vi(s, OPC_VMAX_VV, OPC_VMAX_VI, a0, a1, a2, c2);
        break;
    case INDEX_op_smin_vec:
        set_vtype_len_sew(s, type, vece);
        tcg_out_opc_vv_vi(s, OPC_VMIN_VV, OPC_VMIN_VI, a0, a1, a2, c2);
        break;
    case INDEX_op_umax_vec:
        set_vtype_len_sew(s, type, vece);
        tcg_out_opc_vv_vi(s, OPC_VMAXU_VV, OPC_VMAXU_VI, a0, a1, a2, c2);
        break;
    case INDEX_op_umin_vec:
        set_vtype_len_sew(s, type, vece);
        tcg_out_opc_vv_vi(s, OPC_VMINU_VV, OPC_VMINU_VI, a0, a1, a2, c2);
        break;
    case INDEX_op_shls_vec:
        set_vtype_len_sew(s, type, vece);
        tcg_out_opc_vx(s, OPC_VSLL_VX, a0, a1, a2);
        break;
    case INDEX_op_shrs_vec:
        set_vtype_len_sew(s, type, vece);
        tcg_out_opc_vx(s, OPC_VSRL_VX, a0, a1, a2);
        break;
    case INDEX_op_sars_vec:
        set_vtype_len_sew(s, type, vece);
        tcg_out_opc_vx(s, OPC_VSRA_VX, a0, a1, a2);
        break;
    case INDEX_op_shlv_vec:
        set_vtype_len_sew(s, type, vece);
        tcg_out_opc_vv(s, OPC_VSLL_VV, a0, a1, a2);
        break;
    case INDEX_op_shrv_vec:
        set_vtype_len_sew(s, type, vece);
        tcg_out_opc_vv(s, OPC_VSRL_VV, a0, a1, a2);
        break;
    case INDEX_op_sarv_vec:
        set_vtype_len_sew(s, type, vece);
        tcg_out_opc_vv(s, OPC_VSRA_VV, a0, a1, a2);
        break;
    case INDEX_op_shli_vec:
        set_vtype_len_sew(s, type, vece);
        tcg_out_vshifti(s, OPC_VSLL_VI, OPC_VSLL_VX, a0, a1, a2);
        break;
    case INDEX_op_shri_vec:
        set_vtype_len_sew(s, type, vece);
        tcg_out_vshifti(s, OPC_VSRL_VI, OPC_VSRL_VX, a0, a1, a2);
        break;
    case INDEX_op_sari_vec:
        set_vtype_len_sew(s, type, vece);
        tcg_out_vshifti(s, OPC_VSRA_VI, OPC_VSRA_VX, a0, a1, a2);
        break;
    case INDEX_op_rotli_vec:
        set_vtype_len_sew(s, type, vece);
        tcg_out_vshifti(s, OPC_VSLL_VI, OPC_VSLL_VX, TCG_REG_V0, a1, a2);
        tcg_out_vshifti(s, OPC_VSRL_VI, OPC_VSRL_VX, a0, a1,
                        -a2 & ((8 << vece) - 1));
        tcg_out_opc_vv(s, OPC_VOR_VV, a0, a0, TCG_REG_V0);
        break;
    case INDEX_op_rotls_vec:
        set_vtype_len_sew(s, type, vece);
        tcg_out_opc_vx(s, OPC_VSLL_VX, TCG_REG_V0, a1, a2);
        tcg_out_opc_reg(s, OPC_SUBW, TCG_REG_TMP0, TCG_REG_ZERO, a2);
        tcg_out_opc_vx(s, OPC_VSRL_VX, a0, a1, TCG_REG_TMP0);
        tcg_out_opc_vv(s, OPC_VOR_VV, a0, a0, TCG_REG_V0);
        break;
    case INDEX_op_rotlv_vec:
        set_vtype_len_sew(s, type, vece);
        tcg_out_opc_vi(s, OPC_VRSUB_VI, TCG_REG_V0, a2, 0);
        tcg_out_opc_vv(s, OPC_VSRL_VV, TCG_REG_V0, a1, TCG_REG_V0);
        tcg_out_opc_vv(s, OPC_VSLL_VV, a0, a1, a2);
        tcg_out_opc_vv(s, OPC_VOR_VV, a0, a0, TCG_REG_V0);
        break;
    case INDEX_op_rotrv_vec:
        set_vtype_len_sew(s, type, vece);
        tcg_out_opc_vi(s, OPC_VRSUB_VI, TCG_REG_V0, a2, 0);
        tcg_out_opc_vv(s, OPC_VSLL_VV, TCG_REG_V0, a1, TCG_REG_V0);
        tcg_out_opc_vv(s, OPC_VSRL_VV, a0, a1, a2);
        tcg_out_opc_vv(s, OPC_VOR_VV, a0, a0, TCG_REG_V0);
        break;
    case INDEX_op_cmp_vec:
        tcg_out_cmpsel(s, type, vece, args[3], a0, a1, a2, c2,
                       -1, true, 0, true);
        break;
    case INDEX_op_cmpsel_vec:
        tcg_out_cmpsel(s, type, vece, args[5], a0, a1, a2, c2,
                       args[3], const_args[3], args[4], const_args[4]);
        break;
    case INDEX_op_mov_vec: /* Always emitted via tcg_out_mov.  */
    case INDEX_op_dup_vec: /* Always emitted via tcg_out_dup_vec.  */
    default:
        g_assert_not_reached();
    }
}

void tcg_expand_vec_op(TCGOpcode opc, TCGType type, unsigned vece,
                       TCGArg a0, ...)
{
    g_assert_not_reached();
}

int tcg_can_emit_vec_op(TCGOpcode opc, TCGType type, unsigned vece)
{
    switch (opc) {
    case INDEX_op_add_vec:
    case INDEX_op_sub_vec:
    case INDEX_op_and_vec:
    case INDEX_op_or_vec:
    case INDEX_op_xor_vec:
    case INDEX_op_not_vec:
    case INDEX_op_neg_vec:
    case INDEX_op_mul_vec:
    case INDEX_op_ssadd_vec:
    case INDEX_op_sssub_vec:
    case INDEX_op_usadd_vec:
    case INDEX_op_ussub_vec:
    case INDEX_op_smax_vec:
    case INDEX_op_smin_vec:
    case INDEX_op_umax_vec:
    case INDEX_op_umin_vec:
    case INDEX_op_shls_vec:
    case INDEX_op_shrs_vec:
    case INDEX_op_sars_vec:
    case INDEX_op_shlv_vec:
    case INDEX_op_shrv_vec:
    case INDEX_op_sarv_vec:
    case INDEX_op_shri_vec:
    case INDEX_op_shli_vec:
    case INDEX_op_sari_vec:
    case INDEX_op_rotls_vec:
    case INDEX_op_rotlv_vec:
    case INDEX_op_rotrv_vec:
    case INDEX_op_rotli_vec:
    case INDEX_op_cmp_vec:
    case INDEX_op_cmpsel_vec:
        return 1;
    default:
        return 0;
    }
}

static TCGConstraintSetIndex
tcg_target_op_def(TCGOpcode op, TCGType type, unsigned flags)
{
    switch (op) {
    case INDEX_op_goto_ptr:
        return C_O0_I1(r);

    case INDEX_op_ld8u_i32:
    case INDEX_op_ld8s_i32:
    case INDEX_op_ld16u_i32:
    case INDEX_op_ld16s_i32:
    case INDEX_op_ld_i32:
    case INDEX_op_not_i32:
    case INDEX_op_neg_i32:
    case INDEX_op_ld8u_i64:
    case INDEX_op_ld8s_i64:
    case INDEX_op_ld16u_i64:
    case INDEX_op_ld16s_i64:
    case INDEX_op_ld32s_i64:
    case INDEX_op_ld32u_i64:
    case INDEX_op_ld_i64:
    case INDEX_op_not_i64:
    case INDEX_op_neg_i64:
    case INDEX_op_ext8u_i32:
    case INDEX_op_ext8u_i64:
    case INDEX_op_ext16u_i32:
    case INDEX_op_ext16u_i64:
    case INDEX_op_ext32u_i64:
    case INDEX_op_extu_i32_i64:
    case INDEX_op_ext8s_i32:
    case INDEX_op_ext8s_i64:
    case INDEX_op_ext16s_i32:
    case INDEX_op_ext16s_i64:
    case INDEX_op_ext32s_i64:
    case INDEX_op_extrl_i64_i32:
    case INDEX_op_extrh_i64_i32:
    case INDEX_op_ext_i32_i64:
    case INDEX_op_extract_i32:
    case INDEX_op_extract_i64:
    case INDEX_op_sextract_i32:
    case INDEX_op_sextract_i64:
    case INDEX_op_bswap16_i32:
    case INDEX_op_bswap32_i32:
    case INDEX_op_bswap16_i64:
    case INDEX_op_bswap32_i64:
    case INDEX_op_bswap64_i64:
    case INDEX_op_ctpop_i32:
    case INDEX_op_ctpop_i64:
        return C_O1_I1(r, r);

    case INDEX_op_st8_i32:
    case INDEX_op_st16_i32:
    case INDEX_op_st_i32:
    case INDEX_op_st8_i64:
    case INDEX_op_st16_i64:
    case INDEX_op_st32_i64:
    case INDEX_op_st_i64:
        return C_O0_I2(rz, r);

    case INDEX_op_add_i32:
    case INDEX_op_and_i32:
    case INDEX_op_or_i32:
    case INDEX_op_xor_i32:
    case INDEX_op_add_i64:
    case INDEX_op_and_i64:
    case INDEX_op_or_i64:
    case INDEX_op_xor_i64:
    case INDEX_op_setcond_i32:
    case INDEX_op_setcond_i64:
    case INDEX_op_negsetcond_i32:
    case INDEX_op_negsetcond_i64:
        return C_O1_I2(r, r, rI);

    case INDEX_op_andc_i32:
    case INDEX_op_andc_i64:
    case INDEX_op_orc_i32:
    case INDEX_op_orc_i64:
    case INDEX_op_eqv_i32:
    case INDEX_op_eqv_i64:
        return C_O1_I2(r, r, rJ);

    case INDEX_op_sub_i32:
    case INDEX_op_sub_i64:
        return C_O1_I2(r, rz, rN);

    case INDEX_op_mul_i32:
    case INDEX_op_mulsh_i32:
    case INDEX_op_muluh_i32:
    case INDEX_op_div_i32:
    case INDEX_op_divu_i32:
    case INDEX_op_rem_i32:
    case INDEX_op_remu_i32:
    case INDEX_op_mul_i64:
    case INDEX_op_mulsh_i64:
    case INDEX_op_muluh_i64:
    case INDEX_op_div_i64:
    case INDEX_op_divu_i64:
    case INDEX_op_rem_i64:
    case INDEX_op_remu_i64:
        return C_O1_I2(r, rz, rz);

    case INDEX_op_shl_i32:
    case INDEX_op_shr_i32:
    case INDEX_op_sar_i32:
    case INDEX_op_rotl_i32:
    case INDEX_op_rotr_i32:
    case INDEX_op_shl_i64:
    case INDEX_op_shr_i64:
    case INDEX_op_sar_i64:
    case INDEX_op_rotl_i64:
    case INDEX_op_rotr_i64:
        return C_O1_I2(r, r, ri);

    case INDEX_op_clz_i32:
    case INDEX_op_clz_i64:
    case INDEX_op_ctz_i32:
    case INDEX_op_ctz_i64:
        return C_N1_I2(r, r, rM);

    case INDEX_op_brcond_i32:
    case INDEX_op_brcond_i64:
        return C_O0_I2(rz, rz);

    case INDEX_op_movcond_i32:
    case INDEX_op_movcond_i64:
        return C_O1_I4(r, r, rI, rM, rM);

    case INDEX_op_add2_i32:
    case INDEX_op_add2_i64:
    case INDEX_op_sub2_i32:
    case INDEX_op_sub2_i64:
        return C_O2_I4(r, r, rz, rz, rM, rM);

    case INDEX_op_qemu_ld_i32:
    case INDEX_op_qemu_ld_i64:
        return C_O1_I1(r, r);
    case INDEX_op_qemu_st_i32:
    case INDEX_op_qemu_st_i64:
        return C_O0_I2(rz, r);

    case INDEX_op_st_vec:
        return C_O0_I2(v, r);
    case INDEX_op_dup_vec:
    case INDEX_op_dupm_vec:
    case INDEX_op_ld_vec:
        return C_O1_I1(v, r);
    case INDEX_op_neg_vec:
    case INDEX_op_not_vec:
    case INDEX_op_shli_vec:
    case INDEX_op_shri_vec:
    case INDEX_op_sari_vec:
    case INDEX_op_rotli_vec:
        return C_O1_I1(v, v);
    case INDEX_op_add_vec:
    case INDEX_op_and_vec:
    case INDEX_op_or_vec:
    case INDEX_op_xor_vec:
    case INDEX_op_ssadd_vec:
    case INDEX_op_sssub_vec:
    case INDEX_op_usadd_vec:
    case INDEX_op_ussub_vec:
    case INDEX_op_smax_vec:
    case INDEX_op_smin_vec:
    case INDEX_op_umax_vec:
    case INDEX_op_umin_vec:
        return C_O1_I2(v, v, vK);
    case INDEX_op_sub_vec:
        return C_O1_I2(v, vK, v);
    case INDEX_op_mul_vec:
    case INDEX_op_shlv_vec:
    case INDEX_op_shrv_vec:
    case INDEX_op_sarv_vec:
    case INDEX_op_rotlv_vec:
    case INDEX_op_rotrv_vec:
        return C_O1_I2(v, v, v);
    case INDEX_op_shls_vec:
    case INDEX_op_shrs_vec:
    case INDEX_op_sars_vec:
    case INDEX_op_rotls_vec:
        return C_O1_I2(v, v, r);
    case INDEX_op_cmp_vec:
        return C_O1_I2(v, v, vL);
    case INDEX_op_cmpsel_vec:
        return C_O1_I4(v, v, vL, vK, vK);
    default:
        return C_NotImplemented;
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
    TCG_REG_S9,
    TCG_REG_S10,
    TCG_REG_S11,
    TCG_REG_RA,       /* should be last for ABI compliance */
};

/* Stack frame parameters.  */
#define REG_SIZE   (TCG_TARGET_REG_BITS / 8)
#define SAVE_SIZE  ((int)ARRAY_SIZE(tcg_target_callee_save_regs) * REG_SIZE)
#define TEMP_SIZE  (CPU_TEMP_BUF_NLONGS * (int)sizeof(long))
#define FRAME_SIZE ((TCG_STATIC_CALL_ARGS_SIZE + TEMP_SIZE + SAVE_SIZE \
                     + TCG_TARGET_STACK_ALIGN - 1) \
                    & -TCG_TARGET_STACK_ALIGN)
#define SAVE_OFS   (TCG_STATIC_CALL_ARGS_SIZE + TEMP_SIZE)

/* We're expecting to be able to use an immediate for frame allocation.  */
QEMU_BUILD_BUG_ON(FRAME_SIZE > 0x7ff);

/* Generate global QEMU prologue and epilogue code */
static void tcg_target_qemu_prologue(TCGContext *s)
{
    int i;

    tcg_set_frame(s, TCG_REG_SP, TCG_STATIC_CALL_ARGS_SIZE, TEMP_SIZE);

    /* TB prologue */
    tcg_out_opc_imm(s, OPC_ADDI, TCG_REG_SP, TCG_REG_SP, -FRAME_SIZE);
    for (i = 0; i < ARRAY_SIZE(tcg_target_callee_save_regs); i++) {
        tcg_out_st(s, TCG_TYPE_REG, tcg_target_callee_save_regs[i],
                   TCG_REG_SP, SAVE_OFS + i * REG_SIZE);
    }

    if (!tcg_use_softmmu && guest_base) {
        tcg_out_movi(s, TCG_TYPE_PTR, TCG_GUEST_BASE_REG, guest_base);
        tcg_regset_set_reg(s->reserved_regs, TCG_GUEST_BASE_REG);
    }

    /* Call generated code */
    tcg_out_mov(s, TCG_TYPE_PTR, TCG_AREG0, tcg_target_call_iarg_regs[0]);
    tcg_out_opc_imm(s, OPC_JALR, TCG_REG_ZERO, tcg_target_call_iarg_regs[1], 0);

    /* Return path for goto_ptr. Set return value to 0 */
    tcg_code_gen_epilogue = tcg_splitwx_to_rx(s->code_ptr);
    tcg_out_mov(s, TCG_TYPE_REG, TCG_REG_A0, TCG_REG_ZERO);

    /* TB epilogue */
    tb_ret_addr = tcg_splitwx_to_rx(s->code_ptr);
    for (i = 0; i < ARRAY_SIZE(tcg_target_callee_save_regs); i++) {
        tcg_out_ld(s, TCG_TYPE_REG, tcg_target_callee_save_regs[i],
                   TCG_REG_SP, SAVE_OFS + i * REG_SIZE);
    }

    tcg_out_opc_imm(s, OPC_ADDI, TCG_REG_SP, TCG_REG_SP, FRAME_SIZE);
    tcg_out_opc_imm(s, OPC_JALR, TCG_REG_ZERO, TCG_REG_RA, 0);
}

static void tcg_out_tb_start(TCGContext *s)
{
    init_setting_vtype(s);
}

static bool vtype_check(unsigned vtype)
{
    unsigned long tmp;

    /* vsetvl tmp, zero, vtype */
    asm(".insn r 0x57, 7, 0x40, %0, zero, %1" : "=r"(tmp) : "r"(vtype));
    return tmp != 0;
}

static void probe_frac_lmul_1(TCGType type, MemOp vsew)
{
    VsetCache *p = &riscv_vset_cache[type - TCG_TYPE_V64][vsew];
    unsigned avl = tcg_type_size(type) >> vsew;
    int lmul = type - riscv_lg2_vlenb;
    unsigned vtype = encode_vtype(true, true, vsew, lmul & 7);
    bool lmul_eq_avl = true;

    /* Guaranteed by Zve64x. */
    assert(lmul < 3);

    /*
     * For LMUL < -3, the host vector size is so large that TYPE
     * is smaller than the minimum 1/8 fraction.
     *
     * For other fractional LMUL settings, implementations must
     * support SEW settings between SEW_MIN and LMUL * ELEN, inclusive.
     * So if ELEN = 64, LMUL = 1/2, then SEW will support e8, e16, e32,
     * but e64 may not be supported. In other words, the hardware only
     * guarantees SEW_MIN <= SEW <= LMUL * ELEN.  Check.
     */
    if (lmul < 0 && (lmul < -3 || !vtype_check(vtype))) {
        vtype = encode_vtype(true, true, vsew, VLMUL_M1);
        lmul_eq_avl = false;
    }

    if (avl < 32) {
        p->vset_insn = encode_vseti(OPC_VSETIVLI, TCG_REG_ZERO, avl, vtype);
    } else if (lmul_eq_avl) {
        /* rd != 0 and rs1 == 0 uses vlmax */
        p->vset_insn = encode_vset(OPC_VSETVLI, TCG_REG_TMP0, TCG_REG_ZERO, vtype);
    } else {
        p->movi_insn = encode_i(OPC_ADDI, TCG_REG_TMP0, TCG_REG_ZERO, avl);
        p->vset_insn = encode_vset(OPC_VSETVLI, TCG_REG_ZERO, TCG_REG_TMP0, vtype);
    }
}

static void probe_frac_lmul(void)
{
    /* Match riscv_lg2_vlenb to TCG_TYPE_V64. */
    QEMU_BUILD_BUG_ON(TCG_TYPE_V64 != 3);

    for (TCGType t = TCG_TYPE_V64; t <= TCG_TYPE_V256; t++) {
        for (MemOp e = MO_8; e <= MO_64; e++) {
            probe_frac_lmul_1(t, e);
        }
    }
}

static void tcg_target_init(TCGContext *s)
{
    tcg_target_available_regs[TCG_TYPE_I32] = 0xffffffff;
    tcg_target_available_regs[TCG_TYPE_I64] = 0xffffffff;

    tcg_target_call_clobber_regs = -1;
    tcg_regset_reset_reg(tcg_target_call_clobber_regs, TCG_REG_S0);
    tcg_regset_reset_reg(tcg_target_call_clobber_regs, TCG_REG_S1);
    tcg_regset_reset_reg(tcg_target_call_clobber_regs, TCG_REG_S2);
    tcg_regset_reset_reg(tcg_target_call_clobber_regs, TCG_REG_S3);
    tcg_regset_reset_reg(tcg_target_call_clobber_regs, TCG_REG_S4);
    tcg_regset_reset_reg(tcg_target_call_clobber_regs, TCG_REG_S5);
    tcg_regset_reset_reg(tcg_target_call_clobber_regs, TCG_REG_S6);
    tcg_regset_reset_reg(tcg_target_call_clobber_regs, TCG_REG_S7);
    tcg_regset_reset_reg(tcg_target_call_clobber_regs, TCG_REG_S8);
    tcg_regset_reset_reg(tcg_target_call_clobber_regs, TCG_REG_S9);
    tcg_regset_reset_reg(tcg_target_call_clobber_regs, TCG_REG_S10);
    tcg_regset_reset_reg(tcg_target_call_clobber_regs, TCG_REG_S11);

    s->reserved_regs = 0;
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_ZERO);
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_TMP0);
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_TMP1);
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_TMP2);
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_SP);
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_GP);
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_TP);

    if (cpuinfo & CPUINFO_ZVE64X) {
        switch (riscv_lg2_vlenb) {
        case TCG_TYPE_V64:
            tcg_target_available_regs[TCG_TYPE_V64] = ALL_VECTOR_REGS;
            tcg_target_available_regs[TCG_TYPE_V128] = ALL_DVECTOR_REG_GROUPS;
            tcg_target_available_regs[TCG_TYPE_V256] = ALL_QVECTOR_REG_GROUPS;
            s->reserved_regs |= (~ALL_QVECTOR_REG_GROUPS & ALL_VECTOR_REGS);
            break;
        case TCG_TYPE_V128:
            tcg_target_available_regs[TCG_TYPE_V64] = ALL_VECTOR_REGS;
            tcg_target_available_regs[TCG_TYPE_V128] = ALL_VECTOR_REGS;
            tcg_target_available_regs[TCG_TYPE_V256] = ALL_DVECTOR_REG_GROUPS;
            s->reserved_regs |= (~ALL_DVECTOR_REG_GROUPS & ALL_VECTOR_REGS);
            break;
        default:
            /* Guaranteed by Zve64x. */
            tcg_debug_assert(riscv_lg2_vlenb >= TCG_TYPE_V256);
            tcg_target_available_regs[TCG_TYPE_V64] = ALL_VECTOR_REGS;
            tcg_target_available_regs[TCG_TYPE_V128] = ALL_VECTOR_REGS;
            tcg_target_available_regs[TCG_TYPE_V256] = ALL_VECTOR_REGS;
            break;
        }
        tcg_regset_set_reg(s->reserved_regs, TCG_REG_V0);
        probe_frac_lmul();
    }
}

typedef struct {
    DebugFrameHeader h;
    uint8_t fde_def_cfa[4];
    uint8_t fde_reg_ofs[ARRAY_SIZE(tcg_target_callee_save_regs) * 2];
} DebugFrame;

#define ELF_HOST_MACHINE EM_RISCV

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
        0x80 + 9,  12,                  /* DW_CFA_offset, s1,  -96 */
        0x80 + 18, 11,                  /* DW_CFA_offset, s2,  -88 */
        0x80 + 19, 10,                  /* DW_CFA_offset, s3,  -80 */
        0x80 + 20, 9,                   /* DW_CFA_offset, s4,  -72 */
        0x80 + 21, 8,                   /* DW_CFA_offset, s5,  -64 */
        0x80 + 22, 7,                   /* DW_CFA_offset, s6,  -56 */
        0x80 + 23, 6,                   /* DW_CFA_offset, s7,  -48 */
        0x80 + 24, 5,                   /* DW_CFA_offset, s8,  -40 */
        0x80 + 25, 4,                   /* DW_CFA_offset, s9,  -32 */
        0x80 + 26, 3,                   /* DW_CFA_offset, s10, -24 */
        0x80 + 27, 2,                   /* DW_CFA_offset, s11, -16 */
        0x80 + 1 , 1,                   /* DW_CFA_offset, ra,  -8 */
    }
};

void tcg_register_jit(const void *buf, size_t buf_size)
{
    tcg_register_jit_int(buf, buf_size, &debug_frame, sizeof(debug_frame));
}

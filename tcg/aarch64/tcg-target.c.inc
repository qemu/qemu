/*
 * Initial TCG Implementation for aarch64
 *
 * Copyright (c) 2013 Huawei Technologies Duesseldorf GmbH
 * Written by Claudio Fontana
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.
 *
 * See the COPYING file in the top-level directory for details.
 */

#include "qemu/bitops.h"

/* Used for function call generation. */
#define TCG_REG_CALL_STACK              TCG_REG_SP
#define TCG_TARGET_STACK_ALIGN          16
#define TCG_TARGET_CALL_STACK_OFFSET    0
#define TCG_TARGET_CALL_ARG_I32         TCG_CALL_ARG_NORMAL
#define TCG_TARGET_CALL_ARG_I64         TCG_CALL_ARG_NORMAL
#ifdef CONFIG_DARWIN
# define TCG_TARGET_CALL_ARG_I128       TCG_CALL_ARG_NORMAL
#else
# define TCG_TARGET_CALL_ARG_I128       TCG_CALL_ARG_EVEN
#endif
#define TCG_TARGET_CALL_RET_I128        TCG_CALL_RET_NORMAL

/* We're going to re-use TCGType in setting of the SF bit, which controls
   the size of the operation performed.  If we know the values match, it
   makes things much cleaner.  */
QEMU_BUILD_BUG_ON(TCG_TYPE_I32 != 0 || TCG_TYPE_I64 != 1);

#ifdef CONFIG_DEBUG_TCG
static const char * const tcg_target_reg_names[TCG_TARGET_NB_REGS] = {
    "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7",
    "x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15",
    "x16", "x17", "x18", "x19", "x20", "x21", "x22", "x23",
    "x24", "x25", "x26", "x27", "x28", "fp", "x30", "sp",

    "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7",
    "v8", "v9", "v10", "v11", "v12", "v13", "v14", "v15",
    "v16", "v17", "v18", "v19", "v20", "v21", "v22", "v23",
    "v24", "v25", "v26", "v27", "v28", "fp", "v30", "v31",
};
#endif /* CONFIG_DEBUG_TCG */

static const int tcg_target_reg_alloc_order[] = {
    TCG_REG_X20, TCG_REG_X21, TCG_REG_X22, TCG_REG_X23,
    TCG_REG_X24, TCG_REG_X25, TCG_REG_X26, TCG_REG_X27,
    TCG_REG_X28, /* we will reserve this for guest_base if configured */

    TCG_REG_X8, TCG_REG_X9, TCG_REG_X10, TCG_REG_X11,
    TCG_REG_X12, TCG_REG_X13, TCG_REG_X14, TCG_REG_X15,

    TCG_REG_X0, TCG_REG_X1, TCG_REG_X2, TCG_REG_X3,
    TCG_REG_X4, TCG_REG_X5, TCG_REG_X6, TCG_REG_X7,

    /* X16 reserved as temporary */
    /* X17 reserved as temporary */
    /* X18 reserved by system */
    /* X19 reserved for AREG0 */
    /* X29 reserved as fp */
    /* X30 reserved as temporary */

    TCG_REG_V0, TCG_REG_V1, TCG_REG_V2, TCG_REG_V3,
    TCG_REG_V4, TCG_REG_V5, TCG_REG_V6, TCG_REG_V7,
    /* V8 - V15 are call-saved, and skipped.  */
    TCG_REG_V16, TCG_REG_V17, TCG_REG_V18, TCG_REG_V19,
    TCG_REG_V20, TCG_REG_V21, TCG_REG_V22, TCG_REG_V23,
    TCG_REG_V24, TCG_REG_V25, TCG_REG_V26, TCG_REG_V27,
    TCG_REG_V28, TCG_REG_V29, TCG_REG_V30, TCG_REG_V31,
};

static const int tcg_target_call_iarg_regs[8] = {
    TCG_REG_X0, TCG_REG_X1, TCG_REG_X2, TCG_REG_X3,
    TCG_REG_X4, TCG_REG_X5, TCG_REG_X6, TCG_REG_X7
};

static TCGReg tcg_target_call_oarg_reg(TCGCallReturnKind kind, int slot)
{
    tcg_debug_assert(kind == TCG_CALL_RET_NORMAL);
    tcg_debug_assert(slot >= 0 && slot <= 1);
    return TCG_REG_X0 + slot;
}

#define TCG_REG_TMP0 TCG_REG_X16
#define TCG_REG_TMP1 TCG_REG_X17
#define TCG_REG_TMP2 TCG_REG_X30
#define TCG_VEC_TMP0 TCG_REG_V31

#define TCG_REG_GUEST_BASE TCG_REG_X28

static bool reloc_pc26(tcg_insn_unit *src_rw, const tcg_insn_unit *target)
{
    const tcg_insn_unit *src_rx = tcg_splitwx_to_rx(src_rw);
    ptrdiff_t offset = target - src_rx;

    if (offset == sextract64(offset, 0, 26)) {
        /* read instruction, mask away previous PC_REL26 parameter contents,
           set the proper offset, then write back the instruction. */
        *src_rw = deposit32(*src_rw, 0, 26, offset);
        return true;
    }
    return false;
}

static bool reloc_pc19(tcg_insn_unit *src_rw, const tcg_insn_unit *target)
{
    const tcg_insn_unit *src_rx = tcg_splitwx_to_rx(src_rw);
    ptrdiff_t offset = target - src_rx;

    if (offset == sextract64(offset, 0, 19)) {
        *src_rw = deposit32(*src_rw, 5, 19, offset);
        return true;
    }
    return false;
}

static bool reloc_pc14(tcg_insn_unit *src_rw, const tcg_insn_unit *target)
{
    const tcg_insn_unit *src_rx = tcg_splitwx_to_rx(src_rw);
    ptrdiff_t offset = target - src_rx;

    if (offset == sextract64(offset, 0, 14)) {
        *src_rw = deposit32(*src_rw, 5, 14, offset);
        return true;
    }
    return false;
}

static bool patch_reloc(tcg_insn_unit *code_ptr, int type,
                        intptr_t value, intptr_t addend)
{
    tcg_debug_assert(addend == 0);
    switch (type) {
    case R_AARCH64_JUMP26:
    case R_AARCH64_CALL26:
        return reloc_pc26(code_ptr, (const tcg_insn_unit *)value);
    case R_AARCH64_CONDBR19:
        return reloc_pc19(code_ptr, (const tcg_insn_unit *)value);
    case R_AARCH64_TSTBR14:
        return reloc_pc14(code_ptr, (const tcg_insn_unit *)value);
    default:
        g_assert_not_reached();
    }
}

#define TCG_CT_CONST_AIMM 0x100
#define TCG_CT_CONST_LIMM 0x200
#define TCG_CT_CONST_ZERO 0x400
#define TCG_CT_CONST_MONE 0x800
#define TCG_CT_CONST_ORRI 0x1000
#define TCG_CT_CONST_ANDI 0x2000
#define TCG_CT_CONST_CMP  0x4000
#define TCG_CT_CONST_S8   0x8000
#define TCG_CT_CONST_U8   0x10000

#define ALL_GENERAL_REGS  0xffffffffu
#define ALL_VECTOR_REGS   0xffffffff00000000ull

/* Match a constant valid for addition (12-bit, optionally shifted).  */
static inline bool is_aimm(uint64_t val)
{
    return (val & ~0xfff) == 0 || (val & ~0xfff000) == 0;
}

/* Match a constant valid for logical operations.  */
static inline bool is_limm(uint64_t val)
{
    /* Taking a simplified view of the logical immediates for now, ignoring
       the replication that can happen across the field.  Match bit patterns
       of the forms
           0....01....1
           0..01..10..0
       and their inverses.  */

    /* Make things easier below, by testing the form with msb clear. */
    if ((int64_t)val < 0) {
        val = ~val;
    }
    if (val == 0) {
        return false;
    }
    val += val & -val;
    return (val & (val - 1)) == 0;
}

/* Return true if v16 is a valid 16-bit shifted immediate.  */
static bool is_shimm16(uint16_t v16, int *cmode, int *imm8)
{
    if (v16 == (v16 & 0xff)) {
        *cmode = 0x8;
        *imm8 = v16 & 0xff;
        return true;
    } else if (v16 == (v16 & 0xff00)) {
        *cmode = 0xa;
        *imm8 = v16 >> 8;
        return true;
    }
    return false;
}

/* Return true if v32 is a valid 32-bit shifted immediate.  */
static bool is_shimm32(uint32_t v32, int *cmode, int *imm8)
{
    if (v32 == (v32 & 0xff)) {
        *cmode = 0x0;
        *imm8 = v32 & 0xff;
        return true;
    } else if (v32 == (v32 & 0xff00)) {
        *cmode = 0x2;
        *imm8 = (v32 >> 8) & 0xff;
        return true;
    } else if (v32 == (v32 & 0xff0000)) {
        *cmode = 0x4;
        *imm8 = (v32 >> 16) & 0xff;
        return true;
    } else if (v32 == (v32 & 0xff000000)) {
        *cmode = 0x6;
        *imm8 = v32 >> 24;
        return true;
    }
    return false;
}

/* Return true if v32 is a valid 32-bit shifting ones immediate.  */
static bool is_soimm32(uint32_t v32, int *cmode, int *imm8)
{
    if ((v32 & 0xffff00ff) == 0xff) {
        *cmode = 0xc;
        *imm8 = (v32 >> 8) & 0xff;
        return true;
    } else if ((v32 & 0xff00ffff) == 0xffff) {
        *cmode = 0xd;
        *imm8 = (v32 >> 16) & 0xff;
        return true;
    }
    return false;
}

/* Return true if v32 is a valid float32 immediate.  */
static bool is_fimm32(uint32_t v32, int *cmode, int *imm8)
{
    if (extract32(v32, 0, 19) == 0
        && (extract32(v32, 25, 6) == 0x20
            || extract32(v32, 25, 6) == 0x1f)) {
        *cmode = 0xf;
        *imm8 = (extract32(v32, 31, 1) << 7)
              | (extract32(v32, 25, 1) << 6)
              | extract32(v32, 19, 6);
        return true;
    }
    return false;
}

/* Return true if v64 is a valid float64 immediate.  */
static bool is_fimm64(uint64_t v64, int *cmode, int *imm8)
{
    if (extract64(v64, 0, 48) == 0
        && (extract64(v64, 54, 9) == 0x100
            || extract64(v64, 54, 9) == 0x0ff)) {
        *cmode = 0xf;
        *imm8 = (extract64(v64, 63, 1) << 7)
              | (extract64(v64, 54, 1) << 6)
              | extract64(v64, 48, 6);
        return true;
    }
    return false;
}

/*
 * Return non-zero if v32 can be formed by MOVI+ORR.
 * Place the parameters for MOVI in (cmode, imm8).
 * Return the cmode for ORR; the imm8 can be had via extraction from v32.
 */
static int is_shimm32_pair(uint32_t v32, int *cmode, int *imm8)
{
    int i;

    for (i = 6; i > 0; i -= 2) {
        /* Mask out one byte we can add with ORR.  */
        uint32_t tmp = v32 & ~(0xffu << (i * 4));
        if (is_shimm32(tmp, cmode, imm8) ||
            is_soimm32(tmp, cmode, imm8)) {
            break;
        }
    }
    return i;
}

/* Return true if V is a valid 16-bit or 32-bit shifted immediate.  */
static bool is_shimm1632(uint32_t v32, int *cmode, int *imm8)
{
    if (v32 == deposit32(v32, 16, 16, v32)) {
        return is_shimm16(v32, cmode, imm8);
    } else {
        return is_shimm32(v32, cmode, imm8);
    }
}

static bool tcg_target_const_match(int64_t val, int ct,
                                   TCGType type, TCGCond cond, int vece)
{
    if (ct & TCG_CT_CONST) {
        return 1;
    }
    if (type == TCG_TYPE_I32) {
        val = (int32_t)val;
    }

    if (ct & TCG_CT_CONST_CMP) {
        if (is_tst_cond(cond)) {
            ct |= TCG_CT_CONST_LIMM;
        } else {
            ct |= TCG_CT_CONST_AIMM;
        }
    }

    if ((ct & TCG_CT_CONST_AIMM) && (is_aimm(val) || is_aimm(-val))) {
        return 1;
    }
    if ((ct & TCG_CT_CONST_LIMM) && is_limm(val)) {
        return 1;
    }
    if ((ct & TCG_CT_CONST_S8) && val == (int8_t)val) {
        return 1;
    }
    if ((ct & TCG_CT_CONST_U8) && val == (uint8_t)val) {
        return 1;
    }
    if ((ct & TCG_CT_CONST_ZERO) && val == 0) {
        return 1;
    }
    if ((ct & TCG_CT_CONST_MONE) && val == -1) {
        return 1;
    }

    switch (ct & (TCG_CT_CONST_ORRI | TCG_CT_CONST_ANDI)) {
    case 0:
        break;
    case TCG_CT_CONST_ANDI:
        val = ~val;
        /* fallthru */
    case TCG_CT_CONST_ORRI:
        if (val == deposit64(val, 32, 32, val)) {
            int cmode, imm8;
            return is_shimm1632(val, &cmode, &imm8);
        }
        break;
    default:
        /* Both bits should not be set for the same insn.  */
        g_assert_not_reached();
    }

    return 0;
}

enum aarch64_cond_code {
    COND_EQ = 0x0,
    COND_NE = 0x1,
    COND_CS = 0x2,     /* Unsigned greater or equal */
    COND_HS = COND_CS, /* ALIAS greater or equal */
    COND_CC = 0x3,     /* Unsigned less than */
    COND_LO = COND_CC, /* ALIAS Lower */
    COND_MI = 0x4,     /* Negative */
    COND_PL = 0x5,     /* Zero or greater */
    COND_VS = 0x6,     /* Overflow */
    COND_VC = 0x7,     /* No overflow */
    COND_HI = 0x8,     /* Unsigned greater than */
    COND_LS = 0x9,     /* Unsigned less or equal */
    COND_GE = 0xa,
    COND_LT = 0xb,
    COND_GT = 0xc,
    COND_LE = 0xd,
    COND_AL = 0xe,
    COND_NV = 0xf, /* behaves like COND_AL here */
};

static const enum aarch64_cond_code tcg_cond_to_aarch64[] = {
    [TCG_COND_EQ] = COND_EQ,
    [TCG_COND_NE] = COND_NE,
    [TCG_COND_LT] = COND_LT,
    [TCG_COND_GE] = COND_GE,
    [TCG_COND_LE] = COND_LE,
    [TCG_COND_GT] = COND_GT,
    /* unsigned */
    [TCG_COND_LTU] = COND_LO,
    [TCG_COND_GTU] = COND_HI,
    [TCG_COND_GEU] = COND_HS,
    [TCG_COND_LEU] = COND_LS,
    /* bit test */
    [TCG_COND_TSTEQ] = COND_EQ,
    [TCG_COND_TSTNE] = COND_NE,
};

typedef enum {
    LDST_ST = 0,    /* store */
    LDST_LD = 1,    /* load */
    LDST_LD_S_X = 2,  /* load and sign-extend into Xt */
    LDST_LD_S_W = 3,  /* load and sign-extend into Wt */
} AArch64LdstType;

/* We encode the format of the insn into the beginning of the name, so that
   we can have the preprocessor help "typecheck" the insn vs the output
   function.  Arm didn't provide us with nice names for the formats, so we
   use the section number of the architecture reference manual in which the
   instruction group is described.  */
typedef enum {
    /* Compare and branch (immediate).  */
    Icbz_CBZ         = 0x34000000,
    Icbz_CBNZ        = 0x35000000,

    /* Conditional branch (immediate).  */
    Ibcond_imm_B_C   = 0x54000000,

    /* Test and branch (immediate).  */
    Itbz_TBZ         = 0x36000000,
    Itbz_TBNZ        = 0x37000000,

    /* Unconditional branch (immediate).  */
    Ibranch_B        = 0x14000000,
    Ibranch_BL       = 0x94000000,

    /* Unconditional branch (register).  */
    Ibcond_reg_BR    = 0xd61f0000,
    Ibcond_reg_BLR   = 0xd63f0000,
    Ibcond_reg_RET   = 0xd65f0000,

    /* AdvSIMD load/store single structure.  */
    Isimd_loadrep_LD1R = 0x0d40c000,

    /* Load literal for loading the address at pc-relative offset */
    Ildlit_LDR       = 0x58000000,
    Ildlit_LDR_v64   = 0x5c000000,
    Ildlit_LDR_v128  = 0x9c000000,

    /* Load/store exclusive. */
    Istxp_LDXP       = 0xc8600000,
    Istxp_STXP       = 0xc8200000,

    /* Load/store register.  Described here as 3.3.12, but the helper
       that emits them can transform to 3.3.10 or 3.3.13.  */
    Ildst_imm_STRB   = 0x38000000 | LDST_ST << 22 | MO_8 << 30,
    Ildst_imm_STRH   = 0x38000000 | LDST_ST << 22 | MO_16 << 30,
    Ildst_imm_STRW   = 0x38000000 | LDST_ST << 22 | MO_32 << 30,
    Ildst_imm_STRX   = 0x38000000 | LDST_ST << 22 | MO_64 << 30,

    Ildst_imm_LDRB   = 0x38000000 | LDST_LD << 22 | MO_8 << 30,
    Ildst_imm_LDRH   = 0x38000000 | LDST_LD << 22 | MO_16 << 30,
    Ildst_imm_LDRW   = 0x38000000 | LDST_LD << 22 | MO_32 << 30,
    Ildst_imm_LDRX   = 0x38000000 | LDST_LD << 22 | MO_64 << 30,

    Ildst_imm_LDRSBW = 0x38000000 | LDST_LD_S_W << 22 | MO_8 << 30,
    Ildst_imm_LDRSHW = 0x38000000 | LDST_LD_S_W << 22 | MO_16 << 30,

    Ildst_imm_LDRSBX = 0x38000000 | LDST_LD_S_X << 22 | MO_8 << 30,
    Ildst_imm_LDRSHX = 0x38000000 | LDST_LD_S_X << 22 | MO_16 << 30,
    Ildst_imm_LDRSWX = 0x38000000 | LDST_LD_S_X << 22 | MO_32 << 30,

    Ildst_imm_LDRVS  = 0x3c000000 | LDST_LD << 22 | MO_32 << 30,
    Ildst_imm_STRVS  = 0x3c000000 | LDST_ST << 22 | MO_32 << 30,

    Ildst_imm_LDRVD  = 0x3c000000 | LDST_LD << 22 | MO_64 << 30,
    Ildst_imm_STRVD  = 0x3c000000 | LDST_ST << 22 | MO_64 << 30,

    Ildst_imm_LDRVQ  = 0x3c000000 | 3 << 22 | 0 << 30,
    Ildst_imm_STRVQ  = 0x3c000000 | 2 << 22 | 0 << 30,

    /* Additions to the ldst_imm format */
    ldst_imm_to_reg  = 0x00200800,
    ldst_imm_to_uimm = 0x01000000,

    /* Load/store register pair instructions.  */
    Ildstpair_LDP    = 0x28400000,
    Ildstpair_STP    = 0x28000000,

    /* Add/subtract immediate instructions.  */
    Iaddsub_imm_ADDI  = 0x11000000,
    Iaddsub_imm_ADDSI = 0x31000000,
    Iaddsub_imm_SUBI  = 0x51000000,
    Iaddsub_imm_SUBSI = 0x71000000,

    /* Min/max immediate instructions. */
    Iminmax_imm_SMAXI = 0x11c00000,
    Iminmax_imm_UMAXI = 0x11c40000,
    Iminmax_imm_SMINI = 0x11c80000,
    Iminmax_imm_UMINI = 0x11cc0000,

    /* Bitfield instructions.  */
    Ibitfield_BFM     = 0x33000000,
    Ibitfield_SBFM    = 0x13000000,
    Ibitfield_UBFM    = 0x53000000,

    /* Extract instruction.  */
    Iextract_EXTR     = 0x13800000,

    /* Logical immediate instructions.  */
    Ilogic_imm_ANDI   = 0x12000000,
    Ilogic_imm_ORRI   = 0x32000000,
    Ilogic_imm_EORI   = 0x52000000,
    Ilogic_imm_ANDSI  = 0x72000000,

    /* Move wide immediate instructions.  */
    Imovw_MOVN        = 0x12800000,
    Imovw_MOVZ        = 0x52800000,
    Imovw_MOVK        = 0x72800000,

    /* PC relative addressing instructions.  */
    Ipcrel_ADR        = 0x10000000,
    Ipcrel_ADRP       = 0x90000000,

    /* Add/subtract extended register instructions. */
    Iaddsub_ext_ADD   = 0x0b200000,

    /* Add/subtract shifted register instructions (without a shift).  */
    Iaddsub_shift_ADD  = 0x0b000000,
    Iaddsub_shift_ADDS = 0x2b000000,
    Iaddsub_shift_SUB  = 0x4b000000,
    Iaddsub_shift_SUBS = 0x6b000000,

    /* Add/subtract shifted register instructions (with a shift).  */
    Iaddsub_realshift_ADD_LSL = Iaddsub_shift_ADD,

    /* Add/subtract with carry instructions.  */
    Irrr_sf_ADC        = 0x1a000000,
    Irrr_sf_ADCS       = 0x3a000000,
    Irrr_sf_SBC        = 0x5a000000,
    Irrr_sf_SBCS       = 0x7a000000,

    /* Conditional select instructions.  */
    Icsel_CSEL         = 0x1a800000,
    Icsel_CSINC        = 0x1a800400,
    Icsel_CSINV        = 0x5a800000,
    Icsel_CSNEG        = 0x5a800400,

    /* Data-processing (1 source) instructions.  */
    Irr_sf_CLZ         = 0x5ac01000,
    Irr_sf_CTZ         = 0x5ac01800,
    Irr_sf_CNT         = 0x5ac01c00,
    Irr_sf_RBIT        = 0x5ac00000,
    Irr_sf_REV         = 0x5ac00000, /* + size << 10 */

    /* Data-processing (2 source) instructions.  */
    Irrr_LSLV          = 0x1ac02000,
    Irrr_LSRV          = 0x1ac02400,
    Irrr_ASRV          = 0x1ac02800,
    Irrr_RORV          = 0x1ac02c00,
    Irrr_SMULH         = 0x9b407c00,
    Irrr_UMULH         = 0x9bc07c00,
    Irrr_UDIV          = 0x1ac00800,
    Irrr_SDIV          = 0x1ac00c00,
    Irrr_SMAX          = 0x1ac00600,
    Irrr_UMAX          = 0x1ac00640,
    Irrr_SMIN          = 0x1ac00680,
    Irrr_UMIN          = 0x1ac006c0,

    /* Data-processing (3 source) instructions.  */
    Irrrr_MADD         = 0x1b000000,
    Irrrr_MSUB         = 0x1b008000,

    /* Logical shifted register instructions (without a shift).  */
    Ilogic_shift_AND   = 0x0a000000,
    Ilogic_shift_BIC   = 0x0a200000,
    Ilogic_shift_ORR   = 0x2a000000,
    Ilogic_shift_ORN   = 0x2a200000,
    Ilogic_shift_EOR   = 0x4a000000,
    Ilogic_shift_EON   = 0x4a200000,
    Ilogic_shift_ANDS  = 0x6a000000,

    /* Logical shifted register instructions (with a shift).  */
    Iaddsub_realshift_AND_LSR  = Ilogic_shift_AND | (1 << 22),

    /* AdvSIMD copy */
    Isimd_copy_DUP     = 0x0e000400,
    Isimd_copy_INS     = 0x4e001c00,
    Isimd_copy_UMOV    = 0x0e003c00,

    /* AdvSIMD modified immediate */
    Isimd_imm_MOVI     = 0x0f000400,
    Isimd_imm_MVNI     = 0x2f000400,
    Isimd_imm_BIC      = 0x2f001400,
    Isimd_imm_ORR      = 0x0f001400,

    /* AdvSIMD scalar shift by immediate */
    Iq_shift_SSHR      = 0x5f000400,
    Iq_shift_SSRA      = 0x5f001400,
    Iq_shift_SHL       = 0x5f005400,
    Iq_shift_USHR      = 0x7f000400,
    Iq_shift_USRA      = 0x7f001400,
    Iq_shift_SLI       = 0x7f005400,

    /* AdvSIMD scalar three same */
    Irrr_e_SQADD      = 0x5e200c00,
    Irrr_e_SQSUB      = 0x5e202c00,
    Irrr_e_CMGT       = 0x5e203400,
    Irrr_e_CMGE       = 0x5e203c00,
    Irrr_e_SSHL       = 0x5e204400,
    Irrr_e_ADD        = 0x5e208400,
    Irrr_e_CMTST      = 0x5e208c00,
    Irrr_e_UQADD      = 0x7e200c00,
    Irrr_e_UQSUB      = 0x7e202c00,
    Irrr_e_CMHI       = 0x7e203400,
    Irrr_e_CMHS       = 0x7e203c00,
    Irrr_e_USHL       = 0x7e204400,
    Irrr_e_SUB        = 0x7e208400,
    Irrr_e_CMEQ       = 0x7e208c00,

    /* AdvSIMD scalar two-reg misc */
    Isimd_rr_CMGT0    = 0x5e208800,
    Isimd_rr_CMEQ0    = 0x5e209800,
    Isimd_rr_CMLT0    = 0x5e20a800,
    Isimd_rr_ABS      = 0x5e20b800,
    Isimd_rr_CMGE0    = 0x7e208800,
    Isimd_rr_CMLE0    = 0x7e209800,
    Isimd_rr_NEG      = 0x7e20b800,

    /* AdvSIMD shift by immediate */
    Isimd_shift_imm_SSHR = 0x0f000400,
    Isimd_shift_imm_SSRA = 0x0f001400,
    Isimd_shift_imm_SHL  = 0x0f005400,
    Isimd_shift_imm_SLI  = 0x2f005400,
    Isimd_shift_imm_USHR = 0x2f000400,
    Isimd_shift_imm_USRA = 0x2f001400,

    /* AdvSIMD three same.  */
    Iqrrr_e_ADD       = 0x0e208400,
    Iqrrr_e_AND       = 0x0e201c00,
    Iqrrr_e_BIC       = 0x0e601c00,
    Iqrrr_e_BIF       = 0x2ee01c00,
    Iqrrr_e_BIT       = 0x2ea01c00,
    Iqrrr_e_BSL       = 0x2e601c00,
    Iqrrr_e_EOR       = 0x2e201c00,
    Iqrrr_e_MUL       = 0x0e209c00,
    Iqrrr_e_ORR       = 0x0ea01c00,
    Iqrrr_e_ORN       = 0x0ee01c00,
    Iqrrr_e_SUB       = 0x2e208400,
    Iqrrr_e_CMGT      = 0x0e203400,
    Iqrrr_e_CMGE      = 0x0e203c00,
    Iqrrr_e_CMTST     = 0x0e208c00,
    Iqrrr_e_CMHI      = 0x2e203400,
    Iqrrr_e_CMHS      = 0x2e203c00,
    Iqrrr_e_CMEQ      = 0x2e208c00,
    Iqrrr_e_SMAX      = 0x0e206400,
    Iqrrr_e_SMIN      = 0x0e206c00,
    Iqrrr_e_SSHL      = 0x0e204400,
    Iqrrr_e_SQADD     = 0x0e200c00,
    Iqrrr_e_SQSUB     = 0x0e202c00,
    Iqrrr_e_UMAX      = 0x2e206400,
    Iqrrr_e_UMIN      = 0x2e206c00,
    Iqrrr_e_UQADD     = 0x2e200c00,
    Iqrrr_e_UQSUB     = 0x2e202c00,
    Iqrrr_e_USHL      = 0x2e204400,

    /* AdvSIMD two-reg misc.  */
    Iqrr_e_CMGT0      = 0x0e208800,
    Iqrr_e_CMEQ0      = 0x0e209800,
    Iqrr_e_CMLT0      = 0x0e20a800,
    Iqrr_e_CMGE0      = 0x2e208800,
    Iqrr_e_CMLE0      = 0x2e209800,
    Iqrr_e_NOT        = 0x2e205800,
    Iqrr_e_ABS        = 0x0e20b800,
    Iqrr_e_NEG        = 0x2e20b800,

    /* System instructions.  */
    NOP               = 0xd503201f,
    DMB_ISH           = 0xd50338bf,
    DMB_LD            = 0x00000100,
    DMB_ST            = 0x00000200,

    BTI_C             = 0xd503245f,
    BTI_J             = 0xd503249f,
    BTI_JC            = 0xd50324df,
} AArch64Insn;

static inline uint32_t tcg_in32(TCGContext *s)
{
    uint32_t v = *(uint32_t *)s->code_ptr;
    return v;
}

/* Emit an opcode with "type-checking" of the format.  */
#define tcg_out_insn(S, FMT, OP, ...) \
    glue(tcg_out_insn_,FMT)(S, glue(glue(glue(I,FMT),_),OP), ## __VA_ARGS__)

static void tcg_out_insn_simd_loadrep(TCGContext *s, AArch64Insn insn, bool q,
                              TCGReg rt, TCGReg rn, unsigned size)
{
    tcg_out32(s, insn | (rt & 0x1f) | (rn << 5) | (size << 10) | (q << 30));
}

static void tcg_out_insn_ldlit(TCGContext *s, AArch64Insn insn,
                              int imm19, TCGReg rt)
{
    tcg_out32(s, insn | (imm19 & 0x7ffff) << 5 | rt);
}

static void tcg_out_insn_stxp(TCGContext *s, AArch64Insn insn, TCGReg rs,
                              TCGReg rt, TCGReg rt2, TCGReg rn)
{
    tcg_out32(s, insn | rs << 16 | rt2 << 10 | rn << 5 | rt);
}

static void tcg_out_insn_cbz(TCGContext *s, AArch64Insn insn, TCGType ext,
                              TCGReg rt, int imm19)
{
    tcg_out32(s, insn | ext << 31 | (imm19 & 0x7ffff) << 5 | rt);
}

static void tcg_out_insn_bcond_imm(TCGContext *s, AArch64Insn insn,
                              TCGCond c, int imm19)
{
    tcg_out32(s, insn | tcg_cond_to_aarch64[c] | (imm19 & 0x7ffff) << 5);
}

static void tcg_out_insn_tbz(TCGContext *s, AArch64Insn insn,
                              TCGReg rt, int imm6, int imm14)
{
    insn |= (imm6 & 0x20) << (31 - 5);
    insn |= (imm6 & 0x1f) << 19;
    tcg_out32(s, insn | (imm14 & 0x3fff) << 5 | rt);
}

static void tcg_out_insn_branch(TCGContext *s, AArch64Insn insn, int imm26)
{
    tcg_out32(s, insn | (imm26 & 0x03ffffff));
}

static void tcg_out_insn_bcond_reg(TCGContext *s, AArch64Insn insn, TCGReg rn)
{
    tcg_out32(s, insn | rn << 5);
}

static void tcg_out_insn_ldstpair(TCGContext *s, AArch64Insn insn,
                              TCGReg r1, TCGReg r2, TCGReg rn,
                              tcg_target_long ofs, bool pre, bool w)
{
    insn |= 1u << 31; /* ext */
    insn |= pre << 24;
    insn |= w << 23;

    tcg_debug_assert(ofs >= -0x200 && ofs < 0x200 && (ofs & 7) == 0);
    insn |= (ofs & (0x7f << 3)) << (15 - 3);

    tcg_out32(s, insn | r2 << 10 | rn << 5 | r1);
}

static void tcg_out_insn_addsub_imm(TCGContext *s, AArch64Insn insn,
                                    TCGType ext, TCGReg rd, TCGReg rn,
                                    uint64_t aimm)
{
    if (aimm > 0xfff) {
        tcg_debug_assert((aimm & 0xfff) == 0);
        aimm >>= 12;
        tcg_debug_assert(aimm <= 0xfff);
        aimm |= 1 << 12;  /* apply LSL 12 */
    }
    tcg_out32(s, insn | ext << 31 | aimm << 10 | rn << 5 | rd);
}

static void tcg_out_insn_minmax_imm(TCGContext *s, AArch64Insn insn,
                                    TCGType ext, TCGReg rd, TCGReg rn,
                                    uint8_t imm)
{
    tcg_out32(s, insn | ext << 31 | imm << 10 | rn << 5 | rd);
}

/* This function can be used for both 3.4.2 (Bitfield) and 3.4.4
   (Logical immediate).  Both insn groups have N, IMMR and IMMS fields
   that feed the DecodeBitMasks pseudo function.  */
static void tcg_out_insn_bitfield(TCGContext *s, AArch64Insn insn, TCGType ext,
                                  TCGReg rd, TCGReg rn, int n, int immr,
                                  int imms)
{
    tcg_out32(s, insn | ext << 31 | n << 22 | immr << 16 | imms << 10
              | rn << 5 | rd);
}

#define tcg_out_insn_logic_imm  tcg_out_insn_bitfield

static void tcg_out_insn_extract(TCGContext *s, AArch64Insn insn, TCGType ext,
                              TCGReg rd, TCGReg rn, TCGReg rm, int imms)
{
    tcg_out32(s, insn | ext << 31 | ext << 22 | rm << 16 | imms << 10
              | rn << 5 | rd);
}

/* This function is used for the Move (wide immediate) instruction group.
   Note that SHIFT is a full shift count, not the 2 bit HW field. */
static void tcg_out_insn_movw(TCGContext *s, AArch64Insn insn, TCGType ext,
                              TCGReg rd, uint16_t half, unsigned shift)
{
    tcg_debug_assert((shift & ~0x30) == 0);
    tcg_out32(s, insn | ext << 31 | shift << (21 - 4) | half << 5 | rd);
}

static void tcg_out_insn_pcrel(TCGContext *s, AArch64Insn insn,
                              TCGReg rd, int64_t disp)
{
    tcg_out32(s, insn | (disp & 3) << 29 | (disp & 0x1ffffc) << (5 - 2) | rd);
}

static inline void tcg_out_insn_addsub_ext(TCGContext *s, AArch64Insn insn,
                                     TCGType sf, TCGReg rd, TCGReg rn,
                                     TCGReg rm, int opt, int imm3)
{
    tcg_out32(s, insn | sf << 31 | rm << 16 | opt << 13 |
              imm3 << 10 | rn << 5 | rd);
}

/* This function is for both 3.5.2 (Add/Subtract shifted register), for
   the rare occasion when we actually want to supply a shift amount.  */
static inline void tcg_out_insn_addsub_realshift(TCGContext *s,
                                                 AArch64Insn insn,
                                                 TCGType ext, TCGReg rd,
                                                 TCGReg rn, TCGReg rm,
                                                 int imm6)
{
    tcg_out32(s, insn | ext << 31 | rm << 16 | imm6 << 10 | rn << 5 | rd);
}

/* This function is for 3.5.2 (Add/subtract shifted register),
   and 3.5.10 (Logical shifted register), for the vast majorty of cases
   when we don't want to apply a shift.  Thus it can also be used for
   3.5.3 (Add/subtract with carry) and 3.5.8 (Data processing 2 source).  */
static void tcg_out_insn_addsub_shift(TCGContext *s, AArch64Insn insn,
                                      TCGType ext, TCGReg rd, TCGReg rn,
                                      TCGReg rm)
{
    tcg_out32(s, insn | ext << 31 | rm << 16 | rn << 5 | rd);
}

#define tcg_out_insn_rrr_sf       tcg_out_insn_addsub_shift
#define tcg_out_insn_rrr          tcg_out_insn_addsub_shift
#define tcg_out_insn_logic_shift  tcg_out_insn_addsub_shift

static void tcg_out_insn_csel(TCGContext *s, AArch64Insn insn, TCGType ext,
                              TCGReg rd, TCGReg rn, TCGReg rm, TCGCond c)
{
    tcg_out32(s, insn | ext << 31 | rm << 16 | rn << 5 | rd
              | tcg_cond_to_aarch64[c] << 12);
}

static void tcg_out_insn_rr_sf(TCGContext *s, AArch64Insn insn, TCGType ext,
                              TCGReg rd, TCGReg rn)
{
    tcg_out32(s, insn | ext << 31 | rn << 5 | rd);
}

static void tcg_out_insn_rrrr(TCGContext *s, AArch64Insn insn, TCGType ext,
                              TCGReg rd, TCGReg rn, TCGReg rm, TCGReg ra)
{
    tcg_out32(s, insn | ext << 31 | rm << 16 | ra << 10 | rn << 5 | rd);
}

static void tcg_out_insn_simd_copy(TCGContext *s, AArch64Insn insn, bool q,
                              TCGReg rd, TCGReg rn, int dst_idx, int src_idx)
{
    /* Note that bit 11 set means general register input.  Therefore
       we can handle both register sets with one function.  */
    tcg_out32(s, insn | q << 30 | (dst_idx << 16) | (src_idx << 11)
              | (rd & 0x1f) | (~rn & 0x20) << 6 | (rn & 0x1f) << 5);
}

static void tcg_out_insn_simd_imm(TCGContext *s, AArch64Insn insn, bool q,
                              TCGReg rd, bool op, int cmode, uint8_t imm8)
{
    tcg_out32(s, insn | q << 30 | op << 29 | cmode << 12 | (rd & 0x1f)
              | (imm8 & 0xe0) << (16 - 5) | (imm8 & 0x1f) << 5);
}

static void tcg_out_insn_q_shift(TCGContext *s, AArch64Insn insn,
                              TCGReg rd, TCGReg rn, unsigned immhb)
{
    tcg_out32(s, insn | immhb << 16 | (rn & 0x1f) << 5 | (rd & 0x1f));
}

static void tcg_out_insn_rrr_e(TCGContext *s, AArch64Insn insn,
                              unsigned size, TCGReg rd, TCGReg rn, TCGReg rm)
{
    tcg_out32(s, insn | (size << 22) | (rm & 0x1f) << 16
              | (rn & 0x1f) << 5 | (rd & 0x1f));
}

static void tcg_out_insn_simd_rr(TCGContext *s, AArch64Insn insn,
                              unsigned size, TCGReg rd, TCGReg rn)
{
    tcg_out32(s, insn | (size << 22) | (rn & 0x1f) << 5 | (rd & 0x1f));
}

static void tcg_out_insn_simd_shift_imm(TCGContext *s, AArch64Insn insn, bool q,
                              TCGReg rd, TCGReg rn, unsigned immhb)
{
    tcg_out32(s, insn | q << 30 | immhb << 16
              | (rn & 0x1f) << 5 | (rd & 0x1f));
}

static void tcg_out_insn_qrrr_e(TCGContext *s, AArch64Insn insn, bool q,
                              unsigned size, TCGReg rd, TCGReg rn, TCGReg rm)
{
    tcg_out32(s, insn | q << 30 | (size << 22) | (rm & 0x1f) << 16
              | (rn & 0x1f) << 5 | (rd & 0x1f));
}

static void tcg_out_insn_qrr_e(TCGContext *s, AArch64Insn insn, bool q,
                              unsigned size, TCGReg rd, TCGReg rn)
{
    tcg_out32(s, insn | q << 30 | (size << 22)
              | (rn & 0x1f) << 5 | (rd & 0x1f));
}

static void tcg_out_insn_ldst_reg(TCGContext *s, AArch64Insn insn,
                                  TCGReg rd, TCGReg base, TCGType ext,
                                  TCGReg regoff)
{
    /* Note the AArch64Insn constants above are for C3.3.12.  Adjust.  */
    tcg_out32(s, insn | ldst_imm_to_reg | regoff << 16 | 0x4000 | ext << 13 |
              base << 5 | (rd & 0x1f));
}

static void tcg_out_insn_ldst_imm(TCGContext *s, AArch64Insn insn,
                              TCGReg rd, TCGReg rn, intptr_t offset)
{
    tcg_out32(s, insn | (offset & 0x1ff) << 12 | rn << 5 | (rd & 0x1f));
}

static void tcg_out_insn_ldst_uimm(TCGContext *s, AArch64Insn insn,
                                   TCGReg rd, TCGReg rn, uintptr_t scaled_uimm)
{
    /* Note the AArch64Insn constants above are for C3.3.12.  Adjust.  */
    tcg_out32(s, insn | ldst_imm_to_uimm | scaled_uimm << 10
              | rn << 5 | (rd & 0x1f));
}

static void tcg_out_bti(TCGContext *s, AArch64Insn insn)
{
    /*
     * While BTI insns are nops on hosts without FEAT_BTI,
     * there is no point in emitting them in that case either.
     */
    if (cpuinfo & CPUINFO_BTI) {
        tcg_out32(s, insn);
    }
}

/* Register to register move using ORR (shifted register with no shift). */
static void tcg_out_movr(TCGContext *s, TCGType ext, TCGReg rd, TCGReg rm)
{
    tcg_out_insn(s, logic_shift, ORR, ext, rd, TCG_REG_XZR, rm);
}

/* Register to register move using ADDI (move to/from SP).  */
static void tcg_out_movr_sp(TCGContext *s, TCGType ext, TCGReg rd, TCGReg rn)
{
    tcg_out_insn(s, addsub_imm, ADDI, ext, rd, rn, 0);
}

/* This function is used for the Logical (immediate) instruction group.
   The value of LIMM must satisfy IS_LIMM.  See the comment above about
   only supporting simplified logical immediates.  */
static void tcg_out_logicali(TCGContext *s, AArch64Insn insn, TCGType ext,
                             TCGReg rd, TCGReg rn, uint64_t limm)
{
    unsigned h, l, r, c;

    tcg_debug_assert(is_limm(limm));

    h = clz64(limm);
    l = ctz64(limm);
    if (l == 0) {
        r = 0;                  /* form 0....01....1 */
        c = ctz64(~limm) - 1;
        if (h == 0) {
            r = clz64(~limm);   /* form 1..10..01..1 */
            c += r;
        }
    } else {
        r = 64 - l;             /* form 1....10....0 or 0..01..10..0 */
        c = r - h - 1;
    }
    if (ext == TCG_TYPE_I32) {
        r &= 31;
        c &= 31;
    }

    tcg_out_insn_logic_imm(s, insn, ext, rd, rn, ext, r, c);
}

static void tcg_out_dupi_vec(TCGContext *s, TCGType type, unsigned vece,
                             TCGReg rd, int64_t v64)
{
    bool q = type == TCG_TYPE_V128;
    int cmode, imm8, i;

    /* Test all bytes equal first.  */
    if (vece == MO_8) {
        imm8 = (uint8_t)v64;
        tcg_out_insn(s, simd_imm, MOVI, q, rd, 0, 0xe, imm8);
        return;
    }

    /*
     * Test all bytes 0x00 or 0xff second.  This can match cases that
     * might otherwise take 2 or 3 insns for MO_16 or MO_32 below.
     */
    for (i = imm8 = 0; i < 8; i++) {
        uint8_t byte = v64 >> (i * 8);
        if (byte == 0xff) {
            imm8 |= 1 << i;
        } else if (byte != 0) {
            goto fail_bytes;
        }
    }
    tcg_out_insn(s, simd_imm, MOVI, q, rd, 1, 0xe, imm8);
    return;
 fail_bytes:

    /*
     * Tests for various replications.  For each element width, if we
     * cannot find an expansion there's no point checking a larger
     * width because we already know by replication it cannot match.
     */
    if (vece == MO_16) {
        uint16_t v16 = v64;

        if (is_shimm16(v16, &cmode, &imm8)) {
            tcg_out_insn(s, simd_imm, MOVI, q, rd, 0, cmode, imm8);
            return;
        }
        if (is_shimm16(~v16, &cmode, &imm8)) {
            tcg_out_insn(s, simd_imm, MVNI, q, rd, 0, cmode, imm8);
            return;
        }

        /*
         * Otherwise, all remaining constants can be loaded in two insns:
         * rd = v16 & 0xff, rd |= v16 & 0xff00.
         */
        tcg_out_insn(s, simd_imm, MOVI, q, rd, 0, 0x8, v16 & 0xff);
        tcg_out_insn(s, simd_imm, ORR, q, rd, 0, 0xa, v16 >> 8);
        return;
    } else if (vece == MO_32) {
        uint32_t v32 = v64;
        uint32_t n32 = ~v32;

        if (is_shimm32(v32, &cmode, &imm8) ||
            is_soimm32(v32, &cmode, &imm8) ||
            is_fimm32(v32, &cmode, &imm8)) {
            tcg_out_insn(s, simd_imm, MOVI, q, rd, 0, cmode, imm8);
            return;
        }
        if (is_shimm32(n32, &cmode, &imm8) ||
            is_soimm32(n32, &cmode, &imm8)) {
            tcg_out_insn(s, simd_imm, MVNI, q, rd, 0, cmode, imm8);
            return;
        }

        /*
         * Restrict the set of constants to those we can load with
         * two instructions.  Others we load from the pool.
         */
        i = is_shimm32_pair(v32, &cmode, &imm8);
        if (i) {
            tcg_out_insn(s, simd_imm, MOVI, q, rd, 0, cmode, imm8);
            tcg_out_insn(s, simd_imm, ORR, q, rd, 0, i,
                         extract32(v32, i * 4, 8));
            return;
        }
        i = is_shimm32_pair(n32, &cmode, &imm8);
        if (i) {
            tcg_out_insn(s, simd_imm, MVNI, q, rd, 0, cmode, imm8);
            tcg_out_insn(s, simd_imm, BIC, q, rd, 0, i,
                         extract32(n32, i * 4, 8));
            return;
        }
    } else if (is_fimm64(v64, &cmode, &imm8)) {
        tcg_out_insn(s, simd_imm, MOVI, q, rd, 1, cmode, imm8);
        return;
    }

    /*
     * As a last resort, load from the constant pool.  Sadly there
     * is no LD1R (literal), so store the full 16-byte vector.
     */
    if (type == TCG_TYPE_V128) {
        new_pool_l2(s, R_AARCH64_CONDBR19, s->code_ptr, 0, v64, v64);
        tcg_out_insn(s, ldlit, LDR_v128, 0, rd);
    } else {
        new_pool_label(s, v64, R_AARCH64_CONDBR19, s->code_ptr, 0);
        tcg_out_insn(s, ldlit, LDR_v64, 0, rd);
    }
}

static bool tcg_out_dup_vec(TCGContext *s, TCGType type, unsigned vece,
                            TCGReg rd, TCGReg rs)
{
    int is_q = type - TCG_TYPE_V64;
    tcg_out_insn(s, simd_copy, DUP, is_q, rd, rs, 1 << vece, 0);
    return true;
}

static bool tcg_out_dupm_vec(TCGContext *s, TCGType type, unsigned vece,
                             TCGReg r, TCGReg base, intptr_t offset)
{
    TCGReg temp = TCG_REG_TMP0;

    if (offset < -0xffffff || offset > 0xffffff) {
        tcg_out_movi(s, TCG_TYPE_PTR, temp, offset);
        tcg_out_insn(s, addsub_shift, ADD, 1, temp, temp, base);
        base = temp;
    } else {
        AArch64Insn add_insn = Iaddsub_imm_ADDI;

        if (offset < 0) {
            add_insn = Iaddsub_imm_SUBI;
            offset = -offset;
        }
        if (offset & 0xfff000) {
            tcg_out_insn_addsub_imm(s, add_insn, 1, temp, base,
                                    offset & 0xfff000);
            base = temp;
        }
        if (offset & 0xfff) {
            tcg_out_insn_addsub_imm(s, add_insn, 1, temp, base, offset & 0xfff);
            base = temp;
        }
    }
    tcg_out_insn(s, simd_loadrep, LD1R, type == TCG_TYPE_V128, r, base, vece);
    return true;
}

static void tcg_out_movi(TCGContext *s, TCGType type, TCGReg rd,
                         tcg_target_long value)
{
    tcg_target_long svalue = value;
    tcg_target_long ivalue = ~value;
    tcg_target_long t0, t1, t2;
    int s0, s1;
    AArch64Insn opc;

    switch (type) {
    case TCG_TYPE_I32:
    case TCG_TYPE_I64:
        tcg_debug_assert(rd < 32);
        break;
    default:
        g_assert_not_reached();
    }

    /* For 32-bit values, discard potential garbage in value.  For 64-bit
       values within [2**31, 2**32-1], we can create smaller sequences by
       interpreting this as a negative 32-bit number, while ensuring that
       the high 32 bits are cleared by setting SF=0.  */
    if (type == TCG_TYPE_I32 || (value & ~0xffffffffull) == 0) {
        svalue = (int32_t)value;
        value = (uint32_t)value;
        ivalue = (uint32_t)ivalue;
        type = TCG_TYPE_I32;
    }

    /* Speed things up by handling the common case of small positive
       and negative values specially.  */
    if ((value & ~0xffffull) == 0) {
        tcg_out_insn(s, movw, MOVZ, type, rd, value, 0);
        return;
    } else if ((ivalue & ~0xffffull) == 0) {
        tcg_out_insn(s, movw, MOVN, type, rd, ivalue, 0);
        return;
    }

    /* Check for bitfield immediates.  For the benefit of 32-bit quantities,
       use the sign-extended value.  That lets us match rotated values such
       as 0xff0000ff with the same 64-bit logic matching 0xffffffffff0000ff. */
    if (is_limm(svalue)) {
        tcg_out_logicali(s, Ilogic_imm_ORRI, type, rd, TCG_REG_XZR, svalue);
        return;
    }

    /* Look for host pointer values within 4G of the PC.  This happens
       often when loading pointers to QEMU's own data structures.  */
    if (type == TCG_TYPE_I64) {
        intptr_t src_rx = (intptr_t)tcg_splitwx_to_rx(s->code_ptr);
        tcg_target_long disp = value - src_rx;
        if (disp == sextract64(disp, 0, 21)) {
            tcg_out_insn(s, pcrel, ADR, rd, disp);
            return;
        }
        disp = (value >> 12) - (src_rx >> 12);
        if (disp == sextract64(disp, 0, 21)) {
            tcg_out_insn(s, pcrel, ADRP, rd, disp);
            if (value & 0xfff) {
                tcg_out_insn(s, addsub_imm, ADDI, type, rd, rd, value & 0xfff);
            }
            return;
        }
    }

    /* Would it take fewer insns to begin with MOVN?  */
    if (ctpop64(value) >= 32) {
        t0 = ivalue;
        opc = Imovw_MOVN;
    } else {
        t0 = value;
        opc = Imovw_MOVZ;
    }
    s0 = ctz64(t0) & (63 & -16);
    t1 = t0 & ~(0xffffull << s0);
    s1 = ctz64(t1) & (63 & -16);
    t2 = t1 & ~(0xffffull << s1);
    if (t2 == 0) {
        tcg_out_insn_movw(s, opc, type, rd, t0 >> s0, s0);
        if (t1 != 0) {
            tcg_out_insn(s, movw, MOVK, type, rd, value >> s1, s1);
        }
        return;
    }

    /* For more than 2 insns, dump it into the constant pool.  */
    new_pool_label(s, value, R_AARCH64_CONDBR19, s->code_ptr, 0);
    tcg_out_insn(s, ldlit, LDR, 0, rd);
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

static void tcg_out_ldst(TCGContext *s, AArch64Insn insn, TCGReg rd,
                         TCGReg rn, intptr_t offset, int lgsize)
{
    /* If the offset is naturally aligned and in range, then we can
       use the scaled uimm12 encoding */
    if (offset >= 0 && !(offset & ((1 << lgsize) - 1))) {
        uintptr_t scaled_uimm = offset >> lgsize;
        if (scaled_uimm <= 0xfff) {
            tcg_out_insn_ldst_uimm(s, insn, rd, rn, scaled_uimm);
            return;
        }
    }

    /* Small signed offsets can use the unscaled encoding.  */
    if (offset >= -256 && offset < 256) {
        tcg_out_insn_ldst_imm(s, insn, rd, rn, offset);
        return;
    }

    /* Worst-case scenario, move offset to temp register, use reg offset.  */
    tcg_out_movi(s, TCG_TYPE_I64, TCG_REG_TMP0, offset);
    tcg_out_insn_ldst_reg(s, insn, rd, rn, TCG_TYPE_I64, TCG_REG_TMP0);
}

static bool tcg_out_mov(TCGContext *s, TCGType type, TCGReg ret, TCGReg arg)
{
    if (ret == arg) {
        return true;
    }
    switch (type) {
    case TCG_TYPE_I32:
    case TCG_TYPE_I64:
        if (ret < 32 && arg < 32) {
            tcg_out_movr(s, type, ret, arg);
            break;
        } else if (ret < 32) {
            tcg_out_insn(s, simd_copy, UMOV, type, ret, arg, 0, 0);
            break;
        } else if (arg < 32) {
            tcg_out_insn(s, simd_copy, INS, 0, ret, arg, 4 << type, 0);
            break;
        }
        /* FALLTHRU */

    case TCG_TYPE_V64:
        tcg_debug_assert(ret >= 32 && arg >= 32);
        tcg_out_insn(s, qrrr_e, ORR, 0, 0, ret, arg, arg);
        break;
    case TCG_TYPE_V128:
        tcg_debug_assert(ret >= 32 && arg >= 32);
        tcg_out_insn(s, qrrr_e, ORR, 1, 0, ret, arg, arg);
        break;

    default:
        g_assert_not_reached();
    }
    return true;
}

static void tcg_out_ld(TCGContext *s, TCGType type, TCGReg ret,
                       TCGReg base, intptr_t ofs)
{
    AArch64Insn insn;
    int lgsz;

    switch (type) {
    case TCG_TYPE_I32:
        insn = (ret < 32 ? Ildst_imm_LDRW : Ildst_imm_LDRVS);
        lgsz = 2;
        break;
    case TCG_TYPE_I64:
        insn = (ret < 32 ? Ildst_imm_LDRX : Ildst_imm_LDRVD);
        lgsz = 3;
        break;
    case TCG_TYPE_V64:
        insn = Ildst_imm_LDRVD;
        lgsz = 3;
        break;
    case TCG_TYPE_V128:
        insn = Ildst_imm_LDRVQ;
        lgsz = 4;
        break;
    default:
        g_assert_not_reached();
    }
    tcg_out_ldst(s, insn, ret, base, ofs, lgsz);
}

static void tcg_out_st(TCGContext *s, TCGType type, TCGReg src,
                       TCGReg base, intptr_t ofs)
{
    AArch64Insn insn;
    int lgsz;

    switch (type) {
    case TCG_TYPE_I32:
        insn = (src < 32 ? Ildst_imm_STRW : Ildst_imm_STRVS);
        lgsz = 2;
        break;
    case TCG_TYPE_I64:
        insn = (src < 32 ? Ildst_imm_STRX : Ildst_imm_STRVD);
        lgsz = 3;
        break;
    case TCG_TYPE_V64:
        insn = Ildst_imm_STRVD;
        lgsz = 3;
        break;
    case TCG_TYPE_V128:
        insn = Ildst_imm_STRVQ;
        lgsz = 4;
        break;
    default:
        g_assert_not_reached();
    }
    tcg_out_ldst(s, insn, src, base, ofs, lgsz);
}

static inline bool tcg_out_sti(TCGContext *s, TCGType type, TCGArg val,
                               TCGReg base, intptr_t ofs)
{
    if (type <= TCG_TYPE_I64 && val == 0) {
        tcg_out_st(s, type, TCG_REG_XZR, base, ofs);
        return true;
    }
    return false;
}

static inline void tcg_out_bfm(TCGContext *s, TCGType ext, TCGReg rd,
                               TCGReg rn, unsigned int a, unsigned int b)
{
    tcg_out_insn(s, bitfield, BFM, ext, rd, rn, ext, a, b);
}

static inline void tcg_out_ubfm(TCGContext *s, TCGType ext, TCGReg rd,
                                TCGReg rn, unsigned int a, unsigned int b)
{
    tcg_out_insn(s, bitfield, UBFM, ext, rd, rn, ext, a, b);
}

static inline void tcg_out_sbfm(TCGContext *s, TCGType ext, TCGReg rd,
                                TCGReg rn, unsigned int a, unsigned int b)
{
    tcg_out_insn(s, bitfield, SBFM, ext, rd, rn, ext, a, b);
}

static inline void tcg_out_extr(TCGContext *s, TCGType ext, TCGReg rd,
                                TCGReg rn, TCGReg rm, unsigned int a)
{
    tcg_out_insn(s, extract, EXTR, ext, rd, rn, rm, a);
}

static void tgen_cmp(TCGContext *s, TCGType ext, TCGCond cond,
                     TCGReg a, TCGReg b)
{
    if (is_tst_cond(cond)) {
        tcg_out_insn(s, logic_shift, ANDS, ext, TCG_REG_XZR, a, b);
    } else {
        tcg_out_insn(s, addsub_shift, SUBS, ext, TCG_REG_XZR, a, b);
    }
}

static void tgen_cmpi(TCGContext *s, TCGType ext, TCGCond cond,
                      TCGReg a, tcg_target_long b)
{
    if (is_tst_cond(cond)) {
        tcg_out_logicali(s, Ilogic_imm_ANDSI, ext, TCG_REG_XZR, a, b);
    } else if (b >= 0) {
        tcg_debug_assert(is_aimm(b));
        tcg_out_insn(s, addsub_imm, SUBSI, ext, TCG_REG_XZR, a, b);
    } else {
        tcg_debug_assert(is_aimm(-b));
        tcg_out_insn(s, addsub_imm, ADDSI, ext, TCG_REG_XZR, a, -b);
    }
}

static void tcg_out_cmp(TCGContext *s, TCGType ext, TCGCond cond, TCGReg a,
                        tcg_target_long b, bool const_b)
{
    if (const_b) {
        tgen_cmpi(s, ext, cond, a, b);
    } else {
        tgen_cmp(s, ext, cond, a, b);
    }
}

static void tcg_out_goto(TCGContext *s, const tcg_insn_unit *target)
{
    ptrdiff_t offset = tcg_pcrel_diff(s, target) >> 2;
    tcg_debug_assert(offset == sextract64(offset, 0, 26));
    tcg_out_insn(s, branch, B, offset);
}

static void tcg_out_call_int(TCGContext *s, const tcg_insn_unit *target)
{
    ptrdiff_t offset = tcg_pcrel_diff(s, target) >> 2;
    if (offset == sextract64(offset, 0, 26)) {
        tcg_out_insn(s, branch, BL, offset);
    } else {
        tcg_out_movi(s, TCG_TYPE_I64, TCG_REG_TMP0, (intptr_t)target);
        tcg_out_insn(s, bcond_reg, BLR, TCG_REG_TMP0);
    }
}

static void tcg_out_call(TCGContext *s, const tcg_insn_unit *target,
                         const TCGHelperInfo *info)
{
    tcg_out_call_int(s, target);
}

static void tcg_out_br(TCGContext *s, TCGLabel *l)
{
    if (!l->has_value) {
        tcg_out_reloc(s, s->code_ptr, R_AARCH64_JUMP26, l, 0);
        tcg_out_insn(s, branch, B, 0);
    } else {
        tcg_out_goto(s, l->u.value_ptr);
    }
}

static void tgen_brcond(TCGContext *s, TCGType type, TCGCond c,
                        TCGReg a, TCGReg b, TCGLabel *l)
{
    tgen_cmp(s, type, c, a, b);
    tcg_out_reloc(s, s->code_ptr, R_AARCH64_CONDBR19, l, 0);
    tcg_out_insn(s, bcond_imm, B_C, c, 0);
}

static void tgen_brcondi(TCGContext *s, TCGType ext, TCGCond c,
                         TCGReg a, tcg_target_long b, TCGLabel *l)
{
    int tbit = -1;
    bool need_cmp = true;

    switch (c) {
    case TCG_COND_EQ:
    case TCG_COND_NE:
        /* cmp xN,0; b.ne L -> cbnz xN,L */
        if (b == 0) {
            need_cmp = false;
        }
        break;
    case TCG_COND_LT:
    case TCG_COND_GE:
        /* cmp xN,0; b.mi L -> tbnz xN,63,L */
        if (b == 0) {
            c = (c == TCG_COND_LT ? TCG_COND_TSTNE : TCG_COND_TSTEQ);
            tbit = ext ? 63 : 31;
            need_cmp = false;
        }
        break;
    case TCG_COND_TSTEQ:
    case TCG_COND_TSTNE:
        /* tst xN,0xffffffff; b.ne L -> cbnz wN,L */
        if (b == UINT32_MAX) {
            c = tcg_tst_eqne_cond(c);
            ext = TCG_TYPE_I32;
            need_cmp = false;
            break;
        }
        /* tst xN,1<<B; b.ne L -> tbnz xN,B,L */
        if (is_power_of_2(b)) {
            tbit = ctz64(b);
            need_cmp = false;
        }
        break;
    default:
        break;
    }

    if (need_cmp) {
        tgen_cmpi(s, ext, c, a, b);
        tcg_out_reloc(s, s->code_ptr, R_AARCH64_CONDBR19, l, 0);
        tcg_out_insn(s, bcond_imm, B_C, c, 0);
        return;
    }

    if (tbit >= 0) {
        tcg_out_reloc(s, s->code_ptr, R_AARCH64_TSTBR14, l, 0);
        switch (c) {
        case TCG_COND_TSTEQ:
            tcg_out_insn(s, tbz, TBZ, a, tbit, 0);
            break;
        case TCG_COND_TSTNE:
            tcg_out_insn(s, tbz, TBNZ, a, tbit, 0);
            break;
        default:
            g_assert_not_reached();
        }
    } else {
        tcg_out_reloc(s, s->code_ptr, R_AARCH64_CONDBR19, l, 0);
        switch (c) {
        case TCG_COND_EQ:
            tcg_out_insn(s, cbz, CBZ, ext, a, 0);
            break;
        case TCG_COND_NE:
            tcg_out_insn(s, cbz, CBNZ, ext, a, 0);
            break;
        default:
            g_assert_not_reached();
        }
    }
}

static const TCGOutOpBrcond outop_brcond = {
    .base.static_constraint = C_O0_I2(r, rC),
    .out_rr = tgen_brcond,
    .out_ri = tgen_brcondi,
};

static inline void tcg_out_rev(TCGContext *s, int ext, MemOp s_bits,
                               TCGReg rd, TCGReg rn)
{
    /* REV, REV16, REV32 */
    tcg_out_insn_rr_sf(s, Irr_sf_REV | (s_bits << 10), ext, rd, rn);
}

static inline void tcg_out_sxt(TCGContext *s, TCGType ext, MemOp s_bits,
                               TCGReg rd, TCGReg rn)
{
    /* Using ALIASes SXTB, SXTH, SXTW, of SBFM Xd, Xn, #0, #7|15|31 */
    int bits = (8 << s_bits) - 1;
    tcg_out_sbfm(s, ext, rd, rn, 0, bits);
}

static void tcg_out_ext8s(TCGContext *s, TCGType type, TCGReg rd, TCGReg rn)
{
    tcg_out_sxt(s, type, MO_8, rd, rn);
}

static void tcg_out_ext16s(TCGContext *s, TCGType type, TCGReg rd, TCGReg rn)
{
    tcg_out_sxt(s, type, MO_16, rd, rn);
}

static void tcg_out_ext32s(TCGContext *s, TCGReg rd, TCGReg rn)
{
    tcg_out_sxt(s, TCG_TYPE_I64, MO_32, rd, rn);
}

static void tcg_out_exts_i32_i64(TCGContext *s, TCGReg rd, TCGReg rn)
{
    tcg_out_ext32s(s, rd, rn);
}

static inline void tcg_out_uxt(TCGContext *s, MemOp s_bits,
                               TCGReg rd, TCGReg rn)
{
    /* Using ALIASes UXTB, UXTH of UBFM Wd, Wn, #0, #7|15 */
    int bits = (8 << s_bits) - 1;
    tcg_out_ubfm(s, 0, rd, rn, 0, bits);
}

static void tcg_out_ext8u(TCGContext *s, TCGReg rd, TCGReg rn)
{
    tcg_out_uxt(s, MO_8, rd, rn);
}

static void tcg_out_ext16u(TCGContext *s, TCGReg rd, TCGReg rn)
{
    tcg_out_uxt(s, MO_16, rd, rn);
}

static void tcg_out_ext32u(TCGContext *s, TCGReg rd, TCGReg rn)
{
    tcg_out_movr(s, TCG_TYPE_I32, rd, rn);
}

static void tcg_out_extu_i32_i64(TCGContext *s, TCGReg rd, TCGReg rn)
{
    tcg_out_ext32u(s, rd, rn);
}

static void tcg_out_extrl_i64_i32(TCGContext *s, TCGReg rd, TCGReg rn)
{
    tcg_out_mov(s, TCG_TYPE_I32, rd, rn);
}

static void tcg_out_mb(TCGContext *s, unsigned a0)
{
    static const uint32_t sync[] = {
        [0 ... TCG_MO_ALL]            = DMB_ISH | DMB_LD | DMB_ST,
        [TCG_MO_ST_ST]                = DMB_ISH | DMB_ST,
        [TCG_MO_LD_LD]                = DMB_ISH | DMB_LD,
        [TCG_MO_LD_ST]                = DMB_ISH | DMB_LD,
        [TCG_MO_LD_ST | TCG_MO_LD_LD] = DMB_ISH | DMB_LD,
    };
    tcg_out32(s, sync[a0 & TCG_MO_ALL]);
}

typedef struct {
    TCGReg base;
    TCGReg index;
    TCGType index_ext;
    TCGAtomAlign aa;
} HostAddress;

bool tcg_target_has_memory_bswap(MemOp memop)
{
    return false;
}

static const TCGLdstHelperParam ldst_helper_param = {
    .ntmp = 1, .tmp = { TCG_REG_TMP0 }
};

static bool tcg_out_qemu_ld_slow_path(TCGContext *s, TCGLabelQemuLdst *lb)
{
    MemOp opc = get_memop(lb->oi);

    if (!reloc_pc19(lb->label_ptr[0], tcg_splitwx_to_rx(s->code_ptr))) {
        return false;
    }

    tcg_out_ld_helper_args(s, lb, &ldst_helper_param);
    tcg_out_call_int(s, qemu_ld_helpers[opc & MO_SIZE]);
    tcg_out_ld_helper_ret(s, lb, false, &ldst_helper_param);
    tcg_out_goto(s, lb->raddr);
    return true;
}

static bool tcg_out_qemu_st_slow_path(TCGContext *s, TCGLabelQemuLdst *lb)
{
    MemOp opc = get_memop(lb->oi);

    if (!reloc_pc19(lb->label_ptr[0], tcg_splitwx_to_rx(s->code_ptr))) {
        return false;
    }

    tcg_out_st_helper_args(s, lb, &ldst_helper_param);
    tcg_out_call_int(s, qemu_st_helpers[opc & MO_SIZE]);
    tcg_out_goto(s, lb->raddr);
    return true;
}

/* We expect to use a 7-bit scaled negative offset from ENV.  */
#define MIN_TLB_MASK_TABLE_OFS  -512

/*
 * For system-mode, perform the TLB load and compare.
 * For user-mode, perform any required alignment tests.
 * In both cases, return a TCGLabelQemuLdst structure if the slow path
 * is required and fill in @h with the host address for the fast path.
 */
static TCGLabelQemuLdst *prepare_host_addr(TCGContext *s, HostAddress *h,
                                           TCGReg addr_reg, MemOpIdx oi,
                                           bool is_ld)
{
    TCGType addr_type = s->addr_type;
    TCGLabelQemuLdst *ldst = NULL;
    MemOp opc = get_memop(oi);
    MemOp s_bits = opc & MO_SIZE;
    unsigned a_mask;

    h->aa = atom_and_align_for_opc(s, opc,
                                   have_lse2 ? MO_ATOM_WITHIN16
                                             : MO_ATOM_IFALIGN,
                                   s_bits == MO_128);
    a_mask = (1 << h->aa.align) - 1;

    if (tcg_use_softmmu) {
        unsigned s_mask = (1u << s_bits) - 1;
        unsigned mem_index = get_mmuidx(oi);
        TCGReg addr_adj;
        uint64_t compare_mask;

        ldst = new_ldst_label(s);
        ldst->is_ld = is_ld;
        ldst->oi = oi;
        ldst->addr_reg = addr_reg;

        /* Load CPUTLBDescFast.{mask,table} into {tmp0,tmp1}. */
        QEMU_BUILD_BUG_ON(offsetof(CPUTLBDescFast, mask) != 0);
        QEMU_BUILD_BUG_ON(offsetof(CPUTLBDescFast, table) != 8);
        tcg_out_insn(s, ldstpair, LDP, TCG_REG_TMP0, TCG_REG_TMP1, TCG_AREG0,
                     tlb_mask_table_ofs(s, mem_index), 1, 0);

        /* Extract the TLB index from the address into X0.  */
        tcg_out_insn(s, addsub_realshift, AND_LSR, TCG_TYPE_I64,
                     TCG_REG_TMP0, TCG_REG_TMP0, addr_reg,
                     TARGET_PAGE_BITS - CPU_TLB_ENTRY_BITS);

        /* Add the tlb_table pointer, forming the CPUTLBEntry address. */
        tcg_out_insn(s, addsub_shift, ADD, 1, TCG_REG_TMP1, TCG_REG_TMP1,
                     TCG_REG_TMP0);

        /* Load the tlb comparator into TMP0, and the fast path addend. */
        QEMU_BUILD_BUG_ON(HOST_BIG_ENDIAN);
        tcg_out_ld(s, addr_type, TCG_REG_TMP0, TCG_REG_TMP1,
                   is_ld ? offsetof(CPUTLBEntry, addr_read)
                         : offsetof(CPUTLBEntry, addr_write));
        tcg_out_ld(s, TCG_TYPE_PTR, TCG_REG_TMP1, TCG_REG_TMP1,
                   offsetof(CPUTLBEntry, addend));

        /*
         * For aligned accesses, we check the first byte and include
         * the alignment bits within the address.  For unaligned access,
         * we check that we don't cross pages using the address of the
         * last byte of the access.
         */
        if (a_mask >= s_mask) {
            addr_adj = addr_reg;
        } else {
            addr_adj = TCG_REG_TMP2;
            tcg_out_insn(s, addsub_imm, ADDI, addr_type,
                         addr_adj, addr_reg, s_mask - a_mask);
        }
        compare_mask = (uint64_t)TARGET_PAGE_MASK | a_mask;

        /* Store the page mask part of the address into TMP2.  */
        tcg_out_logicali(s, Ilogic_imm_ANDI, addr_type, TCG_REG_TMP2,
                         addr_adj, compare_mask);

        /* Perform the address comparison. */
        tcg_out_cmp(s, addr_type, TCG_COND_NE, TCG_REG_TMP0, TCG_REG_TMP2, 0);

        /* If not equal, we jump to the slow path. */
        ldst->label_ptr[0] = s->code_ptr;
        tcg_out_insn(s, bcond_imm, B_C, TCG_COND_NE, 0);

        h->base = TCG_REG_TMP1;
        h->index = addr_reg;
        h->index_ext = addr_type;
    } else {
        if (a_mask) {
            ldst = new_ldst_label(s);

            ldst->is_ld = is_ld;
            ldst->oi = oi;
            ldst->addr_reg = addr_reg;

            /* tst addr, #mask */
            tcg_out_logicali(s, Ilogic_imm_ANDSI, 0, TCG_REG_XZR, addr_reg,
                             a_mask);

            /* b.ne slow_path */
            ldst->label_ptr[0] = s->code_ptr;
            tcg_out_insn(s, bcond_imm, B_C, TCG_COND_NE, 0);
        }

        if (guest_base || addr_type == TCG_TYPE_I32) {
            h->base = TCG_REG_GUEST_BASE;
            h->index = addr_reg;
            h->index_ext = addr_type;
        } else {
            h->base = addr_reg;
            h->index = TCG_REG_XZR;
            h->index_ext = TCG_TYPE_I64;
        }
    }

    return ldst;
}

static void tcg_out_qemu_ld_direct(TCGContext *s, MemOp memop, TCGType ext,
                                   TCGReg data_r, HostAddress h)
{
    switch (memop & MO_SSIZE) {
    case MO_UB:
        tcg_out_insn_ldst_reg(s, Ildst_imm_LDRB, data_r, h.base,
                              h.index_ext, h.index);
        break;
    case MO_SB:
        tcg_out_insn_ldst_reg(s, ext ? Ildst_imm_LDRSBX : Ildst_imm_LDRSBW,
                              data_r, h.base, h.index_ext, h.index);
        break;
    case MO_UW:
        tcg_out_insn_ldst_reg(s, Ildst_imm_LDRH, data_r, h.base, h.index_ext,
                              h.index);
        break;
    case MO_SW:
        tcg_out_insn_ldst_reg(s, ext ? Ildst_imm_LDRSHX : Ildst_imm_LDRSHW,
                              data_r, h.base, h.index_ext, h.index);
        break;
    case MO_UL:
        tcg_out_insn_ldst_reg(s, Ildst_imm_LDRW, data_r, h.base, h.index_ext,
                              h.index);
        break;
    case MO_SL:
        tcg_out_insn_ldst_reg(s, Ildst_imm_LDRSWX, data_r, h.base, h.index_ext,
                              h.index);
        break;
    case MO_UQ:
        tcg_out_insn_ldst_reg(s, Ildst_imm_LDRX, data_r, h.base, h.index_ext,
                              h.index);
        break;
    default:
        g_assert_not_reached();
    }
}

static void tcg_out_qemu_st_direct(TCGContext *s, MemOp memop,
                                   TCGReg data_r, HostAddress h)
{
    switch (memop & MO_SIZE) {
    case MO_8:
        tcg_out_insn_ldst_reg(s, Ildst_imm_STRB, data_r, h.base,
                              h.index_ext, h.index);
        break;
    case MO_16:
        tcg_out_insn_ldst_reg(s, Ildst_imm_STRH, data_r, h.base,
                              h.index_ext, h.index);
        break;
    case MO_32:
        tcg_out_insn_ldst_reg(s, Ildst_imm_STRW, data_r, h.base,
                              h.index_ext, h.index);
        break;
    case MO_64:
        tcg_out_insn_ldst_reg(s, Ildst_imm_STRX, data_r, h.base,
                              h.index_ext, h.index);
        break;
    default:
        g_assert_not_reached();
    }
}

static void tgen_qemu_ld(TCGContext *s, TCGType data_type, TCGReg data_reg,
                         TCGReg addr_reg, MemOpIdx oi)
{
    TCGLabelQemuLdst *ldst;
    HostAddress h;

    ldst = prepare_host_addr(s, &h, addr_reg, oi, true);
    tcg_out_qemu_ld_direct(s, get_memop(oi), data_type, data_reg, h);

    if (ldst) {
        ldst->type = data_type;
        ldst->datalo_reg = data_reg;
        ldst->raddr = tcg_splitwx_to_rx(s->code_ptr);
    }
}

static const TCGOutOpQemuLdSt outop_qemu_ld = {
    .base.static_constraint = C_O1_I1(r, r),
    .out = tgen_qemu_ld,
};

static void tgen_qemu_st(TCGContext *s, TCGType data_type, TCGReg data_reg,
                         TCGReg addr_reg, MemOpIdx oi)
{
    TCGLabelQemuLdst *ldst;
    HostAddress h;

    ldst = prepare_host_addr(s, &h, addr_reg, oi, false);
    tcg_out_qemu_st_direct(s, get_memop(oi), data_reg, h);

    if (ldst) {
        ldst->type = data_type;
        ldst->datalo_reg = data_reg;
        ldst->raddr = tcg_splitwx_to_rx(s->code_ptr);
    }
}

static const TCGOutOpQemuLdSt outop_qemu_st = {
    .base.static_constraint = C_O0_I2(rz, r),
    .out = tgen_qemu_st,
};

static void tcg_out_qemu_ldst_i128(TCGContext *s, TCGReg datalo, TCGReg datahi,
                                   TCGReg addr_reg, MemOpIdx oi, bool is_ld)
{
    TCGLabelQemuLdst *ldst;
    HostAddress h;
    TCGReg base;
    bool use_pair;

    ldst = prepare_host_addr(s, &h, addr_reg, oi, is_ld);

    /* Compose the final address, as LDP/STP have no indexing. */
    if (h.index == TCG_REG_XZR) {
        base = h.base;
    } else {
        base = TCG_REG_TMP2;
        if (h.index_ext == TCG_TYPE_I32) {
            /* add base, base, index, uxtw */
            tcg_out_insn(s, addsub_ext, ADD, TCG_TYPE_I64, base,
                         h.base, h.index, MO_32, 0);
        } else {
            /* add base, base, index */
            tcg_out_insn(s, addsub_shift, ADD, 1, base, h.base, h.index);
        }
    }

    use_pair = h.aa.atom < MO_128 || have_lse2;

    if (!use_pair) {
        TCGLabel *label = NULL;
        TCGReg ll, lh, sl, sh;
        /*
         * Check for 16-byte alignment, taking into consideration the
         * alignment that has already been checked.
         */
        int a_mask = 16 - (1 << h.aa.align);

        if (a_mask > 0) {
            label = gen_new_label();
            tgen_brcondi(s, TCG_TYPE_I32, TCG_COND_TSTNE,
                         addr_reg, a_mask, label);
            use_pair = true;
        }

        if (is_ld) {
            /*
             * 16-byte atomicity without LSE2 requires LDXP+STXP loop:
             *    ldxp lo, hi, [base]
             *    stxp t0, lo, hi, [base]
             *    cbnz t0, .-8
             * Require no overlap between data{lo,hi} and base.
             */
            if (datalo == base || datahi == base) {
                tcg_out_mov(s, TCG_TYPE_REG, TCG_REG_TMP2, base);
                base = TCG_REG_TMP2;
            }
            ll = sl = datalo;
            lh = sh = datahi;
        } else {
            /*
             * 16-byte atomicity without LSE2 requires LDXP+STXP loop:
             * 1: ldxp t0, t1, [base]
             *    stxp t0, lo, hi, [base]
             *    cbnz t0, 1b
             */
            tcg_debug_assert(base != TCG_REG_TMP0 && base != TCG_REG_TMP1);
            ll = TCG_REG_TMP0;
            lh = TCG_REG_TMP1;
            sl = datalo;
            sh = datahi;
        }

        tcg_out_insn(s, stxp, LDXP, TCG_REG_XZR, ll, lh, base);
        tcg_out_insn(s, stxp, STXP, TCG_REG_TMP0, sl, sh, base);
        tcg_out_insn(s, cbz, CBNZ, 0, TCG_REG_TMP0, -2);

        if (use_pair) {
            /* "b .+8", branching across the one insn of use_pair. */
            tcg_out_insn(s, branch, B, 2);
            tcg_out_label(s, label);
        }
    }

    if (use_pair) {
        if (is_ld) {
            tcg_out_insn(s, ldstpair, LDP, datalo, datahi, base, 0, 1, 0);
        } else {
            tcg_out_insn(s, ldstpair, STP, datalo, datahi, base, 0, 1, 0);
        }
    }

    if (ldst) {
        ldst->type = TCG_TYPE_I128;
        ldst->datalo_reg = datalo;
        ldst->datahi_reg = datahi;
        ldst->raddr = tcg_splitwx_to_rx(s->code_ptr);
    }
}

static void tgen_qemu_ld2(TCGContext *s, TCGType type, TCGReg datalo,
                          TCGReg datahi, TCGReg addr_reg, MemOpIdx oi)
{
    tcg_out_qemu_ldst_i128(s, datalo, datahi, addr_reg, oi, true);
}

static const TCGOutOpQemuLdSt2 outop_qemu_ld2 = {
    .base.static_constraint = C_O2_I1(r, r, r),
    .out = tgen_qemu_ld2,
};

static void tgen_qemu_st2(TCGContext *s, TCGType type, TCGReg datalo,
                          TCGReg datahi, TCGReg addr_reg, MemOpIdx oi)
{
    tcg_out_qemu_ldst_i128(s, datalo, datahi, addr_reg, oi, false);
}

static const TCGOutOpQemuLdSt2 outop_qemu_st2 = {
    .base.static_constraint = C_O0_I3(rz, rz, r),
    .out = tgen_qemu_st2,
};

static const tcg_insn_unit *tb_ret_addr;

static void tcg_out_exit_tb(TCGContext *s, uintptr_t a0)
{
    const tcg_insn_unit *target;
    ptrdiff_t offset;

    /* Reuse the zeroing that exists for goto_ptr.  */
    if (a0 == 0) {
        target = tcg_code_gen_epilogue;
    } else {
        tcg_out_movi(s, TCG_TYPE_I64, TCG_REG_X0, a0);
        target = tb_ret_addr;
    }

    offset = tcg_pcrel_diff(s, target) >> 2;
    if (offset == sextract64(offset, 0, 26)) {
        tcg_out_insn(s, branch, B, offset);
    } else {
        /*
         * Only x16/x17 generate BTI type Jump (2),
         * other registers generate BTI type Jump|Call (3).
         */
        QEMU_BUILD_BUG_ON(TCG_REG_TMP0 != TCG_REG_X16);
        tcg_out_movi(s, TCG_TYPE_I64, TCG_REG_TMP0, (intptr_t)target);
        tcg_out_insn(s, bcond_reg, BR, TCG_REG_TMP0);
    }
}

static void tcg_out_goto_tb(TCGContext *s, int which)
{
    /*
     * Direct branch, or indirect address load, will be patched
     * by tb_target_set_jmp_target.  Assert indirect load offset
     * in range early, regardless of direct branch distance.
     */
    intptr_t i_off = tcg_pcrel_diff(s, (void *)get_jmp_target_addr(s, which));
    tcg_debug_assert(i_off == sextract64(i_off, 0, 21));

    set_jmp_insn_offset(s, which);
    tcg_out32(s, Ibranch_B);
    tcg_out_insn(s, bcond_reg, BR, TCG_REG_TMP0);
    set_jmp_reset_offset(s, which);
    tcg_out_bti(s, BTI_J);
}

static void tcg_out_goto_ptr(TCGContext *s, TCGReg a0)
{
    tcg_out_insn(s, bcond_reg, BR, a0);
}

void tb_target_set_jmp_target(const TranslationBlock *tb, int n,
                              uintptr_t jmp_rx, uintptr_t jmp_rw)
{
    uintptr_t d_addr = tb->jmp_target_addr[n];
    ptrdiff_t d_offset = d_addr - jmp_rx;
    tcg_insn_unit insn;

    /* Either directly branch, or indirect branch load. */
    if (d_offset == sextract64(d_offset, 0, 28)) {
        insn = deposit32(Ibranch_B, 0, 26, d_offset >> 2);
    } else {
        uintptr_t i_addr = (uintptr_t)&tb->jmp_target_addr[n];
        ptrdiff_t i_offset = i_addr - jmp_rx;

        /* Note that we asserted this in range in tcg_out_goto_tb. */
        insn = deposit32(Ildlit_LDR | TCG_REG_TMP0, 5, 19, i_offset >> 2);
    }
    qatomic_set((uint32_t *)jmp_rw, insn);
    flush_idcache_range(jmp_rx, jmp_rw, 4);
}


static void tgen_add(TCGContext *s, TCGType type,
                     TCGReg a0, TCGReg a1, TCGReg a2)
{
    tcg_out_insn(s, addsub_shift, ADD, type, a0, a1, a2);
}

static void tgen_addi(TCGContext *s, TCGType type,
                      TCGReg a0, TCGReg a1, tcg_target_long a2)
{
    if (a2 >= 0) {
        tcg_out_insn(s, addsub_imm, ADDI, type, a0, a1, a2);
    } else {
        tcg_out_insn(s, addsub_imm, SUBI, type, a0, a1, -a2);
    }
}

static const TCGOutOpBinary outop_add = {
    .base.static_constraint = C_O1_I2(r, r, rA),
    .out_rrr = tgen_add,
    .out_rri = tgen_addi,
};

static void tgen_addco(TCGContext *s, TCGType type,
                       TCGReg a0, TCGReg a1, TCGReg a2)
{
    tcg_out_insn(s, addsub_shift, ADDS, type, a0, a1, a2);
}

static void tgen_addco_imm(TCGContext *s, TCGType type,
                           TCGReg a0, TCGReg a1, tcg_target_long a2)
{
    if (a2 >= 0) {
        tcg_out_insn(s, addsub_imm, ADDSI, type, a0, a1, a2);
    } else {
        tcg_out_insn(s, addsub_imm, SUBSI, type, a0, a1, -a2);
    }
}

static const TCGOutOpBinary outop_addco = {
    .base.static_constraint = C_O1_I2(r, r, rA),
    .out_rrr = tgen_addco,
    .out_rri = tgen_addco_imm,
};

static void tgen_addci_rrr(TCGContext *s, TCGType type,
                           TCGReg a0, TCGReg a1, TCGReg a2)
{
    tcg_out_insn(s, rrr_sf, ADC, type, a0, a1, a2);
}

static void tgen_addci_rri(TCGContext *s, TCGType type,
                           TCGReg a0, TCGReg a1, tcg_target_long a2)
{
    /*
     * Note that the only two constants we support are 0 and -1, and
     * that SBC = rn + ~rm + c, so adc -1 is sbc 0, and vice-versa.
     */
    if (a2) {
        tcg_out_insn(s, rrr_sf, SBC, type, a0, a1, TCG_REG_XZR);
    } else {
        tcg_out_insn(s, rrr_sf, ADC, type, a0, a1, TCG_REG_XZR);
    }
}

static const TCGOutOpAddSubCarry outop_addci = {
    .base.static_constraint = C_O1_I2(r, rz, rMZ),
    .out_rrr = tgen_addci_rrr,
    .out_rri = tgen_addci_rri,
};

static void tgen_addcio(TCGContext *s, TCGType type,
                        TCGReg a0, TCGReg a1, TCGReg a2)
{
    tcg_out_insn(s, rrr_sf, ADCS, type, a0, a1, a2);
}

static void tgen_addcio_imm(TCGContext *s, TCGType type,
                            TCGReg a0, TCGReg a1, tcg_target_long a2)
{
    /* Use SBCS w/0 for ADCS w/-1 -- see above. */
    if (a2) {
        tcg_out_insn(s, rrr_sf, SBCS, type, a0, a1, TCG_REG_XZR);
    } else {
        tcg_out_insn(s, rrr_sf, ADCS, type, a0, a1, TCG_REG_XZR);
    }
}

static const TCGOutOpBinary outop_addcio = {
    .base.static_constraint = C_O1_I2(r, rz, rMZ),
    .out_rrr = tgen_addcio,
    .out_rri = tgen_addcio_imm,
};

static void tcg_out_set_carry(TCGContext *s)
{
    tcg_out_insn(s, addsub_shift, SUBS, TCG_TYPE_I32,
                 TCG_REG_XZR, TCG_REG_XZR, TCG_REG_XZR);
}

static void tgen_and(TCGContext *s, TCGType type,
                     TCGReg a0, TCGReg a1, TCGReg a2)
{
    tcg_out_insn(s, logic_shift, AND, type, a0, a1, a2);
}

static void tgen_andi(TCGContext *s, TCGType type,
                      TCGReg a0, TCGReg a1, tcg_target_long a2)
{
    tcg_out_logicali(s, Ilogic_imm_ANDI, type, a0, a1, a2);
}

static const TCGOutOpBinary outop_and = {
    .base.static_constraint = C_O1_I2(r, r, rL),
    .out_rrr = tgen_and,
    .out_rri = tgen_andi,
};

static void tgen_andc(TCGContext *s, TCGType type,
                      TCGReg a0, TCGReg a1, TCGReg a2)
{
    tcg_out_insn(s, logic_shift, BIC, type, a0, a1, a2);
}

static const TCGOutOpBinary outop_andc = {
    .base.static_constraint = C_O1_I2(r, r, r),
    .out_rrr = tgen_andc,
};

static void tgen_clzctz(TCGContext *s, TCGType type, TCGReg a0, TCGReg a1,
                        TCGReg a2, AArch64Insn insn)
{
    tcg_out_cmp(s, type, TCG_COND_NE, a1, 0, true);
    tcg_out_insn_rr_sf(s, insn, type, TCG_REG_TMP0, a1);
    tcg_out_insn(s, csel, CSEL, type, a0, TCG_REG_TMP0, a2, TCG_COND_NE);
}

static void tgen_clz(TCGContext *s, TCGType type,
                     TCGReg a0, TCGReg a1, TCGReg a2)
{
    tgen_clzctz(s, type, a0, a1, a2, Irr_sf_CLZ);
}

static void tgen_clzctzi(TCGContext *s, TCGType type, TCGReg a0, TCGReg a1,
                         tcg_target_long a2, AArch64Insn insn)
{
    if (a2 == (type == TCG_TYPE_I32 ? 32 : 64)) {
        tcg_out_insn_rr_sf(s, insn, type, a0, a1);
        return;
    }

    tcg_out_cmp(s, type, TCG_COND_NE, a1, 0, true);
    tcg_out_insn_rr_sf(s, insn, type, a0, a1);

    switch (a2) {
    case -1:
        tcg_out_insn(s, csel, CSINV, type, a0, a0, TCG_REG_XZR, TCG_COND_NE);
        break;
    case 0:
        tcg_out_insn(s, csel, CSEL, type, a0, a0, TCG_REG_XZR, TCG_COND_NE);
        break;
    default:
        tcg_out_movi(s, type, TCG_REG_TMP0, a2);
        tcg_out_insn(s, csel, CSEL, type, a0, a0, TCG_REG_TMP0, TCG_COND_NE);
        break;
    }
}

static void tgen_clzi(TCGContext *s, TCGType type,
                      TCGReg a0, TCGReg a1, tcg_target_long a2)
{
    tgen_clzctzi(s, type, a0, a1, a2, Irr_sf_CLZ);
}

static const TCGOutOpBinary outop_clz = {
    .base.static_constraint = C_O1_I2(r, r, rAL),
    .out_rrr = tgen_clz,
    .out_rri = tgen_clzi,
};

static TCGConstraintSetIndex cset_ctpop(TCGType type, unsigned flags)
{
    return cpuinfo & CPUINFO_CSSC ? C_O1_I1(r, r) : C_NotImplemented;
}

static void tgen_ctpop(TCGContext *s, TCGType type, TCGReg a0, TCGReg a1)
{
    tcg_out_insn(s, rr_sf, CNT, type, a0, a1);
}

static const TCGOutOpUnary outop_ctpop = {
    .base.static_constraint = C_Dynamic,
    .base.dynamic_constraint = cset_ctpop,
    .out_rr = tgen_ctpop,
};

static void tgen_ctz(TCGContext *s, TCGType type,
                     TCGReg a0, TCGReg a1, TCGReg a2)
{
    if (cpuinfo & CPUINFO_CSSC) {
        tgen_clzctz(s, type, a0, a1, a2, Irr_sf_CTZ);
    } else {
        tcg_out_insn(s, rr_sf, RBIT, type, TCG_REG_TMP0, a1);
        tgen_clzctz(s, type, a0, TCG_REG_TMP0, a2, Irr_sf_CLZ);
    }
}

static void tgen_ctzi(TCGContext *s, TCGType type,
                      TCGReg a0, TCGReg a1, tcg_target_long a2)
{
    if (cpuinfo & CPUINFO_CSSC) {
        tgen_clzctzi(s, type, a0, a1, a2, Irr_sf_CTZ);
    } else {
        tcg_out_insn(s, rr_sf, RBIT, type, TCG_REG_TMP0, a1);
        tgen_clzctzi(s, type, a0, TCG_REG_TMP0, a2, Irr_sf_CLZ);
    }
}

static const TCGOutOpBinary outop_ctz = {
    .base.static_constraint = C_O1_I2(r, r, rAL),
    .out_rrr = tgen_ctz,
    .out_rri = tgen_ctzi,
};

static void tgen_divs(TCGContext *s, TCGType type,
                      TCGReg a0, TCGReg a1, TCGReg a2)
{
    tcg_out_insn(s, rrr, SDIV, type, a0, a1, a2);
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
    tcg_out_insn(s, rrr, UDIV, type, a0, a1, a2);
}

static const TCGOutOpBinary outop_divu = {
    .base.static_constraint = C_O1_I2(r, r, r),
    .out_rrr = tgen_divu,
};

static const TCGOutOpDivRem outop_divu2 = {
    .base.static_constraint = C_NotImplemented,
};

static void tgen_eqv(TCGContext *s, TCGType type,
                     TCGReg a0, TCGReg a1, TCGReg a2)
{
    tcg_out_insn(s, logic_shift, EON, type, a0, a1, a2);
}

static const TCGOutOpBinary outop_eqv = {
    .base.static_constraint = C_O1_I2(r, r, r),
    .out_rrr = tgen_eqv,
};

static void tgen_extrh_i64_i32(TCGContext *s, TCGType t, TCGReg a0, TCGReg a1)
{
    tcg_out_ubfm(s, TCG_TYPE_I64, a0, a1, 32, 63);
}

static const TCGOutOpUnary outop_extrh_i64_i32 = {
    .base.static_constraint = C_O1_I1(r, r),
    .out_rr = tgen_extrh_i64_i32,
};

static void tgen_mul(TCGContext *s, TCGType type,
                     TCGReg a0, TCGReg a1, TCGReg a2)
{
    tcg_out_insn(s, rrrr, MADD, type, a0, a1, a2, TCG_REG_XZR);
}

static const TCGOutOpBinary outop_mul = {
    .base.static_constraint = C_O1_I2(r, r, r),
    .out_rrr = tgen_mul,
};

static const TCGOutOpMul2 outop_muls2 = {
    .base.static_constraint = C_NotImplemented,
};

static TCGConstraintSetIndex cset_mulh(TCGType type, unsigned flags)
{
    return type == TCG_TYPE_I64 ? C_O1_I2(r, r, r) : C_NotImplemented;
}

static void tgen_mulsh(TCGContext *s, TCGType type,
                       TCGReg a0, TCGReg a1, TCGReg a2)
{
    tcg_out_insn(s, rrr, SMULH, TCG_TYPE_I64, a0, a1, a2);
}

static const TCGOutOpBinary outop_mulsh = {
    .base.static_constraint = C_Dynamic,
    .base.dynamic_constraint = cset_mulh,
    .out_rrr = tgen_mulsh,
};

static const TCGOutOpMul2 outop_mulu2 = {
    .base.static_constraint = C_NotImplemented,
};

static void tgen_muluh(TCGContext *s, TCGType type,
                       TCGReg a0, TCGReg a1, TCGReg a2)
{
    tcg_out_insn(s, rrr, UMULH, TCG_TYPE_I64, a0, a1, a2);
}

static const TCGOutOpBinary outop_muluh = {
    .base.static_constraint = C_Dynamic,
    .base.dynamic_constraint = cset_mulh,
    .out_rrr = tgen_muluh,
};

static const TCGOutOpBinary outop_nand = {
    .base.static_constraint = C_NotImplemented,
};

static const TCGOutOpBinary outop_nor = {
    .base.static_constraint = C_NotImplemented,
};

static void tgen_or(TCGContext *s, TCGType type,
                    TCGReg a0, TCGReg a1, TCGReg a2)
{
    tcg_out_insn(s, logic_shift, ORR, type, a0, a1, a2);
}

static void tgen_ori(TCGContext *s, TCGType type,
                     TCGReg a0, TCGReg a1, tcg_target_long a2)
{
    tcg_out_logicali(s, Ilogic_imm_ORRI, type, a0, a1, a2);
}

static const TCGOutOpBinary outop_or = {
    .base.static_constraint = C_O1_I2(r, r, rL),
    .out_rrr = tgen_or,
    .out_rri = tgen_ori,
};

static void tgen_orc(TCGContext *s, TCGType type,
                     TCGReg a0, TCGReg a1, TCGReg a2)
{
    tcg_out_insn(s, logic_shift, ORN, type, a0, a1, a2);
}

static const TCGOutOpBinary outop_orc = {
    .base.static_constraint = C_O1_I2(r, r, r),
    .out_rrr = tgen_orc,
};

static void tgen_rems(TCGContext *s, TCGType type,
                      TCGReg a0, TCGReg a1, TCGReg a2)
{
    tcg_out_insn(s, rrr, SDIV, type, TCG_REG_TMP0, a1, a2);
    tcg_out_insn(s, rrrr, MSUB, type, a0, TCG_REG_TMP0, a2, a1);
}

static const TCGOutOpBinary outop_rems = {
    .base.static_constraint = C_O1_I2(r, r, r),
    .out_rrr = tgen_rems,
};

static void tgen_remu(TCGContext *s, TCGType type,
                      TCGReg a0, TCGReg a1, TCGReg a2)
{
    tcg_out_insn(s, rrr, UDIV, type, TCG_REG_TMP0, a1, a2);
    tcg_out_insn(s, rrrr, MSUB, type, a0, TCG_REG_TMP0, a2, a1);
}

static const TCGOutOpBinary outop_remu = {
    .base.static_constraint = C_O1_I2(r, r, r),
    .out_rrr = tgen_remu,
};

static const TCGOutOpBinary outop_rotl = {
    .base.static_constraint = C_NotImplemented,
};

static void tgen_rotr(TCGContext *s, TCGType type,
                      TCGReg a0, TCGReg a1, TCGReg a2)
{
    tcg_out_insn(s, rrr, RORV, type, a0, a1, a2);
}

static void tgen_rotri(TCGContext *s, TCGType type,
                       TCGReg a0, TCGReg a1, tcg_target_long a2)
{
    int max = type == TCG_TYPE_I32 ? 31 : 63;
    tcg_out_extr(s, type, a0, a1, a1, a2 & max);
}

static const TCGOutOpBinary outop_rotr = {
    .base.static_constraint = C_O1_I2(r, r, ri),
    .out_rrr = tgen_rotr,
    .out_rri = tgen_rotri,
};

static void tgen_sar(TCGContext *s, TCGType type,
                     TCGReg a0, TCGReg a1, TCGReg a2)
{
    tcg_out_insn(s, rrr, ASRV, type, a0, a1, a2);
}

static void tgen_sari(TCGContext *s, TCGType type,
                      TCGReg a0, TCGReg a1, tcg_target_long a2)
{
    int max = type == TCG_TYPE_I32 ? 31 : 63;
    tcg_out_sbfm(s, type, a0, a1, a2 & max, max);
}

static const TCGOutOpBinary outop_sar = {
    .base.static_constraint = C_O1_I2(r, r, ri),
    .out_rrr = tgen_sar,
    .out_rri = tgen_sari,
};

static void tgen_shl(TCGContext *s, TCGType type,
                     TCGReg a0, TCGReg a1, TCGReg a2)
{
    tcg_out_insn(s, rrr, LSLV, type, a0, a1, a2);
}

static void tgen_shli(TCGContext *s, TCGType type,
                      TCGReg a0, TCGReg a1, tcg_target_long a2)
{
    int max = type == TCG_TYPE_I32 ? 31 : 63;
    tcg_out_ubfm(s, type, a0, a1, -a2 & max, ~a2 & max);
}

static const TCGOutOpBinary outop_shl = {
    .base.static_constraint = C_O1_I2(r, r, ri),
    .out_rrr = tgen_shl,
    .out_rri = tgen_shli,
};

static void tgen_shr(TCGContext *s, TCGType type,
                     TCGReg a0, TCGReg a1, TCGReg a2)
{
    tcg_out_insn(s, rrr, LSRV, type, a0, a1, a2);
}

static void tgen_shri(TCGContext *s, TCGType type,
                      TCGReg a0, TCGReg a1, tcg_target_long a2)
{
    int max = type == TCG_TYPE_I32 ? 31 : 63;
    tcg_out_ubfm(s, type, a0, a1, a2 & max, max);
}

static const TCGOutOpBinary outop_shr = {
    .base.static_constraint = C_O1_I2(r, r, ri),
    .out_rrr = tgen_shr,
    .out_rri = tgen_shri,
};

static void tgen_sub(TCGContext *s, TCGType type,
                     TCGReg a0, TCGReg a1, TCGReg a2)
{
    tcg_out_insn(s, addsub_shift, SUB, type, a0, a1, a2);
}

static const TCGOutOpSubtract outop_sub = {
    .base.static_constraint = C_O1_I2(r, r, r),
    .out_rrr = tgen_sub,
};

static void tgen_subbo_rrr(TCGContext *s, TCGType type,
                           TCGReg a0, TCGReg a1, TCGReg a2)
{
    tcg_out_insn(s, addsub_shift, SUBS, type, a0, a1, a2);
}

static void tgen_subbo_rri(TCGContext *s, TCGType type,
                           TCGReg a0, TCGReg a1, tcg_target_long a2)
{
    if (a2 >= 0) {
        tcg_out_insn(s, addsub_imm, SUBSI, type, a0, a1, a2);
    } else {
        tcg_out_insn(s, addsub_imm, ADDSI, type, a0, a1, -a2);
    }
}

static void tgen_subbo_rir(TCGContext *s, TCGType type,
                           TCGReg a0, tcg_target_long a1, TCGReg a2)
{
    tgen_subbo_rrr(s, type, a0, TCG_REG_XZR, a2);
}

static void tgen_subbo_rii(TCGContext *s, TCGType type,
                           TCGReg a0, tcg_target_long a1, tcg_target_long a2)
{
    if (a2 == 0) {
        tgen_subbo_rrr(s, type, a0, TCG_REG_XZR, TCG_REG_XZR);
        return;
    }

    /*
     * We want to allow a1 to be zero for the benefit of negation via
     * subtraction.  However, that leaves open the possibility of
     * adding 0 +/- const, and the immediate add/sub instructions
     * encode XSP not XZR.  Since we have 0 - non-zero, borrow is
     * always set.
     */
    tcg_out_movi(s, type, a0, -a2);
    tcg_out_set_borrow(s);
}

static const TCGOutOpAddSubCarry outop_subbo = {
    .base.static_constraint = C_O1_I2(r, rZ, rA),
    .out_rrr = tgen_subbo_rrr,
    .out_rri = tgen_subbo_rri,
    .out_rir = tgen_subbo_rir,
    .out_rii = tgen_subbo_rii,
};

static void tgen_subbi_rrr(TCGContext *s, TCGType type,
                           TCGReg a0, TCGReg a1, TCGReg a2)
{
    tcg_out_insn(s, rrr_sf, SBC, type, a0, a1, a2);
}

static void tgen_subbi_rri(TCGContext *s, TCGType type,
                           TCGReg a0, TCGReg a1, tcg_target_long a2)
{
    tgen_addci_rri(s, type, a0, a1, ~a2);
}

static const TCGOutOpAddSubCarry outop_subbi = {
    .base.static_constraint = C_O1_I2(r, rz, rMZ),
    .out_rrr = tgen_subbi_rrr,
    .out_rri = tgen_subbi_rri,
};

static void tgen_subbio_rrr(TCGContext *s, TCGType type,
                            TCGReg a0, TCGReg a1, TCGReg a2)
{
    tcg_out_insn(s, rrr_sf, SBCS, type, a0, a1, a2);
}

static void tgen_subbio_rri(TCGContext *s, TCGType type,
                            TCGReg a0, TCGReg a1, tcg_target_long a2)
{
    tgen_addcio_imm(s, type, a0, a1, ~a2);
}

static const TCGOutOpAddSubCarry outop_subbio = {
    .base.static_constraint = C_O1_I2(r, rz, rMZ),
    .out_rrr = tgen_subbio_rrr,
    .out_rri = tgen_subbio_rri,
};

static void tcg_out_set_borrow(TCGContext *s)
{
    tcg_out_insn(s, addsub_shift, ADDS, TCG_TYPE_I32,
                 TCG_REG_XZR, TCG_REG_XZR, TCG_REG_XZR);
}

static TCGConstraintSetIndex cset_sminmax(TCGType type, unsigned flags)
{
    return cpuinfo & CPUINFO_CSSC ? C_O1_I2(r, r, rS) : C_NotImplemented;
}

static void tgen_smax(TCGContext *s, TCGType type,
                      TCGReg a0, TCGReg a1, TCGReg a2)
{
    tcg_out_insn(s, rrr, SMAX, type, a0, a1, a2);
}

static void tgen_smaxi(TCGContext *s, TCGType type,
                       TCGReg a0, TCGReg a1, tcg_target_long a2)
{
    tcg_out_insn(s, minmax_imm, SMAXI, type, a0, a1, a2);
}

static const TCGOutOpBinary outop_smax = {
    .base.static_constraint = C_Dynamic,
    .base.dynamic_constraint = cset_sminmax,
    .out_rrr = tgen_smax,
    .out_rri = tgen_smaxi,
};

static void tgen_smin(TCGContext *s, TCGType type,
                      TCGReg a0, TCGReg a1, TCGReg a2)
{
    tcg_out_insn(s, rrr, SMIN, type, a0, a1, a2);
}

static void tgen_smini(TCGContext *s, TCGType type,
                       TCGReg a0, TCGReg a1, tcg_target_long a2)
{
    tcg_out_insn(s, minmax_imm, SMINI, type, a0, a1, a2);
}

static const TCGOutOpBinary outop_smin = {
    .base.static_constraint = C_Dynamic,
    .base.dynamic_constraint = cset_sminmax,
    .out_rrr = tgen_smin,
    .out_rri = tgen_smini,
};

static TCGConstraintSetIndex cset_uminmax(TCGType type, unsigned flags)
{
    return cpuinfo & CPUINFO_CSSC ? C_O1_I2(r, r, rU) : C_NotImplemented;
}

static void tgen_umax(TCGContext *s, TCGType type,
                      TCGReg a0, TCGReg a1, TCGReg a2)
{
    tcg_out_insn(s, rrr, UMAX, type, a0, a1, a2);
}

static void tgen_umaxi(TCGContext *s, TCGType type,
                       TCGReg a0, TCGReg a1, tcg_target_long a2)
{
    tcg_out_insn(s, minmax_imm, UMAXI, type, a0, a1, a2);
}

static const TCGOutOpBinary outop_umax = {
    .base.static_constraint = C_Dynamic,
    .base.dynamic_constraint = cset_uminmax,
    .out_rrr = tgen_umax,
    .out_rri = tgen_umaxi,
};

static void tgen_umin(TCGContext *s, TCGType type,
                      TCGReg a0, TCGReg a1, TCGReg a2)
{
    tcg_out_insn(s, rrr, UMIN, type, a0, a1, a2);
}

static void tgen_umini(TCGContext *s, TCGType type,
                       TCGReg a0, TCGReg a1, tcg_target_long a2)
{
    tcg_out_insn(s, minmax_imm, UMINI, type, a0, a1, a2);
}

static const TCGOutOpBinary outop_umin = {
    .base.static_constraint = C_Dynamic,
    .base.dynamic_constraint = cset_uminmax,
    .out_rrr = tgen_umin,
    .out_rri = tgen_umini,
};

static void tgen_xor(TCGContext *s, TCGType type,
                     TCGReg a0, TCGReg a1, TCGReg a2)
{
    tcg_out_insn(s, logic_shift, EOR, type, a0, a1, a2);
}

static void tgen_xori(TCGContext *s, TCGType type,
                      TCGReg a0, TCGReg a1, tcg_target_long a2)
{
    tcg_out_logicali(s, Ilogic_imm_EORI, type, a0, a1, a2);
}

static const TCGOutOpBinary outop_xor = {
    .base.static_constraint = C_O1_I2(r, r, rL),
    .out_rrr = tgen_xor,
    .out_rri = tgen_xori,
};

static void tgen_bswap16(TCGContext *s, TCGType type,
                         TCGReg a0, TCGReg a1, unsigned flags)
{
    tcg_out_rev(s, TCG_TYPE_I32, MO_16, a0, a1);
    if (flags & TCG_BSWAP_OS) {
        /* Output must be sign-extended. */
        tcg_out_ext16s(s, type, a0, a0);
    } else if ((flags & (TCG_BSWAP_IZ | TCG_BSWAP_OZ)) == TCG_BSWAP_OZ) {
        /* Output must be zero-extended, but input isn't. */
        tcg_out_ext16u(s, a0, a0);
    }
}

static const TCGOutOpBswap outop_bswap16 = {
    .base.static_constraint = C_O1_I1(r, r),
    .out_rr = tgen_bswap16,
};

static void tgen_bswap32(TCGContext *s, TCGType type,
                         TCGReg a0, TCGReg a1, unsigned flags)
{
    tcg_out_rev(s, TCG_TYPE_I32, MO_32, a0, a1);
    if (flags & TCG_BSWAP_OS) {
        tcg_out_ext32s(s, a0, a0);
    }
}

static const TCGOutOpBswap outop_bswap32 = {
    .base.static_constraint = C_O1_I1(r, r),
    .out_rr = tgen_bswap32,
};

static void tgen_bswap64(TCGContext *s, TCGType type, TCGReg a0, TCGReg a1)
{
    tcg_out_rev(s, TCG_TYPE_I64, MO_64, a0, a1);
}

static const TCGOutOpUnary outop_bswap64 = {
    .base.static_constraint = C_O1_I1(r, r),
    .out_rr = tgen_bswap64,
};

static const TCGOutOpUnary outop_revbit8 = {
    .base.static_constraint = C_NotImplemented,
};

static void tgen_revbit32(TCGContext *s, TCGType type,
                          TCGReg a0, TCGReg a1, unsigned flags)
{
    tcg_out_insn(s, rr_sf, RBIT, TCG_TYPE_I32, a0, a1);
    if (flags & TCG_BSWAP_OS) {
        tcg_out_ext32s(s, a0, a0);
    }
}

static const TCGOutOpBswap outop_revbit32 = {
    .base.static_constraint = C_O1_I1(r, r),
    .out_rr = tgen_revbit32,
};

static void tgen_revbit64(TCGContext *s, TCGType type, TCGReg a0, TCGReg a1)
{
    tcg_out_insn(s, rr_sf, RBIT, TCG_TYPE_I64, a0, a1);
}

static const TCGOutOpUnary outop_revbit64 = {
    .base.static_constraint = C_O1_I1(r, r),
    .out_rr = tgen_revbit64,
};

static void tgen_neg(TCGContext *s, TCGType type, TCGReg a0, TCGReg a1)
{
    tgen_sub(s, type, a0, TCG_REG_XZR, a1);
}

static const TCGOutOpUnary outop_neg = {
    .base.static_constraint = C_O1_I1(r, r),
    .out_rr = tgen_neg,
};

static void tgen_not(TCGContext *s, TCGType type, TCGReg a0, TCGReg a1)
{
    tgen_orc(s, type, a0, TCG_REG_XZR, a1);
}

static const TCGOutOpUnary outop_not = {
    .base.static_constraint = C_O1_I1(r, r),
    .out_rr = tgen_not,
};

static void tgen_cset(TCGContext *s, TCGCond cond, TCGReg ret)
{
    /* Use CSET alias of CSINC Wd, WZR, WZR, invert(cond).  */
    tcg_out_insn(s, csel, CSINC, TCG_TYPE_I32, ret, TCG_REG_XZR,
                 TCG_REG_XZR, tcg_invert_cond(cond));
}

static void tgen_setcond(TCGContext *s, TCGType type, TCGCond cond,
                         TCGReg a0, TCGReg a1, TCGReg a2)
{
    tgen_cmp(s, type, cond, a1, a2);
    tgen_cset(s, cond, a0);
}

static void tgen_setcondi(TCGContext *s, TCGType type, TCGCond cond,
                          TCGReg a0, TCGReg a1, tcg_target_long a2)
{
    tgen_cmpi(s, type, cond, a1, a2);
    tgen_cset(s, cond, a0);
}

static const TCGOutOpSetcond outop_setcond = {
    .base.static_constraint = C_O1_I2(r, r, rC),
    .out_rrr = tgen_setcond,
    .out_rri = tgen_setcondi,
};

static void tgen_csetm(TCGContext *s, TCGType ext, TCGCond cond, TCGReg ret)
{
    /* Use CSETM alias of CSINV Wd, WZR, WZR, invert(cond).  */
    tcg_out_insn(s, csel, CSINV, ext, ret, TCG_REG_XZR,
                 TCG_REG_XZR, tcg_invert_cond(cond));
}

static void tgen_negsetcond(TCGContext *s, TCGType type, TCGCond cond,
                            TCGReg a0, TCGReg a1, TCGReg a2)
{
    tgen_cmp(s, type, cond, a1, a2);
    tgen_csetm(s, type, cond, a0);
}

static void tgen_negsetcondi(TCGContext *s, TCGType type, TCGCond cond,
                             TCGReg a0, TCGReg a1, tcg_target_long a2)
{
    tgen_cmpi(s, type, cond, a1, a2);
    tgen_csetm(s, type, cond, a0);
}

static const TCGOutOpSetcond outop_negsetcond = {
    .base.static_constraint = C_O1_I2(r, r, rC),
    .out_rrr = tgen_negsetcond,
    .out_rri = tgen_negsetcondi,
};

static void tgen_movcond(TCGContext *s, TCGType type, TCGCond cond,
                         TCGReg ret, TCGReg c1, TCGArg c2, bool const_c2,
                         TCGArg vt, bool const_vt, TCGArg vf, bool const_vf)
{
    tcg_out_cmp(s, type, cond, c1, c2, const_c2);
    tcg_out_insn(s, csel, CSEL, type, ret, vt, vf, cond);
}

static const TCGOutOpMovcond outop_movcond = {
    .base.static_constraint = C_O1_I4(r, r, rC, rz, rz),
    .out = tgen_movcond,
};

static void tgen_deposit(TCGContext *s, TCGType type, TCGReg a0, TCGReg a1,
                         TCGReg a2, unsigned ofs, unsigned len)
{
    unsigned mask = type == TCG_TYPE_I32 ? 31 : 63;

    /*
     * Since we can't support "0Z" as a constraint, we allow a1 in
     * any register.  Fix things up as if a matching constraint.
     */
    if (a0 != a1) {
        if (a0 == a2) {
            tcg_out_mov(s, type, TCG_REG_TMP0, a2);
            a2 = TCG_REG_TMP0;
        }
        tcg_out_mov(s, type, a0, a1);
    }
    tcg_out_bfm(s, type, a0, a2, -ofs & mask, len - 1);
}

static void tgen_depositi(TCGContext *s, TCGType type, TCGReg a0, TCGReg a1,
                          tcg_target_long a2, unsigned ofs, unsigned len)
{
    tgen_andi(s, type, a0, a1, ~MAKE_64BIT_MASK(ofs, len));
}

static void tgen_depositz(TCGContext *s, TCGType type, TCGReg a0, TCGReg a2,
                          unsigned ofs, unsigned len)
{
    int max = type == TCG_TYPE_I32 ? 31 : 63;
    tcg_out_ubfm(s, type, a0, a2, -ofs & max, len - 1);
}

static const TCGOutOpDeposit outop_deposit = {
    .base.static_constraint = C_O1_I2(r, rZ, rZ),
    .out_rrr = tgen_deposit,
    .out_rri = tgen_depositi,
    .out_rzr = tgen_depositz,
};

static void tgen_extract(TCGContext *s, TCGType type, TCGReg a0, TCGReg a1,
                         unsigned ofs, unsigned len)
{
    if (ofs == 0) {
        uint64_t mask = MAKE_64BIT_MASK(0, len);
        tcg_out_logicali(s, Ilogic_imm_ANDI, type, a0, a1, mask);
    } else {
        tcg_out_ubfm(s, type, a0, a1, ofs, ofs + len - 1);
    }
}

static const TCGOutOpExtract outop_extract = {
    .base.static_constraint = C_O1_I1(r, r),
    .out_rr = tgen_extract,
};

static void tgen_sextract(TCGContext *s, TCGType type, TCGReg a0, TCGReg a1,
                          unsigned ofs, unsigned len)
{
    tcg_out_sbfm(s, type, a0, a1, ofs, ofs + len - 1);
}

static const TCGOutOpExtract outop_sextract = {
    .base.static_constraint = C_O1_I1(r, r),
    .out_rr = tgen_sextract,
};

static void tgen_extract2(TCGContext *s, TCGType type, TCGReg a0,
                          TCGReg a1, TCGReg a2, unsigned shr)
{
    tcg_out_extr(s, type, a0, a2, a1, shr);
}

static const TCGOutOpExtract2 outop_extract2 = {
    .base.static_constraint = C_O1_I2(r, rz, rz),
    .out_rrr = tgen_extract2,
};

static void tgen_ld8u(TCGContext *s, TCGType type, TCGReg dest,
                      TCGReg base, ptrdiff_t offset)
{
    tcg_out_ldst(s, Ildst_imm_LDRB, dest, base, offset, 0);
}

static const TCGOutOpLoad outop_ld8u = {
    .base.static_constraint = C_O1_I1(r, r),
    .out = tgen_ld8u,
};

static void tgen_ld8s(TCGContext *s, TCGType type, TCGReg dest,
                      TCGReg base, ptrdiff_t offset)
{
    AArch64Insn insn = type == TCG_TYPE_I32 ? Ildst_imm_LDRSBW
                                            : Ildst_imm_LDRSBX;
    tcg_out_ldst(s, insn, dest, base, offset, 0);
}

static const TCGOutOpLoad outop_ld8s = {
    .base.static_constraint = C_O1_I1(r, r),
    .out = tgen_ld8s,
};

static void tgen_ld16u(TCGContext *s, TCGType type, TCGReg dest,
                       TCGReg base, ptrdiff_t offset)
{
    tcg_out_ldst(s, Ildst_imm_LDRH, dest, base, offset, 1);
}

static const TCGOutOpLoad outop_ld16u = {
    .base.static_constraint = C_O1_I1(r, r),
    .out = tgen_ld16u,
};

static void tgen_ld16s(TCGContext *s, TCGType type, TCGReg dest,
                       TCGReg base, ptrdiff_t offset)
{
    AArch64Insn insn = type == TCG_TYPE_I32 ? Ildst_imm_LDRSHW
                                            : Ildst_imm_LDRSHX;
    tcg_out_ldst(s, insn, dest, base, offset, 1);
}

static const TCGOutOpLoad outop_ld16s = {
    .base.static_constraint = C_O1_I1(r, r),
    .out = tgen_ld16s,
};

static void tgen_ld32u(TCGContext *s, TCGType type, TCGReg dest,
                       TCGReg base, ptrdiff_t offset)
{
    tcg_out_ldst(s, Ildst_imm_LDRW, dest, base, offset, 2);
}

static const TCGOutOpLoad outop_ld32u = {
    .base.static_constraint = C_O1_I1(r, r),
    .out = tgen_ld32u,
};

static void tgen_ld32s(TCGContext *s, TCGType type, TCGReg dest,
                       TCGReg base, ptrdiff_t offset)
{
    tcg_out_ldst(s, Ildst_imm_LDRSWX, dest, base, offset, 2);
}

static const TCGOutOpLoad outop_ld32s = {
    .base.static_constraint = C_O1_I1(r, r),
    .out = tgen_ld32s,
};

static void tgen_st8_r(TCGContext *s, TCGType type, TCGReg data,
                       TCGReg base, ptrdiff_t offset)
{
    tcg_out_ldst(s, Ildst_imm_STRB, data, base, offset, 0);
}

static const TCGOutOpStore outop_st8 = {
    .base.static_constraint = C_O0_I2(rz, r),
    .out_r = tgen_st8_r,
};

static void tgen_st16_r(TCGContext *s, TCGType type, TCGReg data,
                        TCGReg base, ptrdiff_t offset)
{
    tcg_out_ldst(s, Ildst_imm_STRH, data, base, offset, 1);
}

static const TCGOutOpStore outop_st16 = {
    .base.static_constraint = C_O0_I2(rz, r),
    .out_r = tgen_st16_r,
};

static const TCGOutOpStore outop_st = {
    .base.static_constraint = C_O0_I2(rz, r),
    .out_r = tcg_out_st,
};

static void tcg_out_vec_op(TCGContext *s, TCGOpcode opc,
                           unsigned vecl, unsigned vece,
                           const TCGArg args[TCG_MAX_OP_ARGS],
                           const int const_args[TCG_MAX_OP_ARGS])
{
    static const AArch64Insn cmp_vec_insn[16] = {
        [TCG_COND_EQ] = Iqrrr_e_CMEQ,
        [TCG_COND_GT] = Iqrrr_e_CMGT,
        [TCG_COND_GE] = Iqrrr_e_CMGE,
        [TCG_COND_GTU] = Iqrrr_e_CMHI,
        [TCG_COND_GEU] = Iqrrr_e_CMHS,
    };
    static const AArch64Insn cmp_scalar_insn[16] = {
        [TCG_COND_EQ] = Irrr_e_CMEQ,
        [TCG_COND_GT] = Irrr_e_CMGT,
        [TCG_COND_GE] = Irrr_e_CMGE,
        [TCG_COND_GTU] = Irrr_e_CMHI,
        [TCG_COND_GEU] = Irrr_e_CMHS,
    };
    static const AArch64Insn cmp0_vec_insn[16] = {
        [TCG_COND_EQ] = Iqrr_e_CMEQ0,
        [TCG_COND_GT] = Iqrr_e_CMGT0,
        [TCG_COND_GE] = Iqrr_e_CMGE0,
        [TCG_COND_LT] = Iqrr_e_CMLT0,
        [TCG_COND_LE] = Iqrr_e_CMLE0,
    };
    static const AArch64Insn cmp0_scalar_insn[16] = {
        [TCG_COND_EQ] = Isimd_rr_CMEQ0,
        [TCG_COND_GT] = Isimd_rr_CMGT0,
        [TCG_COND_GE] = Isimd_rr_CMGE0,
        [TCG_COND_LT] = Isimd_rr_CMLT0,
        [TCG_COND_LE] = Isimd_rr_CMLE0,
    };

    TCGType type = vecl + TCG_TYPE_V64;
    unsigned is_q = vecl;
    bool is_scalar = !is_q && vece == MO_64;
    TCGArg a0, a1, a2, a3;
    int cmode, imm8;

    a0 = args[0];
    a1 = args[1];
    a2 = args[2];

    switch (opc) {
    case INDEX_op_ld_vec:
        tcg_out_ld(s, type, a0, a1, a2);
        break;
    case INDEX_op_st_vec:
        tcg_out_st(s, type, a0, a1, a2);
        break;
    case INDEX_op_dupm_vec:
        tcg_out_dupm_vec(s, type, vece, a0, a1, a2);
        break;
    case INDEX_op_add_vec:
        if (is_scalar) {
            tcg_out_insn(s, rrr_e, ADD, vece, a0, a1, a2);
        } else {
            tcg_out_insn(s, qrrr_e, ADD, is_q, vece, a0, a1, a2);
        }
        break;
    case INDEX_op_sub_vec:
        if (is_scalar) {
            tcg_out_insn(s, rrr_e, SUB, vece, a0, a1, a2);
        } else {
            tcg_out_insn(s, qrrr_e, SUB, is_q, vece, a0, a1, a2);
        }
        break;
    case INDEX_op_mul_vec:
        tcg_out_insn(s, qrrr_e, MUL, is_q, vece, a0, a1, a2);
        break;
    case INDEX_op_neg_vec:
        if (is_scalar) {
            tcg_out_insn(s, simd_rr, NEG, vece, a0, a1);
        } else {
            tcg_out_insn(s, qrr_e, NEG, is_q, vece, a0, a1);
        }
        break;
    case INDEX_op_abs_vec:
        if (is_scalar) {
            tcg_out_insn(s, simd_rr, ABS, vece, a0, a1);
        } else {
            tcg_out_insn(s, qrr_e, ABS, is_q, vece, a0, a1);
        }
        break;
    case INDEX_op_and_vec:
        if (const_args[2]) {
            is_shimm1632(~a2, &cmode, &imm8);
            if (a0 == a1) {
                tcg_out_insn(s, simd_imm, BIC, is_q, a0, 0, cmode, imm8);
                return;
            }
            tcg_out_insn(s, simd_imm, MVNI, is_q, a0, 0, cmode, imm8);
            a2 = a0;
        }
        tcg_out_insn(s, qrrr_e, AND, is_q, 0, a0, a1, a2);
        break;
    case INDEX_op_or_vec:
        if (const_args[2]) {
            is_shimm1632(a2, &cmode, &imm8);
            if (a0 == a1) {
                tcg_out_insn(s, simd_imm, ORR, is_q, a0, 0, cmode, imm8);
                return;
            }
            tcg_out_insn(s, simd_imm, MOVI, is_q, a0, 0, cmode, imm8);
            a2 = a0;
        }
        tcg_out_insn(s, qrrr_e, ORR, is_q, 0, a0, a1, a2);
        break;
    case INDEX_op_andc_vec:
        if (const_args[2]) {
            is_shimm1632(a2, &cmode, &imm8);
            if (a0 == a1) {
                tcg_out_insn(s, simd_imm, BIC, is_q, a0, 0, cmode, imm8);
                return;
            }
            tcg_out_insn(s, simd_imm, MOVI, is_q, a0, 0, cmode, imm8);
            a2 = a0;
        }
        tcg_out_insn(s, qrrr_e, BIC, is_q, 0, a0, a1, a2);
        break;
    case INDEX_op_orc_vec:
        if (const_args[2]) {
            is_shimm1632(~a2, &cmode, &imm8);
            if (a0 == a1) {
                tcg_out_insn(s, simd_imm, ORR, is_q, a0, 0, cmode, imm8);
                return;
            }
            tcg_out_insn(s, simd_imm, MVNI, is_q, a0, 0, cmode, imm8);
            a2 = a0;
        }
        tcg_out_insn(s, qrrr_e, ORN, is_q, 0, a0, a1, a2);
        break;
    case INDEX_op_xor_vec:
        tcg_out_insn(s, qrrr_e, EOR, is_q, 0, a0, a1, a2);
        break;
    case INDEX_op_ssadd_vec:
        if (is_scalar) {
            tcg_out_insn(s, rrr_e, SQADD, vece, a0, a1, a2);
        } else {
            tcg_out_insn(s, qrrr_e, SQADD, is_q, vece, a0, a1, a2);
        }
        break;
    case INDEX_op_sssub_vec:
        if (is_scalar) {
            tcg_out_insn(s, rrr_e, SQSUB, vece, a0, a1, a2);
        } else {
            tcg_out_insn(s, qrrr_e, SQSUB, is_q, vece, a0, a1, a2);
        }
        break;
    case INDEX_op_usadd_vec:
        if (is_scalar) {
            tcg_out_insn(s, rrr_e, UQADD, vece, a0, a1, a2);
        } else {
            tcg_out_insn(s, qrrr_e, UQADD, is_q, vece, a0, a1, a2);
        }
        break;
    case INDEX_op_ussub_vec:
        if (is_scalar) {
            tcg_out_insn(s, rrr_e, UQSUB, vece, a0, a1, a2);
        } else {
            tcg_out_insn(s, qrrr_e, UQSUB, is_q, vece, a0, a1, a2);
        }
        break;
    case INDEX_op_smax_vec:
        tcg_out_insn(s, qrrr_e, SMAX, is_q, vece, a0, a1, a2);
        break;
    case INDEX_op_smin_vec:
        tcg_out_insn(s, qrrr_e, SMIN, is_q, vece, a0, a1, a2);
        break;
    case INDEX_op_umax_vec:
        tcg_out_insn(s, qrrr_e, UMAX, is_q, vece, a0, a1, a2);
        break;
    case INDEX_op_umin_vec:
        tcg_out_insn(s, qrrr_e, UMIN, is_q, vece, a0, a1, a2);
        break;
    case INDEX_op_not_vec:
        tcg_out_insn(s, qrr_e, NOT, is_q, 0, a0, a1);
        break;
    case INDEX_op_shli_vec:
        if (is_scalar) {
            tcg_out_insn(s, q_shift, SHL, a0, a1, a2 + (8 << vece));
        } else {
            tcg_out_insn(s, simd_shift_imm, SHL, is_q, a0, a1,
                         a2 + (8 << vece));
        }
        break;
    case INDEX_op_shri_vec:
        if (is_scalar) {
            tcg_out_insn(s, q_shift, USHR, a0, a1, (16 << vece) - a2);
        } else {
            tcg_out_insn(s, simd_shift_imm, USHR, is_q, a0, a1,
                         (16 << vece) - a2);
        }
        break;
    case INDEX_op_sari_vec:
        if (is_scalar) {
            tcg_out_insn(s, q_shift, SSHR, a0, a1, (16 << vece) - a2);
        } else {
            tcg_out_insn(s, simd_shift_imm, SSHR, is_q, a0, a1,
                         (16 << vece) - a2);
        }
        break;
    case INDEX_op_aa64_sli_vec:
        if (is_scalar) {
            tcg_out_insn(s, q_shift, SLI, a0, a2, args[3] + (8 << vece));
        } else {
            tcg_out_insn(s, simd_shift_imm, SLI, is_q, a0, a2,
                         args[3] + (8 << vece));
        }
        break;
    case INDEX_op_shlv_vec:
        if (is_scalar) {
            tcg_out_insn(s, rrr_e, USHL, vece, a0, a1, a2);
        } else {
            tcg_out_insn(s, qrrr_e, USHL, is_q, vece, a0, a1, a2);
        }
        break;
    case INDEX_op_aa64_sshl_vec:
        if (is_scalar) {
            tcg_out_insn(s, rrr_e, SSHL, vece, a0, a1, a2);
        } else {
            tcg_out_insn(s, qrrr_e, SSHL, is_q, vece, a0, a1, a2);
        }
        break;
    case INDEX_op_cmp_vec:
        {
            TCGCond cond = args[3];
            AArch64Insn insn;

            switch (cond) {
            case TCG_COND_NE:
                if (const_args[2]) {
                    if (is_scalar) {
                        tcg_out_insn(s, rrr_e, CMTST, vece, a0, a1, a1);
                    } else {
                        tcg_out_insn(s, qrrr_e, CMTST, is_q, vece, a0, a1, a1);
                    }
                } else {
                    if (is_scalar) {
                        tcg_out_insn(s, rrr_e, CMEQ, vece, a0, a1, a2);
                    } else {
                        tcg_out_insn(s, qrrr_e, CMEQ, is_q, vece, a0, a1, a2);
                    }
                    tcg_out_insn(s, qrr_e, NOT, is_q, 0, a0, a0);
                }
                break;

            case TCG_COND_TSTNE:
            case TCG_COND_TSTEQ:
                if (const_args[2]) {
                    /* (x & 0) == 0 */
                    tcg_out_dupi_vec(s, type, MO_8, a0,
                                     -(cond == TCG_COND_TSTEQ));
                    break;
                }
                if (is_scalar) {
                    tcg_out_insn(s, rrr_e, CMTST, vece, a0, a1, a2);
                } else {
                    tcg_out_insn(s, qrrr_e, CMTST, is_q, vece, a0, a1, a2);
                }
                if (cond == TCG_COND_TSTEQ) {
                    tcg_out_insn(s, qrr_e, NOT, is_q, 0, a0, a0);
                }
                break;

            default:
                if (const_args[2]) {
                    if (is_scalar) {
                        insn = cmp0_scalar_insn[cond];
                        if (insn) {
                            tcg_out_insn_simd_rr(s, insn, vece, a0, a1);
                            break;
                        }
                    } else {
                        insn = cmp0_vec_insn[cond];
                        if (insn) {
                            tcg_out_insn_qrr_e(s, insn, is_q, vece, a0, a1);
                            break;
                        }
                    }
                    tcg_out_dupi_vec(s, type, MO_8, TCG_VEC_TMP0, 0);
                    a2 = TCG_VEC_TMP0;
                }
                if (is_scalar) {
                    insn = cmp_scalar_insn[cond];
                    if (insn == 0) {
                        TCGArg t;
                        t = a1, a1 = a2, a2 = t;
                        cond = tcg_swap_cond(cond);
                        insn = cmp_scalar_insn[cond];
                        tcg_debug_assert(insn != 0);
                    }
                    tcg_out_insn_rrr_e(s, insn, vece, a0, a1, a2);
                } else {
                    insn = cmp_vec_insn[cond];
                    if (insn == 0) {
                        TCGArg t;
                        t = a1, a1 = a2, a2 = t;
                        cond = tcg_swap_cond(cond);
                        insn = cmp_vec_insn[cond];
                        tcg_debug_assert(insn != 0);
                    }
                    tcg_out_insn_qrrr_e(s, insn, is_q, vece, a0, a1, a2);
                }
                break;
            }
        }
        break;

    case INDEX_op_bitsel_vec:
        a3 = args[3];
        if (a0 == a3) {
            tcg_out_insn(s, qrrr_e, BIT, is_q, 0, a0, a2, a1);
        } else if (a0 == a2) {
            tcg_out_insn(s, qrrr_e, BIF, is_q, 0, a0, a3, a1);
        } else {
            if (a0 != a1) {
                tcg_out_mov(s, type, a0, a1);
            }
            tcg_out_insn(s, qrrr_e, BSL, is_q, 0, a0, a2, a3);
        }
        break;

    case INDEX_op_mov_vec:  /* Always emitted via tcg_out_mov.  */
    case INDEX_op_dup_vec:  /* Always emitted via tcg_out_dup_vec.  */
    default:
        g_assert_not_reached();
    }
}

int tcg_can_emit_vec_op(TCGOpcode opc, TCGType type, unsigned vece)
{
    switch (opc) {
    case INDEX_op_add_vec:
    case INDEX_op_sub_vec:
    case INDEX_op_and_vec:
    case INDEX_op_or_vec:
    case INDEX_op_xor_vec:
    case INDEX_op_andc_vec:
    case INDEX_op_orc_vec:
    case INDEX_op_neg_vec:
    case INDEX_op_abs_vec:
    case INDEX_op_not_vec:
    case INDEX_op_cmp_vec:
    case INDEX_op_shli_vec:
    case INDEX_op_shri_vec:
    case INDEX_op_sari_vec:
    case INDEX_op_ssadd_vec:
    case INDEX_op_sssub_vec:
    case INDEX_op_usadd_vec:
    case INDEX_op_ussub_vec:
    case INDEX_op_shlv_vec:
    case INDEX_op_bitsel_vec:
        return 1;
    case INDEX_op_rotli_vec:
    case INDEX_op_shrv_vec:
    case INDEX_op_sarv_vec:
    case INDEX_op_rotlv_vec:
    case INDEX_op_rotrv_vec:
        return -1;
    case INDEX_op_mul_vec:
    case INDEX_op_smax_vec:
    case INDEX_op_smin_vec:
    case INDEX_op_umax_vec:
    case INDEX_op_umin_vec:
        return vece < MO_64;

    default:
        return 0;
    }
}

void tcg_expand_vec_op(TCGOpcode opc, TCGType type, unsigned vece,
                       TCGArg a0, ...)
{
    va_list va;
    TCGv_vec v0, v1, v2, t1, t2, c1;
    TCGArg a2;

    va_start(va, a0);
    v0 = temp_tcgv_vec(arg_temp(a0));
    v1 = temp_tcgv_vec(arg_temp(va_arg(va, TCGArg)));
    a2 = va_arg(va, TCGArg);
    va_end(va);

    switch (opc) {
    case INDEX_op_rotli_vec:
        t1 = tcg_temp_new_vec(type);
        tcg_gen_shri_vec(vece, t1, v1, -a2 & ((8 << vece) - 1));
        vec_gen_4(INDEX_op_aa64_sli_vec, type, vece,
                  tcgv_vec_arg(v0), tcgv_vec_arg(t1), tcgv_vec_arg(v1), a2);
        tcg_temp_free_vec(t1);
        break;

    case INDEX_op_shrv_vec:
    case INDEX_op_sarv_vec:
        /* Right shifts are negative left shifts for AArch64.  */
        v2 = temp_tcgv_vec(arg_temp(a2));
        t1 = tcg_temp_new_vec(type);
        tcg_gen_neg_vec(vece, t1, v2);
        opc = (opc == INDEX_op_shrv_vec
               ? INDEX_op_shlv_vec : INDEX_op_aa64_sshl_vec);
        vec_gen_3(opc, type, vece, tcgv_vec_arg(v0),
                  tcgv_vec_arg(v1), tcgv_vec_arg(t1));
        tcg_temp_free_vec(t1);
        break;

    case INDEX_op_rotlv_vec:
        v2 = temp_tcgv_vec(arg_temp(a2));
        t1 = tcg_temp_new_vec(type);
        c1 = tcg_constant_vec(type, vece, 8 << vece);
        tcg_gen_sub_vec(vece, t1, v2, c1);
        /* Right shifts are negative left shifts for AArch64.  */
        vec_gen_3(INDEX_op_shlv_vec, type, vece, tcgv_vec_arg(t1),
                  tcgv_vec_arg(v1), tcgv_vec_arg(t1));
        vec_gen_3(INDEX_op_shlv_vec, type, vece, tcgv_vec_arg(v0),
                  tcgv_vec_arg(v1), tcgv_vec_arg(v2));
        tcg_gen_or_vec(vece, v0, v0, t1);
        tcg_temp_free_vec(t1);
        break;

    case INDEX_op_rotrv_vec:
        v2 = temp_tcgv_vec(arg_temp(a2));
        t1 = tcg_temp_new_vec(type);
        t2 = tcg_temp_new_vec(type);
        c1 = tcg_constant_vec(type, vece, 8 << vece);
        tcg_gen_neg_vec(vece, t1, v2);
        tcg_gen_sub_vec(vece, t2, c1, v2);
        /* Right shifts are negative left shifts for AArch64.  */
        vec_gen_3(INDEX_op_shlv_vec, type, vece, tcgv_vec_arg(t1),
                  tcgv_vec_arg(v1), tcgv_vec_arg(t1));
        vec_gen_3(INDEX_op_shlv_vec, type, vece, tcgv_vec_arg(t2),
                  tcgv_vec_arg(v1), tcgv_vec_arg(t2));
        tcg_gen_or_vec(vece, v0, t1, t2);
        tcg_temp_free_vec(t1);
        tcg_temp_free_vec(t2);
        break;

    default:
        g_assert_not_reached();
    }
}

static TCGConstraintSetIndex
tcg_target_op_def(TCGOpcode op, TCGType type, unsigned flags)
{
    switch (op) {
    case INDEX_op_add_vec:
    case INDEX_op_sub_vec:
    case INDEX_op_mul_vec:
    case INDEX_op_xor_vec:
    case INDEX_op_ssadd_vec:
    case INDEX_op_sssub_vec:
    case INDEX_op_usadd_vec:
    case INDEX_op_ussub_vec:
    case INDEX_op_smax_vec:
    case INDEX_op_smin_vec:
    case INDEX_op_umax_vec:
    case INDEX_op_umin_vec:
    case INDEX_op_shlv_vec:
    case INDEX_op_shrv_vec:
    case INDEX_op_sarv_vec:
    case INDEX_op_aa64_sshl_vec:
        return C_O1_I2(w, w, w);
    case INDEX_op_not_vec:
    case INDEX_op_neg_vec:
    case INDEX_op_abs_vec:
    case INDEX_op_shli_vec:
    case INDEX_op_shri_vec:
    case INDEX_op_sari_vec:
        return C_O1_I1(w, w);
    case INDEX_op_ld_vec:
    case INDEX_op_dupm_vec:
        return C_O1_I1(w, r);
    case INDEX_op_st_vec:
        return C_O0_I2(w, r);
    case INDEX_op_dup_vec:
        return C_O1_I1(w, wr);
    case INDEX_op_or_vec:
    case INDEX_op_andc_vec:
        return C_O1_I2(w, w, wO);
    case INDEX_op_and_vec:
    case INDEX_op_orc_vec:
        return C_O1_I2(w, w, wN);
    case INDEX_op_cmp_vec:
        return C_O1_I2(w, w, wZ);
    case INDEX_op_bitsel_vec:
        return C_O1_I3(w, w, w, w);
    case INDEX_op_aa64_sli_vec:
        return C_O1_I2(w, 0, w);

    default:
        return C_NotImplemented;
    }
}

static void tcg_target_init(TCGContext *s)
{
    tcg_target_available_regs[TCG_TYPE_I32] = 0xffffffffu;
    tcg_target_available_regs[TCG_TYPE_I64] = 0xffffffffu;
    tcg_target_available_regs[TCG_TYPE_V64] = 0xffffffff00000000ull;
    tcg_target_available_regs[TCG_TYPE_V128] = 0xffffffff00000000ull;

    tcg_target_call_clobber_regs = -1ull;
    tcg_regset_reset_reg(tcg_target_call_clobber_regs, TCG_REG_X19);
    tcg_regset_reset_reg(tcg_target_call_clobber_regs, TCG_REG_X20);
    tcg_regset_reset_reg(tcg_target_call_clobber_regs, TCG_REG_X21);
    tcg_regset_reset_reg(tcg_target_call_clobber_regs, TCG_REG_X22);
    tcg_regset_reset_reg(tcg_target_call_clobber_regs, TCG_REG_X23);
    tcg_regset_reset_reg(tcg_target_call_clobber_regs, TCG_REG_X24);
    tcg_regset_reset_reg(tcg_target_call_clobber_regs, TCG_REG_X25);
    tcg_regset_reset_reg(tcg_target_call_clobber_regs, TCG_REG_X26);
    tcg_regset_reset_reg(tcg_target_call_clobber_regs, TCG_REG_X27);
    tcg_regset_reset_reg(tcg_target_call_clobber_regs, TCG_REG_X28);
    tcg_regset_reset_reg(tcg_target_call_clobber_regs, TCG_REG_X29);
    tcg_regset_reset_reg(tcg_target_call_clobber_regs, TCG_REG_V8);
    tcg_regset_reset_reg(tcg_target_call_clobber_regs, TCG_REG_V9);
    tcg_regset_reset_reg(tcg_target_call_clobber_regs, TCG_REG_V10);
    tcg_regset_reset_reg(tcg_target_call_clobber_regs, TCG_REG_V11);
    tcg_regset_reset_reg(tcg_target_call_clobber_regs, TCG_REG_V12);
    tcg_regset_reset_reg(tcg_target_call_clobber_regs, TCG_REG_V13);
    tcg_regset_reset_reg(tcg_target_call_clobber_regs, TCG_REG_V14);
    tcg_regset_reset_reg(tcg_target_call_clobber_regs, TCG_REG_V15);

    s->reserved_regs = 0;
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_SP);
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_FP);
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_X18); /* platform register */
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_TMP0);
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_TMP1);
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_TMP2);
    tcg_regset_set_reg(s->reserved_regs, TCG_VEC_TMP0);
}

/* Saving pairs: (X19, X20) .. (X27, X28), (X29(fp), X30(lr)).  */
#define PUSH_SIZE  ((30 - 19 + 1) * 8)

#define FRAME_SIZE \
    ((PUSH_SIZE \
      + TCG_STATIC_CALL_ARGS_SIZE \
      + CPU_TEMP_BUF_NLONGS * sizeof(long) \
      + TCG_TARGET_STACK_ALIGN - 1) \
     & ~(TCG_TARGET_STACK_ALIGN - 1))

/* We're expecting a 2 byte uleb128 encoded value.  */
QEMU_BUILD_BUG_ON(FRAME_SIZE >= (1 << 14));

/* We're expecting to use a single ADDI insn.  */
QEMU_BUILD_BUG_ON(FRAME_SIZE - PUSH_SIZE > 0xfff);

static void tcg_target_qemu_prologue(TCGContext *s)
{
    TCGReg r;

    tcg_out_bti(s, BTI_C);

    /* Push (FP, LR) and allocate space for all saved registers.  */
    tcg_out_insn(s, ldstpair, STP, TCG_REG_FP, TCG_REG_LR,
                 TCG_REG_SP, -PUSH_SIZE, 1, 1);

    /* Set up frame pointer for canonical unwinding.  */
    tcg_out_movr_sp(s, TCG_TYPE_I64, TCG_REG_FP, TCG_REG_SP);

    /* Store callee-preserved regs x19..x28.  */
    for (r = TCG_REG_X19; r <= TCG_REG_X27; r += 2) {
        int ofs = (r - TCG_REG_X19 + 2) * 8;
        tcg_out_insn(s, ldstpair, STP, r, r + 1, TCG_REG_SP, ofs, 1, 0);
    }

    /* Make stack space for TCG locals.  */
    tcg_out_insn(s, addsub_imm, SUBI, TCG_TYPE_I64, TCG_REG_SP, TCG_REG_SP,
                 FRAME_SIZE - PUSH_SIZE);

    /* Inform TCG about how to find TCG locals with register, offset, size.  */
    tcg_set_frame(s, TCG_REG_SP, TCG_STATIC_CALL_ARGS_SIZE,
                  CPU_TEMP_BUF_NLONGS * sizeof(long));

    if (!tcg_use_softmmu) {
        /*
         * Note that XZR cannot be encoded in the address base register slot,
         * as that actually encodes SP.  Depending on the guest, we may need
         * to zero-extend the guest address via the address index register slot,
         * therefore we need to load even a zero guest base into a register.
         */
        tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_GUEST_BASE, guest_base);
        tcg_regset_set_reg(s->reserved_regs, TCG_REG_GUEST_BASE);
    }

    tcg_out_mov(s, TCG_TYPE_PTR, TCG_AREG0, tcg_target_call_iarg_regs[0]);
    tcg_out_insn(s, bcond_reg, BR, tcg_target_call_iarg_regs[1]);

    /*
     * Return path for goto_ptr. Set return value to 0, a-la exit_tb,
     * and fall through to the rest of the epilogue.
     */
    tcg_code_gen_epilogue = tcg_splitwx_to_rx(s->code_ptr);
    tcg_out_bti(s, BTI_J);
    tcg_out_movi(s, TCG_TYPE_REG, TCG_REG_X0, 0);

    /* TB epilogue */
    tb_ret_addr = tcg_splitwx_to_rx(s->code_ptr);
    tcg_out_bti(s, BTI_J);

    /* Remove TCG locals stack space.  */
    tcg_out_insn(s, addsub_imm, ADDI, TCG_TYPE_I64, TCG_REG_SP, TCG_REG_SP,
                 FRAME_SIZE - PUSH_SIZE);

    /* Restore registers x19..x28.  */
    for (r = TCG_REG_X19; r <= TCG_REG_X27; r += 2) {
        int ofs = (r - TCG_REG_X19 + 2) * 8;
        tcg_out_insn(s, ldstpair, LDP, r, r + 1, TCG_REG_SP, ofs, 1, 0);
    }

    /* Pop (FP, LR), restore SP to previous frame.  */
    tcg_out_insn(s, ldstpair, LDP, TCG_REG_FP, TCG_REG_LR,
                 TCG_REG_SP, PUSH_SIZE, 0, 1);
    tcg_out_insn(s, bcond_reg, RET, TCG_REG_LR);
}

static void tcg_out_tb_start(TCGContext *s)
{
    tcg_out_bti(s, BTI_J);
}

static void tcg_out_nop_fill(tcg_insn_unit *p, int count)
{
    int i;
    for (i = 0; i < count; ++i) {
        p[i] = NOP;
    }
}

typedef struct {
    DebugFrameHeader h;
    uint8_t fde_def_cfa[4];
    uint8_t fde_reg_ofs[24];
} DebugFrame;

#define ELF_HOST_MACHINE EM_AARCH64

static const DebugFrame debug_frame = {
    .h.cie.len = sizeof(DebugFrameCIE)-4, /* length after .len member */
    .h.cie.id = -1,
    .h.cie.version = 1,
    .h.cie.code_align = 1,
    .h.cie.data_align = 0x78,             /* sleb128 -8 */
    .h.cie.return_column = TCG_REG_LR,

    /* Total FDE size does not include the "len" member.  */
    .h.fde.len = sizeof(DebugFrame) - offsetof(DebugFrame, h.fde.cie_offset),

    .fde_def_cfa = {
        12, TCG_REG_SP,                 /* DW_CFA_def_cfa sp, ... */
        (FRAME_SIZE & 0x7f) | 0x80,     /* ... uleb128 FRAME_SIZE */
        (FRAME_SIZE >> 7)
    },
    .fde_reg_ofs = {
        0x80 + 28, 1,                   /* DW_CFA_offset, x28,  -8 */
        0x80 + 27, 2,                   /* DW_CFA_offset, x27, -16 */
        0x80 + 26, 3,                   /* DW_CFA_offset, x26, -24 */
        0x80 + 25, 4,                   /* DW_CFA_offset, x25, -32 */
        0x80 + 24, 5,                   /* DW_CFA_offset, x24, -40 */
        0x80 + 23, 6,                   /* DW_CFA_offset, x23, -48 */
        0x80 + 22, 7,                   /* DW_CFA_offset, x22, -56 */
        0x80 + 21, 8,                   /* DW_CFA_offset, x21, -64 */
        0x80 + 20, 9,                   /* DW_CFA_offset, x20, -72 */
        0x80 + 19, 10,                  /* DW_CFA_offset, x1p, -80 */
        0x80 + 30, 11,                  /* DW_CFA_offset,  lr, -88 */
        0x80 + 29, 12,                  /* DW_CFA_offset,  fp, -96 */
    }
};

void tcg_register_jit(const void *buf, size_t buf_size)
{
    tcg_register_jit_int(buf, buf_size, &debug_frame, sizeof(debug_frame));
}

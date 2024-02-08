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

#include "../tcg-ldst.c.inc"
#include "../tcg-pool.c.inc"
#include "qemu/bitops.h"

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
    I3201_CBZ       = 0x34000000,
    I3201_CBNZ      = 0x35000000,

    /* Conditional branch (immediate).  */
    I3202_B_C       = 0x54000000,

    /* Test and branch (immediate).  */
    I3205_TBZ       = 0x36000000,
    I3205_TBNZ      = 0x37000000,

    /* Unconditional branch (immediate).  */
    I3206_B         = 0x14000000,
    I3206_BL        = 0x94000000,

    /* Unconditional branch (register).  */
    I3207_BR        = 0xd61f0000,
    I3207_BLR       = 0xd63f0000,
    I3207_RET       = 0xd65f0000,

    /* AdvSIMD load/store single structure.  */
    I3303_LD1R      = 0x0d40c000,

    /* Load literal for loading the address at pc-relative offset */
    I3305_LDR       = 0x58000000,
    I3305_LDR_v64   = 0x5c000000,
    I3305_LDR_v128  = 0x9c000000,

    /* Load/store exclusive. */
    I3306_LDXP      = 0xc8600000,
    I3306_STXP      = 0xc8200000,

    /* Load/store register.  Described here as 3.3.12, but the helper
       that emits them can transform to 3.3.10 or 3.3.13.  */
    I3312_STRB      = 0x38000000 | LDST_ST << 22 | MO_8 << 30,
    I3312_STRH      = 0x38000000 | LDST_ST << 22 | MO_16 << 30,
    I3312_STRW      = 0x38000000 | LDST_ST << 22 | MO_32 << 30,
    I3312_STRX      = 0x38000000 | LDST_ST << 22 | MO_64 << 30,

    I3312_LDRB      = 0x38000000 | LDST_LD << 22 | MO_8 << 30,
    I3312_LDRH      = 0x38000000 | LDST_LD << 22 | MO_16 << 30,
    I3312_LDRW      = 0x38000000 | LDST_LD << 22 | MO_32 << 30,
    I3312_LDRX      = 0x38000000 | LDST_LD << 22 | MO_64 << 30,

    I3312_LDRSBW    = 0x38000000 | LDST_LD_S_W << 22 | MO_8 << 30,
    I3312_LDRSHW    = 0x38000000 | LDST_LD_S_W << 22 | MO_16 << 30,

    I3312_LDRSBX    = 0x38000000 | LDST_LD_S_X << 22 | MO_8 << 30,
    I3312_LDRSHX    = 0x38000000 | LDST_LD_S_X << 22 | MO_16 << 30,
    I3312_LDRSWX    = 0x38000000 | LDST_LD_S_X << 22 | MO_32 << 30,

    I3312_LDRVS     = 0x3c000000 | LDST_LD << 22 | MO_32 << 30,
    I3312_STRVS     = 0x3c000000 | LDST_ST << 22 | MO_32 << 30,

    I3312_LDRVD     = 0x3c000000 | LDST_LD << 22 | MO_64 << 30,
    I3312_STRVD     = 0x3c000000 | LDST_ST << 22 | MO_64 << 30,

    I3312_LDRVQ     = 0x3c000000 | 3 << 22 | 0 << 30,
    I3312_STRVQ     = 0x3c000000 | 2 << 22 | 0 << 30,

    I3312_TO_I3310  = 0x00200800,
    I3312_TO_I3313  = 0x01000000,

    /* Load/store register pair instructions.  */
    I3314_LDP       = 0x28400000,
    I3314_STP       = 0x28000000,

    /* Add/subtract immediate instructions.  */
    I3401_ADDI      = 0x11000000,
    I3401_ADDSI     = 0x31000000,
    I3401_SUBI      = 0x51000000,
    I3401_SUBSI     = 0x71000000,

    /* Bitfield instructions.  */
    I3402_BFM       = 0x33000000,
    I3402_SBFM      = 0x13000000,
    I3402_UBFM      = 0x53000000,

    /* Extract instruction.  */
    I3403_EXTR      = 0x13800000,

    /* Logical immediate instructions.  */
    I3404_ANDI      = 0x12000000,
    I3404_ORRI      = 0x32000000,
    I3404_EORI      = 0x52000000,
    I3404_ANDSI     = 0x72000000,

    /* Move wide immediate instructions.  */
    I3405_MOVN      = 0x12800000,
    I3405_MOVZ      = 0x52800000,
    I3405_MOVK      = 0x72800000,

    /* PC relative addressing instructions.  */
    I3406_ADR       = 0x10000000,
    I3406_ADRP      = 0x90000000,

    /* Add/subtract extended register instructions. */
    I3501_ADD       = 0x0b200000,

    /* Add/subtract shifted register instructions (without a shift).  */
    I3502_ADD       = 0x0b000000,
    I3502_ADDS      = 0x2b000000,
    I3502_SUB       = 0x4b000000,
    I3502_SUBS      = 0x6b000000,

    /* Add/subtract shifted register instructions (with a shift).  */
    I3502S_ADD_LSL  = I3502_ADD,

    /* Add/subtract with carry instructions.  */
    I3503_ADC       = 0x1a000000,
    I3503_SBC       = 0x5a000000,

    /* Conditional select instructions.  */
    I3506_CSEL      = 0x1a800000,
    I3506_CSINC     = 0x1a800400,
    I3506_CSINV     = 0x5a800000,
    I3506_CSNEG     = 0x5a800400,

    /* Data-processing (1 source) instructions.  */
    I3507_CLZ       = 0x5ac01000,
    I3507_RBIT      = 0x5ac00000,
    I3507_REV       = 0x5ac00000, /* + size << 10 */

    /* Data-processing (2 source) instructions.  */
    I3508_LSLV      = 0x1ac02000,
    I3508_LSRV      = 0x1ac02400,
    I3508_ASRV      = 0x1ac02800,
    I3508_RORV      = 0x1ac02c00,
    I3508_SMULH     = 0x9b407c00,
    I3508_UMULH     = 0x9bc07c00,
    I3508_UDIV      = 0x1ac00800,
    I3508_SDIV      = 0x1ac00c00,

    /* Data-processing (3 source) instructions.  */
    I3509_MADD      = 0x1b000000,
    I3509_MSUB      = 0x1b008000,

    /* Logical shifted register instructions (without a shift).  */
    I3510_AND       = 0x0a000000,
    I3510_BIC       = 0x0a200000,
    I3510_ORR       = 0x2a000000,
    I3510_ORN       = 0x2a200000,
    I3510_EOR       = 0x4a000000,
    I3510_EON       = 0x4a200000,
    I3510_ANDS      = 0x6a000000,

    /* Logical shifted register instructions (with a shift).  */
    I3502S_AND_LSR  = I3510_AND | (1 << 22),

    /* AdvSIMD copy */
    I3605_DUP      = 0x0e000400,
    I3605_INS      = 0x4e001c00,
    I3605_UMOV     = 0x0e003c00,

    /* AdvSIMD modified immediate */
    I3606_MOVI      = 0x0f000400,
    I3606_MVNI      = 0x2f000400,
    I3606_BIC       = 0x2f001400,
    I3606_ORR       = 0x0f001400,

    /* AdvSIMD scalar shift by immediate */
    I3609_SSHR      = 0x5f000400,
    I3609_SSRA      = 0x5f001400,
    I3609_SHL       = 0x5f005400,
    I3609_USHR      = 0x7f000400,
    I3609_USRA      = 0x7f001400,
    I3609_SLI       = 0x7f005400,

    /* AdvSIMD scalar three same */
    I3611_SQADD     = 0x5e200c00,
    I3611_SQSUB     = 0x5e202c00,
    I3611_CMGT      = 0x5e203400,
    I3611_CMGE      = 0x5e203c00,
    I3611_SSHL      = 0x5e204400,
    I3611_ADD       = 0x5e208400,
    I3611_CMTST     = 0x5e208c00,
    I3611_UQADD     = 0x7e200c00,
    I3611_UQSUB     = 0x7e202c00,
    I3611_CMHI      = 0x7e203400,
    I3611_CMHS      = 0x7e203c00,
    I3611_USHL      = 0x7e204400,
    I3611_SUB       = 0x7e208400,
    I3611_CMEQ      = 0x7e208c00,

    /* AdvSIMD scalar two-reg misc */
    I3612_CMGT0     = 0x5e208800,
    I3612_CMEQ0     = 0x5e209800,
    I3612_CMLT0     = 0x5e20a800,
    I3612_ABS       = 0x5e20b800,
    I3612_CMGE0     = 0x7e208800,
    I3612_CMLE0     = 0x7e209800,
    I3612_NEG       = 0x7e20b800,

    /* AdvSIMD shift by immediate */
    I3614_SSHR      = 0x0f000400,
    I3614_SSRA      = 0x0f001400,
    I3614_SHL       = 0x0f005400,
    I3614_SLI       = 0x2f005400,
    I3614_USHR      = 0x2f000400,
    I3614_USRA      = 0x2f001400,

    /* AdvSIMD three same.  */
    I3616_ADD       = 0x0e208400,
    I3616_AND       = 0x0e201c00,
    I3616_BIC       = 0x0e601c00,
    I3616_BIF       = 0x2ee01c00,
    I3616_BIT       = 0x2ea01c00,
    I3616_BSL       = 0x2e601c00,
    I3616_EOR       = 0x2e201c00,
    I3616_MUL       = 0x0e209c00,
    I3616_ORR       = 0x0ea01c00,
    I3616_ORN       = 0x0ee01c00,
    I3616_SUB       = 0x2e208400,
    I3616_CMGT      = 0x0e203400,
    I3616_CMGE      = 0x0e203c00,
    I3616_CMTST     = 0x0e208c00,
    I3616_CMHI      = 0x2e203400,
    I3616_CMHS      = 0x2e203c00,
    I3616_CMEQ      = 0x2e208c00,
    I3616_SMAX      = 0x0e206400,
    I3616_SMIN      = 0x0e206c00,
    I3616_SSHL      = 0x0e204400,
    I3616_SQADD     = 0x0e200c00,
    I3616_SQSUB     = 0x0e202c00,
    I3616_UMAX      = 0x2e206400,
    I3616_UMIN      = 0x2e206c00,
    I3616_UQADD     = 0x2e200c00,
    I3616_UQSUB     = 0x2e202c00,
    I3616_USHL      = 0x2e204400,

    /* AdvSIMD two-reg misc.  */
    I3617_CMGT0     = 0x0e208800,
    I3617_CMEQ0     = 0x0e209800,
    I3617_CMLT0     = 0x0e20a800,
    I3617_CMGE0     = 0x2e208800,
    I3617_CMLE0     = 0x2e209800,
    I3617_NOT       = 0x2e205800,
    I3617_ABS       = 0x0e20b800,
    I3617_NEG       = 0x2e20b800,

    /* System instructions.  */
    NOP             = 0xd503201f,
    DMB_ISH         = 0xd50338bf,
    DMB_LD          = 0x00000100,
    DMB_ST          = 0x00000200,

    BTI_C           = 0xd503245f,
    BTI_J           = 0xd503249f,
    BTI_JC          = 0xd50324df,
} AArch64Insn;

static inline uint32_t tcg_in32(TCGContext *s)
{
    uint32_t v = *(uint32_t *)s->code_ptr;
    return v;
}

/* Emit an opcode with "type-checking" of the format.  */
#define tcg_out_insn(S, FMT, OP, ...) \
    glue(tcg_out_insn_,FMT)(S, glue(glue(glue(I,FMT),_),OP), ## __VA_ARGS__)

static void tcg_out_insn_3303(TCGContext *s, AArch64Insn insn, bool q,
                              TCGReg rt, TCGReg rn, unsigned size)
{
    tcg_out32(s, insn | (rt & 0x1f) | (rn << 5) | (size << 10) | (q << 30));
}

static void tcg_out_insn_3305(TCGContext *s, AArch64Insn insn,
                              int imm19, TCGReg rt)
{
    tcg_out32(s, insn | (imm19 & 0x7ffff) << 5 | rt);
}

static void tcg_out_insn_3306(TCGContext *s, AArch64Insn insn, TCGReg rs,
                              TCGReg rt, TCGReg rt2, TCGReg rn)
{
    tcg_out32(s, insn | rs << 16 | rt2 << 10 | rn << 5 | rt);
}

static void tcg_out_insn_3201(TCGContext *s, AArch64Insn insn, TCGType ext,
                              TCGReg rt, int imm19)
{
    tcg_out32(s, insn | ext << 31 | (imm19 & 0x7ffff) << 5 | rt);
}

static void tcg_out_insn_3202(TCGContext *s, AArch64Insn insn,
                              TCGCond c, int imm19)
{
    tcg_out32(s, insn | tcg_cond_to_aarch64[c] | (imm19 & 0x7ffff) << 5);
}

static void tcg_out_insn_3205(TCGContext *s, AArch64Insn insn,
                              TCGReg rt, int imm6, int imm14)
{
    insn |= (imm6 & 0x20) << (31 - 5);
    insn |= (imm6 & 0x1f) << 19;
    tcg_out32(s, insn | (imm14 & 0x3fff) << 5 | rt);
}

static void tcg_out_insn_3206(TCGContext *s, AArch64Insn insn, int imm26)
{
    tcg_out32(s, insn | (imm26 & 0x03ffffff));
}

static void tcg_out_insn_3207(TCGContext *s, AArch64Insn insn, TCGReg rn)
{
    tcg_out32(s, insn | rn << 5);
}

static void tcg_out_insn_3314(TCGContext *s, AArch64Insn insn,
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

static void tcg_out_insn_3401(TCGContext *s, AArch64Insn insn, TCGType ext,
                              TCGReg rd, TCGReg rn, uint64_t aimm)
{
    if (aimm > 0xfff) {
        tcg_debug_assert((aimm & 0xfff) == 0);
        aimm >>= 12;
        tcg_debug_assert(aimm <= 0xfff);
        aimm |= 1 << 12;  /* apply LSL 12 */
    }
    tcg_out32(s, insn | ext << 31 | aimm << 10 | rn << 5 | rd);
}

/* This function can be used for both 3.4.2 (Bitfield) and 3.4.4
   (Logical immediate).  Both insn groups have N, IMMR and IMMS fields
   that feed the DecodeBitMasks pseudo function.  */
static void tcg_out_insn_3402(TCGContext *s, AArch64Insn insn, TCGType ext,
                              TCGReg rd, TCGReg rn, int n, int immr, int imms)
{
    tcg_out32(s, insn | ext << 31 | n << 22 | immr << 16 | imms << 10
              | rn << 5 | rd);
}

#define tcg_out_insn_3404  tcg_out_insn_3402

static void tcg_out_insn_3403(TCGContext *s, AArch64Insn insn, TCGType ext,
                              TCGReg rd, TCGReg rn, TCGReg rm, int imms)
{
    tcg_out32(s, insn | ext << 31 | ext << 22 | rm << 16 | imms << 10
              | rn << 5 | rd);
}

/* This function is used for the Move (wide immediate) instruction group.
   Note that SHIFT is a full shift count, not the 2 bit HW field. */
static void tcg_out_insn_3405(TCGContext *s, AArch64Insn insn, TCGType ext,
                              TCGReg rd, uint16_t half, unsigned shift)
{
    tcg_debug_assert((shift & ~0x30) == 0);
    tcg_out32(s, insn | ext << 31 | shift << (21 - 4) | half << 5 | rd);
}

static void tcg_out_insn_3406(TCGContext *s, AArch64Insn insn,
                              TCGReg rd, int64_t disp)
{
    tcg_out32(s, insn | (disp & 3) << 29 | (disp & 0x1ffffc) << (5 - 2) | rd);
}

static inline void tcg_out_insn_3501(TCGContext *s, AArch64Insn insn,
                                     TCGType sf, TCGReg rd, TCGReg rn,
                                     TCGReg rm, int opt, int imm3)
{
    tcg_out32(s, insn | sf << 31 | rm << 16 | opt << 13 |
              imm3 << 10 | rn << 5 | rd);
}

/* This function is for both 3.5.2 (Add/Subtract shifted register), for
   the rare occasion when we actually want to supply a shift amount.  */
static inline void tcg_out_insn_3502S(TCGContext *s, AArch64Insn insn,
                                      TCGType ext, TCGReg rd, TCGReg rn,
                                      TCGReg rm, int imm6)
{
    tcg_out32(s, insn | ext << 31 | rm << 16 | imm6 << 10 | rn << 5 | rd);
}

/* This function is for 3.5.2 (Add/subtract shifted register),
   and 3.5.10 (Logical shifted register), for the vast majorty of cases
   when we don't want to apply a shift.  Thus it can also be used for
   3.5.3 (Add/subtract with carry) and 3.5.8 (Data processing 2 source).  */
static void tcg_out_insn_3502(TCGContext *s, AArch64Insn insn, TCGType ext,
                              TCGReg rd, TCGReg rn, TCGReg rm)
{
    tcg_out32(s, insn | ext << 31 | rm << 16 | rn << 5 | rd);
}

#define tcg_out_insn_3503  tcg_out_insn_3502
#define tcg_out_insn_3508  tcg_out_insn_3502
#define tcg_out_insn_3510  tcg_out_insn_3502

static void tcg_out_insn_3506(TCGContext *s, AArch64Insn insn, TCGType ext,
                              TCGReg rd, TCGReg rn, TCGReg rm, TCGCond c)
{
    tcg_out32(s, insn | ext << 31 | rm << 16 | rn << 5 | rd
              | tcg_cond_to_aarch64[c] << 12);
}

static void tcg_out_insn_3507(TCGContext *s, AArch64Insn insn, TCGType ext,
                              TCGReg rd, TCGReg rn)
{
    tcg_out32(s, insn | ext << 31 | rn << 5 | rd);
}

static void tcg_out_insn_3509(TCGContext *s, AArch64Insn insn, TCGType ext,
                              TCGReg rd, TCGReg rn, TCGReg rm, TCGReg ra)
{
    tcg_out32(s, insn | ext << 31 | rm << 16 | ra << 10 | rn << 5 | rd);
}

static void tcg_out_insn_3605(TCGContext *s, AArch64Insn insn, bool q,
                              TCGReg rd, TCGReg rn, int dst_idx, int src_idx)
{
    /* Note that bit 11 set means general register input.  Therefore
       we can handle both register sets with one function.  */
    tcg_out32(s, insn | q << 30 | (dst_idx << 16) | (src_idx << 11)
              | (rd & 0x1f) | (~rn & 0x20) << 6 | (rn & 0x1f) << 5);
}

static void tcg_out_insn_3606(TCGContext *s, AArch64Insn insn, bool q,
                              TCGReg rd, bool op, int cmode, uint8_t imm8)
{
    tcg_out32(s, insn | q << 30 | op << 29 | cmode << 12 | (rd & 0x1f)
              | (imm8 & 0xe0) << (16 - 5) | (imm8 & 0x1f) << 5);
}

static void tcg_out_insn_3609(TCGContext *s, AArch64Insn insn,
                              TCGReg rd, TCGReg rn, unsigned immhb)
{
    tcg_out32(s, insn | immhb << 16 | (rn & 0x1f) << 5 | (rd & 0x1f));
}

static void tcg_out_insn_3611(TCGContext *s, AArch64Insn insn,
                              unsigned size, TCGReg rd, TCGReg rn, TCGReg rm)
{
    tcg_out32(s, insn | (size << 22) | (rm & 0x1f) << 16
              | (rn & 0x1f) << 5 | (rd & 0x1f));
}

static void tcg_out_insn_3612(TCGContext *s, AArch64Insn insn,
                              unsigned size, TCGReg rd, TCGReg rn)
{
    tcg_out32(s, insn | (size << 22) | (rn & 0x1f) << 5 | (rd & 0x1f));
}

static void tcg_out_insn_3614(TCGContext *s, AArch64Insn insn, bool q,
                              TCGReg rd, TCGReg rn, unsigned immhb)
{
    tcg_out32(s, insn | q << 30 | immhb << 16
              | (rn & 0x1f) << 5 | (rd & 0x1f));
}

static void tcg_out_insn_3616(TCGContext *s, AArch64Insn insn, bool q,
                              unsigned size, TCGReg rd, TCGReg rn, TCGReg rm)
{
    tcg_out32(s, insn | q << 30 | (size << 22) | (rm & 0x1f) << 16
              | (rn & 0x1f) << 5 | (rd & 0x1f));
}

static void tcg_out_insn_3617(TCGContext *s, AArch64Insn insn, bool q,
                              unsigned size, TCGReg rd, TCGReg rn)
{
    tcg_out32(s, insn | q << 30 | (size << 22)
              | (rn & 0x1f) << 5 | (rd & 0x1f));
}

static void tcg_out_insn_3310(TCGContext *s, AArch64Insn insn,
                              TCGReg rd, TCGReg base, TCGType ext,
                              TCGReg regoff)
{
    /* Note the AArch64Insn constants above are for C3.3.12.  Adjust.  */
    tcg_out32(s, insn | I3312_TO_I3310 | regoff << 16 |
              0x4000 | ext << 13 | base << 5 | (rd & 0x1f));
}

static void tcg_out_insn_3312(TCGContext *s, AArch64Insn insn,
                              TCGReg rd, TCGReg rn, intptr_t offset)
{
    tcg_out32(s, insn | (offset & 0x1ff) << 12 | rn << 5 | (rd & 0x1f));
}

static void tcg_out_insn_3313(TCGContext *s, AArch64Insn insn,
                              TCGReg rd, TCGReg rn, uintptr_t scaled_uimm)
{
    /* Note the AArch64Insn constants above are for C3.3.12.  Adjust.  */
    tcg_out32(s, insn | I3312_TO_I3313 | scaled_uimm << 10
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
    tcg_out_insn(s, 3510, ORR, ext, rd, TCG_REG_XZR, rm);
}

/* Register to register move using ADDI (move to/from SP).  */
static void tcg_out_movr_sp(TCGContext *s, TCGType ext, TCGReg rd, TCGReg rn)
{
    tcg_out_insn(s, 3401, ADDI, ext, rd, rn, 0);
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

    tcg_out_insn_3404(s, insn, ext, rd, rn, ext, r, c);
}

static void tcg_out_dupi_vec(TCGContext *s, TCGType type, unsigned vece,
                             TCGReg rd, int64_t v64)
{
    bool q = type == TCG_TYPE_V128;
    int cmode, imm8, i;

    /* Test all bytes equal first.  */
    if (vece == MO_8) {
        imm8 = (uint8_t)v64;
        tcg_out_insn(s, 3606, MOVI, q, rd, 0, 0xe, imm8);
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
    tcg_out_insn(s, 3606, MOVI, q, rd, 1, 0xe, imm8);
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
            tcg_out_insn(s, 3606, MOVI, q, rd, 0, cmode, imm8);
            return;
        }
        if (is_shimm16(~v16, &cmode, &imm8)) {
            tcg_out_insn(s, 3606, MVNI, q, rd, 0, cmode, imm8);
            return;
        }

        /*
         * Otherwise, all remaining constants can be loaded in two insns:
         * rd = v16 & 0xff, rd |= v16 & 0xff00.
         */
        tcg_out_insn(s, 3606, MOVI, q, rd, 0, 0x8, v16 & 0xff);
        tcg_out_insn(s, 3606, ORR, q, rd, 0, 0xa, v16 >> 8);
        return;
    } else if (vece == MO_32) {
        uint32_t v32 = v64;
        uint32_t n32 = ~v32;

        if (is_shimm32(v32, &cmode, &imm8) ||
            is_soimm32(v32, &cmode, &imm8) ||
            is_fimm32(v32, &cmode, &imm8)) {
            tcg_out_insn(s, 3606, MOVI, q, rd, 0, cmode, imm8);
            return;
        }
        if (is_shimm32(n32, &cmode, &imm8) ||
            is_soimm32(n32, &cmode, &imm8)) {
            tcg_out_insn(s, 3606, MVNI, q, rd, 0, cmode, imm8);
            return;
        }

        /*
         * Restrict the set of constants to those we can load with
         * two instructions.  Others we load from the pool.
         */
        i = is_shimm32_pair(v32, &cmode, &imm8);
        if (i) {
            tcg_out_insn(s, 3606, MOVI, q, rd, 0, cmode, imm8);
            tcg_out_insn(s, 3606, ORR, q, rd, 0, i, extract32(v32, i * 4, 8));
            return;
        }
        i = is_shimm32_pair(n32, &cmode, &imm8);
        if (i) {
            tcg_out_insn(s, 3606, MVNI, q, rd, 0, cmode, imm8);
            tcg_out_insn(s, 3606, BIC, q, rd, 0, i, extract32(n32, i * 4, 8));
            return;
        }
    } else if (is_fimm64(v64, &cmode, &imm8)) {
        tcg_out_insn(s, 3606, MOVI, q, rd, 1, cmode, imm8);
        return;
    }

    /*
     * As a last resort, load from the constant pool.  Sadly there
     * is no LD1R (literal), so store the full 16-byte vector.
     */
    if (type == TCG_TYPE_V128) {
        new_pool_l2(s, R_AARCH64_CONDBR19, s->code_ptr, 0, v64, v64);
        tcg_out_insn(s, 3305, LDR_v128, 0, rd);
    } else {
        new_pool_label(s, v64, R_AARCH64_CONDBR19, s->code_ptr, 0);
        tcg_out_insn(s, 3305, LDR_v64, 0, rd);
    }
}

static bool tcg_out_dup_vec(TCGContext *s, TCGType type, unsigned vece,
                            TCGReg rd, TCGReg rs)
{
    int is_q = type - TCG_TYPE_V64;
    tcg_out_insn(s, 3605, DUP, is_q, rd, rs, 1 << vece, 0);
    return true;
}

static bool tcg_out_dupm_vec(TCGContext *s, TCGType type, unsigned vece,
                             TCGReg r, TCGReg base, intptr_t offset)
{
    TCGReg temp = TCG_REG_TMP0;

    if (offset < -0xffffff || offset > 0xffffff) {
        tcg_out_movi(s, TCG_TYPE_PTR, temp, offset);
        tcg_out_insn(s, 3502, ADD, 1, temp, temp, base);
        base = temp;
    } else {
        AArch64Insn add_insn = I3401_ADDI;

        if (offset < 0) {
            add_insn = I3401_SUBI;
            offset = -offset;
        }
        if (offset & 0xfff000) {
            tcg_out_insn_3401(s, add_insn, 1, temp, base, offset & 0xfff000);
            base = temp;
        }
        if (offset & 0xfff) {
            tcg_out_insn_3401(s, add_insn, 1, temp, base, offset & 0xfff);
            base = temp;
        }
    }
    tcg_out_insn(s, 3303, LD1R, type == TCG_TYPE_V128, r, base, vece);
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
        tcg_out_insn(s, 3405, MOVZ, type, rd, value, 0);
        return;
    } else if ((ivalue & ~0xffffull) == 0) {
        tcg_out_insn(s, 3405, MOVN, type, rd, ivalue, 0);
        return;
    }

    /* Check for bitfield immediates.  For the benefit of 32-bit quantities,
       use the sign-extended value.  That lets us match rotated values such
       as 0xff0000ff with the same 64-bit logic matching 0xffffffffff0000ff. */
    if (is_limm(svalue)) {
        tcg_out_logicali(s, I3404_ORRI, type, rd, TCG_REG_XZR, svalue);
        return;
    }

    /* Look for host pointer values within 4G of the PC.  This happens
       often when loading pointers to QEMU's own data structures.  */
    if (type == TCG_TYPE_I64) {
        intptr_t src_rx = (intptr_t)tcg_splitwx_to_rx(s->code_ptr);
        tcg_target_long disp = value - src_rx;
        if (disp == sextract64(disp, 0, 21)) {
            tcg_out_insn(s, 3406, ADR, rd, disp);
            return;
        }
        disp = (value >> 12) - (src_rx >> 12);
        if (disp == sextract64(disp, 0, 21)) {
            tcg_out_insn(s, 3406, ADRP, rd, disp);
            if (value & 0xfff) {
                tcg_out_insn(s, 3401, ADDI, type, rd, rd, value & 0xfff);
            }
            return;
        }
    }

    /* Would it take fewer insns to begin with MOVN?  */
    if (ctpop64(value) >= 32) {
        t0 = ivalue;
        opc = I3405_MOVN;
    } else {
        t0 = value;
        opc = I3405_MOVZ;
    }
    s0 = ctz64(t0) & (63 & -16);
    t1 = t0 & ~(0xffffull << s0);
    s1 = ctz64(t1) & (63 & -16);
    t2 = t1 & ~(0xffffull << s1);
    if (t2 == 0) {
        tcg_out_insn_3405(s, opc, type, rd, t0 >> s0, s0);
        if (t1 != 0) {
            tcg_out_insn(s, 3405, MOVK, type, rd, value >> s1, s1);
        }
        return;
    }

    /* For more than 2 insns, dump it into the constant pool.  */
    new_pool_label(s, value, R_AARCH64_CONDBR19, s->code_ptr, 0);
    tcg_out_insn(s, 3305, LDR, 0, rd);
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

/* Define something more legible for general use.  */
#define tcg_out_ldst_r  tcg_out_insn_3310

static void tcg_out_ldst(TCGContext *s, AArch64Insn insn, TCGReg rd,
                         TCGReg rn, intptr_t offset, int lgsize)
{
    /* If the offset is naturally aligned and in range, then we can
       use the scaled uimm12 encoding */
    if (offset >= 0 && !(offset & ((1 << lgsize) - 1))) {
        uintptr_t scaled_uimm = offset >> lgsize;
        if (scaled_uimm <= 0xfff) {
            tcg_out_insn_3313(s, insn, rd, rn, scaled_uimm);
            return;
        }
    }

    /* Small signed offsets can use the unscaled encoding.  */
    if (offset >= -256 && offset < 256) {
        tcg_out_insn_3312(s, insn, rd, rn, offset);
        return;
    }

    /* Worst-case scenario, move offset to temp register, use reg offset.  */
    tcg_out_movi(s, TCG_TYPE_I64, TCG_REG_TMP0, offset);
    tcg_out_ldst_r(s, insn, rd, rn, TCG_TYPE_I64, TCG_REG_TMP0);
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
            tcg_out_insn(s, 3605, UMOV, type, ret, arg, 0, 0);
            break;
        } else if (arg < 32) {
            tcg_out_insn(s, 3605, INS, 0, ret, arg, 4 << type, 0);
            break;
        }
        /* FALLTHRU */

    case TCG_TYPE_V64:
        tcg_debug_assert(ret >= 32 && arg >= 32);
        tcg_out_insn(s, 3616, ORR, 0, 0, ret, arg, arg);
        break;
    case TCG_TYPE_V128:
        tcg_debug_assert(ret >= 32 && arg >= 32);
        tcg_out_insn(s, 3616, ORR, 1, 0, ret, arg, arg);
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
        insn = (ret < 32 ? I3312_LDRW : I3312_LDRVS);
        lgsz = 2;
        break;
    case TCG_TYPE_I64:
        insn = (ret < 32 ? I3312_LDRX : I3312_LDRVD);
        lgsz = 3;
        break;
    case TCG_TYPE_V64:
        insn = I3312_LDRVD;
        lgsz = 3;
        break;
    case TCG_TYPE_V128:
        insn = I3312_LDRVQ;
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
        insn = (src < 32 ? I3312_STRW : I3312_STRVS);
        lgsz = 2;
        break;
    case TCG_TYPE_I64:
        insn = (src < 32 ? I3312_STRX : I3312_STRVD);
        lgsz = 3;
        break;
    case TCG_TYPE_V64:
        insn = I3312_STRVD;
        lgsz = 3;
        break;
    case TCG_TYPE_V128:
        insn = I3312_STRVQ;
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
    tcg_out_insn(s, 3402, BFM, ext, rd, rn, ext, a, b);
}

static inline void tcg_out_ubfm(TCGContext *s, TCGType ext, TCGReg rd,
                                TCGReg rn, unsigned int a, unsigned int b)
{
    tcg_out_insn(s, 3402, UBFM, ext, rd, rn, ext, a, b);
}

static inline void tcg_out_sbfm(TCGContext *s, TCGType ext, TCGReg rd,
                                TCGReg rn, unsigned int a, unsigned int b)
{
    tcg_out_insn(s, 3402, SBFM, ext, rd, rn, ext, a, b);
}

static inline void tcg_out_extr(TCGContext *s, TCGType ext, TCGReg rd,
                                TCGReg rn, TCGReg rm, unsigned int a)
{
    tcg_out_insn(s, 3403, EXTR, ext, rd, rn, rm, a);
}

static inline void tcg_out_shl(TCGContext *s, TCGType ext,
                               TCGReg rd, TCGReg rn, unsigned int m)
{
    int bits = ext ? 64 : 32;
    int max = bits - 1;
    tcg_out_ubfm(s, ext, rd, rn, (bits - m) & max, (max - m) & max);
}

static inline void tcg_out_shr(TCGContext *s, TCGType ext,
                               TCGReg rd, TCGReg rn, unsigned int m)
{
    int max = ext ? 63 : 31;
    tcg_out_ubfm(s, ext, rd, rn, m & max, max);
}

static inline void tcg_out_sar(TCGContext *s, TCGType ext,
                               TCGReg rd, TCGReg rn, unsigned int m)
{
    int max = ext ? 63 : 31;
    tcg_out_sbfm(s, ext, rd, rn, m & max, max);
}

static inline void tcg_out_rotr(TCGContext *s, TCGType ext,
                                TCGReg rd, TCGReg rn, unsigned int m)
{
    int max = ext ? 63 : 31;
    tcg_out_extr(s, ext, rd, rn, rn, m & max);
}

static inline void tcg_out_rotl(TCGContext *s, TCGType ext,
                                TCGReg rd, TCGReg rn, unsigned int m)
{
    int max = ext ? 63 : 31;
    tcg_out_extr(s, ext, rd, rn, rn, -m & max);
}

static inline void tcg_out_dep(TCGContext *s, TCGType ext, TCGReg rd,
                               TCGReg rn, unsigned lsb, unsigned width)
{
    unsigned size = ext ? 64 : 32;
    unsigned a = (size - lsb) & (size - 1);
    unsigned b = width - 1;
    tcg_out_bfm(s, ext, rd, rn, a, b);
}

static void tcg_out_cmp(TCGContext *s, TCGType ext, TCGCond cond, TCGReg a,
                        tcg_target_long b, bool const_b)
{
    if (is_tst_cond(cond)) {
        if (!const_b) {
            tcg_out_insn(s, 3510, ANDS, ext, TCG_REG_XZR, a, b);
        } else {
            tcg_debug_assert(is_limm(b));
            tcg_out_logicali(s, I3404_ANDSI, 0, TCG_REG_XZR, a, b);
        }
    } else {
        if (!const_b) {
            tcg_out_insn(s, 3502, SUBS, ext, TCG_REG_XZR, a, b);
        } else if (b >= 0) {
            tcg_debug_assert(is_aimm(b));
            tcg_out_insn(s, 3401, SUBSI, ext, TCG_REG_XZR, a, b);
        } else {
            tcg_debug_assert(is_aimm(-b));
            tcg_out_insn(s, 3401, ADDSI, ext, TCG_REG_XZR, a, -b);
        }
    }
}

static void tcg_out_goto(TCGContext *s, const tcg_insn_unit *target)
{
    ptrdiff_t offset = tcg_pcrel_diff(s, target) >> 2;
    tcg_debug_assert(offset == sextract64(offset, 0, 26));
    tcg_out_insn(s, 3206, B, offset);
}

static void tcg_out_call_int(TCGContext *s, const tcg_insn_unit *target)
{
    ptrdiff_t offset = tcg_pcrel_diff(s, target) >> 2;
    if (offset == sextract64(offset, 0, 26)) {
        tcg_out_insn(s, 3206, BL, offset);
    } else {
        tcg_out_movi(s, TCG_TYPE_I64, TCG_REG_TMP0, (intptr_t)target);
        tcg_out_insn(s, 3207, BLR, TCG_REG_TMP0);
    }
}

static void tcg_out_call(TCGContext *s, const tcg_insn_unit *target,
                         const TCGHelperInfo *info)
{
    tcg_out_call_int(s, target);
}

static inline void tcg_out_goto_label(TCGContext *s, TCGLabel *l)
{
    if (!l->has_value) {
        tcg_out_reloc(s, s->code_ptr, R_AARCH64_JUMP26, l, 0);
        tcg_out_insn(s, 3206, B, 0);
    } else {
        tcg_out_goto(s, l->u.value_ptr);
    }
}

static void tcg_out_brcond(TCGContext *s, TCGType ext, TCGCond c, TCGArg a,
                           TCGArg b, bool b_const, TCGLabel *l)
{
    int tbit = -1;
    bool need_cmp = true;

    switch (c) {
    case TCG_COND_EQ:
    case TCG_COND_NE:
        /* cmp xN,0; b.ne L -> cbnz xN,L */
        if (b_const && b == 0) {
            need_cmp = false;
        }
        break;
    case TCG_COND_LT:
    case TCG_COND_GE:
        /* cmp xN,0; b.mi L -> tbnz xN,63,L */
        if (b_const && b == 0) {
            c = (c == TCG_COND_LT ? TCG_COND_TSTNE : TCG_COND_TSTEQ);
            tbit = ext ? 63 : 31;
            need_cmp = false;
        }
        break;
    case TCG_COND_TSTEQ:
    case TCG_COND_TSTNE:
        /* tst xN,0xffffffff; b.ne L -> cbnz wN,L */
        if (b_const && b == UINT32_MAX) {
            ext = TCG_TYPE_I32;
            need_cmp = false;
            break;
        }
        /* tst xN,1<<B; b.ne L -> tbnz xN,B,L */
        if (b_const && is_power_of_2(b)) {
            tbit = ctz64(b);
            need_cmp = false;
        }
        break;
    default:
        break;
    }

    if (need_cmp) {
        tcg_out_cmp(s, ext, c, a, b, b_const);
        tcg_out_reloc(s, s->code_ptr, R_AARCH64_CONDBR19, l, 0);
        tcg_out_insn(s, 3202, B_C, c, 0);
        return;
    }

    if (tbit >= 0) {
        tcg_out_reloc(s, s->code_ptr, R_AARCH64_TSTBR14, l, 0);
        switch (c) {
        case TCG_COND_TSTEQ:
            tcg_out_insn(s, 3205, TBZ, a, tbit, 0);
            break;
        case TCG_COND_TSTNE:
            tcg_out_insn(s, 3205, TBNZ, a, tbit, 0);
            break;
        default:
            g_assert_not_reached();
        }
    } else {
        tcg_out_reloc(s, s->code_ptr, R_AARCH64_CONDBR19, l, 0);
        switch (c) {
        case TCG_COND_EQ:
            tcg_out_insn(s, 3201, CBZ, ext, a, 0);
            break;
        case TCG_COND_NE:
            tcg_out_insn(s, 3201, CBNZ, ext, a, 0);
            break;
        default:
            g_assert_not_reached();
        }
    }
}

static inline void tcg_out_rev(TCGContext *s, int ext, MemOp s_bits,
                               TCGReg rd, TCGReg rn)
{
    /* REV, REV16, REV32 */
    tcg_out_insn_3507(s, I3507_REV | (s_bits << 10), ext, rd, rn);
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

static void tcg_out_addsubi(TCGContext *s, int ext, TCGReg rd,
                            TCGReg rn, int64_t aimm)
{
    if (aimm >= 0) {
        tcg_out_insn(s, 3401, ADDI, ext, rd, rn, aimm);
    } else {
        tcg_out_insn(s, 3401, SUBI, ext, rd, rn, -aimm);
    }
}

static void tcg_out_addsub2(TCGContext *s, TCGType ext, TCGReg rl,
                            TCGReg rh, TCGReg al, TCGReg ah,
                            tcg_target_long bl, tcg_target_long bh,
                            bool const_bl, bool const_bh, bool sub)
{
    TCGReg orig_rl = rl;
    AArch64Insn insn;

    if (rl == ah || (!const_bh && rl == bh)) {
        rl = TCG_REG_TMP0;
    }

    if (const_bl) {
        if (bl < 0) {
            bl = -bl;
            insn = sub ? I3401_ADDSI : I3401_SUBSI;
        } else {
            insn = sub ? I3401_SUBSI : I3401_ADDSI;
        }

        if (unlikely(al == TCG_REG_XZR)) {
            /* ??? We want to allow al to be zero for the benefit of
               negation via subtraction.  However, that leaves open the
               possibility of adding 0+const in the low part, and the
               immediate add instructions encode XSP not XZR.  Don't try
               anything more elaborate here than loading another zero.  */
            al = TCG_REG_TMP0;
            tcg_out_movi(s, ext, al, 0);
        }
        tcg_out_insn_3401(s, insn, ext, rl, al, bl);
    } else {
        tcg_out_insn_3502(s, sub ? I3502_SUBS : I3502_ADDS, ext, rl, al, bl);
    }

    insn = I3503_ADC;
    if (const_bh) {
        /* Note that the only two constants we support are 0 and -1, and
           that SBC = rn + ~rm + c, so adc -1 is sbc 0, and vice-versa.  */
        if ((bh != 0) ^ sub) {
            insn = I3503_SBC;
        }
        bh = TCG_REG_XZR;
    } else if (sub) {
        insn = I3503_SBC;
    }
    tcg_out_insn_3503(s, insn, ext, rh, ah, bh);

    tcg_out_mov(s, ext, orig_rl, rl);
}

static inline void tcg_out_mb(TCGContext *s, TCGArg a0)
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

static void tcg_out_cltz(TCGContext *s, TCGType ext, TCGReg d,
                         TCGReg a0, TCGArg b, bool const_b, bool is_ctz)
{
    TCGReg a1 = a0;
    if (is_ctz) {
        a1 = TCG_REG_TMP0;
        tcg_out_insn(s, 3507, RBIT, ext, a1, a0);
    }
    if (const_b && b == (ext ? 64 : 32)) {
        tcg_out_insn(s, 3507, CLZ, ext, d, a1);
    } else {
        AArch64Insn sel = I3506_CSEL;

        tcg_out_cmp(s, ext, TCG_COND_NE, a0, 0, 1);
        tcg_out_insn(s, 3507, CLZ, ext, TCG_REG_TMP0, a1);

        if (const_b) {
            if (b == -1) {
                b = TCG_REG_XZR;
                sel = I3506_CSINV;
            } else if (b == 0) {
                b = TCG_REG_XZR;
            } else {
                tcg_out_movi(s, ext, d, b);
                b = d;
            }
        }
        tcg_out_insn_3506(s, sel, ext, d, TCG_REG_TMP0, b, TCG_COND_NE);
    }
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
        TCGType mask_type;
        uint64_t compare_mask;

        ldst = new_ldst_label(s);
        ldst->is_ld = is_ld;
        ldst->oi = oi;
        ldst->addrlo_reg = addr_reg;

        mask_type = (s->page_bits + s->tlb_dyn_max_bits > 32
                     ? TCG_TYPE_I64 : TCG_TYPE_I32);

        /* Load cpu->neg.tlb.f[mmu_idx].{mask,table} into {tmp0,tmp1}. */
        QEMU_BUILD_BUG_ON(offsetof(CPUTLBDescFast, mask) != 0);
        QEMU_BUILD_BUG_ON(offsetof(CPUTLBDescFast, table) != 8);
        tcg_out_insn(s, 3314, LDP, TCG_REG_TMP0, TCG_REG_TMP1, TCG_AREG0,
                     tlb_mask_table_ofs(s, mem_index), 1, 0);

        /* Extract the TLB index from the address into X0.  */
        tcg_out_insn(s, 3502S, AND_LSR, mask_type == TCG_TYPE_I64,
                     TCG_REG_TMP0, TCG_REG_TMP0, addr_reg,
                     s->page_bits - CPU_TLB_ENTRY_BITS);

        /* Add the tlb_table pointer, forming the CPUTLBEntry address. */
        tcg_out_insn(s, 3502, ADD, 1, TCG_REG_TMP1, TCG_REG_TMP1, TCG_REG_TMP0);

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
            tcg_out_insn(s, 3401, ADDI, addr_type,
                         addr_adj, addr_reg, s_mask - a_mask);
        }
        compare_mask = (uint64_t)s->page_mask | a_mask;

        /* Store the page mask part of the address into TMP2.  */
        tcg_out_logicali(s, I3404_ANDI, addr_type, TCG_REG_TMP2,
                         addr_adj, compare_mask);

        /* Perform the address comparison. */
        tcg_out_cmp(s, addr_type, TCG_COND_NE, TCG_REG_TMP0, TCG_REG_TMP2, 0);

        /* If not equal, we jump to the slow path. */
        ldst->label_ptr[0] = s->code_ptr;
        tcg_out_insn(s, 3202, B_C, TCG_COND_NE, 0);

        h->base = TCG_REG_TMP1;
        h->index = addr_reg;
        h->index_ext = addr_type;
    } else {
        if (a_mask) {
            ldst = new_ldst_label(s);

            ldst->is_ld = is_ld;
            ldst->oi = oi;
            ldst->addrlo_reg = addr_reg;

            /* tst addr, #mask */
            tcg_out_logicali(s, I3404_ANDSI, 0, TCG_REG_XZR, addr_reg, a_mask);

            /* b.ne slow_path */
            ldst->label_ptr[0] = s->code_ptr;
            tcg_out_insn(s, 3202, B_C, TCG_COND_NE, 0);
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
        tcg_out_ldst_r(s, I3312_LDRB, data_r, h.base, h.index_ext, h.index);
        break;
    case MO_SB:
        tcg_out_ldst_r(s, ext ? I3312_LDRSBX : I3312_LDRSBW,
                       data_r, h.base, h.index_ext, h.index);
        break;
    case MO_UW:
        tcg_out_ldst_r(s, I3312_LDRH, data_r, h.base, h.index_ext, h.index);
        break;
    case MO_SW:
        tcg_out_ldst_r(s, (ext ? I3312_LDRSHX : I3312_LDRSHW),
                       data_r, h.base, h.index_ext, h.index);
        break;
    case MO_UL:
        tcg_out_ldst_r(s, I3312_LDRW, data_r, h.base, h.index_ext, h.index);
        break;
    case MO_SL:
        tcg_out_ldst_r(s, I3312_LDRSWX, data_r, h.base, h.index_ext, h.index);
        break;
    case MO_UQ:
        tcg_out_ldst_r(s, I3312_LDRX, data_r, h.base, h.index_ext, h.index);
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
        tcg_out_ldst_r(s, I3312_STRB, data_r, h.base, h.index_ext, h.index);
        break;
    case MO_16:
        tcg_out_ldst_r(s, I3312_STRH, data_r, h.base, h.index_ext, h.index);
        break;
    case MO_32:
        tcg_out_ldst_r(s, I3312_STRW, data_r, h.base, h.index_ext, h.index);
        break;
    case MO_64:
        tcg_out_ldst_r(s, I3312_STRX, data_r, h.base, h.index_ext, h.index);
        break;
    default:
        g_assert_not_reached();
    }
}

static void tcg_out_qemu_ld(TCGContext *s, TCGReg data_reg, TCGReg addr_reg,
                            MemOpIdx oi, TCGType data_type)
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

static void tcg_out_qemu_st(TCGContext *s, TCGReg data_reg, TCGReg addr_reg,
                            MemOpIdx oi, TCGType data_type)
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
            tcg_out_insn(s, 3501, ADD, TCG_TYPE_I64, base,
                         h.base, h.index, MO_32, 0);
        } else {
            /* add base, base, index */
            tcg_out_insn(s, 3502, ADD, 1, base, h.base, h.index);
        }
    }

    use_pair = h.aa.atom < MO_128 || have_lse2;

    if (!use_pair) {
        tcg_insn_unit *branch = NULL;
        TCGReg ll, lh, sl, sh;

        /*
         * If we have already checked for 16-byte alignment, that's all
         * we need. Otherwise we have determined that misaligned atomicity
         * may be handled with two 8-byte loads.
         */
        if (h.aa.align < MO_128) {
            /*
             * TODO: align should be MO_64, so we only need test bit 3,
             * which means we could use TBNZ instead of ANDS+B_C.
             */
            tcg_out_logicali(s, I3404_ANDSI, 0, TCG_REG_XZR, addr_reg, 15);
            branch = s->code_ptr;
            tcg_out_insn(s, 3202, B_C, TCG_COND_NE, 0);
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

        tcg_out_insn(s, 3306, LDXP, TCG_REG_XZR, ll, lh, base);
        tcg_out_insn(s, 3306, STXP, TCG_REG_TMP0, sl, sh, base);
        tcg_out_insn(s, 3201, CBNZ, 0, TCG_REG_TMP0, -2);

        if (use_pair) {
            /* "b .+8", branching across the one insn of use_pair. */
            tcg_out_insn(s, 3206, B, 2);
            reloc_pc19(branch, tcg_splitwx_to_rx(s->code_ptr));
        }
    }

    if (use_pair) {
        if (is_ld) {
            tcg_out_insn(s, 3314, LDP, datalo, datahi, base, 0, 1, 0);
        } else {
            tcg_out_insn(s, 3314, STP, datalo, datahi, base, 0, 1, 0);
        }
    }

    if (ldst) {
        ldst->type = TCG_TYPE_I128;
        ldst->datalo_reg = datalo;
        ldst->datahi_reg = datahi;
        ldst->raddr = tcg_splitwx_to_rx(s->code_ptr);
    }
}

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
        tcg_out_insn(s, 3206, B, offset);
    } else {
        /*
         * Only x16/x17 generate BTI type Jump (2),
         * other registers generate BTI type Jump|Call (3).
         */
        QEMU_BUILD_BUG_ON(TCG_REG_TMP0 != TCG_REG_X16);
        tcg_out_movi(s, TCG_TYPE_I64, TCG_REG_TMP0, (intptr_t)target);
        tcg_out_insn(s, 3207, BR, TCG_REG_TMP0);
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
    tcg_out32(s, I3206_B);
    tcg_out_insn(s, 3207, BR, TCG_REG_TMP0);
    set_jmp_reset_offset(s, which);
    tcg_out_bti(s, BTI_J);
}

void tb_target_set_jmp_target(const TranslationBlock *tb, int n,
                              uintptr_t jmp_rx, uintptr_t jmp_rw)
{
    uintptr_t d_addr = tb->jmp_target_addr[n];
    ptrdiff_t d_offset = d_addr - jmp_rx;
    tcg_insn_unit insn;

    /* Either directly branch, or indirect branch load. */
    if (d_offset == sextract64(d_offset, 0, 28)) {
        insn = deposit32(I3206_B, 0, 26, d_offset >> 2);
    } else {
        uintptr_t i_addr = (uintptr_t)&tb->jmp_target_addr[n];
        ptrdiff_t i_offset = i_addr - jmp_rx;

        /* Note that we asserted this in range in tcg_out_goto_tb. */
        insn = deposit32(I3305_LDR | TCG_REG_TMP0, 5, 19, i_offset >> 2);
    }
    qatomic_set((uint32_t *)jmp_rw, insn);
    flush_idcache_range(jmp_rx, jmp_rw, 4);
}

static void tcg_out_op(TCGContext *s, TCGOpcode opc,
                       const TCGArg args[TCG_MAX_OP_ARGS],
                       const int const_args[TCG_MAX_OP_ARGS])
{
    /* 99% of the time, we can signal the use of extension registers
       by looking to see if the opcode handles 64-bit data.  */
    TCGType ext = (tcg_op_defs[opc].flags & TCG_OPF_64BIT) != 0;

    /* Hoist the loads of the most common arguments.  */
    TCGArg a0 = args[0];
    TCGArg a1 = args[1];
    TCGArg a2 = args[2];
    int c2 = const_args[2];

    /* Some operands are defined with "rZ" constraint, a register or
       the zero register.  These need not actually test args[I] == 0.  */
#define REG0(I)  (const_args[I] ? TCG_REG_XZR : (TCGReg)args[I])

    switch (opc) {
    case INDEX_op_goto_ptr:
        tcg_out_insn(s, 3207, BR, a0);
        break;

    case INDEX_op_br:
        tcg_out_goto_label(s, arg_label(a0));
        break;

    case INDEX_op_ld8u_i32:
    case INDEX_op_ld8u_i64:
        tcg_out_ldst(s, I3312_LDRB, a0, a1, a2, 0);
        break;
    case INDEX_op_ld8s_i32:
        tcg_out_ldst(s, I3312_LDRSBW, a0, a1, a2, 0);
        break;
    case INDEX_op_ld8s_i64:
        tcg_out_ldst(s, I3312_LDRSBX, a0, a1, a2, 0);
        break;
    case INDEX_op_ld16u_i32:
    case INDEX_op_ld16u_i64:
        tcg_out_ldst(s, I3312_LDRH, a0, a1, a2, 1);
        break;
    case INDEX_op_ld16s_i32:
        tcg_out_ldst(s, I3312_LDRSHW, a0, a1, a2, 1);
        break;
    case INDEX_op_ld16s_i64:
        tcg_out_ldst(s, I3312_LDRSHX, a0, a1, a2, 1);
        break;
    case INDEX_op_ld_i32:
    case INDEX_op_ld32u_i64:
        tcg_out_ldst(s, I3312_LDRW, a0, a1, a2, 2);
        break;
    case INDEX_op_ld32s_i64:
        tcg_out_ldst(s, I3312_LDRSWX, a0, a1, a2, 2);
        break;
    case INDEX_op_ld_i64:
        tcg_out_ldst(s, I3312_LDRX, a0, a1, a2, 3);
        break;

    case INDEX_op_st8_i32:
    case INDEX_op_st8_i64:
        tcg_out_ldst(s, I3312_STRB, REG0(0), a1, a2, 0);
        break;
    case INDEX_op_st16_i32:
    case INDEX_op_st16_i64:
        tcg_out_ldst(s, I3312_STRH, REG0(0), a1, a2, 1);
        break;
    case INDEX_op_st_i32:
    case INDEX_op_st32_i64:
        tcg_out_ldst(s, I3312_STRW, REG0(0), a1, a2, 2);
        break;
    case INDEX_op_st_i64:
        tcg_out_ldst(s, I3312_STRX, REG0(0), a1, a2, 3);
        break;

    case INDEX_op_add_i32:
        a2 = (int32_t)a2;
        /* FALLTHRU */
    case INDEX_op_add_i64:
        if (c2) {
            tcg_out_addsubi(s, ext, a0, a1, a2);
        } else {
            tcg_out_insn(s, 3502, ADD, ext, a0, a1, a2);
        }
        break;

    case INDEX_op_sub_i32:
        a2 = (int32_t)a2;
        /* FALLTHRU */
    case INDEX_op_sub_i64:
        if (c2) {
            tcg_out_addsubi(s, ext, a0, a1, -a2);
        } else {
            tcg_out_insn(s, 3502, SUB, ext, a0, a1, a2);
        }
        break;

    case INDEX_op_neg_i64:
    case INDEX_op_neg_i32:
        tcg_out_insn(s, 3502, SUB, ext, a0, TCG_REG_XZR, a1);
        break;

    case INDEX_op_and_i32:
        a2 = (int32_t)a2;
        /* FALLTHRU */
    case INDEX_op_and_i64:
        if (c2) {
            tcg_out_logicali(s, I3404_ANDI, ext, a0, a1, a2);
        } else {
            tcg_out_insn(s, 3510, AND, ext, a0, a1, a2);
        }
        break;

    case INDEX_op_andc_i32:
        a2 = (int32_t)a2;
        /* FALLTHRU */
    case INDEX_op_andc_i64:
        if (c2) {
            tcg_out_logicali(s, I3404_ANDI, ext, a0, a1, ~a2);
        } else {
            tcg_out_insn(s, 3510, BIC, ext, a0, a1, a2);
        }
        break;

    case INDEX_op_or_i32:
        a2 = (int32_t)a2;
        /* FALLTHRU */
    case INDEX_op_or_i64:
        if (c2) {
            tcg_out_logicali(s, I3404_ORRI, ext, a0, a1, a2);
        } else {
            tcg_out_insn(s, 3510, ORR, ext, a0, a1, a2);
        }
        break;

    case INDEX_op_orc_i32:
        a2 = (int32_t)a2;
        /* FALLTHRU */
    case INDEX_op_orc_i64:
        if (c2) {
            tcg_out_logicali(s, I3404_ORRI, ext, a0, a1, ~a2);
        } else {
            tcg_out_insn(s, 3510, ORN, ext, a0, a1, a2);
        }
        break;

    case INDEX_op_xor_i32:
        a2 = (int32_t)a2;
        /* FALLTHRU */
    case INDEX_op_xor_i64:
        if (c2) {
            tcg_out_logicali(s, I3404_EORI, ext, a0, a1, a2);
        } else {
            tcg_out_insn(s, 3510, EOR, ext, a0, a1, a2);
        }
        break;

    case INDEX_op_eqv_i32:
        a2 = (int32_t)a2;
        /* FALLTHRU */
    case INDEX_op_eqv_i64:
        if (c2) {
            tcg_out_logicali(s, I3404_EORI, ext, a0, a1, ~a2);
        } else {
            tcg_out_insn(s, 3510, EON, ext, a0, a1, a2);
        }
        break;

    case INDEX_op_not_i64:
    case INDEX_op_not_i32:
        tcg_out_insn(s, 3510, ORN, ext, a0, TCG_REG_XZR, a1);
        break;

    case INDEX_op_mul_i64:
    case INDEX_op_mul_i32:
        tcg_out_insn(s, 3509, MADD, ext, a0, a1, a2, TCG_REG_XZR);
        break;

    case INDEX_op_div_i64:
    case INDEX_op_div_i32:
        tcg_out_insn(s, 3508, SDIV, ext, a0, a1, a2);
        break;
    case INDEX_op_divu_i64:
    case INDEX_op_divu_i32:
        tcg_out_insn(s, 3508, UDIV, ext, a0, a1, a2);
        break;

    case INDEX_op_rem_i64:
    case INDEX_op_rem_i32:
        tcg_out_insn(s, 3508, SDIV, ext, TCG_REG_TMP0, a1, a2);
        tcg_out_insn(s, 3509, MSUB, ext, a0, TCG_REG_TMP0, a2, a1);
        break;
    case INDEX_op_remu_i64:
    case INDEX_op_remu_i32:
        tcg_out_insn(s, 3508, UDIV, ext, TCG_REG_TMP0, a1, a2);
        tcg_out_insn(s, 3509, MSUB, ext, a0, TCG_REG_TMP0, a2, a1);
        break;

    case INDEX_op_shl_i64:
    case INDEX_op_shl_i32:
        if (c2) {
            tcg_out_shl(s, ext, a0, a1, a2);
        } else {
            tcg_out_insn(s, 3508, LSLV, ext, a0, a1, a2);
        }
        break;

    case INDEX_op_shr_i64:
    case INDEX_op_shr_i32:
        if (c2) {
            tcg_out_shr(s, ext, a0, a1, a2);
        } else {
            tcg_out_insn(s, 3508, LSRV, ext, a0, a1, a2);
        }
        break;

    case INDEX_op_sar_i64:
    case INDEX_op_sar_i32:
        if (c2) {
            tcg_out_sar(s, ext, a0, a1, a2);
        } else {
            tcg_out_insn(s, 3508, ASRV, ext, a0, a1, a2);
        }
        break;

    case INDEX_op_rotr_i64:
    case INDEX_op_rotr_i32:
        if (c2) {
            tcg_out_rotr(s, ext, a0, a1, a2);
        } else {
            tcg_out_insn(s, 3508, RORV, ext, a0, a1, a2);
        }
        break;

    case INDEX_op_rotl_i64:
    case INDEX_op_rotl_i32:
        if (c2) {
            tcg_out_rotl(s, ext, a0, a1, a2);
        } else {
            tcg_out_insn(s, 3502, SUB, 0, TCG_REG_TMP0, TCG_REG_XZR, a2);
            tcg_out_insn(s, 3508, RORV, ext, a0, a1, TCG_REG_TMP0);
        }
        break;

    case INDEX_op_clz_i64:
    case INDEX_op_clz_i32:
        tcg_out_cltz(s, ext, a0, a1, a2, c2, false);
        break;
    case INDEX_op_ctz_i64:
    case INDEX_op_ctz_i32:
        tcg_out_cltz(s, ext, a0, a1, a2, c2, true);
        break;

    case INDEX_op_brcond_i32:
        a1 = (int32_t)a1;
        /* FALLTHRU */
    case INDEX_op_brcond_i64:
        tcg_out_brcond(s, ext, a2, a0, a1, const_args[1], arg_label(args[3]));
        break;

    case INDEX_op_setcond_i32:
        a2 = (int32_t)a2;
        /* FALLTHRU */
    case INDEX_op_setcond_i64:
        tcg_out_cmp(s, ext, args[3], a1, a2, c2);
        /* Use CSET alias of CSINC Wd, WZR, WZR, invert(cond).  */
        tcg_out_insn(s, 3506, CSINC, TCG_TYPE_I32, a0, TCG_REG_XZR,
                     TCG_REG_XZR, tcg_invert_cond(args[3]));
        break;

    case INDEX_op_negsetcond_i32:
        a2 = (int32_t)a2;
        /* FALLTHRU */
    case INDEX_op_negsetcond_i64:
        tcg_out_cmp(s, ext, args[3], a1, a2, c2);
        /* Use CSETM alias of CSINV Wd, WZR, WZR, invert(cond).  */
        tcg_out_insn(s, 3506, CSINV, ext, a0, TCG_REG_XZR,
                     TCG_REG_XZR, tcg_invert_cond(args[3]));
        break;

    case INDEX_op_movcond_i32:
        a2 = (int32_t)a2;
        /* FALLTHRU */
    case INDEX_op_movcond_i64:
        tcg_out_cmp(s, ext, args[5], a1, a2, c2);
        tcg_out_insn(s, 3506, CSEL, ext, a0, REG0(3), REG0(4), args[5]);
        break;

    case INDEX_op_qemu_ld_a32_i32:
    case INDEX_op_qemu_ld_a64_i32:
    case INDEX_op_qemu_ld_a32_i64:
    case INDEX_op_qemu_ld_a64_i64:
        tcg_out_qemu_ld(s, a0, a1, a2, ext);
        break;
    case INDEX_op_qemu_st_a32_i32:
    case INDEX_op_qemu_st_a64_i32:
    case INDEX_op_qemu_st_a32_i64:
    case INDEX_op_qemu_st_a64_i64:
        tcg_out_qemu_st(s, REG0(0), a1, a2, ext);
        break;
    case INDEX_op_qemu_ld_a32_i128:
    case INDEX_op_qemu_ld_a64_i128:
        tcg_out_qemu_ldst_i128(s, a0, a1, a2, args[3], true);
        break;
    case INDEX_op_qemu_st_a32_i128:
    case INDEX_op_qemu_st_a64_i128:
        tcg_out_qemu_ldst_i128(s, REG0(0), REG0(1), a2, args[3], false);
        break;

    case INDEX_op_bswap64_i64:
        tcg_out_rev(s, TCG_TYPE_I64, MO_64, a0, a1);
        break;
    case INDEX_op_bswap32_i64:
        tcg_out_rev(s, TCG_TYPE_I32, MO_32, a0, a1);
        if (a2 & TCG_BSWAP_OS) {
            tcg_out_ext32s(s, a0, a0);
        }
        break;
    case INDEX_op_bswap32_i32:
        tcg_out_rev(s, TCG_TYPE_I32, MO_32, a0, a1);
        break;
    case INDEX_op_bswap16_i64:
    case INDEX_op_bswap16_i32:
        tcg_out_rev(s, TCG_TYPE_I32, MO_16, a0, a1);
        if (a2 & TCG_BSWAP_OS) {
            /* Output must be sign-extended. */
            tcg_out_ext16s(s, ext, a0, a0);
        } else if ((a2 & (TCG_BSWAP_IZ | TCG_BSWAP_OZ)) == TCG_BSWAP_OZ) {
            /* Output must be zero-extended, but input isn't. */
            tcg_out_ext16u(s, a0, a0);
        }
        break;

    case INDEX_op_deposit_i64:
    case INDEX_op_deposit_i32:
        tcg_out_dep(s, ext, a0, REG0(2), args[3], args[4]);
        break;

    case INDEX_op_extract_i64:
    case INDEX_op_extract_i32:
        tcg_out_ubfm(s, ext, a0, a1, a2, a2 + args[3] - 1);
        break;

    case INDEX_op_sextract_i64:
    case INDEX_op_sextract_i32:
        tcg_out_sbfm(s, ext, a0, a1, a2, a2 + args[3] - 1);
        break;

    case INDEX_op_extract2_i64:
    case INDEX_op_extract2_i32:
        tcg_out_extr(s, ext, a0, REG0(2), REG0(1), args[3]);
        break;

    case INDEX_op_add2_i32:
        tcg_out_addsub2(s, TCG_TYPE_I32, a0, a1, REG0(2), REG0(3),
                        (int32_t)args[4], args[5], const_args[4],
                        const_args[5], false);
        break;
    case INDEX_op_add2_i64:
        tcg_out_addsub2(s, TCG_TYPE_I64, a0, a1, REG0(2), REG0(3), args[4],
                        args[5], const_args[4], const_args[5], false);
        break;
    case INDEX_op_sub2_i32:
        tcg_out_addsub2(s, TCG_TYPE_I32, a0, a1, REG0(2), REG0(3),
                        (int32_t)args[4], args[5], const_args[4],
                        const_args[5], true);
        break;
    case INDEX_op_sub2_i64:
        tcg_out_addsub2(s, TCG_TYPE_I64, a0, a1, REG0(2), REG0(3), args[4],
                        args[5], const_args[4], const_args[5], true);
        break;

    case INDEX_op_muluh_i64:
        tcg_out_insn(s, 3508, UMULH, TCG_TYPE_I64, a0, a1, a2);
        break;
    case INDEX_op_mulsh_i64:
        tcg_out_insn(s, 3508, SMULH, TCG_TYPE_I64, a0, a1, a2);
        break;

    case INDEX_op_mb:
        tcg_out_mb(s, a0);
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
    case INDEX_op_ext16s_i64:
    case INDEX_op_ext16s_i32:
    case INDEX_op_ext16u_i64:
    case INDEX_op_ext16u_i32:
    case INDEX_op_ext32s_i64:
    case INDEX_op_ext32u_i64:
    case INDEX_op_ext_i32_i64:
    case INDEX_op_extu_i32_i64:
    case INDEX_op_extrl_i64_i32:
    default:
        g_assert_not_reached();
    }

#undef REG0
}

static void tcg_out_vec_op(TCGContext *s, TCGOpcode opc,
                           unsigned vecl, unsigned vece,
                           const TCGArg args[TCG_MAX_OP_ARGS],
                           const int const_args[TCG_MAX_OP_ARGS])
{
    static const AArch64Insn cmp_vec_insn[16] = {
        [TCG_COND_EQ] = I3616_CMEQ,
        [TCG_COND_GT] = I3616_CMGT,
        [TCG_COND_GE] = I3616_CMGE,
        [TCG_COND_GTU] = I3616_CMHI,
        [TCG_COND_GEU] = I3616_CMHS,
    };
    static const AArch64Insn cmp_scalar_insn[16] = {
        [TCG_COND_EQ] = I3611_CMEQ,
        [TCG_COND_GT] = I3611_CMGT,
        [TCG_COND_GE] = I3611_CMGE,
        [TCG_COND_GTU] = I3611_CMHI,
        [TCG_COND_GEU] = I3611_CMHS,
    };
    static const AArch64Insn cmp0_vec_insn[16] = {
        [TCG_COND_EQ] = I3617_CMEQ0,
        [TCG_COND_GT] = I3617_CMGT0,
        [TCG_COND_GE] = I3617_CMGE0,
        [TCG_COND_LT] = I3617_CMLT0,
        [TCG_COND_LE] = I3617_CMLE0,
    };
    static const AArch64Insn cmp0_scalar_insn[16] = {
        [TCG_COND_EQ] = I3612_CMEQ0,
        [TCG_COND_GT] = I3612_CMGT0,
        [TCG_COND_GE] = I3612_CMGE0,
        [TCG_COND_LT] = I3612_CMLT0,
        [TCG_COND_LE] = I3612_CMLE0,
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
            tcg_out_insn(s, 3611, ADD, vece, a0, a1, a2);
        } else {
            tcg_out_insn(s, 3616, ADD, is_q, vece, a0, a1, a2);
        }
        break;
    case INDEX_op_sub_vec:
        if (is_scalar) {
            tcg_out_insn(s, 3611, SUB, vece, a0, a1, a2);
        } else {
            tcg_out_insn(s, 3616, SUB, is_q, vece, a0, a1, a2);
        }
        break;
    case INDEX_op_mul_vec:
        tcg_out_insn(s, 3616, MUL, is_q, vece, a0, a1, a2);
        break;
    case INDEX_op_neg_vec:
        if (is_scalar) {
            tcg_out_insn(s, 3612, NEG, vece, a0, a1);
        } else {
            tcg_out_insn(s, 3617, NEG, is_q, vece, a0, a1);
        }
        break;
    case INDEX_op_abs_vec:
        if (is_scalar) {
            tcg_out_insn(s, 3612, ABS, vece, a0, a1);
        } else {
            tcg_out_insn(s, 3617, ABS, is_q, vece, a0, a1);
        }
        break;
    case INDEX_op_and_vec:
        if (const_args[2]) {
            is_shimm1632(~a2, &cmode, &imm8);
            if (a0 == a1) {
                tcg_out_insn(s, 3606, BIC, is_q, a0, 0, cmode, imm8);
                return;
            }
            tcg_out_insn(s, 3606, MVNI, is_q, a0, 0, cmode, imm8);
            a2 = a0;
        }
        tcg_out_insn(s, 3616, AND, is_q, 0, a0, a1, a2);
        break;
    case INDEX_op_or_vec:
        if (const_args[2]) {
            is_shimm1632(a2, &cmode, &imm8);
            if (a0 == a1) {
                tcg_out_insn(s, 3606, ORR, is_q, a0, 0, cmode, imm8);
                return;
            }
            tcg_out_insn(s, 3606, MOVI, is_q, a0, 0, cmode, imm8);
            a2 = a0;
        }
        tcg_out_insn(s, 3616, ORR, is_q, 0, a0, a1, a2);
        break;
    case INDEX_op_andc_vec:
        if (const_args[2]) {
            is_shimm1632(a2, &cmode, &imm8);
            if (a0 == a1) {
                tcg_out_insn(s, 3606, BIC, is_q, a0, 0, cmode, imm8);
                return;
            }
            tcg_out_insn(s, 3606, MOVI, is_q, a0, 0, cmode, imm8);
            a2 = a0;
        }
        tcg_out_insn(s, 3616, BIC, is_q, 0, a0, a1, a2);
        break;
    case INDEX_op_orc_vec:
        if (const_args[2]) {
            is_shimm1632(~a2, &cmode, &imm8);
            if (a0 == a1) {
                tcg_out_insn(s, 3606, ORR, is_q, a0, 0, cmode, imm8);
                return;
            }
            tcg_out_insn(s, 3606, MVNI, is_q, a0, 0, cmode, imm8);
            a2 = a0;
        }
        tcg_out_insn(s, 3616, ORN, is_q, 0, a0, a1, a2);
        break;
    case INDEX_op_xor_vec:
        tcg_out_insn(s, 3616, EOR, is_q, 0, a0, a1, a2);
        break;
    case INDEX_op_ssadd_vec:
        if (is_scalar) {
            tcg_out_insn(s, 3611, SQADD, vece, a0, a1, a2);
        } else {
            tcg_out_insn(s, 3616, SQADD, is_q, vece, a0, a1, a2);
        }
        break;
    case INDEX_op_sssub_vec:
        if (is_scalar) {
            tcg_out_insn(s, 3611, SQSUB, vece, a0, a1, a2);
        } else {
            tcg_out_insn(s, 3616, SQSUB, is_q, vece, a0, a1, a2);
        }
        break;
    case INDEX_op_usadd_vec:
        if (is_scalar) {
            tcg_out_insn(s, 3611, UQADD, vece, a0, a1, a2);
        } else {
            tcg_out_insn(s, 3616, UQADD, is_q, vece, a0, a1, a2);
        }
        break;
    case INDEX_op_ussub_vec:
        if (is_scalar) {
            tcg_out_insn(s, 3611, UQSUB, vece, a0, a1, a2);
        } else {
            tcg_out_insn(s, 3616, UQSUB, is_q, vece, a0, a1, a2);
        }
        break;
    case INDEX_op_smax_vec:
        tcg_out_insn(s, 3616, SMAX, is_q, vece, a0, a1, a2);
        break;
    case INDEX_op_smin_vec:
        tcg_out_insn(s, 3616, SMIN, is_q, vece, a0, a1, a2);
        break;
    case INDEX_op_umax_vec:
        tcg_out_insn(s, 3616, UMAX, is_q, vece, a0, a1, a2);
        break;
    case INDEX_op_umin_vec:
        tcg_out_insn(s, 3616, UMIN, is_q, vece, a0, a1, a2);
        break;
    case INDEX_op_not_vec:
        tcg_out_insn(s, 3617, NOT, is_q, 0, a0, a1);
        break;
    case INDEX_op_shli_vec:
        if (is_scalar) {
            tcg_out_insn(s, 3609, SHL, a0, a1, a2 + (8 << vece));
        } else {
            tcg_out_insn(s, 3614, SHL, is_q, a0, a1, a2 + (8 << vece));
        }
        break;
    case INDEX_op_shri_vec:
        if (is_scalar) {
            tcg_out_insn(s, 3609, USHR, a0, a1, (16 << vece) - a2);
        } else {
            tcg_out_insn(s, 3614, USHR, is_q, a0, a1, (16 << vece) - a2);
        }
        break;
    case INDEX_op_sari_vec:
        if (is_scalar) {
            tcg_out_insn(s, 3609, SSHR, a0, a1, (16 << vece) - a2);
        } else {
            tcg_out_insn(s, 3614, SSHR, is_q, a0, a1, (16 << vece) - a2);
        }
        break;
    case INDEX_op_aa64_sli_vec:
        if (is_scalar) {
            tcg_out_insn(s, 3609, SLI, a0, a2, args[3] + (8 << vece));
        } else {
            tcg_out_insn(s, 3614, SLI, is_q, a0, a2, args[3] + (8 << vece));
        }
        break;
    case INDEX_op_shlv_vec:
        if (is_scalar) {
            tcg_out_insn(s, 3611, USHL, vece, a0, a1, a2);
        } else {
            tcg_out_insn(s, 3616, USHL, is_q, vece, a0, a1, a2);
        }
        break;
    case INDEX_op_aa64_sshl_vec:
        if (is_scalar) {
            tcg_out_insn(s, 3611, SSHL, vece, a0, a1, a2);
        } else {
            tcg_out_insn(s, 3616, SSHL, is_q, vece, a0, a1, a2);
        }
        break;
    case INDEX_op_cmp_vec:
        {
            TCGCond cond = args[3];
            AArch64Insn insn;

            if (cond == TCG_COND_NE) {
                if (const_args[2]) {
                    if (is_scalar) {
                        tcg_out_insn(s, 3611, CMTST, vece, a0, a1, a1);
                    } else {
                        tcg_out_insn(s, 3616, CMTST, is_q, vece, a0, a1, a1);
                    }
                } else {
                    if (is_scalar) {
                        tcg_out_insn(s, 3611, CMEQ, vece, a0, a1, a2);
                    } else {
                        tcg_out_insn(s, 3616, CMEQ, is_q, vece, a0, a1, a2);
                    }
                    tcg_out_insn(s, 3617, NOT, is_q, 0, a0, a0);
                }
            } else {
                if (const_args[2]) {
                    if (is_scalar) {
                        insn = cmp0_scalar_insn[cond];
                        if (insn) {
                            tcg_out_insn_3612(s, insn, vece, a0, a1);
                            break;
                        }
                    } else {
                        insn = cmp0_vec_insn[cond];
                        if (insn) {
                            tcg_out_insn_3617(s, insn, is_q, vece, a0, a1);
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
                    tcg_out_insn_3611(s, insn, vece, a0, a1, a2);
                } else {
                    insn = cmp_vec_insn[cond];
                    if (insn == 0) {
                        TCGArg t;
                        t = a1, a1 = a2, a2 = t;
                        cond = tcg_swap_cond(cond);
                        insn = cmp_vec_insn[cond];
                        tcg_debug_assert(insn != 0);
                    }
                    tcg_out_insn_3616(s, insn, is_q, vece, a0, a1, a2);
                }
            }
        }
        break;

    case INDEX_op_bitsel_vec:
        a3 = args[3];
        if (a0 == a3) {
            tcg_out_insn(s, 3616, BIT, is_q, 0, a0, a2, a1);
        } else if (a0 == a2) {
            tcg_out_insn(s, 3616, BIF, is_q, 0, a0, a3, a1);
        } else {
            if (a0 != a1) {
                tcg_out_mov(s, type, a0, a1);
            }
            tcg_out_insn(s, 3616, BSL, is_q, 0, a0, a2, a3);
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

static TCGConstraintSetIndex tcg_target_op_def(TCGOpcode op)
{
    switch (op) {
    case INDEX_op_goto_ptr:
        return C_O0_I1(r);

    case INDEX_op_ld8u_i32:
    case INDEX_op_ld8s_i32:
    case INDEX_op_ld16u_i32:
    case INDEX_op_ld16s_i32:
    case INDEX_op_ld_i32:
    case INDEX_op_ld8u_i64:
    case INDEX_op_ld8s_i64:
    case INDEX_op_ld16u_i64:
    case INDEX_op_ld16s_i64:
    case INDEX_op_ld32u_i64:
    case INDEX_op_ld32s_i64:
    case INDEX_op_ld_i64:
    case INDEX_op_neg_i32:
    case INDEX_op_neg_i64:
    case INDEX_op_not_i32:
    case INDEX_op_not_i64:
    case INDEX_op_bswap16_i32:
    case INDEX_op_bswap32_i32:
    case INDEX_op_bswap16_i64:
    case INDEX_op_bswap32_i64:
    case INDEX_op_bswap64_i64:
    case INDEX_op_ext8s_i32:
    case INDEX_op_ext16s_i32:
    case INDEX_op_ext8u_i32:
    case INDEX_op_ext16u_i32:
    case INDEX_op_ext8s_i64:
    case INDEX_op_ext16s_i64:
    case INDEX_op_ext32s_i64:
    case INDEX_op_ext8u_i64:
    case INDEX_op_ext16u_i64:
    case INDEX_op_ext32u_i64:
    case INDEX_op_ext_i32_i64:
    case INDEX_op_extu_i32_i64:
    case INDEX_op_extract_i32:
    case INDEX_op_extract_i64:
    case INDEX_op_sextract_i32:
    case INDEX_op_sextract_i64:
        return C_O1_I1(r, r);

    case INDEX_op_st8_i32:
    case INDEX_op_st16_i32:
    case INDEX_op_st_i32:
    case INDEX_op_st8_i64:
    case INDEX_op_st16_i64:
    case INDEX_op_st32_i64:
    case INDEX_op_st_i64:
        return C_O0_I2(rZ, r);

    case INDEX_op_add_i32:
    case INDEX_op_add_i64:
    case INDEX_op_sub_i32:
    case INDEX_op_sub_i64:
        return C_O1_I2(r, r, rA);

    case INDEX_op_setcond_i32:
    case INDEX_op_setcond_i64:
    case INDEX_op_negsetcond_i32:
    case INDEX_op_negsetcond_i64:
        return C_O1_I2(r, r, rC);

    case INDEX_op_mul_i32:
    case INDEX_op_mul_i64:
    case INDEX_op_div_i32:
    case INDEX_op_div_i64:
    case INDEX_op_divu_i32:
    case INDEX_op_divu_i64:
    case INDEX_op_rem_i32:
    case INDEX_op_rem_i64:
    case INDEX_op_remu_i32:
    case INDEX_op_remu_i64:
    case INDEX_op_muluh_i64:
    case INDEX_op_mulsh_i64:
        return C_O1_I2(r, r, r);

    case INDEX_op_and_i32:
    case INDEX_op_and_i64:
    case INDEX_op_or_i32:
    case INDEX_op_or_i64:
    case INDEX_op_xor_i32:
    case INDEX_op_xor_i64:
    case INDEX_op_andc_i32:
    case INDEX_op_andc_i64:
    case INDEX_op_orc_i32:
    case INDEX_op_orc_i64:
    case INDEX_op_eqv_i32:
    case INDEX_op_eqv_i64:
        return C_O1_I2(r, r, rL);

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
    case INDEX_op_ctz_i32:
    case INDEX_op_clz_i64:
    case INDEX_op_ctz_i64:
        return C_O1_I2(r, r, rAL);

    case INDEX_op_brcond_i32:
    case INDEX_op_brcond_i64:
        return C_O0_I2(r, rC);

    case INDEX_op_movcond_i32:
    case INDEX_op_movcond_i64:
        return C_O1_I4(r, r, rC, rZ, rZ);

    case INDEX_op_qemu_ld_a32_i32:
    case INDEX_op_qemu_ld_a64_i32:
    case INDEX_op_qemu_ld_a32_i64:
    case INDEX_op_qemu_ld_a64_i64:
        return C_O1_I1(r, r);
    case INDEX_op_qemu_ld_a32_i128:
    case INDEX_op_qemu_ld_a64_i128:
        return C_O2_I1(r, r, r);
    case INDEX_op_qemu_st_a32_i32:
    case INDEX_op_qemu_st_a64_i32:
    case INDEX_op_qemu_st_a32_i64:
    case INDEX_op_qemu_st_a64_i64:
        return C_O0_I2(rZ, r);
    case INDEX_op_qemu_st_a32_i128:
    case INDEX_op_qemu_st_a64_i128:
        return C_O0_I3(rZ, rZ, r);

    case INDEX_op_deposit_i32:
    case INDEX_op_deposit_i64:
        return C_O1_I2(r, 0, rZ);

    case INDEX_op_extract2_i32:
    case INDEX_op_extract2_i64:
        return C_O1_I2(r, rZ, rZ);

    case INDEX_op_add2_i32:
    case INDEX_op_add2_i64:
    case INDEX_op_sub2_i32:
    case INDEX_op_sub2_i64:
        return C_O2_I4(r, r, rZ, rZ, rA, rMZ);

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
        g_assert_not_reached();
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
    tcg_out_insn(s, 3314, STP, TCG_REG_FP, TCG_REG_LR,
                 TCG_REG_SP, -PUSH_SIZE, 1, 1);

    /* Set up frame pointer for canonical unwinding.  */
    tcg_out_movr_sp(s, TCG_TYPE_I64, TCG_REG_FP, TCG_REG_SP);

    /* Store callee-preserved regs x19..x28.  */
    for (r = TCG_REG_X19; r <= TCG_REG_X27; r += 2) {
        int ofs = (r - TCG_REG_X19 + 2) * 8;
        tcg_out_insn(s, 3314, STP, r, r + 1, TCG_REG_SP, ofs, 1, 0);
    }

    /* Make stack space for TCG locals.  */
    tcg_out_insn(s, 3401, SUBI, TCG_TYPE_I64, TCG_REG_SP, TCG_REG_SP,
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
    tcg_out_insn(s, 3207, BR, tcg_target_call_iarg_regs[1]);

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
    tcg_out_insn(s, 3401, ADDI, TCG_TYPE_I64, TCG_REG_SP, TCG_REG_SP,
                 FRAME_SIZE - PUSH_SIZE);

    /* Restore registers x19..x28.  */
    for (r = TCG_REG_X19; r <= TCG_REG_X27; r += 2) {
        int ofs = (r - TCG_REG_X19 + 2) * 8;
        tcg_out_insn(s, 3314, LDP, r, r + 1, TCG_REG_SP, ofs, 1, 0);
    }

    /* Pop (FP, LR), restore SP to previous frame.  */
    tcg_out_insn(s, 3314, LDP, TCG_REG_FP, TCG_REG_LR,
                 TCG_REG_SP, PUSH_SIZE, 0, 1);
    tcg_out_insn(s, 3207, RET, TCG_REG_LR);
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

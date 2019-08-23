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

#include "tcg-pool.inc.c"
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
    TCG_REG_X16, TCG_REG_X17,

    TCG_REG_X0, TCG_REG_X1, TCG_REG_X2, TCG_REG_X3,
    TCG_REG_X4, TCG_REG_X5, TCG_REG_X6, TCG_REG_X7,

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
static const int tcg_target_call_oarg_regs[1] = {
    TCG_REG_X0
};

#define TCG_REG_TMP TCG_REG_X30
#define TCG_VEC_TMP TCG_REG_V31

#ifndef CONFIG_SOFTMMU
/* Note that XZR cannot be encoded in the address base register slot,
   as that actaully encodes SP.  So if we need to zero-extend the guest
   address, via the address index register slot, we need to load even
   a zero guest base into a register.  */
#define USE_GUEST_BASE     (guest_base != 0 || TARGET_LONG_BITS == 32)
#define TCG_REG_GUEST_BASE TCG_REG_X28
#endif

static inline bool reloc_pc26(tcg_insn_unit *code_ptr, tcg_insn_unit *target)
{
    ptrdiff_t offset = target - code_ptr;
    if (offset == sextract64(offset, 0, 26)) {
        /* read instruction, mask away previous PC_REL26 parameter contents,
           set the proper offset, then write back the instruction. */
        *code_ptr = deposit32(*code_ptr, 0, 26, offset);
        return true;
    }
    return false;
}

static inline bool reloc_pc19(tcg_insn_unit *code_ptr, tcg_insn_unit *target)
{
    ptrdiff_t offset = target - code_ptr;
    if (offset == sextract64(offset, 0, 19)) {
        *code_ptr = deposit32(*code_ptr, 5, 19, offset);
        return true;
    }
    return false;
}

static inline bool patch_reloc(tcg_insn_unit *code_ptr, int type,
                               intptr_t value, intptr_t addend)
{
    tcg_debug_assert(addend == 0);
    switch (type) {
    case R_AARCH64_JUMP26:
    case R_AARCH64_CALL26:
        return reloc_pc26(code_ptr, (tcg_insn_unit *)value);
    case R_AARCH64_CONDBR19:
        return reloc_pc19(code_ptr, (tcg_insn_unit *)value);
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

/* parse target specific constraints */
static const char *target_parse_constraint(TCGArgConstraint *ct,
                                           const char *ct_str, TCGType type)
{
    switch (*ct_str++) {
    case 'r': /* general registers */
        ct->ct |= TCG_CT_REG;
        ct->u.regs |= 0xffffffffu;
        break;
    case 'w': /* advsimd registers */
        ct->ct |= TCG_CT_REG;
        ct->u.regs |= 0xffffffff00000000ull;
        break;
    case 'l': /* qemu_ld / qemu_st address, data_reg */
        ct->ct |= TCG_CT_REG;
        ct->u.regs = 0xffffffffu;
#ifdef CONFIG_SOFTMMU
        /* x0 and x1 will be overwritten when reading the tlb entry,
           and x2, and x3 for helper args, better to avoid using them. */
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_X0);
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_X1);
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_X2);
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_X3);
#endif
        break;
    case 'A': /* Valid for arithmetic immediate (positive or negative).  */
        ct->ct |= TCG_CT_CONST_AIMM;
        break;
    case 'L': /* Valid for logical immediate.  */
        ct->ct |= TCG_CT_CONST_LIMM;
        break;
    case 'M': /* minus one */
        ct->ct |= TCG_CT_CONST_MONE;
        break;
    case 'O': /* vector orr/bic immediate */
        ct->ct |= TCG_CT_CONST_ORRI;
        break;
    case 'N': /* vector orr/bic immediate, inverted */
        ct->ct |= TCG_CT_CONST_ANDI;
        break;
    case 'Z': /* zero */
        ct->ct |= TCG_CT_CONST_ZERO;
        break;
    default:
        return NULL;
    }
    return ct_str;
}

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

static int tcg_target_const_match(tcg_target_long val, TCGType type,
                                  const TCGArgConstraint *arg_ct)
{
    int ct = arg_ct->ct;

    if (ct & TCG_CT_CONST) {
        return 1;
    }
    if (type == TCG_TYPE_I32) {
        val = (int32_t)val;
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

    /* Move wide immediate instructions.  */
    I3405_MOVN      = 0x12800000,
    I3405_MOVZ      = 0x52800000,
    I3405_MOVK      = 0x72800000,

    /* PC relative addressing instructions.  */
    I3406_ADR       = 0x10000000,
    I3406_ADRP      = 0x90000000,

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
    I3507_REV16     = 0x5ac00400,
    I3507_REV32     = 0x5ac00800,
    I3507_REV64     = 0x5ac00c00,

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

    /* AdvSIMD shift by immediate */
    I3614_SSHR      = 0x0f000400,
    I3614_SSRA      = 0x0f001400,
    I3614_SHL       = 0x0f005400,
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
    I3617_CMLE0     = 0x2e20a800,
    I3617_NOT       = 0x2e205800,
    I3617_ABS       = 0x0e20b800,
    I3617_NEG       = 0x2e20b800,

    /* System instructions.  */
    NOP             = 0xd503201f,
    DMB_ISH         = 0xd50338bf,
    DMB_LD          = 0x00000100,
    DMB_ST          = 0x00000200,
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

static void tcg_out_dupi_vec(TCGContext *s, TCGType type,
                             TCGReg rd, tcg_target_long v64)
{
    bool q = type == TCG_TYPE_V128;
    int cmode, imm8, i;

    /* Test all bytes equal first.  */
    if (v64 == dup_const(MO_8, v64)) {
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
    if (v64 == dup_const(MO_16, v64)) {
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
    } else if (v64 == dup_const(MO_32, v64)) {
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
    TCGReg temp = TCG_REG_TMP;

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

    case TCG_TYPE_V64:
    case TCG_TYPE_V128:
        tcg_debug_assert(rd >= 32);
        tcg_out_dupi_vec(s, type, rd, value);
        return;

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
        tcg_target_long disp = value - (intptr_t)s->code_ptr;
        if (disp == sextract64(disp, 0, 21)) {
            tcg_out_insn(s, 3406, ADR, rd, disp);
            return;
        }
        disp = (value >> 12) - ((intptr_t)s->code_ptr >> 12);
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
    t1 = t0 & ~(0xffffUL << s0);
    s1 = ctz64(t1) & (63 & -16);
    t2 = t1 & ~(0xffffUL << s1);
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
    tcg_out_movi(s, TCG_TYPE_I64, TCG_REG_TMP, offset);
    tcg_out_ldst_r(s, insn, rd, rn, TCG_TYPE_I64, TCG_REG_TMP);
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
    tcg_out_ubfm(s, ext, rd, rn, bits - (m & max), max - (m & max));
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
    int bits = ext ? 64 : 32;
    int max = bits - 1;
    tcg_out_extr(s, ext, rd, rn, rn, bits - (m & max));
}

static inline void tcg_out_dep(TCGContext *s, TCGType ext, TCGReg rd,
                               TCGReg rn, unsigned lsb, unsigned width)
{
    unsigned size = ext ? 64 : 32;
    unsigned a = (size - lsb) & (size - 1);
    unsigned b = width - 1;
    tcg_out_bfm(s, ext, rd, rn, a, b);
}

static void tcg_out_cmp(TCGContext *s, TCGType ext, TCGReg a,
                        tcg_target_long b, bool const_b)
{
    if (const_b) {
        /* Using CMP or CMN aliases.  */
        if (b >= 0) {
            tcg_out_insn(s, 3401, SUBSI, ext, TCG_REG_XZR, a, b);
        } else {
            tcg_out_insn(s, 3401, ADDSI, ext, TCG_REG_XZR, a, -b);
        }
    } else {
        /* Using CMP alias SUBS wzr, Wn, Wm */
        tcg_out_insn(s, 3502, SUBS, ext, TCG_REG_XZR, a, b);
    }
}

static inline void tcg_out_goto(TCGContext *s, tcg_insn_unit *target)
{
    ptrdiff_t offset = target - s->code_ptr;
    tcg_debug_assert(offset == sextract64(offset, 0, 26));
    tcg_out_insn(s, 3206, B, offset);
}

static inline void tcg_out_goto_long(TCGContext *s, tcg_insn_unit *target)
{
    ptrdiff_t offset = target - s->code_ptr;
    if (offset == sextract64(offset, 0, 26)) {
        tcg_out_insn(s, 3206, BL, offset);
    } else {
        tcg_out_movi(s, TCG_TYPE_I64, TCG_REG_TMP, (intptr_t)target);
        tcg_out_insn(s, 3207, BR, TCG_REG_TMP);
    }
}

static inline void tcg_out_callr(TCGContext *s, TCGReg reg)
{
    tcg_out_insn(s, 3207, BLR, reg);
}

static inline void tcg_out_call(TCGContext *s, tcg_insn_unit *target)
{
    ptrdiff_t offset = target - s->code_ptr;
    if (offset == sextract64(offset, 0, 26)) {
        tcg_out_insn(s, 3206, BL, offset);
    } else {
        tcg_out_movi(s, TCG_TYPE_I64, TCG_REG_TMP, (intptr_t)target);
        tcg_out_callr(s, TCG_REG_TMP);
    }
}

void tb_target_set_jmp_target(uintptr_t tc_ptr, uintptr_t jmp_addr,
                              uintptr_t addr)
{
    tcg_insn_unit i1, i2;
    TCGType rt = TCG_TYPE_I64;
    TCGReg  rd = TCG_REG_TMP;
    uint64_t pair;

    ptrdiff_t offset = addr - jmp_addr;

    if (offset == sextract64(offset, 0, 26)) {
        i1 = I3206_B | ((offset >> 2) & 0x3ffffff);
        i2 = NOP;
    } else {
        offset = (addr >> 12) - (jmp_addr >> 12);

        /* patch ADRP */
        i1 = I3406_ADRP | (offset & 3) << 29 | (offset & 0x1ffffc) << (5 - 2) | rd;
        /* patch ADDI */
        i2 = I3401_ADDI | rt << 31 | (addr & 0xfff) << 10 | rd << 5 | rd;
    }
    pair = (uint64_t)i2 << 32 | i1;
    atomic_set((uint64_t *)jmp_addr, pair);
    flush_icache_range(jmp_addr, jmp_addr + 8);
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
    intptr_t offset;
    bool need_cmp;

    if (b_const && b == 0 && (c == TCG_COND_EQ || c == TCG_COND_NE)) {
        need_cmp = false;
    } else {
        need_cmp = true;
        tcg_out_cmp(s, ext, a, b, b_const);
    }

    if (!l->has_value) {
        tcg_out_reloc(s, s->code_ptr, R_AARCH64_CONDBR19, l, 0);
        offset = tcg_in32(s) >> 5;
    } else {
        offset = l->u.value_ptr - s->code_ptr;
        tcg_debug_assert(offset == sextract64(offset, 0, 19));
    }

    if (need_cmp) {
        tcg_out_insn(s, 3202, B_C, c, offset);
    } else if (c == TCG_COND_EQ) {
        tcg_out_insn(s, 3201, CBZ, ext, a, offset);
    } else {
        tcg_out_insn(s, 3201, CBNZ, ext, a, offset);
    }
}

static inline void tcg_out_rev64(TCGContext *s, TCGReg rd, TCGReg rn)
{
    tcg_out_insn(s, 3507, REV64, TCG_TYPE_I64, rd, rn);
}

static inline void tcg_out_rev32(TCGContext *s, TCGReg rd, TCGReg rn)
{
    tcg_out_insn(s, 3507, REV32, TCG_TYPE_I32, rd, rn);
}

static inline void tcg_out_rev16(TCGContext *s, TCGReg rd, TCGReg rn)
{
    tcg_out_insn(s, 3507, REV16, TCG_TYPE_I32, rd, rn);
}

static inline void tcg_out_sxt(TCGContext *s, TCGType ext, MemOp s_bits,
                               TCGReg rd, TCGReg rn)
{
    /* Using ALIASes SXTB, SXTH, SXTW, of SBFM Xd, Xn, #0, #7|15|31 */
    int bits = (8 << s_bits) - 1;
    tcg_out_sbfm(s, ext, rd, rn, 0, bits);
}

static inline void tcg_out_uxt(TCGContext *s, MemOp s_bits,
                               TCGReg rd, TCGReg rn)
{
    /* Using ALIASes UXTB, UXTH of UBFM Wd, Wn, #0, #7|15 */
    int bits = (8 << s_bits) - 1;
    tcg_out_ubfm(s, 0, rd, rn, 0, bits);
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

static inline void tcg_out_addsub2(TCGContext *s, TCGType ext, TCGReg rl,
                                   TCGReg rh, TCGReg al, TCGReg ah,
                                   tcg_target_long bl, tcg_target_long bh,
                                   bool const_bl, bool const_bh, bool sub)
{
    TCGReg orig_rl = rl;
    AArch64Insn insn;

    if (rl == ah || (!const_bh && rl == bh)) {
        rl = TCG_REG_TMP;
    }

    if (const_bl) {
        insn = I3401_ADDSI;
        if ((bl < 0) ^ sub) {
            insn = I3401_SUBSI;
            bl = -bl;
        }
        if (unlikely(al == TCG_REG_XZR)) {
            /* ??? We want to allow al to be zero for the benefit of
               negation via subtraction.  However, that leaves open the
               possibility of adding 0+const in the low part, and the
               immediate add instructions encode XSP not XZR.  Don't try
               anything more elaborate here than loading another zero.  */
            al = TCG_REG_TMP;
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
        a1 = TCG_REG_TMP;
        tcg_out_insn(s, 3507, RBIT, ext, a1, a0);
    }
    if (const_b && b == (ext ? 64 : 32)) {
        tcg_out_insn(s, 3507, CLZ, ext, d, a1);
    } else {
        AArch64Insn sel = I3506_CSEL;

        tcg_out_cmp(s, ext, a0, 0, 1);
        tcg_out_insn(s, 3507, CLZ, ext, TCG_REG_TMP, a1);

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
        tcg_out_insn_3506(s, sel, ext, d, TCG_REG_TMP, b, TCG_COND_NE);
    }
}

#ifdef CONFIG_SOFTMMU
#include "tcg-ldst.inc.c"

/* helper signature: helper_ret_ld_mmu(CPUState *env, target_ulong addr,
 *                                     TCGMemOpIdx oi, uintptr_t ra)
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
 *                                     uintxx_t val, TCGMemOpIdx oi,
 *                                     uintptr_t ra)
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

static inline void tcg_out_adr(TCGContext *s, TCGReg rd, void *target)
{
    ptrdiff_t offset = tcg_pcrel_diff(s, target);
    tcg_debug_assert(offset == sextract64(offset, 0, 21));
    tcg_out_insn(s, 3406, ADR, rd, offset);
}

static bool tcg_out_qemu_ld_slow_path(TCGContext *s, TCGLabelQemuLdst *lb)
{
    TCGMemOpIdx oi = lb->oi;
    MemOp opc = get_memop(oi);
    MemOp size = opc & MO_SIZE;

    if (!reloc_pc19(lb->label_ptr[0], s->code_ptr)) {
        return false;
    }

    tcg_out_mov(s, TCG_TYPE_PTR, TCG_REG_X0, TCG_AREG0);
    tcg_out_mov(s, TARGET_LONG_BITS == 64, TCG_REG_X1, lb->addrlo_reg);
    tcg_out_movi(s, TCG_TYPE_I32, TCG_REG_X2, oi);
    tcg_out_adr(s, TCG_REG_X3, lb->raddr);
    tcg_out_call(s, qemu_ld_helpers[opc & (MO_BSWAP | MO_SIZE)]);
    if (opc & MO_SIGN) {
        tcg_out_sxt(s, lb->type, size, lb->datalo_reg, TCG_REG_X0);
    } else {
        tcg_out_mov(s, size == MO_64, lb->datalo_reg, TCG_REG_X0);
    }

    tcg_out_goto(s, lb->raddr);
    return true;
}

static bool tcg_out_qemu_st_slow_path(TCGContext *s, TCGLabelQemuLdst *lb)
{
    TCGMemOpIdx oi = lb->oi;
    MemOp opc = get_memop(oi);
    MemOp size = opc & MO_SIZE;

    if (!reloc_pc19(lb->label_ptr[0], s->code_ptr)) {
        return false;
    }

    tcg_out_mov(s, TCG_TYPE_PTR, TCG_REG_X0, TCG_AREG0);
    tcg_out_mov(s, TARGET_LONG_BITS == 64, TCG_REG_X1, lb->addrlo_reg);
    tcg_out_mov(s, size == MO_64, TCG_REG_X2, lb->datalo_reg);
    tcg_out_movi(s, TCG_TYPE_I32, TCG_REG_X3, oi);
    tcg_out_adr(s, TCG_REG_X4, lb->raddr);
    tcg_out_call(s, qemu_st_helpers[opc & (MO_BSWAP | MO_SIZE)]);
    tcg_out_goto(s, lb->raddr);
    return true;
}

static void add_qemu_ldst_label(TCGContext *s, bool is_ld, TCGMemOpIdx oi,
                                TCGType ext, TCGReg data_reg, TCGReg addr_reg,
                                tcg_insn_unit *raddr, tcg_insn_unit *label_ptr)
{
    TCGLabelQemuLdst *label = new_ldst_label(s);

    label->is_ld = is_ld;
    label->oi = oi;
    label->type = ext;
    label->datalo_reg = data_reg;
    label->addrlo_reg = addr_reg;
    label->raddr = raddr;
    label->label_ptr[0] = label_ptr;
}

/* We expect to use a 7-bit scaled negative offset from ENV.  */
QEMU_BUILD_BUG_ON(TLB_MASK_TABLE_OFS(0) > 0);
QEMU_BUILD_BUG_ON(TLB_MASK_TABLE_OFS(0) < -512);

/* These offsets are built into the LDP below.  */
QEMU_BUILD_BUG_ON(offsetof(CPUTLBDescFast, mask) != 0);
QEMU_BUILD_BUG_ON(offsetof(CPUTLBDescFast, table) != 8);

/* Load and compare a TLB entry, emitting the conditional jump to the
   slow path for the failure case, which will be patched later when finalizing
   the slow path. Generated code returns the host addend in X1,
   clobbers X0,X2,X3,TMP. */
static void tcg_out_tlb_read(TCGContext *s, TCGReg addr_reg, MemOp opc,
                             tcg_insn_unit **label_ptr, int mem_index,
                             bool is_read)
{
    unsigned a_bits = get_alignment_bits(opc);
    unsigned s_bits = opc & MO_SIZE;
    unsigned a_mask = (1u << a_bits) - 1;
    unsigned s_mask = (1u << s_bits) - 1;
    TCGReg x3;
    TCGType mask_type;
    uint64_t compare_mask;

    mask_type = (TARGET_PAGE_BITS + CPU_TLB_DYN_MAX_BITS > 32
                 ? TCG_TYPE_I64 : TCG_TYPE_I32);

    /* Load env_tlb(env)->f[mmu_idx].{mask,table} into {x0,x1}.  */
    tcg_out_insn(s, 3314, LDP, TCG_REG_X0, TCG_REG_X1, TCG_AREG0,
                 TLB_MASK_TABLE_OFS(mem_index), 1, 0);

    /* Extract the TLB index from the address into X0.  */
    tcg_out_insn(s, 3502S, AND_LSR, mask_type == TCG_TYPE_I64,
                 TCG_REG_X0, TCG_REG_X0, addr_reg,
                 TARGET_PAGE_BITS - CPU_TLB_ENTRY_BITS);

    /* Add the tlb_table pointer, creating the CPUTLBEntry address into X1.  */
    tcg_out_insn(s, 3502, ADD, 1, TCG_REG_X1, TCG_REG_X1, TCG_REG_X0);

    /* Load the tlb comparator into X0, and the fast path addend into X1.  */
    tcg_out_ld(s, TCG_TYPE_TL, TCG_REG_X0, TCG_REG_X1, is_read
               ? offsetof(CPUTLBEntry, addr_read)
               : offsetof(CPUTLBEntry, addr_write));
    tcg_out_ld(s, TCG_TYPE_PTR, TCG_REG_X1, TCG_REG_X1,
               offsetof(CPUTLBEntry, addend));

    /* For aligned accesses, we check the first byte and include the alignment
       bits within the address.  For unaligned access, we check that we don't
       cross pages using the address of the last byte of the access.  */
    if (a_bits >= s_bits) {
        x3 = addr_reg;
    } else {
        tcg_out_insn(s, 3401, ADDI, TARGET_LONG_BITS == 64,
                     TCG_REG_X3, addr_reg, s_mask - a_mask);
        x3 = TCG_REG_X3;
    }
    compare_mask = (uint64_t)TARGET_PAGE_MASK | a_mask;

    /* Store the page mask part of the address into X3.  */
    tcg_out_logicali(s, I3404_ANDI, TARGET_LONG_BITS == 64,
                     TCG_REG_X3, x3, compare_mask);

    /* Perform the address comparison. */
    tcg_out_cmp(s, TARGET_LONG_BITS == 64, TCG_REG_X0, TCG_REG_X3, 0);

    /* If not equal, we jump to the slow path. */
    *label_ptr = s->code_ptr;
    tcg_out_insn(s, 3202, B_C, TCG_COND_NE, 0);
}

#endif /* CONFIG_SOFTMMU */

static void tcg_out_qemu_ld_direct(TCGContext *s, MemOp memop, TCGType ext,
                                   TCGReg data_r, TCGReg addr_r,
                                   TCGType otype, TCGReg off_r)
{
    const MemOp bswap = memop & MO_BSWAP;

    switch (memop & MO_SSIZE) {
    case MO_UB:
        tcg_out_ldst_r(s, I3312_LDRB, data_r, addr_r, otype, off_r);
        break;
    case MO_SB:
        tcg_out_ldst_r(s, ext ? I3312_LDRSBX : I3312_LDRSBW,
                       data_r, addr_r, otype, off_r);
        break;
    case MO_UW:
        tcg_out_ldst_r(s, I3312_LDRH, data_r, addr_r, otype, off_r);
        if (bswap) {
            tcg_out_rev16(s, data_r, data_r);
        }
        break;
    case MO_SW:
        if (bswap) {
            tcg_out_ldst_r(s, I3312_LDRH, data_r, addr_r, otype, off_r);
            tcg_out_rev16(s, data_r, data_r);
            tcg_out_sxt(s, ext, MO_16, data_r, data_r);
        } else {
            tcg_out_ldst_r(s, (ext ? I3312_LDRSHX : I3312_LDRSHW),
                           data_r, addr_r, otype, off_r);
        }
        break;
    case MO_UL:
        tcg_out_ldst_r(s, I3312_LDRW, data_r, addr_r, otype, off_r);
        if (bswap) {
            tcg_out_rev32(s, data_r, data_r);
        }
        break;
    case MO_SL:
        if (bswap) {
            tcg_out_ldst_r(s, I3312_LDRW, data_r, addr_r, otype, off_r);
            tcg_out_rev32(s, data_r, data_r);
            tcg_out_sxt(s, TCG_TYPE_I64, MO_32, data_r, data_r);
        } else {
            tcg_out_ldst_r(s, I3312_LDRSWX, data_r, addr_r, otype, off_r);
        }
        break;
    case MO_Q:
        tcg_out_ldst_r(s, I3312_LDRX, data_r, addr_r, otype, off_r);
        if (bswap) {
            tcg_out_rev64(s, data_r, data_r);
        }
        break;
    default:
        tcg_abort();
    }
}

static void tcg_out_qemu_st_direct(TCGContext *s, MemOp memop,
                                   TCGReg data_r, TCGReg addr_r,
                                   TCGType otype, TCGReg off_r)
{
    const MemOp bswap = memop & MO_BSWAP;

    switch (memop & MO_SIZE) {
    case MO_8:
        tcg_out_ldst_r(s, I3312_STRB, data_r, addr_r, otype, off_r);
        break;
    case MO_16:
        if (bswap && data_r != TCG_REG_XZR) {
            tcg_out_rev16(s, TCG_REG_TMP, data_r);
            data_r = TCG_REG_TMP;
        }
        tcg_out_ldst_r(s, I3312_STRH, data_r, addr_r, otype, off_r);
        break;
    case MO_32:
        if (bswap && data_r != TCG_REG_XZR) {
            tcg_out_rev32(s, TCG_REG_TMP, data_r);
            data_r = TCG_REG_TMP;
        }
        tcg_out_ldst_r(s, I3312_STRW, data_r, addr_r, otype, off_r);
        break;
    case MO_64:
        if (bswap && data_r != TCG_REG_XZR) {
            tcg_out_rev64(s, TCG_REG_TMP, data_r);
            data_r = TCG_REG_TMP;
        }
        tcg_out_ldst_r(s, I3312_STRX, data_r, addr_r, otype, off_r);
        break;
    default:
        tcg_abort();
    }
}

static void tcg_out_qemu_ld(TCGContext *s, TCGReg data_reg, TCGReg addr_reg,
                            TCGMemOpIdx oi, TCGType ext)
{
    MemOp memop = get_memop(oi);
    const TCGType otype = TARGET_LONG_BITS == 64 ? TCG_TYPE_I64 : TCG_TYPE_I32;
#ifdef CONFIG_SOFTMMU
    unsigned mem_index = get_mmuidx(oi);
    tcg_insn_unit *label_ptr;

    tcg_out_tlb_read(s, addr_reg, memop, &label_ptr, mem_index, 1);
    tcg_out_qemu_ld_direct(s, memop, ext, data_reg,
                           TCG_REG_X1, otype, addr_reg);
    add_qemu_ldst_label(s, true, oi, ext, data_reg, addr_reg,
                        s->code_ptr, label_ptr);
#else /* !CONFIG_SOFTMMU */
    if (USE_GUEST_BASE) {
        tcg_out_qemu_ld_direct(s, memop, ext, data_reg,
                               TCG_REG_GUEST_BASE, otype, addr_reg);
    } else {
        tcg_out_qemu_ld_direct(s, memop, ext, data_reg,
                               addr_reg, TCG_TYPE_I64, TCG_REG_XZR);
    }
#endif /* CONFIG_SOFTMMU */
}

static void tcg_out_qemu_st(TCGContext *s, TCGReg data_reg, TCGReg addr_reg,
                            TCGMemOpIdx oi)
{
    MemOp memop = get_memop(oi);
    const TCGType otype = TARGET_LONG_BITS == 64 ? TCG_TYPE_I64 : TCG_TYPE_I32;
#ifdef CONFIG_SOFTMMU
    unsigned mem_index = get_mmuidx(oi);
    tcg_insn_unit *label_ptr;

    tcg_out_tlb_read(s, addr_reg, memop, &label_ptr, mem_index, 0);
    tcg_out_qemu_st_direct(s, memop, data_reg,
                           TCG_REG_X1, otype, addr_reg);
    add_qemu_ldst_label(s, false, oi, (memop & MO_SIZE)== MO_64,
                        data_reg, addr_reg, s->code_ptr, label_ptr);
#else /* !CONFIG_SOFTMMU */
    if (USE_GUEST_BASE) {
        tcg_out_qemu_st_direct(s, memop, data_reg,
                               TCG_REG_GUEST_BASE, otype, addr_reg);
    } else {
        tcg_out_qemu_st_direct(s, memop, data_reg,
                               addr_reg, TCG_TYPE_I64, TCG_REG_XZR);
    }
#endif /* CONFIG_SOFTMMU */
}

static tcg_insn_unit *tb_ret_addr;

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
    case INDEX_op_exit_tb:
        /* Reuse the zeroing that exists for goto_ptr.  */
        if (a0 == 0) {
            tcg_out_goto_long(s, s->code_gen_epilogue);
        } else {
            tcg_out_movi(s, TCG_TYPE_I64, TCG_REG_X0, a0);
            tcg_out_goto_long(s, tb_ret_addr);
        }
        break;

    case INDEX_op_goto_tb:
        if (s->tb_jmp_insn_offset != NULL) {
            /* TCG_TARGET_HAS_direct_jump */
            /* Ensure that ADRP+ADD are 8-byte aligned so that an atomic
               write can be used to patch the target address. */
            if ((uintptr_t)s->code_ptr & 7) {
                tcg_out32(s, NOP);
            }
            s->tb_jmp_insn_offset[a0] = tcg_current_code_size(s);
            /* actual branch destination will be patched by
               tb_target_set_jmp_target later. */
            tcg_out_insn(s, 3406, ADRP, TCG_REG_TMP, 0);
            tcg_out_insn(s, 3401, ADDI, TCG_TYPE_I64, TCG_REG_TMP, TCG_REG_TMP, 0);
        } else {
            /* !TCG_TARGET_HAS_direct_jump */
            tcg_debug_assert(s->tb_jmp_target_addr != NULL);
            intptr_t offset = tcg_pcrel_diff(s, (s->tb_jmp_target_addr + a0)) >> 2;
            tcg_out_insn(s, 3305, LDR, offset, TCG_REG_TMP);
        }
        tcg_out_insn(s, 3207, BR, TCG_REG_TMP);
        set_jmp_reset_offset(s, a0);
        break;

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
        tcg_out_insn(s, 3508, SDIV, ext, TCG_REG_TMP, a1, a2);
        tcg_out_insn(s, 3509, MSUB, ext, a0, TCG_REG_TMP, a2, a1);
        break;
    case INDEX_op_remu_i64:
    case INDEX_op_remu_i32:
        tcg_out_insn(s, 3508, UDIV, ext, TCG_REG_TMP, a1, a2);
        tcg_out_insn(s, 3509, MSUB, ext, a0, TCG_REG_TMP, a2, a1);
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
            tcg_out_insn(s, 3502, SUB, 0, TCG_REG_TMP, TCG_REG_XZR, a2);
            tcg_out_insn(s, 3508, RORV, ext, a0, a1, TCG_REG_TMP);
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
        tcg_out_cmp(s, ext, a1, a2, c2);
        /* Use CSET alias of CSINC Wd, WZR, WZR, invert(cond).  */
        tcg_out_insn(s, 3506, CSINC, TCG_TYPE_I32, a0, TCG_REG_XZR,
                     TCG_REG_XZR, tcg_invert_cond(args[3]));
        break;

    case INDEX_op_movcond_i32:
        a2 = (int32_t)a2;
        /* FALLTHRU */
    case INDEX_op_movcond_i64:
        tcg_out_cmp(s, ext, a1, a2, c2);
        tcg_out_insn(s, 3506, CSEL, ext, a0, REG0(3), REG0(4), args[5]);
        break;

    case INDEX_op_qemu_ld_i32:
    case INDEX_op_qemu_ld_i64:
        tcg_out_qemu_ld(s, a0, a1, a2, ext);
        break;
    case INDEX_op_qemu_st_i32:
    case INDEX_op_qemu_st_i64:
        tcg_out_qemu_st(s, REG0(0), a1, a2);
        break;

    case INDEX_op_bswap64_i64:
        tcg_out_rev64(s, a0, a1);
        break;
    case INDEX_op_bswap32_i64:
    case INDEX_op_bswap32_i32:
        tcg_out_rev32(s, a0, a1);
        break;
    case INDEX_op_bswap16_i64:
    case INDEX_op_bswap16_i32:
        tcg_out_rev16(s, a0, a1);
        break;

    case INDEX_op_ext8s_i64:
    case INDEX_op_ext8s_i32:
        tcg_out_sxt(s, ext, MO_8, a0, a1);
        break;
    case INDEX_op_ext16s_i64:
    case INDEX_op_ext16s_i32:
        tcg_out_sxt(s, ext, MO_16, a0, a1);
        break;
    case INDEX_op_ext_i32_i64:
    case INDEX_op_ext32s_i64:
        tcg_out_sxt(s, TCG_TYPE_I64, MO_32, a0, a1);
        break;
    case INDEX_op_ext8u_i64:
    case INDEX_op_ext8u_i32:
        tcg_out_uxt(s, MO_8, a0, a1);
        break;
    case INDEX_op_ext16u_i64:
    case INDEX_op_ext16u_i32:
        tcg_out_uxt(s, MO_16, a0, a1);
        break;
    case INDEX_op_extu_i32_i64:
    case INDEX_op_ext32u_i64:
        tcg_out_movr(s, TCG_TYPE_I32, a0, a1);
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
    case INDEX_op_movi_i32: /* Always emitted via tcg_out_movi.  */
    case INDEX_op_movi_i64:
    case INDEX_op_call:     /* Always emitted via tcg_out_call.  */
    default:
        g_assert_not_reached();
    }

#undef REG0
}

static void tcg_out_vec_op(TCGContext *s, TCGOpcode opc,
                           unsigned vecl, unsigned vece,
                           const TCGArg *args, const int *const_args)
{
    static const AArch64Insn cmp_insn[16] = {
        [TCG_COND_EQ] = I3616_CMEQ,
        [TCG_COND_GT] = I3616_CMGT,
        [TCG_COND_GE] = I3616_CMGE,
        [TCG_COND_GTU] = I3616_CMHI,
        [TCG_COND_GEU] = I3616_CMHS,
    };
    static const AArch64Insn cmp0_insn[16] = {
        [TCG_COND_EQ] = I3617_CMEQ0,
        [TCG_COND_GT] = I3617_CMGT0,
        [TCG_COND_GE] = I3617_CMGE0,
        [TCG_COND_LT] = I3617_CMLT0,
        [TCG_COND_LE] = I3617_CMLE0,
    };

    TCGType type = vecl + TCG_TYPE_V64;
    unsigned is_q = vecl;
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
        tcg_out_insn(s, 3616, ADD, is_q, vece, a0, a1, a2);
        break;
    case INDEX_op_sub_vec:
        tcg_out_insn(s, 3616, SUB, is_q, vece, a0, a1, a2);
        break;
    case INDEX_op_mul_vec:
        tcg_out_insn(s, 3616, MUL, is_q, vece, a0, a1, a2);
        break;
    case INDEX_op_neg_vec:
        tcg_out_insn(s, 3617, NEG, is_q, vece, a0, a1);
        break;
    case INDEX_op_abs_vec:
        tcg_out_insn(s, 3617, ABS, is_q, vece, a0, a1);
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
        tcg_out_insn(s, 3616, SQADD, is_q, vece, a0, a1, a2);
        break;
    case INDEX_op_sssub_vec:
        tcg_out_insn(s, 3616, SQSUB, is_q, vece, a0, a1, a2);
        break;
    case INDEX_op_usadd_vec:
        tcg_out_insn(s, 3616, UQADD, is_q, vece, a0, a1, a2);
        break;
    case INDEX_op_ussub_vec:
        tcg_out_insn(s, 3616, UQSUB, is_q, vece, a0, a1, a2);
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
        tcg_out_insn(s, 3614, SHL, is_q, a0, a1, a2 + (8 << vece));
        break;
    case INDEX_op_shri_vec:
        tcg_out_insn(s, 3614, USHR, is_q, a0, a1, (16 << vece) - a2);
        break;
    case INDEX_op_sari_vec:
        tcg_out_insn(s, 3614, SSHR, is_q, a0, a1, (16 << vece) - a2);
        break;
    case INDEX_op_shlv_vec:
        tcg_out_insn(s, 3616, USHL, is_q, vece, a0, a1, a2);
        break;
    case INDEX_op_aa64_sshl_vec:
        tcg_out_insn(s, 3616, SSHL, is_q, vece, a0, a1, a2);
        break;
    case INDEX_op_cmp_vec:
        {
            TCGCond cond = args[3];
            AArch64Insn insn;

            if (cond == TCG_COND_NE) {
                if (const_args[2]) {
                    tcg_out_insn(s, 3616, CMTST, is_q, vece, a0, a1, a1);
                } else {
                    tcg_out_insn(s, 3616, CMEQ, is_q, vece, a0, a1, a2);
                    tcg_out_insn(s, 3617, NOT, is_q, 0, a0, a0);
                }
            } else {
                if (const_args[2]) {
                    insn = cmp0_insn[cond];
                    if (insn) {
                        tcg_out_insn_3617(s, insn, is_q, vece, a0, a1);
                        break;
                    }
                    tcg_out_dupi_vec(s, type, TCG_VEC_TMP, 0);
                    a2 = TCG_VEC_TMP;
                }
                insn = cmp_insn[cond];
                if (insn == 0) {
                    TCGArg t;
                    t = a1, a1 = a2, a2 = t;
                    cond = tcg_swap_cond(cond);
                    insn = cmp_insn[cond];
                    tcg_debug_assert(insn != 0);
                }
                tcg_out_insn_3616(s, insn, is_q, vece, a0, a1, a2);
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
    case INDEX_op_dupi_vec: /* Always emitted via tcg_out_movi.  */
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
    case INDEX_op_shrv_vec:
    case INDEX_op_sarv_vec:
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
    TCGv_vec v0, v1, v2, t1;

    va_start(va, a0);
    v0 = temp_tcgv_vec(arg_temp(a0));
    v1 = temp_tcgv_vec(arg_temp(va_arg(va, TCGArg)));
    v2 = temp_tcgv_vec(arg_temp(va_arg(va, TCGArg)));

    switch (opc) {
    case INDEX_op_shrv_vec:
    case INDEX_op_sarv_vec:
        /* Right shifts are negative left shifts for AArch64.  */
        t1 = tcg_temp_new_vec(type);
        tcg_gen_neg_vec(vece, t1, v2);
        opc = (opc == INDEX_op_shrv_vec
               ? INDEX_op_shlv_vec : INDEX_op_aa64_sshl_vec);
        vec_gen_3(opc, type, vece, tcgv_vec_arg(v0),
                  tcgv_vec_arg(v1), tcgv_vec_arg(t1));
        tcg_temp_free_vec(t1);
        break;

    default:
        g_assert_not_reached();
    }

    va_end(va);
}

static const TCGTargetOpDef *tcg_target_op_def(TCGOpcode op)
{
    static const TCGTargetOpDef r = { .args_ct_str = { "r" } };
    static const TCGTargetOpDef r_r = { .args_ct_str = { "r", "r" } };
    static const TCGTargetOpDef w_w = { .args_ct_str = { "w", "w" } };
    static const TCGTargetOpDef w_r = { .args_ct_str = { "w", "r" } };
    static const TCGTargetOpDef w_wr = { .args_ct_str = { "w", "wr" } };
    static const TCGTargetOpDef r_l = { .args_ct_str = { "r", "l" } };
    static const TCGTargetOpDef r_rA = { .args_ct_str = { "r", "rA" } };
    static const TCGTargetOpDef rZ_r = { .args_ct_str = { "rZ", "r" } };
    static const TCGTargetOpDef lZ_l = { .args_ct_str = { "lZ", "l" } };
    static const TCGTargetOpDef r_r_r = { .args_ct_str = { "r", "r", "r" } };
    static const TCGTargetOpDef w_w_w = { .args_ct_str = { "w", "w", "w" } };
    static const TCGTargetOpDef w_w_wO = { .args_ct_str = { "w", "w", "wO" } };
    static const TCGTargetOpDef w_w_wN = { .args_ct_str = { "w", "w", "wN" } };
    static const TCGTargetOpDef w_w_wZ = { .args_ct_str = { "w", "w", "wZ" } };
    static const TCGTargetOpDef r_r_ri = { .args_ct_str = { "r", "r", "ri" } };
    static const TCGTargetOpDef r_r_rA = { .args_ct_str = { "r", "r", "rA" } };
    static const TCGTargetOpDef r_r_rL = { .args_ct_str = { "r", "r", "rL" } };
    static const TCGTargetOpDef r_r_rAL
        = { .args_ct_str = { "r", "r", "rAL" } };
    static const TCGTargetOpDef dep
        = { .args_ct_str = { "r", "0", "rZ" } };
    static const TCGTargetOpDef ext2
        = { .args_ct_str = { "r", "rZ", "rZ" } };
    static const TCGTargetOpDef movc
        = { .args_ct_str = { "r", "r", "rA", "rZ", "rZ" } };
    static const TCGTargetOpDef add2
        = { .args_ct_str = { "r", "r", "rZ", "rZ", "rA", "rMZ" } };
    static const TCGTargetOpDef w_w_w_w
        = { .args_ct_str = { "w", "w", "w", "w" } };

    switch (op) {
    case INDEX_op_goto_ptr:
        return &r;

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
    case INDEX_op_sub_i32:
    case INDEX_op_sub_i64:
    case INDEX_op_setcond_i32:
    case INDEX_op_setcond_i64:
        return &r_r_rA;

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
        return &r_r_r;

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
        return &r_r_rL;

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
        return &r_r_ri;

    case INDEX_op_clz_i32:
    case INDEX_op_ctz_i32:
    case INDEX_op_clz_i64:
    case INDEX_op_ctz_i64:
        return &r_r_rAL;

    case INDEX_op_brcond_i32:
    case INDEX_op_brcond_i64:
        return &r_rA;

    case INDEX_op_movcond_i32:
    case INDEX_op_movcond_i64:
        return &movc;

    case INDEX_op_qemu_ld_i32:
    case INDEX_op_qemu_ld_i64:
        return &r_l;
    case INDEX_op_qemu_st_i32:
    case INDEX_op_qemu_st_i64:
        return &lZ_l;

    case INDEX_op_deposit_i32:
    case INDEX_op_deposit_i64:
        return &dep;

    case INDEX_op_extract2_i32:
    case INDEX_op_extract2_i64:
        return &ext2;

    case INDEX_op_add2_i32:
    case INDEX_op_add2_i64:
    case INDEX_op_sub2_i32:
    case INDEX_op_sub2_i64:
        return &add2;

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
        return &w_w_w;
    case INDEX_op_not_vec:
    case INDEX_op_neg_vec:
    case INDEX_op_abs_vec:
    case INDEX_op_shli_vec:
    case INDEX_op_shri_vec:
    case INDEX_op_sari_vec:
        return &w_w;
    case INDEX_op_ld_vec:
    case INDEX_op_st_vec:
    case INDEX_op_dupm_vec:
        return &w_r;
    case INDEX_op_dup_vec:
        return &w_wr;
    case INDEX_op_or_vec:
    case INDEX_op_andc_vec:
        return &w_w_wO;
    case INDEX_op_and_vec:
    case INDEX_op_orc_vec:
        return &w_w_wN;
    case INDEX_op_cmp_vec:
        return &w_w_wZ;
    case INDEX_op_bitsel_vec:
        return &w_w_w_w;

    default:
        return NULL;
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
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_TMP);
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_X18); /* platform register */
    tcg_regset_set_reg(s->reserved_regs, TCG_VEC_TMP);
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

#if !defined(CONFIG_SOFTMMU)
    if (USE_GUEST_BASE) {
        tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_GUEST_BASE, guest_base);
        tcg_regset_set_reg(s->reserved_regs, TCG_REG_GUEST_BASE);
    }
#endif

    tcg_out_mov(s, TCG_TYPE_PTR, TCG_AREG0, tcg_target_call_iarg_regs[0]);
    tcg_out_insn(s, 3207, BR, tcg_target_call_iarg_regs[1]);

    /*
     * Return path for goto_ptr. Set return value to 0, a-la exit_tb,
     * and fall through to the rest of the epilogue.
     */
    s->code_gen_epilogue = s->code_ptr;
    tcg_out_movi(s, TCG_TYPE_REG, TCG_REG_X0, 0);

    /* TB epilogue */
    tb_ret_addr = s->code_ptr;

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

void tcg_register_jit(void *buf, size_t buf_size)
{
    tcg_register_jit_int(buf, buf_size, &debug_frame, sizeof(debug_frame));
}

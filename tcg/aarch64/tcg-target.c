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

#include "tcg-be-ldst.h"
#include "qemu/bitops.h"

/* We're going to re-use TCGType in setting of the SF bit, which controls
   the size of the operation performed.  If we know the values match, it
   makes things much cleaner.  */
QEMU_BUILD_BUG_ON(TCG_TYPE_I32 != 0 || TCG_TYPE_I64 != 1);

#ifndef NDEBUG
static const char * const tcg_target_reg_names[TCG_TARGET_NB_REGS] = {
    "%x0", "%x1", "%x2", "%x3", "%x4", "%x5", "%x6", "%x7",
    "%x8", "%x9", "%x10", "%x11", "%x12", "%x13", "%x14", "%x15",
    "%x16", "%x17", "%x18", "%x19", "%x20", "%x21", "%x22", "%x23",
    "%x24", "%x25", "%x26", "%x27", "%x28", "%fp", "%x30", "%sp",
};
#endif /* NDEBUG */

static const int tcg_target_reg_alloc_order[] = {
    TCG_REG_X20, TCG_REG_X21, TCG_REG_X22, TCG_REG_X23,
    TCG_REG_X24, TCG_REG_X25, TCG_REG_X26, TCG_REG_X27,
    TCG_REG_X28, /* we will reserve this for GUEST_BASE if configured */

    TCG_REG_X8, TCG_REG_X9, TCG_REG_X10, TCG_REG_X11,
    TCG_REG_X12, TCG_REG_X13, TCG_REG_X14, TCG_REG_X15,
    TCG_REG_X16, TCG_REG_X17,

    TCG_REG_X0, TCG_REG_X1, TCG_REG_X2, TCG_REG_X3,
    TCG_REG_X4, TCG_REG_X5, TCG_REG_X6, TCG_REG_X7,

    /* X18 reserved by system */
    /* X19 reserved for AREG0 */
    /* X29 reserved as fp */
    /* X30 reserved as temporary */
};

static const int tcg_target_call_iarg_regs[8] = {
    TCG_REG_X0, TCG_REG_X1, TCG_REG_X2, TCG_REG_X3,
    TCG_REG_X4, TCG_REG_X5, TCG_REG_X6, TCG_REG_X7
};
static const int tcg_target_call_oarg_regs[1] = {
    TCG_REG_X0
};

#define TCG_REG_TMP TCG_REG_X30

#ifndef CONFIG_SOFTMMU
# ifdef CONFIG_USE_GUEST_BASE
#  define TCG_REG_GUEST_BASE TCG_REG_X28
# else
#  define TCG_REG_GUEST_BASE TCG_REG_XZR
# endif
#endif

static inline void reloc_pc26(tcg_insn_unit *code_ptr, tcg_insn_unit *target)
{
    ptrdiff_t offset = target - code_ptr;
    assert(offset == sextract64(offset, 0, 26));
    /* read instruction, mask away previous PC_REL26 parameter contents,
       set the proper offset, then write back the instruction. */
    *code_ptr = deposit32(*code_ptr, 0, 26, offset);
}

static inline void reloc_pc19(tcg_insn_unit *code_ptr, tcg_insn_unit *target)
{
    ptrdiff_t offset = target - code_ptr;
    assert(offset == sextract64(offset, 0, 19));
    *code_ptr = deposit32(*code_ptr, 5, 19, offset);
}

static inline void patch_reloc(tcg_insn_unit *code_ptr, int type,
                               intptr_t value, intptr_t addend)
{
    assert(addend == 0);
    switch (type) {
    case R_AARCH64_JUMP26:
    case R_AARCH64_CALL26:
        reloc_pc26(code_ptr, (tcg_insn_unit *)value);
        break;
    case R_AARCH64_CONDBR19:
        reloc_pc19(code_ptr, (tcg_insn_unit *)value);
        break;
    default:
        tcg_abort();
    }
}

#define TCG_CT_CONST_AIMM 0x100
#define TCG_CT_CONST_LIMM 0x200
#define TCG_CT_CONST_ZERO 0x400
#define TCG_CT_CONST_MONE 0x800

/* parse target specific constraints */
static int target_parse_constraint(TCGArgConstraint *ct,
                                   const char **pct_str)
{
    const char *ct_str = *pct_str;

    switch (ct_str[0]) {
    case 'r':
        ct->ct |= TCG_CT_REG;
        tcg_regset_set32(ct->u.regs, 0, (1ULL << TCG_TARGET_NB_REGS) - 1);
        break;
    case 'l': /* qemu_ld / qemu_st address, data_reg */
        ct->ct |= TCG_CT_REG;
        tcg_regset_set32(ct->u.regs, 0, (1ULL << TCG_TARGET_NB_REGS) - 1);
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
    case 'Z': /* zero */
        ct->ct |= TCG_CT_CONST_ZERO;
        break;
    default:
        return -1;
    }

    ct_str++;
    *pct_str = ct_str;
    return 0;
}

static inline bool is_aimm(uint64_t val)
{
    return (val & ~0xfff) == 0 || (val & ~0xfff000) == 0;
}

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

    I3312_TO_I3310  = 0x00206800,
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

    /* Data-processing (1 source) instructions.  */
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
} AArch64Insn;

static inline uint32_t tcg_in32(TCGContext *s)
{
    uint32_t v = *(uint32_t *)s->code_ptr;
    return v;
}

/* Emit an opcode with "type-checking" of the format.  */
#define tcg_out_insn(S, FMT, OP, ...) \
    glue(tcg_out_insn_,FMT)(S, glue(glue(glue(I,FMT),_),OP), ## __VA_ARGS__)

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

    assert(ofs >= -0x200 && ofs < 0x200 && (ofs & 7) == 0);
    insn |= (ofs & (0x7f << 3)) << (15 - 3);

    tcg_out32(s, insn | r2 << 10 | rn << 5 | r1);
}

static void tcg_out_insn_3401(TCGContext *s, AArch64Insn insn, TCGType ext,
                              TCGReg rd, TCGReg rn, uint64_t aimm)
{
    if (aimm > 0xfff) {
        assert((aimm & 0xfff) == 0);
        aimm >>= 12;
        assert(aimm <= 0xfff);
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
    assert((shift & ~0x30) == 0);
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

static void tcg_out_insn_3310(TCGContext *s, AArch64Insn insn,
                              TCGReg rd, TCGReg base, TCGReg regoff)
{
    /* Note the AArch64Insn constants above are for C3.3.12.  Adjust.  */
    tcg_out32(s, insn | I3312_TO_I3310 | regoff << 16 | base << 5 | rd);
}


static void tcg_out_insn_3312(TCGContext *s, AArch64Insn insn,
                              TCGReg rd, TCGReg rn, intptr_t offset)
{
    tcg_out32(s, insn | (offset & 0x1ff) << 12 | rn << 5 | rd);
}

static void tcg_out_insn_3313(TCGContext *s, AArch64Insn insn,
                              TCGReg rd, TCGReg rn, uintptr_t scaled_uimm)
{
    /* Note the AArch64Insn constants above are for C3.3.12.  Adjust.  */
    tcg_out32(s, insn | I3312_TO_I3313 | scaled_uimm << 10 | rn << 5 | rd);
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

    assert(is_limm(limm));

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

static void tcg_out_movi(TCGContext *s, TCGType type, TCGReg rd,
                         tcg_target_long value)
{
    AArch64Insn insn;
    int i, wantinv, shift;
    tcg_target_long svalue = value;
    tcg_target_long ivalue = ~value;
    tcg_target_long imask;

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
        tcg_target_long disp = (value >> 12) - ((intptr_t)s->code_ptr >> 12);
        if (disp == sextract64(disp, 0, 21)) {
            tcg_out_insn(s, 3406, ADRP, rd, disp);
            if (value & 0xfff) {
                tcg_out_insn(s, 3401, ADDI, type, rd, rd, value & 0xfff);
            }
            return;
        }
    }

    /* Would it take fewer insns to begin with MOVN?  For the value and its
       inverse, count the number of 16-bit lanes that are 0.  */
    for (i = wantinv = imask = 0; i < 64; i += 16) {
        tcg_target_long mask = 0xffffull << i;
        if ((value & mask) == 0) {
            wantinv -= 1;
        }
        if ((ivalue & mask) == 0) {
            wantinv += 1;
            imask |= mask;
        }
    }

    /* If we had more 0xffff than 0x0000, invert VALUE and use MOVN.  */
    insn = I3405_MOVZ;
    if (wantinv > 0) {
        value = ivalue;
        insn = I3405_MOVN;
    }

    /* Find the lowest lane that is not 0x0000.  */
    shift = ctz64(value) & (63 & -16);
    tcg_out_insn_3405(s, insn, type, rd, value >> shift, shift);

    if (wantinv > 0) {
        /* Re-invert the value, so MOVK sees non-inverted bits.  */
        value = ~value;
        /* Clear out all the 0xffff lanes.  */
        value ^= imask;
    }
    /* Clear out the lane that we just set.  */
    value &= ~(0xffffUL << shift);

    /* Iterate until all lanes have been set, and thus cleared from VALUE.  */
    while (value) {
        shift = ctz64(value) & (63 & -16);
        tcg_out_insn(s, 3405, MOVK, type, rd, value >> shift, shift);
        value &= ~(0xffffUL << shift);
    }
}

/* Define something more legible for general use.  */
#define tcg_out_ldst_r  tcg_out_insn_3310

static void tcg_out_ldst(TCGContext *s, AArch64Insn insn,
                         TCGReg rd, TCGReg rn, intptr_t offset)
{
    TCGMemOp size = (uint32_t)insn >> 30;

    /* If the offset is naturally aligned and in range, then we can
       use the scaled uimm12 encoding */
    if (offset >= 0 && !(offset & ((1 << size) - 1))) {
        uintptr_t scaled_uimm = offset >> size;
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
    tcg_out_ldst_r(s, insn, rd, rn, TCG_REG_TMP);
}

static inline void tcg_out_mov(TCGContext *s,
                               TCGType type, TCGReg ret, TCGReg arg)
{
    if (ret != arg) {
        tcg_out_movr(s, type, ret, arg);
    }
}

static inline void tcg_out_ld(TCGContext *s, TCGType type, TCGReg arg,
                              TCGReg arg1, intptr_t arg2)
{
    tcg_out_ldst(s, type == TCG_TYPE_I32 ? I3312_LDRW : I3312_LDRX,
                 arg, arg1, arg2);
}

static inline void tcg_out_st(TCGContext *s, TCGType type, TCGReg arg,
                              TCGReg arg1, intptr_t arg2)
{
    tcg_out_ldst(s, type == TCG_TYPE_I32 ? I3312_STRW : I3312_STRX,
                 arg, arg1, arg2);
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
    assert(offset == sextract64(offset, 0, 26));
    tcg_out_insn(s, 3206, B, offset);
}

static inline void tcg_out_goto_noaddr(TCGContext *s)
{
    /* We pay attention here to not modify the branch target by reading from
       the buffer. This ensure that caches and memory are kept coherent during
       retranslation.  Mask away possible garbage in the high bits for the
       first translation, while keeping the offset bits for retranslation. */
    uint32_t old = tcg_in32(s);
    tcg_out_insn(s, 3206, B, old);
}

static inline void tcg_out_goto_cond_noaddr(TCGContext *s, TCGCond c)
{
    /* See comments in tcg_out_goto_noaddr.  */
    uint32_t old = tcg_in32(s) >> 5;
    tcg_out_insn(s, 3202, B_C, c, old);
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

void aarch64_tb_set_jmp_target(uintptr_t jmp_addr, uintptr_t addr)
{
    tcg_insn_unit *code_ptr = (tcg_insn_unit *)jmp_addr;
    tcg_insn_unit *target = (tcg_insn_unit *)addr;

    reloc_pc26(code_ptr, target);
    flush_icache_range(jmp_addr, jmp_addr + 4);
}

static inline void tcg_out_goto_label(TCGContext *s, int label_index)
{
    TCGLabel *l = &s->labels[label_index];

    if (!l->has_value) {
        tcg_out_reloc(s, s->code_ptr, R_AARCH64_JUMP26, label_index, 0);
        tcg_out_goto_noaddr(s);
    } else {
        tcg_out_goto(s, l->u.value_ptr);
    }
}

static void tcg_out_brcond(TCGContext *s, TCGMemOp ext, TCGCond c, TCGArg a,
                           TCGArg b, bool b_const, int label)
{
    TCGLabel *l = &s->labels[label];
    intptr_t offset;
    bool need_cmp;

    if (b_const && b == 0 && (c == TCG_COND_EQ || c == TCG_COND_NE)) {
        need_cmp = false;
    } else {
        need_cmp = true;
        tcg_out_cmp(s, ext, a, b, b_const);
    }

    if (!l->has_value) {
        tcg_out_reloc(s, s->code_ptr, R_AARCH64_CONDBR19, label, 0);
        offset = tcg_in32(s) >> 5;
    } else {
        offset = l->u.value_ptr - s->code_ptr;
        assert(offset == sextract64(offset, 0, 19));
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

static inline void tcg_out_sxt(TCGContext *s, TCGType ext, TCGMemOp s_bits,
                               TCGReg rd, TCGReg rn)
{
    /* Using ALIASes SXTB, SXTH, SXTW, of SBFM Xd, Xn, #0, #7|15|31 */
    int bits = (8 << s_bits) - 1;
    tcg_out_sbfm(s, ext, rd, rn, 0, bits);
}

static inline void tcg_out_uxt(TCGContext *s, TCGMemOp s_bits,
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

static inline void tcg_out_addsub2(TCGContext *s, int ext, TCGReg rl,
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

#ifdef CONFIG_SOFTMMU
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

static inline void tcg_out_adr(TCGContext *s, TCGReg rd, void *target)
{
    ptrdiff_t offset = tcg_pcrel_diff(s, target);
    assert(offset == sextract64(offset, 0, 21));
    tcg_out_insn(s, 3406, ADR, rd, offset);
}

static void tcg_out_qemu_ld_slow_path(TCGContext *s, TCGLabelQemuLdst *lb)
{
    TCGMemOp opc = lb->opc;
    TCGMemOp size = opc & MO_SIZE;

    reloc_pc19(lb->label_ptr[0], s->code_ptr);

    tcg_out_mov(s, TCG_TYPE_I64, TCG_REG_X0, TCG_AREG0);
    tcg_out_mov(s, TARGET_LONG_BITS == 64, TCG_REG_X1, lb->addrlo_reg);
    tcg_out_movi(s, TCG_TYPE_I32, TCG_REG_X2, lb->mem_index);
    tcg_out_adr(s, TCG_REG_X3, lb->raddr);
    tcg_out_call(s, qemu_ld_helpers[opc & ~MO_SIGN]);
    if (opc & MO_SIGN) {
        tcg_out_sxt(s, TCG_TYPE_I64, size, lb->datalo_reg, TCG_REG_X0);
    } else {
        tcg_out_mov(s, size == MO_64, lb->datalo_reg, TCG_REG_X0);
    }

    tcg_out_goto(s, lb->raddr);
}

static void tcg_out_qemu_st_slow_path(TCGContext *s, TCGLabelQemuLdst *lb)
{
    TCGMemOp opc = lb->opc;
    TCGMemOp size = opc & MO_SIZE;

    reloc_pc19(lb->label_ptr[0], s->code_ptr);

    tcg_out_mov(s, TCG_TYPE_I64, TCG_REG_X0, TCG_AREG0);
    tcg_out_mov(s, TARGET_LONG_BITS == 64, TCG_REG_X1, lb->addrlo_reg);
    tcg_out_mov(s, size == MO_64, TCG_REG_X2, lb->datalo_reg);
    tcg_out_movi(s, TCG_TYPE_I32, TCG_REG_X3, lb->mem_index);
    tcg_out_adr(s, TCG_REG_X4, lb->raddr);
    tcg_out_call(s, qemu_st_helpers[opc]);
    tcg_out_goto(s, lb->raddr);
}

static void add_qemu_ldst_label(TCGContext *s, bool is_ld, TCGMemOp opc,
                                TCGReg data_reg, TCGReg addr_reg,
                                int mem_index, tcg_insn_unit *raddr,
                                tcg_insn_unit *label_ptr)
{
    TCGLabelQemuLdst *label = new_ldst_label(s);

    label->is_ld = is_ld;
    label->opc = opc;
    label->datalo_reg = data_reg;
    label->addrlo_reg = addr_reg;
    label->mem_index = mem_index;
    label->raddr = raddr;
    label->label_ptr[0] = label_ptr;
}

/* Load and compare a TLB entry, emitting the conditional jump to the
   slow path for the failure case, which will be patched later when finalizing
   the slow path. Generated code returns the host addend in X1,
   clobbers X0,X2,X3,TMP. */
static void tcg_out_tlb_read(TCGContext *s, TCGReg addr_reg, TCGMemOp s_bits,
                             tcg_insn_unit **label_ptr, int mem_index,
                             bool is_read)
{
    TCGReg base = TCG_AREG0;
    int tlb_offset = is_read ?
        offsetof(CPUArchState, tlb_table[mem_index][0].addr_read)
        : offsetof(CPUArchState, tlb_table[mem_index][0].addr_write);

    /* Extract the TLB index from the address into X0.
       X0<CPU_TLB_BITS:0> =
       addr_reg<TARGET_PAGE_BITS+CPU_TLB_BITS:TARGET_PAGE_BITS> */
    tcg_out_ubfm(s, TARGET_LONG_BITS == 64, TCG_REG_X0, addr_reg,
                 TARGET_PAGE_BITS, TARGET_PAGE_BITS + CPU_TLB_BITS);

    /* Store the page mask part of the address and the low s_bits into X3.
       Later this allows checking for equality and alignment at the same time.
       X3 = addr_reg & (PAGE_MASK | ((1 << s_bits) - 1)) */
    tcg_out_logicali(s, I3404_ANDI, TARGET_LONG_BITS == 64, TCG_REG_X3,
                     addr_reg, TARGET_PAGE_MASK | ((1 << s_bits) - 1));

    /* Add any "high bits" from the tlb offset to the env address into X2,
       to take advantage of the LSL12 form of the ADDI instruction.
       X2 = env + (tlb_offset & 0xfff000) */
    if (tlb_offset & 0xfff000) {
        tcg_out_insn(s, 3401, ADDI, TCG_TYPE_I64, TCG_REG_X2, base,
                     tlb_offset & 0xfff000);
        base = TCG_REG_X2;
    }

    /* Merge the tlb index contribution into X2.
       X2 = X2 + (X0 << CPU_TLB_ENTRY_BITS) */
    tcg_out_insn(s, 3502S, ADD_LSL, TCG_TYPE_I64, TCG_REG_X2, base,
                 TCG_REG_X0, CPU_TLB_ENTRY_BITS);

    /* Merge "low bits" from tlb offset, load the tlb comparator into X0.
       X0 = load [X2 + (tlb_offset & 0x000fff)] */
    tcg_out_ldst(s, TARGET_LONG_BITS == 32 ? I3312_LDRW : I3312_LDRX,
                 TCG_REG_X0, TCG_REG_X2, tlb_offset & 0xfff);

    /* Load the tlb addend. Do that early to avoid stalling.
       X1 = load [X2 + (tlb_offset & 0xfff) + offsetof(addend)] */
    tcg_out_ldst(s, I3312_LDRX, TCG_REG_X1, TCG_REG_X2,
                 (tlb_offset & 0xfff) + (offsetof(CPUTLBEntry, addend)) -
                 (is_read ? offsetof(CPUTLBEntry, addr_read)
                  : offsetof(CPUTLBEntry, addr_write)));

    /* Perform the address comparison. */
    tcg_out_cmp(s, (TARGET_LONG_BITS == 64), TCG_REG_X0, TCG_REG_X3, 0);

    /* If not equal, we jump to the slow path. */
    *label_ptr = s->code_ptr;
    tcg_out_goto_cond_noaddr(s, TCG_COND_NE);
}

#endif /* CONFIG_SOFTMMU */

static void tcg_out_qemu_ld_direct(TCGContext *s, TCGMemOp memop,
                                   TCGReg data_r, TCGReg addr_r, TCGReg off_r)
{
    const TCGMemOp bswap = memop & MO_BSWAP;

    switch (memop & MO_SSIZE) {
    case MO_UB:
        tcg_out_ldst_r(s, I3312_LDRB, data_r, addr_r, off_r);
        break;
    case MO_SB:
        tcg_out_ldst_r(s, I3312_LDRSBX, data_r, addr_r, off_r);
        break;
    case MO_UW:
        tcg_out_ldst_r(s, I3312_LDRH, data_r, addr_r, off_r);
        if (bswap) {
            tcg_out_rev16(s, data_r, data_r);
        }
        break;
    case MO_SW:
        if (bswap) {
            tcg_out_ldst_r(s, I3312_LDRH, data_r, addr_r, off_r);
            tcg_out_rev16(s, data_r, data_r);
            tcg_out_sxt(s, TCG_TYPE_I64, MO_16, data_r, data_r);
        } else {
            tcg_out_ldst_r(s, I3312_LDRSHX, data_r, addr_r, off_r);
        }
        break;
    case MO_UL:
        tcg_out_ldst_r(s, I3312_LDRW, data_r, addr_r, off_r);
        if (bswap) {
            tcg_out_rev32(s, data_r, data_r);
        }
        break;
    case MO_SL:
        if (bswap) {
            tcg_out_ldst_r(s, I3312_LDRW, data_r, addr_r, off_r);
            tcg_out_rev32(s, data_r, data_r);
            tcg_out_sxt(s, TCG_TYPE_I64, MO_32, data_r, data_r);
        } else {
            tcg_out_ldst_r(s, I3312_LDRSWX, data_r, addr_r, off_r);
        }
        break;
    case MO_Q:
        tcg_out_ldst_r(s, I3312_LDRX, data_r, addr_r, off_r);
        if (bswap) {
            tcg_out_rev64(s, data_r, data_r);
        }
        break;
    default:
        tcg_abort();
    }
}

static void tcg_out_qemu_st_direct(TCGContext *s, TCGMemOp memop,
                                   TCGReg data_r, TCGReg addr_r, TCGReg off_r)
{
    const TCGMemOp bswap = memop & MO_BSWAP;

    switch (memop & MO_SIZE) {
    case MO_8:
        tcg_out_ldst_r(s, I3312_STRB, data_r, addr_r, off_r);
        break;
    case MO_16:
        if (bswap && data_r != TCG_REG_XZR) {
            tcg_out_rev16(s, TCG_REG_TMP, data_r);
            data_r = TCG_REG_TMP;
        }
        tcg_out_ldst_r(s, I3312_STRH, data_r, addr_r, off_r);
        break;
    case MO_32:
        if (bswap && data_r != TCG_REG_XZR) {
            tcg_out_rev32(s, TCG_REG_TMP, data_r);
            data_r = TCG_REG_TMP;
        }
        tcg_out_ldst_r(s, I3312_STRW, data_r, addr_r, off_r);
        break;
    case MO_64:
        if (bswap && data_r != TCG_REG_XZR) {
            tcg_out_rev64(s, TCG_REG_TMP, data_r);
            data_r = TCG_REG_TMP;
        }
        tcg_out_ldst_r(s, I3312_STRX, data_r, addr_r, off_r);
        break;
    default:
        tcg_abort();
    }
}

static void tcg_out_qemu_ld(TCGContext *s, TCGReg data_reg, TCGReg addr_reg,
                            TCGMemOp memop, int mem_index)
{
#ifdef CONFIG_SOFTMMU
    TCGMemOp s_bits = memop & MO_SIZE;
    tcg_insn_unit *label_ptr;

    tcg_out_tlb_read(s, addr_reg, s_bits, &label_ptr, mem_index, 1);
    tcg_out_qemu_ld_direct(s, memop, data_reg, addr_reg, TCG_REG_X1);
    add_qemu_ldst_label(s, true, memop, data_reg, addr_reg,
                        mem_index, s->code_ptr, label_ptr);
#else /* !CONFIG_SOFTMMU */
    tcg_out_qemu_ld_direct(s, memop, data_reg, addr_reg,
                           GUEST_BASE ? TCG_REG_GUEST_BASE : TCG_REG_XZR);
#endif /* CONFIG_SOFTMMU */
}

static void tcg_out_qemu_st(TCGContext *s, TCGReg data_reg, TCGReg addr_reg,
                            TCGMemOp memop, int mem_index)
{
#ifdef CONFIG_SOFTMMU
    TCGMemOp s_bits = memop & MO_SIZE;
    tcg_insn_unit *label_ptr;

    tcg_out_tlb_read(s, addr_reg, s_bits, &label_ptr, mem_index, 0);
    tcg_out_qemu_st_direct(s, memop, data_reg, addr_reg, TCG_REG_X1);
    add_qemu_ldst_label(s, false, memop, data_reg, addr_reg,
                        mem_index, s->code_ptr, label_ptr);
#else /* !CONFIG_SOFTMMU */
    tcg_out_qemu_st_direct(s, memop, data_reg, addr_reg,
                           GUEST_BASE ? TCG_REG_GUEST_BASE : TCG_REG_XZR);
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
        tcg_out_movi(s, TCG_TYPE_I64, TCG_REG_X0, a0);
        tcg_out_goto(s, tb_ret_addr);
        break;

    case INDEX_op_goto_tb:
#ifndef USE_DIRECT_JUMP
#error "USE_DIRECT_JUMP required for aarch64"
#endif
        assert(s->tb_jmp_offset != NULL); /* consistency for USE_DIRECT_JUMP */
        s->tb_jmp_offset[a0] = tcg_current_code_size(s);
        /* actual branch destination will be patched by
           aarch64_tb_set_jmp_target later, beware retranslation. */
        tcg_out_goto_noaddr(s);
        s->tb_next_offset[a0] = tcg_current_code_size(s);
        break;

    case INDEX_op_call:
        if (const_args[0]) {
            tcg_out_call(s, (tcg_insn_unit *)(intptr_t)a0);
        } else {
            tcg_out_callr(s, a0);
        }
        break;

    case INDEX_op_br:
        tcg_out_goto_label(s, a0);
        break;

    case INDEX_op_ld8u_i32:
    case INDEX_op_ld8u_i64:
        tcg_out_ldst(s, I3312_LDRB, a0, a1, a2);
        break;
    case INDEX_op_ld8s_i32:
        tcg_out_ldst(s, I3312_LDRSBW, a0, a1, a2);
        break;
    case INDEX_op_ld8s_i64:
        tcg_out_ldst(s, I3312_LDRSBX, a0, a1, a2);
        break;
    case INDEX_op_ld16u_i32:
    case INDEX_op_ld16u_i64:
        tcg_out_ldst(s, I3312_LDRH, a0, a1, a2);
        break;
    case INDEX_op_ld16s_i32:
        tcg_out_ldst(s, I3312_LDRSHW, a0, a1, a2);
        break;
    case INDEX_op_ld16s_i64:
        tcg_out_ldst(s, I3312_LDRSHX, a0, a1, a2);
        break;
    case INDEX_op_ld_i32:
    case INDEX_op_ld32u_i64:
        tcg_out_ldst(s, I3312_LDRW, a0, a1, a2);
        break;
    case INDEX_op_ld32s_i64:
        tcg_out_ldst(s, I3312_LDRSWX, a0, a1, a2);
        break;
    case INDEX_op_ld_i64:
        tcg_out_ldst(s, I3312_LDRX, a0, a1, a2);
        break;

    case INDEX_op_st8_i32:
    case INDEX_op_st8_i64:
        tcg_out_ldst(s, I3312_STRB, REG0(0), a1, a2);
        break;
    case INDEX_op_st16_i32:
    case INDEX_op_st16_i64:
        tcg_out_ldst(s, I3312_STRH, REG0(0), a1, a2);
        break;
    case INDEX_op_st_i32:
    case INDEX_op_st32_i64:
        tcg_out_ldst(s, I3312_STRW, REG0(0), a1, a2);
        break;
    case INDEX_op_st_i64:
        tcg_out_ldst(s, I3312_STRX, REG0(0), a1, a2);
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

    case INDEX_op_brcond_i32:
        a1 = (int32_t)a1;
        /* FALLTHRU */
    case INDEX_op_brcond_i64:
        tcg_out_brcond(s, ext, a2, a0, a1, const_args[1], args[3]);
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
        tcg_out_qemu_ld(s, a0, a1, a2, args[3]);
        break;
    case INDEX_op_qemu_st_i32:
    case INDEX_op_qemu_st_i64:
        tcg_out_qemu_st(s, REG0(0), a1, a2, args[3]);
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
    case INDEX_op_ext32u_i64:
        tcg_out_movr(s, TCG_TYPE_I32, a0, a1);
        break;

    case INDEX_op_deposit_i64:
    case INDEX_op_deposit_i32:
        tcg_out_dep(s, ext, a0, REG0(2), args[3], args[4]);
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

    case INDEX_op_mov_i64:
    case INDEX_op_mov_i32:
    case INDEX_op_movi_i64:
    case INDEX_op_movi_i32:
        /* Always implemented with tcg_out_mov/i, never with tcg_out_op.  */
    default:
        /* Opcode not implemented.  */
        tcg_abort();
    }

#undef REG0
}

static const TCGTargetOpDef aarch64_op_defs[] = {
    { INDEX_op_exit_tb, { } },
    { INDEX_op_goto_tb, { } },
    { INDEX_op_call, { "ri" } },
    { INDEX_op_br, { } },

    { INDEX_op_mov_i32, { "r", "r" } },
    { INDEX_op_mov_i64, { "r", "r" } },

    { INDEX_op_movi_i32, { "r" } },
    { INDEX_op_movi_i64, { "r" } },

    { INDEX_op_ld8u_i32, { "r", "r" } },
    { INDEX_op_ld8s_i32, { "r", "r" } },
    { INDEX_op_ld16u_i32, { "r", "r" } },
    { INDEX_op_ld16s_i32, { "r", "r" } },
    { INDEX_op_ld_i32, { "r", "r" } },
    { INDEX_op_ld8u_i64, { "r", "r" } },
    { INDEX_op_ld8s_i64, { "r", "r" } },
    { INDEX_op_ld16u_i64, { "r", "r" } },
    { INDEX_op_ld16s_i64, { "r", "r" } },
    { INDEX_op_ld32u_i64, { "r", "r" } },
    { INDEX_op_ld32s_i64, { "r", "r" } },
    { INDEX_op_ld_i64, { "r", "r" } },

    { INDEX_op_st8_i32, { "rZ", "r" } },
    { INDEX_op_st16_i32, { "rZ", "r" } },
    { INDEX_op_st_i32, { "rZ", "r" } },
    { INDEX_op_st8_i64, { "rZ", "r" } },
    { INDEX_op_st16_i64, { "rZ", "r" } },
    { INDEX_op_st32_i64, { "rZ", "r" } },
    { INDEX_op_st_i64, { "rZ", "r" } },

    { INDEX_op_add_i32, { "r", "r", "rA" } },
    { INDEX_op_add_i64, { "r", "r", "rA" } },
    { INDEX_op_sub_i32, { "r", "r", "rA" } },
    { INDEX_op_sub_i64, { "r", "r", "rA" } },
    { INDEX_op_mul_i32, { "r", "r", "r" } },
    { INDEX_op_mul_i64, { "r", "r", "r" } },
    { INDEX_op_div_i32, { "r", "r", "r" } },
    { INDEX_op_div_i64, { "r", "r", "r" } },
    { INDEX_op_divu_i32, { "r", "r", "r" } },
    { INDEX_op_divu_i64, { "r", "r", "r" } },
    { INDEX_op_rem_i32, { "r", "r", "r" } },
    { INDEX_op_rem_i64, { "r", "r", "r" } },
    { INDEX_op_remu_i32, { "r", "r", "r" } },
    { INDEX_op_remu_i64, { "r", "r", "r" } },
    { INDEX_op_and_i32, { "r", "r", "rL" } },
    { INDEX_op_and_i64, { "r", "r", "rL" } },
    { INDEX_op_or_i32, { "r", "r", "rL" } },
    { INDEX_op_or_i64, { "r", "r", "rL" } },
    { INDEX_op_xor_i32, { "r", "r", "rL" } },
    { INDEX_op_xor_i64, { "r", "r", "rL" } },
    { INDEX_op_andc_i32, { "r", "r", "rL" } },
    { INDEX_op_andc_i64, { "r", "r", "rL" } },
    { INDEX_op_orc_i32, { "r", "r", "rL" } },
    { INDEX_op_orc_i64, { "r", "r", "rL" } },
    { INDEX_op_eqv_i32, { "r", "r", "rL" } },
    { INDEX_op_eqv_i64, { "r", "r", "rL" } },

    { INDEX_op_neg_i32, { "r", "r" } },
    { INDEX_op_neg_i64, { "r", "r" } },
    { INDEX_op_not_i32, { "r", "r" } },
    { INDEX_op_not_i64, { "r", "r" } },

    { INDEX_op_shl_i32, { "r", "r", "ri" } },
    { INDEX_op_shr_i32, { "r", "r", "ri" } },
    { INDEX_op_sar_i32, { "r", "r", "ri" } },
    { INDEX_op_rotl_i32, { "r", "r", "ri" } },
    { INDEX_op_rotr_i32, { "r", "r", "ri" } },
    { INDEX_op_shl_i64, { "r", "r", "ri" } },
    { INDEX_op_shr_i64, { "r", "r", "ri" } },
    { INDEX_op_sar_i64, { "r", "r", "ri" } },
    { INDEX_op_rotl_i64, { "r", "r", "ri" } },
    { INDEX_op_rotr_i64, { "r", "r", "ri" } },

    { INDEX_op_brcond_i32, { "r", "rA" } },
    { INDEX_op_brcond_i64, { "r", "rA" } },
    { INDEX_op_setcond_i32, { "r", "r", "rA" } },
    { INDEX_op_setcond_i64, { "r", "r", "rA" } },
    { INDEX_op_movcond_i32, { "r", "r", "rA", "rZ", "rZ" } },
    { INDEX_op_movcond_i64, { "r", "r", "rA", "rZ", "rZ" } },

    { INDEX_op_qemu_ld_i32, { "r", "l" } },
    { INDEX_op_qemu_ld_i64, { "r", "l" } },
    { INDEX_op_qemu_st_i32, { "lZ", "l" } },
    { INDEX_op_qemu_st_i64, { "lZ", "l" } },

    { INDEX_op_bswap16_i32, { "r", "r" } },
    { INDEX_op_bswap32_i32, { "r", "r" } },
    { INDEX_op_bswap16_i64, { "r", "r" } },
    { INDEX_op_bswap32_i64, { "r", "r" } },
    { INDEX_op_bswap64_i64, { "r", "r" } },

    { INDEX_op_ext8s_i32, { "r", "r" } },
    { INDEX_op_ext16s_i32, { "r", "r" } },
    { INDEX_op_ext8u_i32, { "r", "r" } },
    { INDEX_op_ext16u_i32, { "r", "r" } },

    { INDEX_op_ext8s_i64, { "r", "r" } },
    { INDEX_op_ext16s_i64, { "r", "r" } },
    { INDEX_op_ext32s_i64, { "r", "r" } },
    { INDEX_op_ext8u_i64, { "r", "r" } },
    { INDEX_op_ext16u_i64, { "r", "r" } },
    { INDEX_op_ext32u_i64, { "r", "r" } },

    { INDEX_op_deposit_i32, { "r", "0", "rZ" } },
    { INDEX_op_deposit_i64, { "r", "0", "rZ" } },

    { INDEX_op_add2_i32, { "r", "r", "rZ", "rZ", "rA", "rMZ" } },
    { INDEX_op_add2_i64, { "r", "r", "rZ", "rZ", "rA", "rMZ" } },
    { INDEX_op_sub2_i32, { "r", "r", "rZ", "rZ", "rA", "rMZ" } },
    { INDEX_op_sub2_i64, { "r", "r", "rZ", "rZ", "rA", "rMZ" } },

    { INDEX_op_muluh_i64, { "r", "r", "r" } },
    { INDEX_op_mulsh_i64, { "r", "r", "r" } },

    { -1 },
};

static void tcg_target_init(TCGContext *s)
{
    tcg_regset_set32(tcg_target_available_regs[TCG_TYPE_I32], 0, 0xffffffff);
    tcg_regset_set32(tcg_target_available_regs[TCG_TYPE_I64], 0, 0xffffffff);

    tcg_regset_set32(tcg_target_call_clobber_regs, 0,
                     (1 << TCG_REG_X0) | (1 << TCG_REG_X1) |
                     (1 << TCG_REG_X2) | (1 << TCG_REG_X3) |
                     (1 << TCG_REG_X4) | (1 << TCG_REG_X5) |
                     (1 << TCG_REG_X6) | (1 << TCG_REG_X7) |
                     (1 << TCG_REG_X8) | (1 << TCG_REG_X9) |
                     (1 << TCG_REG_X10) | (1 << TCG_REG_X11) |
                     (1 << TCG_REG_X12) | (1 << TCG_REG_X13) |
                     (1 << TCG_REG_X14) | (1 << TCG_REG_X15) |
                     (1 << TCG_REG_X16) | (1 << TCG_REG_X17) |
                     (1 << TCG_REG_X18) | (1 << TCG_REG_X30));

    tcg_regset_clear(s->reserved_regs);
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_SP);
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_FP);
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_TMP);
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_X18); /* platform register */

    tcg_add_target_add_op_defs(aarch64_op_defs);
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

#if defined(CONFIG_USE_GUEST_BASE)
    if (GUEST_BASE) {
        tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_GUEST_BASE, GUEST_BASE);
        tcg_regset_set_reg(s->reserved_regs, TCG_REG_GUEST_BASE);
    }
#endif

    tcg_out_mov(s, TCG_TYPE_PTR, TCG_AREG0, tcg_target_call_iarg_regs[0]);
    tcg_out_insn(s, 3207, BR, tcg_target_call_iarg_regs[1]);

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

typedef struct {
    DebugFrameCIE cie;
    DebugFrameFDEHeader fde;
    uint8_t fde_def_cfa[4];
    uint8_t fde_reg_ofs[24];
} DebugFrame;

#define ELF_HOST_MACHINE EM_AARCH64

static DebugFrame debug_frame = {
    .cie.len = sizeof(DebugFrameCIE)-4, /* length after .len member */
    .cie.id = -1,
    .cie.version = 1,
    .cie.code_align = 1,
    .cie.data_align = 0x78,             /* sleb128 -8 */
    .cie.return_column = TCG_REG_LR,

    /* Total FDE size does not include the "len" member.  */
    .fde.len = sizeof(DebugFrame) - offsetof(DebugFrame, fde.cie_offset),

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
    debug_frame.fde.func_start = (intptr_t)buf;
    debug_frame.fde.func_len = buf_size;

    tcg_register_jit_int(buf, buf_size, &debug_frame, sizeof(debug_frame));
}

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
    "%x24", "%x25", "%x26", "%x27", "%x28",
    "%fp", /* frame pointer */
    "%lr", /* link register */
    "%sp",  /* stack pointer */
};
#endif /* NDEBUG */

#ifdef TARGET_WORDS_BIGENDIAN
 #define TCG_LDST_BSWAP 1
#else
 #define TCG_LDST_BSWAP 0
#endif

static const int tcg_target_reg_alloc_order[] = {
    TCG_REG_X20, TCG_REG_X21, TCG_REG_X22, TCG_REG_X23,
    TCG_REG_X24, TCG_REG_X25, TCG_REG_X26, TCG_REG_X27,
    TCG_REG_X28, /* we will reserve this for GUEST_BASE if configured */

    TCG_REG_X9, TCG_REG_X10, TCG_REG_X11, TCG_REG_X12,
    TCG_REG_X13, TCG_REG_X14, TCG_REG_X15,
    TCG_REG_X16, TCG_REG_X17,

    TCG_REG_X18, TCG_REG_X19, /* will not use these, see tcg_target_init */

    TCG_REG_X0, TCG_REG_X1, TCG_REG_X2, TCG_REG_X3,
    TCG_REG_X4, TCG_REG_X5, TCG_REG_X6, TCG_REG_X7,

    TCG_REG_X8, /* will not use, see tcg_target_init */
};

static const int tcg_target_call_iarg_regs[8] = {
    TCG_REG_X0, TCG_REG_X1, TCG_REG_X2, TCG_REG_X3,
    TCG_REG_X4, TCG_REG_X5, TCG_REG_X6, TCG_REG_X7
};
static const int tcg_target_call_oarg_regs[1] = {
    TCG_REG_X0
};

#define TCG_REG_TMP TCG_REG_X8

#ifndef CONFIG_SOFTMMU
# if defined(CONFIG_USE_GUEST_BASE)
# define TCG_REG_GUEST_BASE TCG_REG_X28
# else
# define TCG_REG_GUEST_BASE TCG_REG_XZR
# endif
#endif

static inline void reloc_pc26(void *code_ptr, intptr_t target)
{
    intptr_t offset = (target - (intptr_t)code_ptr) / 4;
    /* read instruction, mask away previous PC_REL26 parameter contents,
       set the proper offset, then write back the instruction. */
    uint32_t insn = *(uint32_t *)code_ptr;
    insn = deposit32(insn, 0, 26, offset);
    *(uint32_t *)code_ptr = insn;
}

static inline void reloc_pc19(void *code_ptr, intptr_t target)
{
    intptr_t offset = (target - (intptr_t)code_ptr) / 4;
    /* read instruction, mask away previous PC_REL19 parameter contents,
       set the proper offset, then write back the instruction. */
    uint32_t insn = *(uint32_t *)code_ptr;
    insn = deposit32(insn, 5, 19, offset);
    *(uint32_t *)code_ptr = insn;
}

static inline void patch_reloc(uint8_t *code_ptr, int type,
                               intptr_t value, intptr_t addend)
{
    value += addend;

    switch (type) {
    case R_AARCH64_JUMP26:
    case R_AARCH64_CALL26:
        reloc_pc26(code_ptr, value);
        break;
    case R_AARCH64_CONDBR19:
        reloc_pc19(code_ptr, value);
        break;

    default:
        tcg_abort();
    }
}

#define TCG_CT_CONST_IS32 0x100
#define TCG_CT_CONST_AIMM 0x200
#define TCG_CT_CONST_LIMM 0x400
#define TCG_CT_CONST_ZERO 0x800
#define TCG_CT_CONST_MONE 0x1000

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
    case 'w': /* The operand should be considered 32-bit.  */
        ct->ct |= TCG_CT_CONST_IS32;
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

static int tcg_target_const_match(tcg_target_long val,
                                  const TCGArgConstraint *arg_ct)
{
    int ct = arg_ct->ct;

    if (ct & TCG_CT_CONST) {
        return 1;
    }
    if (ct & TCG_CT_CONST_IS32) {
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

/* opcodes for LDR / STR instructions with base + simm9 addressing */
enum aarch64_ldst_op_data { /* size of the data moved */
    LDST_8 = 0x38,
    LDST_16 = 0x78,
    LDST_32 = 0xb8,
    LDST_64 = 0xf8,
};
enum aarch64_ldst_op_type { /* type of operation */
    LDST_ST = 0x0,    /* store */
    LDST_LD = 0x4,    /* load */
    LDST_LD_S_X = 0x8,  /* load and sign-extend into Xt */
    LDST_LD_S_W = 0xc,  /* load and sign-extend into Wt */
};

/* We encode the format of the insn into the beginning of the name, so that
   we can have the preprocessor help "typecheck" the insn vs the output
   function.  Arm didn't provide us with nice names for the formats, so we
   use the section number of the architecture reference manual in which the
   instruction group is described.  */
typedef enum {
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

static inline enum aarch64_ldst_op_data
aarch64_ldst_get_data(TCGOpcode tcg_op)
{
    switch (tcg_op) {
    case INDEX_op_ld8u_i32:
    case INDEX_op_ld8s_i32:
    case INDEX_op_ld8u_i64:
    case INDEX_op_ld8s_i64:
    case INDEX_op_st8_i32:
    case INDEX_op_st8_i64:
        return LDST_8;

    case INDEX_op_ld16u_i32:
    case INDEX_op_ld16s_i32:
    case INDEX_op_ld16u_i64:
    case INDEX_op_ld16s_i64:
    case INDEX_op_st16_i32:
    case INDEX_op_st16_i64:
        return LDST_16;

    case INDEX_op_ld_i32:
    case INDEX_op_st_i32:
    case INDEX_op_ld32u_i64:
    case INDEX_op_ld32s_i64:
    case INDEX_op_st32_i64:
        return LDST_32;

    case INDEX_op_ld_i64:
    case INDEX_op_st_i64:
        return LDST_64;

    default:
        tcg_abort();
    }
}

static inline enum aarch64_ldst_op_type
aarch64_ldst_get_type(TCGOpcode tcg_op)
{
    switch (tcg_op) {
    case INDEX_op_st8_i32:
    case INDEX_op_st16_i32:
    case INDEX_op_st8_i64:
    case INDEX_op_st16_i64:
    case INDEX_op_st_i32:
    case INDEX_op_st32_i64:
    case INDEX_op_st_i64:
        return LDST_ST;

    case INDEX_op_ld8u_i32:
    case INDEX_op_ld16u_i32:
    case INDEX_op_ld8u_i64:
    case INDEX_op_ld16u_i64:
    case INDEX_op_ld_i32:
    case INDEX_op_ld32u_i64:
    case INDEX_op_ld_i64:
        return LDST_LD;

    case INDEX_op_ld8s_i32:
    case INDEX_op_ld16s_i32:
        return LDST_LD_S_W;

    case INDEX_op_ld8s_i64:
    case INDEX_op_ld16s_i64:
    case INDEX_op_ld32s_i64:
        return LDST_LD_S_X;

    default:
        tcg_abort();
    }
}

static inline uint32_t tcg_in32(TCGContext *s)
{
    uint32_t v = *(uint32_t *)s->code_ptr;
    return v;
}

/* Emit an opcode with "type-checking" of the format.  */
#define tcg_out_insn(S, FMT, OP, ...) \
    glue(tcg_out_insn_,FMT)(S, glue(glue(glue(I,FMT),_),OP), ## __VA_ARGS__)

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

static void tcg_out_insn_3509(TCGContext *s, AArch64Insn insn, TCGType ext,
                              TCGReg rd, TCGReg rn, TCGReg rm, TCGReg ra)
{
    tcg_out32(s, insn | ext << 31 | rm << 16 | ra << 10 | rn << 5 | rd);
}


static inline void tcg_out_ldst_9(TCGContext *s,
                                  enum aarch64_ldst_op_data op_data,
                                  enum aarch64_ldst_op_type op_type,
                                  TCGReg rd, TCGReg rn, tcg_target_long offset)
{
    /* use LDUR with BASE register with 9bit signed unscaled offset */
    tcg_out32(s, op_data << 24 | op_type << 20
              | (offset & 0x1ff) << 12 | rn << 5 | rd);
}

/* tcg_out_ldst_12 expects a scaled unsigned immediate offset */
static inline void tcg_out_ldst_12(TCGContext *s,
                                   enum aarch64_ldst_op_data op_data,
                                   enum aarch64_ldst_op_type op_type,
                                   TCGReg rd, TCGReg rn,
                                   tcg_target_ulong scaled_uimm)
{
    tcg_out32(s, (op_data | 1) << 24
              | op_type << 20 | scaled_uimm << 10 | rn << 5 | rd);
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

static void tcg_out_movi(TCGContext *s, TCGType type, TCGReg rd,
                         tcg_target_long value)
{
    AArch64Insn insn;

    if (type == TCG_TYPE_I32) {
        value = (uint32_t)value;
    }

    /* count trailing zeros in 16 bit steps, mapping 64 to 0. Emit the
       first MOVZ with the half-word immediate skipping the zeros, with a shift
       (LSL) equal to this number. Then all next instructions use MOVKs.
       Zero the processed half-word in the value, continue until empty.
       We build the final result 16bits at a time with up to 4 instructions,
       but do not emit instructions for 16bit zero holes. */
    insn = I3405_MOVZ;
    do {
        unsigned shift = ctz64(value) & (63 & -16);
        tcg_out_insn_3405(s, insn, shift >= 32, rd, value >> shift, shift);
        value &= ~(0xffffUL << shift);
        insn = I3405_MOVK;
    } while (value);
}

static inline void tcg_out_ldst_r(TCGContext *s,
                                  enum aarch64_ldst_op_data op_data,
                                  enum aarch64_ldst_op_type op_type,
                                  TCGReg rd, TCGReg base, TCGReg regoff)
{
    /* load from memory to register using base + 64bit register offset */
    /* using f.e. STR Wt, [Xn, Xm] 0xb8600800|(regoff << 16)|(base << 5)|rd */
    /* the 0x6000 is for the "no extend field" */
    tcg_out32(s, 0x00206800
              | op_data << 24 | op_type << 20 | regoff << 16 | base << 5 | rd);
}

/* solve the whole ldst problem */
static inline void tcg_out_ldst(TCGContext *s, enum aarch64_ldst_op_data data,
                                enum aarch64_ldst_op_type type,
                                TCGReg rd, TCGReg rn, tcg_target_long offset)
{
    if (offset >= -256 && offset < 256) {
        tcg_out_ldst_9(s, data, type, rd, rn, offset);
        return;
    }

    if (offset >= 256) {
        /* if the offset is naturally aligned and in range,
           then we can use the scaled uimm12 encoding */
        unsigned int s_bits = data >> 6;
        if (!(offset & ((1 << s_bits) - 1))) {
            tcg_target_ulong scaled_uimm = offset >> s_bits;
            if (scaled_uimm <= 0xfff) {
                tcg_out_ldst_12(s, data, type, rd, rn, scaled_uimm);
                return;
            }
        }
    }

    /* worst-case scenario, move offset to temp register, use reg offset */
    tcg_out_movi(s, TCG_TYPE_I64, TCG_REG_TMP, offset);
    tcg_out_ldst_r(s, data, type, rd, rn, TCG_REG_TMP);
}

static inline void tcg_out_mov(TCGContext *s,
                               TCGType type, TCGReg ret, TCGReg arg)
{
    if (ret != arg) {
        tcg_out_movr(s, type == TCG_TYPE_I64, ret, arg);
    }
}

static inline void tcg_out_ld(TCGContext *s, TCGType type, TCGReg arg,
                              TCGReg arg1, intptr_t arg2)
{
    tcg_out_ldst(s, (type == TCG_TYPE_I64) ? LDST_64 : LDST_32, LDST_LD,
                 arg, arg1, arg2);
}

static inline void tcg_out_st(TCGContext *s, TCGType type, TCGReg arg,
                              TCGReg arg1, intptr_t arg2)
{
    tcg_out_ldst(s, (type == TCG_TYPE_I64) ? LDST_64 : LDST_32, LDST_ST,
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

static inline void tcg_out_goto(TCGContext *s, intptr_t target)
{
    intptr_t offset = (target - (intptr_t)s->code_ptr) / 4;

    if (offset < -0x02000000 || offset >= 0x02000000) {
        /* out of 26bit range */
        tcg_abort();
    }

    tcg_out32(s, 0x14000000 | (offset & 0x03ffffff));
}

static inline void tcg_out_goto_noaddr(TCGContext *s)
{
    /* We pay attention here to not modify the branch target by
       reading from the buffer. This ensure that caches and memory are
       kept coherent during retranslation.
       Mask away possible garbage in the high bits for the first translation,
       while keeping the offset bits for retranslation. */
    uint32_t insn;
    insn = (tcg_in32(s) & 0x03ffffff) | 0x14000000;
    tcg_out32(s, insn);
}

static inline void tcg_out_goto_cond_noaddr(TCGContext *s, TCGCond c)
{
    /* see comments in tcg_out_goto_noaddr */
    uint32_t insn;
    insn = tcg_in32(s) & (0x07ffff << 5);
    insn |= 0x54000000 | tcg_cond_to_aarch64[c];
    tcg_out32(s, insn);
}

static inline void tcg_out_goto_cond(TCGContext *s, TCGCond c, intptr_t target)
{
    intptr_t offset = (target - (intptr_t)s->code_ptr) / 4;

    if (offset < -0x40000 || offset >= 0x40000) {
        /* out of 19bit range */
        tcg_abort();
    }

    offset &= 0x7ffff;
    tcg_out32(s, 0x54000000 | tcg_cond_to_aarch64[c] | offset << 5);
}

static inline void tcg_out_callr(TCGContext *s, TCGReg reg)
{
    tcg_out32(s, 0xd63f0000 | reg << 5);
}

static inline void tcg_out_gotor(TCGContext *s, TCGReg reg)
{
    tcg_out32(s, 0xd61f0000 | reg << 5);
}

static inline void tcg_out_call(TCGContext *s, intptr_t target)
{
    intptr_t offset = (target - (intptr_t)s->code_ptr) / 4;

    if (offset < -0x02000000 || offset >= 0x02000000) { /* out of 26bit rng */
        tcg_out_movi(s, TCG_TYPE_I64, TCG_REG_TMP, target);
        tcg_out_callr(s, TCG_REG_TMP);
    } else {
        tcg_out32(s, 0x94000000 | (offset & 0x03ffffff));
    }
}

static inline void tcg_out_ret(TCGContext *s)
{
    /* emit RET { LR } */
    tcg_out32(s, 0xd65f03c0);
}

void aarch64_tb_set_jmp_target(uintptr_t jmp_addr, uintptr_t addr)
{
    intptr_t target = addr;
    intptr_t offset = (target - (intptr_t)jmp_addr) / 4;

    if (offset < -0x02000000 || offset >= 0x02000000) {
        /* out of 26bit range */
        tcg_abort();
    }

    patch_reloc((uint8_t *)jmp_addr, R_AARCH64_JUMP26, target, 0);
    flush_icache_range(jmp_addr, jmp_addr + 4);
}

static inline void tcg_out_goto_label(TCGContext *s, int label_index)
{
    TCGLabel *l = &s->labels[label_index];

    if (!l->has_value) {
        tcg_out_reloc(s, s->code_ptr, R_AARCH64_JUMP26, label_index, 0);
        tcg_out_goto_noaddr(s);
    } else {
        tcg_out_goto(s, l->u.value);
    }
}

static inline void tcg_out_goto_label_cond(TCGContext *s,
                                           TCGCond c, int label_index)
{
    TCGLabel *l = &s->labels[label_index];

    if (!l->has_value) {
        tcg_out_reloc(s, s->code_ptr, R_AARCH64_CONDBR19, label_index, 0);
        tcg_out_goto_cond_noaddr(s, c);
    } else {
        tcg_out_goto_cond(s, c, l->u.value);
    }
}

static inline void tcg_out_rev(TCGContext *s, TCGType ext,
                               TCGReg rd, TCGReg rm)
{
    /* using REV 0x5ac00800 */
    unsigned int base = ext ? 0xdac00c00 : 0x5ac00800;
    tcg_out32(s, base | rm << 5 | rd);
}

static inline void tcg_out_rev16(TCGContext *s, TCGType ext,
                                 TCGReg rd, TCGReg rm)
{
    /* using REV16 0x5ac00400 */
    unsigned int base = ext ? 0xdac00400 : 0x5ac00400;
    tcg_out32(s, base | rm << 5 | rd);
}

static inline void tcg_out_sxt(TCGContext *s, TCGType ext, int s_bits,
                               TCGReg rd, TCGReg rn)
{
    /* Using ALIASes SXTB, SXTH, SXTW, of SBFM Xd, Xn, #0, #7|15|31 */
    int bits = 8 * (1 << s_bits) - 1;
    tcg_out_sbfm(s, ext, rd, rn, 0, bits);
}

static inline void tcg_out_uxt(TCGContext *s, int s_bits,
                               TCGReg rd, TCGReg rn)
{
    /* Using ALIASes UXTB, UXTH of UBFM Wd, Wn, #0, #7|15 */
    int bits = 8 * (1 << s_bits) - 1;
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

    if (rl != orig_rl) {
        tcg_out_movr(s, ext, orig_rl, rl);
    }
}

#ifdef CONFIG_SOFTMMU
/* helper signature: helper_ret_ld_mmu(CPUState *env, target_ulong addr,
 *                                     int mmu_idx, uintptr_t ra)
 */
static const void * const qemu_ld_helpers[4] = {
    helper_ret_ldub_mmu,
    helper_ret_lduw_mmu,
    helper_ret_ldul_mmu,
    helper_ret_ldq_mmu,
};

/* helper signature: helper_ret_st_mmu(CPUState *env, target_ulong addr,
 *                                     uintxx_t val, int mmu_idx, uintptr_t ra)
 */
static const void * const qemu_st_helpers[4] = {
    helper_ret_stb_mmu,
    helper_ret_stw_mmu,
    helper_ret_stl_mmu,
    helper_ret_stq_mmu,
};

static void tcg_out_qemu_ld_slow_path(TCGContext *s, TCGLabelQemuLdst *lb)
{
    reloc_pc19(lb->label_ptr[0], (intptr_t)s->code_ptr);

    tcg_out_movr(s, 1, TCG_REG_X0, TCG_AREG0);
    tcg_out_movr(s, (TARGET_LONG_BITS == 64), TCG_REG_X1, lb->addrlo_reg);
    tcg_out_movi(s, TCG_TYPE_I32, TCG_REG_X2, lb->mem_index);
    tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_X3, (tcg_target_long)lb->raddr);
    tcg_out_movi(s, TCG_TYPE_I64, TCG_REG_TMP,
                 (tcg_target_long)qemu_ld_helpers[lb->opc & 3]);
    tcg_out_callr(s, TCG_REG_TMP);
    if (lb->opc & 0x04) {
        tcg_out_sxt(s, 1, lb->opc & 3, lb->datalo_reg, TCG_REG_X0);
    } else {
        tcg_out_movr(s, 1, lb->datalo_reg, TCG_REG_X0);
    }

    tcg_out_goto(s, (intptr_t)lb->raddr);
}

static void tcg_out_qemu_st_slow_path(TCGContext *s, TCGLabelQemuLdst *lb)
{
    reloc_pc19(lb->label_ptr[0], (intptr_t)s->code_ptr);

    tcg_out_movr(s, 1, TCG_REG_X0, TCG_AREG0);
    tcg_out_movr(s, (TARGET_LONG_BITS == 64), TCG_REG_X1, lb->addrlo_reg);
    tcg_out_movr(s, 1, TCG_REG_X2, lb->datalo_reg);
    tcg_out_movi(s, TCG_TYPE_I32, TCG_REG_X3, lb->mem_index);
    tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_X4, (intptr_t)lb->raddr);
    tcg_out_movi(s, TCG_TYPE_I64, TCG_REG_TMP,
                 (intptr_t)qemu_st_helpers[lb->opc & 3]);
    tcg_out_callr(s, TCG_REG_TMP);
    tcg_out_goto(s, (tcg_target_long)lb->raddr);
}

static void add_qemu_ldst_label(TCGContext *s, int is_ld, int opc,
                                TCGReg data_reg, TCGReg addr_reg,
                                int mem_index,
                                uint8_t *raddr, uint8_t *label_ptr)
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
static void tcg_out_tlb_read(TCGContext *s, TCGReg addr_reg,
            int s_bits, uint8_t **label_ptr, int mem_index, int is_read)
{
    TCGReg base = TCG_AREG0;
    int tlb_offset = is_read ?
        offsetof(CPUArchState, tlb_table[mem_index][0].addr_read)
        : offsetof(CPUArchState, tlb_table[mem_index][0].addr_write);
    /* Extract the TLB index from the address into X0.
       X0<CPU_TLB_BITS:0> =
       addr_reg<TARGET_PAGE_BITS+CPU_TLB_BITS:TARGET_PAGE_BITS> */
    tcg_out_ubfm(s, (TARGET_LONG_BITS == 64), TCG_REG_X0, addr_reg,
                 TARGET_PAGE_BITS, TARGET_PAGE_BITS + CPU_TLB_BITS);
    /* Store the page mask part of the address and the low s_bits into X3.
       Later this allows checking for equality and alignment at the same time.
       X3 = addr_reg & (PAGE_MASK | ((1 << s_bits) - 1)) */
    tcg_out_logicali(s, I3404_ANDI, TARGET_LONG_BITS == 64, TCG_REG_X3,
                     addr_reg, TARGET_PAGE_MASK | ((1 << s_bits) - 1));
    /* Add any "high bits" from the tlb offset to the env address into X2,
       to take advantage of the LSL12 form of the ADDI instruction.
       X2 = env + (tlb_offset & 0xfff000) */
    tcg_out_insn(s, 3401, ADDI, TCG_TYPE_I64, TCG_REG_X2, base,
                 tlb_offset & 0xfff000);
    /* Merge the tlb index contribution into X2.
       X2 = X2 + (X0 << CPU_TLB_ENTRY_BITS) */
    tcg_out_insn(s, 3502S, ADD_LSL, 1, TCG_REG_X2, TCG_REG_X2,
                 TCG_REG_X0, CPU_TLB_ENTRY_BITS);
    /* Merge "low bits" from tlb offset, load the tlb comparator into X0.
       X0 = load [X2 + (tlb_offset & 0x000fff)] */
    tcg_out_ldst(s, TARGET_LONG_BITS == 64 ? LDST_64 : LDST_32,
                 LDST_LD, TCG_REG_X0, TCG_REG_X2,
                 (tlb_offset & 0xfff));
    /* Load the tlb addend. Do that early to avoid stalling.
       X1 = load [X2 + (tlb_offset & 0xfff) + offsetof(addend)] */
    tcg_out_ldst(s, LDST_64, LDST_LD, TCG_REG_X1, TCG_REG_X2,
                 (tlb_offset & 0xfff) + (offsetof(CPUTLBEntry, addend)) -
                 (is_read ? offsetof(CPUTLBEntry, addr_read)
                  : offsetof(CPUTLBEntry, addr_write)));
    /* Perform the address comparison. */
    tcg_out_cmp(s, (TARGET_LONG_BITS == 64), TCG_REG_X0, TCG_REG_X3, 0);
    *label_ptr = s->code_ptr;
    /* If not equal, we jump to the slow path. */
    tcg_out_goto_cond_noaddr(s, TCG_COND_NE);
}

#endif /* CONFIG_SOFTMMU */

static void tcg_out_qemu_ld_direct(TCGContext *s, int opc, TCGReg data_r,
                                   TCGReg addr_r, TCGReg off_r)
{
    switch (opc) {
    case 0:
        tcg_out_ldst_r(s, LDST_8, LDST_LD, data_r, addr_r, off_r);
        break;
    case 0 | 4:
        tcg_out_ldst_r(s, LDST_8, LDST_LD_S_X, data_r, addr_r, off_r);
        break;
    case 1:
        tcg_out_ldst_r(s, LDST_16, LDST_LD, data_r, addr_r, off_r);
        if (TCG_LDST_BSWAP) {
            tcg_out_rev16(s, 0, data_r, data_r);
        }
        break;
    case 1 | 4:
        if (TCG_LDST_BSWAP) {
            tcg_out_ldst_r(s, LDST_16, LDST_LD, data_r, addr_r, off_r);
            tcg_out_rev16(s, 0, data_r, data_r);
            tcg_out_sxt(s, 1, 1, data_r, data_r);
        } else {
            tcg_out_ldst_r(s, LDST_16, LDST_LD_S_X, data_r, addr_r, off_r);
        }
        break;
    case 2:
        tcg_out_ldst_r(s, LDST_32, LDST_LD, data_r, addr_r, off_r);
        if (TCG_LDST_BSWAP) {
            tcg_out_rev(s, 0, data_r, data_r);
        }
        break;
    case 2 | 4:
        if (TCG_LDST_BSWAP) {
            tcg_out_ldst_r(s, LDST_32, LDST_LD, data_r, addr_r, off_r);
            tcg_out_rev(s, 0, data_r, data_r);
            tcg_out_sxt(s, 1, 2, data_r, data_r);
        } else {
            tcg_out_ldst_r(s, LDST_32, LDST_LD_S_X, data_r, addr_r, off_r);
        }
        break;
    case 3:
        tcg_out_ldst_r(s, LDST_64, LDST_LD, data_r, addr_r, off_r);
        if (TCG_LDST_BSWAP) {
            tcg_out_rev(s, 1, data_r, data_r);
        }
        break;
    default:
        tcg_abort();
    }
}

static void tcg_out_qemu_st_direct(TCGContext *s, int opc, TCGReg data_r,
                                   TCGReg addr_r, TCGReg off_r)
{
    switch (opc) {
    case 0:
        tcg_out_ldst_r(s, LDST_8, LDST_ST, data_r, addr_r, off_r);
        break;
    case 1:
        if (TCG_LDST_BSWAP) {
            tcg_out_rev16(s, 0, TCG_REG_TMP, data_r);
            tcg_out_ldst_r(s, LDST_16, LDST_ST, TCG_REG_TMP, addr_r, off_r);
        } else {
            tcg_out_ldst_r(s, LDST_16, LDST_ST, data_r, addr_r, off_r);
        }
        break;
    case 2:
        if (TCG_LDST_BSWAP) {
            tcg_out_rev(s, 0, TCG_REG_TMP, data_r);
            tcg_out_ldst_r(s, LDST_32, LDST_ST, TCG_REG_TMP, addr_r, off_r);
        } else {
            tcg_out_ldst_r(s, LDST_32, LDST_ST, data_r, addr_r, off_r);
        }
        break;
    case 3:
        if (TCG_LDST_BSWAP) {
            tcg_out_rev(s, 1, TCG_REG_TMP, data_r);
            tcg_out_ldst_r(s, LDST_64, LDST_ST, TCG_REG_TMP, addr_r, off_r);
        } else {
            tcg_out_ldst_r(s, LDST_64, LDST_ST, data_r, addr_r, off_r);
        }
        break;
    default:
        tcg_abort();
    }
}

static void tcg_out_qemu_ld(TCGContext *s, const TCGArg *args, int opc)
{
    TCGReg addr_reg, data_reg;
#ifdef CONFIG_SOFTMMU
    int mem_index, s_bits;
    uint8_t *label_ptr;
#endif
    data_reg = args[0];
    addr_reg = args[1];

#ifdef CONFIG_SOFTMMU
    mem_index = args[2];
    s_bits = opc & 3;
    tcg_out_tlb_read(s, addr_reg, s_bits, &label_ptr, mem_index, 1);
    tcg_out_qemu_ld_direct(s, opc, data_reg, addr_reg, TCG_REG_X1);
    add_qemu_ldst_label(s, 1, opc, data_reg, addr_reg,
                        mem_index, s->code_ptr, label_ptr);
#else /* !CONFIG_SOFTMMU */
    tcg_out_qemu_ld_direct(s, opc, data_reg, addr_reg,
                           GUEST_BASE ? TCG_REG_GUEST_BASE : TCG_REG_XZR);
#endif /* CONFIG_SOFTMMU */
}

static void tcg_out_qemu_st(TCGContext *s, const TCGArg *args, int opc)
{
    TCGReg addr_reg, data_reg;
#ifdef CONFIG_SOFTMMU
    int mem_index, s_bits;
    uint8_t *label_ptr;
#endif
    data_reg = args[0];
    addr_reg = args[1];

#ifdef CONFIG_SOFTMMU
    mem_index = args[2];
    s_bits = opc & 3;

    tcg_out_tlb_read(s, addr_reg, s_bits, &label_ptr, mem_index, 0);
    tcg_out_qemu_st_direct(s, opc, data_reg, addr_reg, TCG_REG_X1);
    add_qemu_ldst_label(s, 0, opc, data_reg, addr_reg,
                        mem_index, s->code_ptr, label_ptr);
#else /* !CONFIG_SOFTMMU */
    tcg_out_qemu_st_direct(s, opc, data_reg, addr_reg,
                           GUEST_BASE ? TCG_REG_GUEST_BASE : TCG_REG_XZR);
#endif /* CONFIG_SOFTMMU */
}

static uint8_t *tb_ret_addr;

/* callee stack use example:
   stp     x29, x30, [sp,#-32]!
   mov     x29, sp
   stp     x1, x2, [sp,#16]
   ...
   ldp     x1, x2, [sp,#16]
   ldp     x29, x30, [sp],#32
   ret
*/

/* push r1 and r2, and alloc stack space for a total of
   alloc_n elements (1 element=16 bytes, must be between 1 and 31. */
static inline void tcg_out_push_pair(TCGContext *s, TCGReg addr,
                                     TCGReg r1, TCGReg r2, int alloc_n)
{
    /* using indexed scaled simm7 STP 0x28800000 | (ext) | 0x01000000 (pre-idx)
       | alloc_n * (-1) << 16 | r2 << 10 | addr << 5 | r1 */
    assert(alloc_n > 0 && alloc_n < 0x20);
    alloc_n = (-alloc_n) & 0x3f;
    tcg_out32(s, 0xa9800000 | alloc_n << 16 | r2 << 10 | addr << 5 | r1);
}

/* dealloc stack space for a total of alloc_n elements and pop r1, r2.  */
static inline void tcg_out_pop_pair(TCGContext *s, TCGReg addr,
                                    TCGReg r1, TCGReg r2, int alloc_n)
{
    /* using indexed scaled simm7 LDP 0x28c00000 | (ext) | nothing (post-idx)
       | alloc_n << 16 | r2 << 10 | addr << 5 | r1 */
    assert(alloc_n > 0 && alloc_n < 0x20);
    tcg_out32(s, 0xa8c00000 | alloc_n << 16 | r2 << 10 | addr << 5 | r1);
}

static inline void tcg_out_store_pair(TCGContext *s, TCGReg addr,
                                      TCGReg r1, TCGReg r2, int idx)
{
    /* using register pair offset simm7 STP 0x29000000 | (ext)
       | idx << 16 | r2 << 10 | addr << 5 | r1 */
    assert(idx > 0 && idx < 0x20);
    tcg_out32(s, 0xa9000000 | idx << 16 | r2 << 10 | addr << 5 | r1);
}

static inline void tcg_out_load_pair(TCGContext *s, TCGReg addr,
                                     TCGReg r1, TCGReg r2, int idx)
{
    /* using register pair offset simm7 LDP 0x29400000 | (ext)
       | idx << 16 | r2 << 10 | addr << 5 | r1 */
    assert(idx > 0 && idx < 0x20);
    tcg_out32(s, 0xa9400000 | idx << 16 | r2 << 10 | addr << 5 | r1);
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
    case INDEX_op_exit_tb:
        tcg_out_movi(s, TCG_TYPE_I64, TCG_REG_X0, a0);
        tcg_out_goto(s, (intptr_t)tb_ret_addr);
        break;

    case INDEX_op_goto_tb:
#ifndef USE_DIRECT_JUMP
#error "USE_DIRECT_JUMP required for aarch64"
#endif
        assert(s->tb_jmp_offset != NULL); /* consistency for USE_DIRECT_JUMP */
        s->tb_jmp_offset[a0] = s->code_ptr - s->code_buf;
        /* actual branch destination will be patched by
           aarch64_tb_set_jmp_target later, beware retranslation. */
        tcg_out_goto_noaddr(s);
        s->tb_next_offset[a0] = s->code_ptr - s->code_buf;
        break;

    case INDEX_op_call:
        if (const_args[0]) {
            tcg_out_call(s, a0);
        } else {
            tcg_out_callr(s, a0);
        }
        break;

    case INDEX_op_br:
        tcg_out_goto_label(s, a0);
        break;

    case INDEX_op_ld_i32:
    case INDEX_op_ld_i64:
    case INDEX_op_st_i32:
    case INDEX_op_st_i64:
    case INDEX_op_ld8u_i32:
    case INDEX_op_ld8s_i32:
    case INDEX_op_ld16u_i32:
    case INDEX_op_ld16s_i32:
    case INDEX_op_ld8u_i64:
    case INDEX_op_ld8s_i64:
    case INDEX_op_ld16u_i64:
    case INDEX_op_ld16s_i64:
    case INDEX_op_ld32u_i64:
    case INDEX_op_ld32s_i64:
    case INDEX_op_st8_i32:
    case INDEX_op_st8_i64:
    case INDEX_op_st16_i32:
    case INDEX_op_st16_i64:
    case INDEX_op_st32_i64:
        tcg_out_ldst(s, aarch64_ldst_get_data(opc), aarch64_ldst_get_type(opc),
                     a0, a1, a2);
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
        tcg_out_cmp(s, ext, a0, a1, const_args[1]);
        tcg_out_goto_label_cond(s, a2, args[3]);
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

    case INDEX_op_qemu_ld8u:
        tcg_out_qemu_ld(s, args, 0 | 0);
        break;
    case INDEX_op_qemu_ld8s:
        tcg_out_qemu_ld(s, args, 4 | 0);
        break;
    case INDEX_op_qemu_ld16u:
        tcg_out_qemu_ld(s, args, 0 | 1);
        break;
    case INDEX_op_qemu_ld16s:
        tcg_out_qemu_ld(s, args, 4 | 1);
        break;
    case INDEX_op_qemu_ld32u:
        tcg_out_qemu_ld(s, args, 0 | 2);
        break;
    case INDEX_op_qemu_ld32s:
        tcg_out_qemu_ld(s, args, 4 | 2);
        break;
    case INDEX_op_qemu_ld32:
        tcg_out_qemu_ld(s, args, 0 | 2);
        break;
    case INDEX_op_qemu_ld64:
        tcg_out_qemu_ld(s, args, 0 | 3);
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

    case INDEX_op_bswap32_i64:
        /* Despite the _i64, this is a 32-bit bswap.  */
        ext = 0;
        /* FALLTHRU */
    case INDEX_op_bswap64_i64:
    case INDEX_op_bswap32_i32:
        tcg_out_rev(s, ext, a0, a1);
        break;
    case INDEX_op_bswap16_i64:
    case INDEX_op_bswap16_i32:
        tcg_out_rev16(s, 0, a0, a1);
        break;

    case INDEX_op_ext8s_i64:
    case INDEX_op_ext8s_i32:
        tcg_out_sxt(s, ext, 0, a0, a1);
        break;
    case INDEX_op_ext16s_i64:
    case INDEX_op_ext16s_i32:
        tcg_out_sxt(s, ext, 1, a0, a1);
        break;
    case INDEX_op_ext32s_i64:
        tcg_out_sxt(s, 1, 2, a0, a1);
        break;
    case INDEX_op_ext8u_i64:
    case INDEX_op_ext8u_i32:
        tcg_out_uxt(s, 0, a0, a1);
        break;
    case INDEX_op_ext16u_i64:
    case INDEX_op_ext16u_i32:
        tcg_out_uxt(s, 1, a0, a1);
        break;
    case INDEX_op_ext32u_i64:
        tcg_out_movr(s, 0, a0, a1);
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

    { INDEX_op_st8_i32, { "r", "r" } },
    { INDEX_op_st16_i32, { "r", "r" } },
    { INDEX_op_st_i32, { "r", "r" } },
    { INDEX_op_st8_i64, { "r", "r" } },
    { INDEX_op_st16_i64, { "r", "r" } },
    { INDEX_op_st32_i64, { "r", "r" } },
    { INDEX_op_st_i64, { "r", "r" } },

    { INDEX_op_add_i32, { "r", "r", "rwA" } },
    { INDEX_op_add_i64, { "r", "r", "rA" } },
    { INDEX_op_sub_i32, { "r", "r", "rwA" } },
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
    { INDEX_op_and_i32, { "r", "r", "rwL" } },
    { INDEX_op_and_i64, { "r", "r", "rL" } },
    { INDEX_op_or_i32, { "r", "r", "rwL" } },
    { INDEX_op_or_i64, { "r", "r", "rL" } },
    { INDEX_op_xor_i32, { "r", "r", "rwL" } },
    { INDEX_op_xor_i64, { "r", "r", "rL" } },
    { INDEX_op_andc_i32, { "r", "r", "rwL" } },
    { INDEX_op_andc_i64, { "r", "r", "rL" } },
    { INDEX_op_orc_i32, { "r", "r", "rwL" } },
    { INDEX_op_orc_i64, { "r", "r", "rL" } },
    { INDEX_op_eqv_i32, { "r", "r", "rwL" } },
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

    { INDEX_op_brcond_i32, { "r", "rwA" } },
    { INDEX_op_brcond_i64, { "r", "rA" } },
    { INDEX_op_setcond_i32, { "r", "r", "rwA" } },
    { INDEX_op_setcond_i64, { "r", "r", "rA" } },
    { INDEX_op_movcond_i32, { "r", "r", "rwA", "rZ", "rZ" } },
    { INDEX_op_movcond_i64, { "r", "r", "rA", "rZ", "rZ" } },

    { INDEX_op_qemu_ld8u, { "r", "l" } },
    { INDEX_op_qemu_ld8s, { "r", "l" } },
    { INDEX_op_qemu_ld16u, { "r", "l" } },
    { INDEX_op_qemu_ld16s, { "r", "l" } },
    { INDEX_op_qemu_ld32u, { "r", "l" } },
    { INDEX_op_qemu_ld32s, { "r", "l" } },

    { INDEX_op_qemu_ld32, { "r", "l" } },
    { INDEX_op_qemu_ld64, { "r", "l" } },

    { INDEX_op_qemu_st8, { "l", "l" } },
    { INDEX_op_qemu_st16, { "l", "l" } },
    { INDEX_op_qemu_st32, { "l", "l" } },
    { INDEX_op_qemu_st64, { "l", "l" } },

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

    { INDEX_op_add2_i32, { "r", "r", "rZ", "rZ", "rwA", "rwMZ" } },
    { INDEX_op_add2_i64, { "r", "r", "rZ", "rZ", "rA", "rMZ" } },
    { INDEX_op_sub2_i32, { "r", "r", "rZ", "rZ", "rwA", "rwMZ" } },
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
                     (1 << TCG_REG_X18));

    tcg_regset_clear(s->reserved_regs);
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_SP);
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_FP);
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_TMP);
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_X18); /* platform register */

    tcg_add_target_add_op_defs(aarch64_op_defs);
}

static void tcg_target_qemu_prologue(TCGContext *s)
{
    /* NB: frame sizes are in 16 byte stack units! */
    int frame_size_callee_saved, frame_size_tcg_locals;
    TCGReg r;

    /* save pairs             (FP, LR) and (X19, X20) .. (X27, X28) */
    frame_size_callee_saved = (1) + (TCG_REG_X28 - TCG_REG_X19) / 2 + 1;

    /* frame size requirement for TCG local variables */
    frame_size_tcg_locals = TCG_STATIC_CALL_ARGS_SIZE
        + CPU_TEMP_BUF_NLONGS * sizeof(long)
        + (TCG_TARGET_STACK_ALIGN - 1);
    frame_size_tcg_locals &= ~(TCG_TARGET_STACK_ALIGN - 1);
    frame_size_tcg_locals /= TCG_TARGET_STACK_ALIGN;

    /* push (FP, LR) and update sp */
    tcg_out_push_pair(s, TCG_REG_SP,
                      TCG_REG_FP, TCG_REG_LR, frame_size_callee_saved);

    /* FP -> callee_saved */
    tcg_out_movr_sp(s, 1, TCG_REG_FP, TCG_REG_SP);

    /* store callee-preserved regs x19..x28 using FP -> callee_saved */
    for (r = TCG_REG_X19; r <= TCG_REG_X27; r += 2) {
        int idx = (r - TCG_REG_X19) / 2 + 1;
        tcg_out_store_pair(s, TCG_REG_FP, r, r + 1, idx);
    }

    /* Make stack space for TCG locals.  */
    tcg_out_insn(s, 3401, SUBI, TCG_TYPE_I64, TCG_REG_SP, TCG_REG_SP,
                 frame_size_tcg_locals * TCG_TARGET_STACK_ALIGN);

    /* inform TCG about how to find TCG locals with register, offset, size */
    tcg_set_frame(s, TCG_REG_SP, TCG_STATIC_CALL_ARGS_SIZE,
                  CPU_TEMP_BUF_NLONGS * sizeof(long));

#if defined(CONFIG_USE_GUEST_BASE)
    if (GUEST_BASE) {
        tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_GUEST_BASE, GUEST_BASE);
        tcg_regset_set_reg(s->reserved_regs, TCG_REG_GUEST_BASE);
    }
#endif

    tcg_out_mov(s, TCG_TYPE_PTR, TCG_AREG0, tcg_target_call_iarg_regs[0]);
    tcg_out_gotor(s, tcg_target_call_iarg_regs[1]);

    tb_ret_addr = s->code_ptr;

    /* Remove TCG locals stack space.  */
    tcg_out_insn(s, 3401, ADDI, TCG_TYPE_I64, TCG_REG_SP, TCG_REG_SP,
                 frame_size_tcg_locals * TCG_TARGET_STACK_ALIGN);

    /* restore registers x19..x28.
       FP must be preserved, so it still points to callee_saved area */
    for (r = TCG_REG_X19; r <= TCG_REG_X27; r += 2) {
        int idx = (r - TCG_REG_X19) / 2 + 1;
        tcg_out_load_pair(s, TCG_REG_FP, r, r + 1, idx);
    }

    /* pop (FP, LR), restore SP to previous frame, return */
    tcg_out_pop_pair(s, TCG_REG_SP,
                     TCG_REG_FP, TCG_REG_LR, frame_size_callee_saved);
    tcg_out_ret(s);
}

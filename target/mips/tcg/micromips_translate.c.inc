/*
 *  microMIPS translation routines
 *
 *  Copyright (c) 2004-2005 Jocelyn Mayer
 *  Copyright (c) 2006 Marius Groeger (FPU operations)
 *  Copyright (c) 2006 Thiemo Seufer (MIPS32R2 support)
 *  Copyright (c) 2009 CodeSourcery (MIPS16 and microMIPS support)
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/*
 * microMIPS32/microMIPS64 major opcodes
 *
 * 1. MIPS Architecture for Programmers Volume II-B:
 *      The microMIPS32 Instruction Set (Revision 3.05)
 *
 *    Table 6.2 microMIPS32 Encoding of Major Opcode Field
 *
 * 2. MIPS Architecture For Programmers Volume II-A:
 *      The MIPS64 Instruction Set (Revision 3.51)
 */

enum {
    POOL32A = 0x00,
    POOL16A = 0x01,
    LBU16 = 0x02,
    MOVE16 = 0x03,
    ADDI32 = 0x04,
    R6_LUI = 0x04,
    AUI = 0x04,
    LBU32 = 0x05,
    SB32 = 0x06,
    LB32 = 0x07,

    POOL32B = 0x08,
    POOL16B = 0x09,
    LHU16 = 0x0a,
    ANDI16 = 0x0b,
    ADDIU32 = 0x0c,
    LHU32 = 0x0d,
    SH32 = 0x0e,
    LH32 = 0x0f,

    POOL32I = 0x10,
    POOL16C = 0x11,
    LWSP16 = 0x12,
    POOL16D = 0x13,
    ORI32 = 0x14,
    POOL32F = 0x15,
    POOL32S = 0x16,  /* MIPS64 */
    DADDIU32 = 0x17, /* MIPS64 */

    POOL32C = 0x18,
    LWGP16 = 0x19,
    LW16 = 0x1a,
    POOL16E = 0x1b,
    XORI32 = 0x1c,
    JALS32 = 0x1d,
    BOVC = 0x1d,
    BEQC = 0x1d,
    BEQZALC = 0x1d,
    ADDIUPC = 0x1e,
    PCREL = 0x1e,
    BNVC = 0x1f,
    BNEC = 0x1f,
    BNEZALC = 0x1f,

    R6_BEQZC = 0x20,
    JIC = 0x20,
    POOL16F = 0x21,
    SB16 = 0x22,
    BEQZ16 = 0x23,
    BEQZC16 = 0x23,
    SLTI32 = 0x24,
    BEQ32 = 0x25,
    BC = 0x25,
    SWC132 = 0x26,
    LWC132 = 0x27,

    /* 0x29 is reserved */
    RES_29 = 0x29,
    R6_BNEZC = 0x28,
    JIALC = 0x28,
    SH16 = 0x2a,
    BNEZ16 = 0x2b,
    BNEZC16 = 0x2b,
    SLTIU32 = 0x2c,
    BNE32 = 0x2d,
    BALC = 0x2d,
    SDC132 = 0x2e,
    LDC132 = 0x2f,

    /* 0x31 is reserved */
    RES_31 = 0x31,
    BLEZALC = 0x30,
    BGEZALC = 0x30,
    BGEUC = 0x30,
    SWSP16 = 0x32,
    B16 = 0x33,
    BC16 = 0x33,
    ANDI32 = 0x34,
    J32 = 0x35,
    BGTZC = 0x35,
    BLTZC = 0x35,
    BLTC = 0x35,
    SD32 = 0x36, /* MIPS64 */
    LD32 = 0x37, /* MIPS64 */

    /* 0x39 is reserved */
    RES_39 = 0x39,
    BGTZALC = 0x38,
    BLTZALC = 0x38,
    BLTUC = 0x38,
    SW16 = 0x3a,
    LI16 = 0x3b,
    JALX32 = 0x3c,
    JAL32 = 0x3d,
    BLEZC = 0x3d,
    BGEZC = 0x3d,
    BGEC = 0x3d,
    SW32 = 0x3e,
    LW32 = 0x3f
};

/* PCREL Instructions perform PC-Relative address calculation. bits 20..16 */
enum {
    ADDIUPC_00 = 0x00,
    ADDIUPC_01 = 0x01,
    ADDIUPC_02 = 0x02,
    ADDIUPC_03 = 0x03,
    ADDIUPC_04 = 0x04,
    ADDIUPC_05 = 0x05,
    ADDIUPC_06 = 0x06,
    ADDIUPC_07 = 0x07,
    AUIPC = 0x1e,
    ALUIPC = 0x1f,
    LWPC_08 = 0x08,
    LWPC_09 = 0x09,
    LWPC_0A = 0x0A,
    LWPC_0B = 0x0B,
    LWPC_0C = 0x0C,
    LWPC_0D = 0x0D,
    LWPC_0E = 0x0E,
    LWPC_0F = 0x0F,
};

/* POOL32A encoding of minor opcode field */

enum {
    /*
     * These opcodes are distinguished only by bits 9..6; those bits are
     * what are recorded below.
     */
    SLL32 = 0x0,
    SRL32 = 0x1,
    SRA = 0x2,
    ROTR = 0x3,
    SELEQZ = 0x5,
    SELNEZ = 0x6,
    R6_RDHWR = 0x7,

    SLLV = 0x0,
    SRLV = 0x1,
    SRAV = 0x2,
    ROTRV = 0x3,
    ADD = 0x4,
    ADDU32 = 0x5,
    SUB = 0x6,
    SUBU32 = 0x7,
    MUL = 0x8,
    AND = 0x9,
    OR32 = 0xa,
    NOR = 0xb,
    XOR32 = 0xc,
    SLT = 0xd,
    SLTU = 0xe,

    MOVN = 0x0,
    R6_MUL  = 0x0,
    MOVZ = 0x1,
    MUH  = 0x1,
    MULU = 0x2,
    MUHU = 0x3,
    LWXS = 0x4,
    R6_DIV  = 0x4,
    MOD  = 0x5,
    R6_DIVU = 0x6,
    MODU = 0x7,

    /* The following can be distinguished by their lower 6 bits. */
    BREAK32 = 0x07,
    INS = 0x0c,
    LSA = 0x0f,
    ALIGN = 0x1f,
    EXT = 0x2c,
    POOL32AXF = 0x3c,
    SIGRIE = 0x3f
};

/* POOL32AXF encoding of minor opcode field extension */

/*
 * 1. MIPS Architecture for Programmers Volume II-B:
 *      The microMIPS32 Instruction Set (Revision 3.05)
 *
 *    Table 6.5 POOL32Axf Encoding of Minor Opcode Extension Field
 *
 * 2. MIPS Architecture for Programmers VolumeIV-e:
 *      The MIPS DSP Application-Specific Extension
 *        to the microMIPS32 Architecture (Revision 2.34)
 *
 *    Table 5.5 POOL32Axf Encoding of Minor Opcode Extension Field
 */

enum {
    /* bits 11..6 */
    TEQ = 0x00,
    TGE = 0x08,
    TGEU = 0x10,
    TLT = 0x20,
    TLTU = 0x28,
    TNE = 0x30,

    MFC0 = 0x03,
    MTC0 = 0x0b,

    /* begin of microMIPS32 DSP */

    /* bits 13..12 for 0x01 */
    MFHI_ACC = 0x0,
    MFLO_ACC = 0x1,
    MTHI_ACC = 0x2,
    MTLO_ACC = 0x3,

    /* bits 13..12 for 0x2a */
    MADD_ACC = 0x0,
    MADDU_ACC = 0x1,
    MSUB_ACC = 0x2,
    MSUBU_ACC = 0x3,

    /* bits 13..12 for 0x32 */
    MULT_ACC = 0x0,
    MULTU_ACC = 0x1,

    /* end of microMIPS32 DSP */

    /* bits 15..12 for 0x2c */
    BITSWAP = 0x0,
    SEB = 0x2,
    SEH = 0x3,
    CLO = 0x4,
    CLZ = 0x5,
    RDHWR = 0x6,
    WSBH = 0x7,
    MULT = 0x8,
    MULTU = 0x9,
    DIV = 0xa,
    DIVU = 0xb,
    MADD = 0xc,
    MADDU = 0xd,
    MSUB = 0xe,
    MSUBU = 0xf,

    /* bits 15..12 for 0x34 */
    MFC2 = 0x4,
    MTC2 = 0x5,
    MFHC2 = 0x8,
    MTHC2 = 0x9,
    CFC2 = 0xc,
    CTC2 = 0xd,

    /* bits 15..12 for 0x3c */
    JALR = 0x0,
    JR = 0x0,                   /* alias */
    JALRC = 0x0,
    JRC = 0x0,
    JALR_HB = 0x1,
    JALRC_HB = 0x1,
    JALRS = 0x4,
    JALRS_HB = 0x5,

    /* bits 15..12 for 0x05 */
    RDPGPR = 0xe,
    WRPGPR = 0xf,

    /* bits 15..12 for 0x0d */
    TLBP = 0x0,
    TLBR = 0x1,
    TLBWI = 0x2,
    TLBWR = 0x3,
    TLBINV = 0x4,
    TLBINVF = 0x5,
    WAIT = 0x9,
    IRET = 0xd,
    DERET = 0xe,
    ERET = 0xf,

    /* bits 15..12 for 0x15 */
    DMT = 0x0,
    DVPE = 0x1,
    EMT = 0x2,
    EVPE = 0x3,

    /* bits 15..12 for 0x1d */
    DI = 0x4,
    EI = 0x5,

    /* bits 15..12 for 0x2d */
    SYNC = 0x6,
    SYSCALL = 0x8,
    SDBBP = 0xd,

    /* bits 15..12 for 0x35 */
    MFHI32 = 0x0,
    MFLO32 = 0x1,
    MTHI32 = 0x2,
    MTLO32 = 0x3,
};

/* POOL32B encoding of minor opcode field (bits 15..12) */

enum {
    LWC2 = 0x0,
    LWP = 0x1,
    LDP = 0x4,
    LWM32 = 0x5,
    CACHE = 0x6,
    LDM = 0x7,
    SWC2 = 0x8,
    SWP = 0x9,
    SDP = 0xc,
    SWM32 = 0xd,
    SDM = 0xf
};

/* POOL32C encoding of minor opcode field (bits 15..12) */

enum {
    LWL = 0x0,
    SWL = 0x8,
    LWR = 0x1,
    SWR = 0x9,
    PREF = 0x2,
    ST_EVA = 0xa,
    LL = 0x3,
    SC = 0xb,
    LDL = 0x4,
    SDL = 0xc,
    LDR = 0x5,
    SDR = 0xd,
    LD_EVA = 0x6,
    LWU = 0xe,
    LLD = 0x7,
    SCD = 0xf
};

/* POOL32C LD-EVA encoding of minor opcode field (bits 11..9) */

enum {
    LBUE = 0x0,
    LHUE = 0x1,
    LWLE = 0x2,
    LWRE = 0x3,
    LBE = 0x4,
    LHE = 0x5,
    LLE = 0x6,
    LWE = 0x7,
};

/* POOL32C ST-EVA encoding of minor opcode field (bits 11..9) */

enum {
    SWLE = 0x0,
    SWRE = 0x1,
    PREFE = 0x2,
    CACHEE = 0x3,
    SBE = 0x4,
    SHE = 0x5,
    SCE = 0x6,
    SWE = 0x7,
};

/* POOL32F encoding of minor opcode field (bits 5..0) */

enum {
    /* These are the bit 7..6 values */
    ADD_FMT = 0x0,

    SUB_FMT = 0x1,

    MUL_FMT = 0x2,

    DIV_FMT = 0x3,

    /* These are the bit 8..6 values */
    MOVN_FMT = 0x0,
    RSQRT2_FMT = 0x0,
    MOVF_FMT = 0x0,
    RINT_FMT = 0x0,
    SELNEZ_FMT = 0x0,

    MOVZ_FMT = 0x1,
    LWXC1 = 0x1,
    MOVT_FMT = 0x1,
    CLASS_FMT = 0x1,
    SELEQZ_FMT = 0x1,

    PLL_PS = 0x2,
    SWXC1 = 0x2,
    SEL_FMT = 0x2,

    PLU_PS = 0x3,
    LDXC1 = 0x3,

    MOVN_FMT_04 = 0x4,
    PUL_PS = 0x4,
    SDXC1 = 0x4,
    RECIP2_FMT = 0x4,

    MOVZ_FMT_05 = 0x05,
    PUU_PS = 0x5,
    LUXC1 = 0x5,

    CVT_PS_S = 0x6,
    SUXC1 = 0x6,
    ADDR_PS = 0x6,
    PREFX = 0x6,
    MADDF_FMT = 0x6,

    MULR_PS = 0x7,
    MSUBF_FMT = 0x7,

    MADD_S = 0x01,
    MADD_D = 0x09,
    MADD_PS = 0x11,
    ALNV_PS = 0x19,
    MSUB_S = 0x21,
    MSUB_D = 0x29,
    MSUB_PS = 0x31,

    NMADD_S = 0x02,
    NMADD_D = 0x0a,
    NMADD_PS = 0x12,
    NMSUB_S = 0x22,
    NMSUB_D = 0x2a,
    NMSUB_PS = 0x32,

    MIN_FMT = 0x3,
    MAX_FMT = 0xb,
    MINA_FMT = 0x23,
    MAXA_FMT = 0x2b,
    POOL32FXF = 0x3b,

    CABS_COND_FMT = 0x1c,              /* MIPS3D */
    C_COND_FMT = 0x3c,

    CMP_CONDN_S = 0x5,
    CMP_CONDN_D = 0x15
};

/* POOL32Fxf encoding of minor opcode extension field */

enum {
    CVT_L = 0x04,
    RSQRT_FMT = 0x08,
    FLOOR_L = 0x0c,
    CVT_PW_PS = 0x1c,
    CVT_W = 0x24,
    SQRT_FMT = 0x28,
    FLOOR_W = 0x2c,
    CVT_PS_PW = 0x3c,
    CFC1 = 0x40,
    RECIP_FMT = 0x48,
    CEIL_L = 0x4c,
    CTC1 = 0x60,
    CEIL_W = 0x6c,
    MFC1 = 0x80,
    CVT_S_PL = 0x84,
    TRUNC_L = 0x8c,
    MTC1 = 0xa0,
    CVT_S_PU = 0xa4,
    TRUNC_W = 0xac,
    MFHC1 = 0xc0,
    ROUND_L = 0xcc,
    MTHC1 = 0xe0,
    ROUND_W = 0xec,

    MOV_FMT = 0x01,
    MOVF = 0x05,
    ABS_FMT = 0x0d,
    RSQRT1_FMT = 0x1d,
    MOVT = 0x25,
    NEG_FMT = 0x2d,
    CVT_D = 0x4d,
    RECIP1_FMT = 0x5d,
    CVT_S = 0x6d
};

/* POOL32I encoding of minor opcode field (bits 25..21) */

enum {
    BLTZ = 0x00,
    BLTZAL = 0x01,
    BGEZ = 0x02,
    BGEZAL = 0x03,
    BLEZ = 0x04,
    BNEZC = 0x05,
    BGTZ = 0x06,
    BEQZC = 0x07,
    TLTI = 0x08,
    BC1EQZC = 0x08,
    TGEI = 0x09,
    BC1NEZC = 0x09,
    TLTIU = 0x0a,
    BC2EQZC = 0x0a,
    TGEIU = 0x0b,
    BC2NEZC = 0x0a,
    TNEI = 0x0c,
    R6_SYNCI = 0x0c,
    LUI = 0x0d,
    TEQI = 0x0e,
    SYNCI = 0x10,
    BLTZALS = 0x11,
    BGEZALS = 0x13,
    BC2F = 0x14,
    BC2T = 0x15,
    /* These overlap and are distinguished by bit16 of the instruction */
    BC1F = 0x1c,
    BC1T = 0x1d,
    BC1ANY2F = 0x1c,
    BC1ANY2T = 0x1d,
    BC1ANY4F = 0x1e,
    BC1ANY4T = 0x1f
};

/* POOL16A encoding of minor opcode field */

enum {
    ADDU16 = 0x0,
    SUBU16 = 0x1
};

/* POOL16B encoding of minor opcode field */

enum {
    SLL16 = 0x0,
    SRL16 = 0x1
};

/* POOL16C encoding of minor opcode field */

enum {
    NOT16 = 0x00,
    XOR16 = 0x04,
    AND16 = 0x08,
    OR16 = 0x0c,
    LWM16 = 0x10,
    SWM16 = 0x14,
    JR16 = 0x18,
    JRC16 = 0x1a,
    JALR16 = 0x1c,
    JALR16S = 0x1e,
    MFHI16 = 0x20,
    MFLO16 = 0x24,
    BREAK16 = 0x28,
    SDBBP16 = 0x2c,
    JRADDIUSP = 0x30
};

/* R6 POOL16C encoding of minor opcode field (bits 0..5) */

enum {
    R6_NOT16    = 0x00,
    R6_AND16    = 0x01,
    R6_LWM16    = 0x02,
    R6_JRC16    = 0x03,
    MOVEP       = 0x04,
    MOVEP_05    = 0x05,
    MOVEP_06    = 0x06,
    MOVEP_07    = 0x07,
    R6_XOR16    = 0x08,
    R6_OR16     = 0x09,
    R6_SWM16    = 0x0a,
    JALRC16     = 0x0b,
    MOVEP_0C    = 0x0c,
    MOVEP_0D    = 0x0d,
    MOVEP_0E    = 0x0e,
    MOVEP_0F    = 0x0f,
    JRCADDIUSP  = 0x13,
    R6_BREAK16  = 0x1b,
    R6_SDBBP16  = 0x3b
};

/* POOL16D encoding of minor opcode field */

enum {
    ADDIUS5 = 0x0,
    ADDIUSP = 0x1
};

/* POOL16E encoding of minor opcode field */

enum {
    ADDIUR2 = 0x0,
    ADDIUR1SP = 0x1
};

static int mmreg(int r)
{
    static const int map[] = { 16, 17, 2, 3, 4, 5, 6, 7 };

    return map[r];
}

/* Used for 16-bit store instructions.  */
static int mmreg2(int r)
{
    static const int map[] = { 0, 17, 2, 3, 4, 5, 6, 7 };

    return map[r];
}

#define uMIPS_RD(op) ((op >> 7) & 0x7)
#define uMIPS_RS(op) ((op >> 4) & 0x7)
#define uMIPS_RS2(op) uMIPS_RS(op)
#define uMIPS_RS1(op) ((op >> 1) & 0x7)
#define uMIPS_RD5(op) ((op >> 5) & 0x1f)
#define uMIPS_RS5(op) (op & 0x1f)

/* Signed immediate */
#define SIMM(op, start, width)                                          \
    ((int32_t)(((op >> start) & ((~0U) >> (32 - width)))                \
               << (32 - width))                                         \
     >> (32 - width))
/* Zero-extended immediate */
#define ZIMM(op, start, width) ((op >> start) & ((~0U) >> (32 - width)))

static void gen_addiur1sp(DisasContext *ctx)
{
    int rd = mmreg(uMIPS_RD(ctx->opcode));

    gen_arith_imm(ctx, OPC_ADDIU, rd, 29, ((ctx->opcode >> 1) & 0x3f) << 2);
}

static void gen_addiur2(DisasContext *ctx)
{
    static const int decoded_imm[] = { 1, 4, 8, 12, 16, 20, 24, -1 };
    int rd = mmreg(uMIPS_RD(ctx->opcode));
    int rs = mmreg(uMIPS_RS(ctx->opcode));

    gen_arith_imm(ctx, OPC_ADDIU, rd, rs, decoded_imm[ZIMM(ctx->opcode, 1, 3)]);
}

static void gen_addiusp(DisasContext *ctx)
{
    int encoded = ZIMM(ctx->opcode, 1, 9);
    int decoded;

    if (encoded <= 1) {
        decoded = 256 + encoded;
    } else if (encoded <= 255) {
        decoded = encoded;
    } else if (encoded <= 509) {
        decoded = encoded - 512;
    } else {
        decoded = encoded - 768;
    }

    gen_arith_imm(ctx, OPC_ADDIU, 29, 29, decoded << 2);
}

static void gen_addius5(DisasContext *ctx)
{
    int imm = SIMM(ctx->opcode, 1, 4);
    int rd = (ctx->opcode >> 5) & 0x1f;

    gen_arith_imm(ctx, OPC_ADDIU, rd, rd, imm);
}

static void gen_andi16(DisasContext *ctx)
{
    static const int decoded_imm[] = { 128, 1, 2, 3, 4, 7, 8, 15, 16,
                                 31, 32, 63, 64, 255, 32768, 65535 };
    int rd = mmreg(uMIPS_RD(ctx->opcode));
    int rs = mmreg(uMIPS_RS(ctx->opcode));
    int encoded = ZIMM(ctx->opcode, 0, 4);

    gen_logic_imm(ctx, OPC_ANDI, rd, rs, decoded_imm[encoded]);
}

static void gen_ldst_multiple(DisasContext *ctx, uint32_t opc, int reglist,
                              int base, int16_t offset)
{
    TCGv t0, t1;
    TCGv_i32 t2;

    if (ctx->hflags & MIPS_HFLAG_BMASK) {
        gen_reserved_instruction(ctx);
        return;
    }

    t0 = tcg_temp_new();

    gen_base_offset_addr(ctx, t0, base, offset);

    t1 = tcg_constant_tl(reglist);
    t2 = tcg_constant_i32(ctx->mem_idx);

    save_cpu_state(ctx, 1);
    switch (opc) {
    case LWM32:
        gen_helper_lwm(cpu_env, t0, t1, t2);
        break;
    case SWM32:
        gen_helper_swm(cpu_env, t0, t1, t2);
        break;
#ifdef TARGET_MIPS64
    case LDM:
        gen_helper_ldm(cpu_env, t0, t1, t2);
        break;
    case SDM:
        gen_helper_sdm(cpu_env, t0, t1, t2);
        break;
#endif
    }
}


static void gen_pool16c_insn(DisasContext *ctx)
{
    int rd = mmreg((ctx->opcode >> 3) & 0x7);
    int rs = mmreg(ctx->opcode & 0x7);

    switch (((ctx->opcode) >> 4) & 0x3f) {
    case NOT16 + 0:
    case NOT16 + 1:
    case NOT16 + 2:
    case NOT16 + 3:
        gen_logic(ctx, OPC_NOR, rd, rs, 0);
        break;
    case XOR16 + 0:
    case XOR16 + 1:
    case XOR16 + 2:
    case XOR16 + 3:
        gen_logic(ctx, OPC_XOR, rd, rd, rs);
        break;
    case AND16 + 0:
    case AND16 + 1:
    case AND16 + 2:
    case AND16 + 3:
        gen_logic(ctx, OPC_AND, rd, rd, rs);
        break;
    case OR16 + 0:
    case OR16 + 1:
    case OR16 + 2:
    case OR16 + 3:
        gen_logic(ctx, OPC_OR, rd, rd, rs);
        break;
    case LWM16 + 0:
    case LWM16 + 1:
    case LWM16 + 2:
    case LWM16 + 3:
        {
            static const int lwm_convert[] = { 0x11, 0x12, 0x13, 0x14 };
            int offset = ZIMM(ctx->opcode, 0, 4);

            gen_ldst_multiple(ctx, LWM32, lwm_convert[(ctx->opcode >> 4) & 0x3],
                              29, offset << 2);
        }
        break;
    case SWM16 + 0:
    case SWM16 + 1:
    case SWM16 + 2:
    case SWM16 + 3:
        {
            static const int swm_convert[] = { 0x11, 0x12, 0x13, 0x14 };
            int offset = ZIMM(ctx->opcode, 0, 4);

            gen_ldst_multiple(ctx, SWM32, swm_convert[(ctx->opcode >> 4) & 0x3],
                              29, offset << 2);
        }
        break;
    case JR16 + 0:
    case JR16 + 1:
        {
            int reg = ctx->opcode & 0x1f;

            gen_compute_branch(ctx, OPC_JR, 2, reg, 0, 0, 4);
        }
        break;
    case JRC16 + 0:
    case JRC16 + 1:
        {
            int reg = ctx->opcode & 0x1f;
            gen_compute_branch(ctx, OPC_JR, 2, reg, 0, 0, 0);
            /*
             * Let normal delay slot handling in our caller take us
             * to the branch target.
             */
        }
        break;
    case JALR16 + 0:
    case JALR16 + 1:
        gen_compute_branch(ctx, OPC_JALR, 2, ctx->opcode & 0x1f, 31, 0, 4);
        ctx->hflags |= MIPS_HFLAG_BDS_STRICT;
        break;
    case JALR16S + 0:
    case JALR16S + 1:
        gen_compute_branch(ctx, OPC_JALR, 2, ctx->opcode & 0x1f, 31, 0, 2);
        ctx->hflags |= MIPS_HFLAG_BDS_STRICT;
        break;
    case MFHI16 + 0:
    case MFHI16 + 1:
        gen_HILO(ctx, OPC_MFHI, 0, uMIPS_RS5(ctx->opcode));
        break;
    case MFLO16 + 0:
    case MFLO16 + 1:
        gen_HILO(ctx, OPC_MFLO, 0, uMIPS_RS5(ctx->opcode));
        break;
    case BREAK16:
        generate_exception_break(ctx, extract32(ctx->opcode, 0, 4));
        break;
    case SDBBP16:
        if (is_uhi(ctx, extract32(ctx->opcode, 0, 4))) {
            ctx->base.is_jmp = DISAS_SEMIHOST;
        } else {
            /*
             * XXX: not clear which exception should be raised
             *      when in debug mode...
             */
            check_insn(ctx, ISA_MIPS_R1);
            generate_exception_end(ctx, EXCP_DBp);
        }
        break;
    case JRADDIUSP + 0:
    case JRADDIUSP + 1:
        {
            int imm = ZIMM(ctx->opcode, 0, 5);
            gen_compute_branch(ctx, OPC_JR, 2, 31, 0, 0, 0);
            gen_arith_imm(ctx, OPC_ADDIU, 29, 29, imm << 2);
            /*
             * Let normal delay slot handling in our caller take us
             * to the branch target.
             */
        }
        break;
    default:
        gen_reserved_instruction(ctx);
        break;
    }
}

static inline void gen_movep(DisasContext *ctx, int enc_dest, int enc_rt,
                             int enc_rs)
{
    int rd, re;
    static const int rd_enc[] = { 5, 5, 6, 4, 4, 4, 4, 4 };
    static const int re_enc[] = { 6, 7, 7, 21, 22, 5, 6, 7 };
    static const int rs_rt_enc[] = { 0, 17, 2, 3, 16, 18, 19, 20 };

    rd = rd_enc[enc_dest];
    re = re_enc[enc_dest];
    gen_load_gpr(cpu_gpr[rd], rs_rt_enc[enc_rs]);
    gen_load_gpr(cpu_gpr[re], rs_rt_enc[enc_rt]);
}

static void gen_pool16c_r6_insn(DisasContext *ctx)
{
    int rt = mmreg((ctx->opcode >> 7) & 0x7);
    int rs = mmreg((ctx->opcode >> 4) & 0x7);

    switch (ctx->opcode & 0xf) {
    case R6_NOT16:
        gen_logic(ctx, OPC_NOR, rt, rs, 0);
        break;
    case R6_AND16:
        gen_logic(ctx, OPC_AND, rt, rt, rs);
        break;
    case R6_LWM16:
        {
            int lwm_converted = 0x11 + extract32(ctx->opcode, 8, 2);
            int offset = extract32(ctx->opcode, 4, 4);
            gen_ldst_multiple(ctx, LWM32, lwm_converted, 29, offset << 2);
        }
        break;
    case R6_JRC16: /* JRCADDIUSP */
        if ((ctx->opcode >> 4) & 1) {
            /* JRCADDIUSP */
            int imm = extract32(ctx->opcode, 5, 5);
            gen_compute_branch(ctx, OPC_JR, 2, 31, 0, 0, 0);
            gen_arith_imm(ctx, OPC_ADDIU, 29, 29, imm << 2);
        } else {
            /* JRC16 */
            rs = extract32(ctx->opcode, 5, 5);
            gen_compute_branch(ctx, OPC_JR, 2, rs, 0, 0, 0);
        }
        break;
    case MOVEP:
    case MOVEP_05:
    case MOVEP_06:
    case MOVEP_07:
    case MOVEP_0C:
    case MOVEP_0D:
    case MOVEP_0E:
    case MOVEP_0F:
        {
            int enc_dest = uMIPS_RD(ctx->opcode);
            int enc_rt = uMIPS_RS2(ctx->opcode);
            int enc_rs = (ctx->opcode & 3) | ((ctx->opcode >> 1) & 4);
            gen_movep(ctx, enc_dest, enc_rt, enc_rs);
        }
        break;
    case R6_XOR16:
        gen_logic(ctx, OPC_XOR, rt, rt, rs);
        break;
    case R6_OR16:
        gen_logic(ctx, OPC_OR, rt, rt, rs);
        break;
    case R6_SWM16:
        {
            int swm_converted = 0x11 + extract32(ctx->opcode, 8, 2);
            int offset = extract32(ctx->opcode, 4, 4);
            gen_ldst_multiple(ctx, SWM32, swm_converted, 29, offset << 2);
        }
        break;
    case JALRC16: /* BREAK16, SDBBP16 */
        switch (ctx->opcode & 0x3f) {
        case JALRC16:
        case JALRC16 + 0x20:
            /* JALRC16 */
            gen_compute_branch(ctx, OPC_JALR, 2, (ctx->opcode >> 5) & 0x1f,
                               31, 0, 0);
            break;
        case R6_BREAK16:
            /* BREAK16 */
            generate_exception_break(ctx, extract32(ctx->opcode, 6, 4));
            break;
        case R6_SDBBP16:
            /* SDBBP16 */
            if (is_uhi(ctx, extract32(ctx->opcode, 6, 4))) {
                ctx->base.is_jmp = DISAS_SEMIHOST;
            } else {
                if (ctx->hflags & MIPS_HFLAG_SBRI) {
                    generate_exception(ctx, EXCP_RI);
                } else {
                    generate_exception(ctx, EXCP_DBp);
                }
            }
            break;
        }
        break;
    default:
        generate_exception(ctx, EXCP_RI);
        break;
    }
}

static void gen_ldst_pair(DisasContext *ctx, uint32_t opc, int rd,
                          int base, int16_t offset)
{
    TCGv t0, t1;

    if (ctx->hflags & MIPS_HFLAG_BMASK || rd == 31) {
        gen_reserved_instruction(ctx);
        return;
    }

    t0 = tcg_temp_new();
    t1 = tcg_temp_new();

    gen_base_offset_addr(ctx, t0, base, offset);

    switch (opc) {
    case LWP:
        if (rd == base) {
            gen_reserved_instruction(ctx);
            return;
        }
        tcg_gen_qemu_ld_tl(t1, t0, ctx->mem_idx, MO_TESL |
                           ctx->default_tcg_memop_mask);
        gen_store_gpr(t1, rd);
        tcg_gen_movi_tl(t1, 4);
        gen_op_addr_add(ctx, t0, t0, t1);
        tcg_gen_qemu_ld_tl(t1, t0, ctx->mem_idx, MO_TESL |
                           ctx->default_tcg_memop_mask);
        gen_store_gpr(t1, rd + 1);
        break;
    case SWP:
        gen_load_gpr(t1, rd);
        tcg_gen_qemu_st_tl(t1, t0, ctx->mem_idx, MO_TEUL |
                           ctx->default_tcg_memop_mask);
        tcg_gen_movi_tl(t1, 4);
        gen_op_addr_add(ctx, t0, t0, t1);
        gen_load_gpr(t1, rd + 1);
        tcg_gen_qemu_st_tl(t1, t0, ctx->mem_idx, MO_TEUL |
                           ctx->default_tcg_memop_mask);
        break;
#ifdef TARGET_MIPS64
    case LDP:
        if (rd == base) {
            gen_reserved_instruction(ctx);
            return;
        }
        tcg_gen_qemu_ld_tl(t1, t0, ctx->mem_idx, MO_TEUQ |
                           ctx->default_tcg_memop_mask);
        gen_store_gpr(t1, rd);
        tcg_gen_movi_tl(t1, 8);
        gen_op_addr_add(ctx, t0, t0, t1);
        tcg_gen_qemu_ld_tl(t1, t0, ctx->mem_idx, MO_TEUQ |
                           ctx->default_tcg_memop_mask);
        gen_store_gpr(t1, rd + 1);
        break;
    case SDP:
        gen_load_gpr(t1, rd);
        tcg_gen_qemu_st_tl(t1, t0, ctx->mem_idx, MO_TEUQ |
                           ctx->default_tcg_memop_mask);
        tcg_gen_movi_tl(t1, 8);
        gen_op_addr_add(ctx, t0, t0, t1);
        gen_load_gpr(t1, rd + 1);
        tcg_gen_qemu_st_tl(t1, t0, ctx->mem_idx, MO_TEUQ |
                           ctx->default_tcg_memop_mask);
        break;
#endif
    }
}

static void gen_pool32axf(CPUMIPSState *env, DisasContext *ctx, int rt, int rs)
{
    int extension = (ctx->opcode >> 6) & 0x3f;
    int minor = (ctx->opcode >> 12) & 0xf;
    uint32_t mips32_op;

    switch (extension) {
    case TEQ:
        mips32_op = OPC_TEQ;
        goto do_trap;
    case TGE:
        mips32_op = OPC_TGE;
        goto do_trap;
    case TGEU:
        mips32_op = OPC_TGEU;
        goto do_trap;
    case TLT:
        mips32_op = OPC_TLT;
        goto do_trap;
    case TLTU:
        mips32_op = OPC_TLTU;
        goto do_trap;
    case TNE:
        mips32_op = OPC_TNE;
    do_trap:
        gen_trap(ctx, mips32_op, rs, rt, -1, extract32(ctx->opcode, 12, 4));
        break;
#ifndef CONFIG_USER_ONLY
    case MFC0:
    case MFC0 + 32:
        check_cp0_enabled(ctx);
        if (rt == 0) {
            /* Treat as NOP. */
            break;
        }
        gen_mfc0(ctx, cpu_gpr[rt], rs, (ctx->opcode >> 11) & 0x7);
        break;
    case MTC0:
    case MTC0 + 32:
        check_cp0_enabled(ctx);
        {
            TCGv t0 = tcg_temp_new();

            gen_load_gpr(t0, rt);
            gen_mtc0(ctx, t0, rs, (ctx->opcode >> 11) & 0x7);
        }
        break;
#endif
    case 0x2a:
        switch (minor & 3) {
        case MADD_ACC:
            gen_muldiv(ctx, OPC_MADD, (ctx->opcode >> 14) & 3, rs, rt);
            break;
        case MADDU_ACC:
            gen_muldiv(ctx, OPC_MADDU, (ctx->opcode >> 14) & 3, rs, rt);
            break;
        case MSUB_ACC:
            gen_muldiv(ctx, OPC_MSUB, (ctx->opcode >> 14) & 3, rs, rt);
            break;
        case MSUBU_ACC:
            gen_muldiv(ctx, OPC_MSUBU, (ctx->opcode >> 14) & 3, rs, rt);
            break;
        default:
            goto pool32axf_invalid;
        }
        break;
    case 0x32:
        switch (minor & 3) {
        case MULT_ACC:
            gen_muldiv(ctx, OPC_MULT, (ctx->opcode >> 14) & 3, rs, rt);
            break;
        case MULTU_ACC:
            gen_muldiv(ctx, OPC_MULTU, (ctx->opcode >> 14) & 3, rs, rt);
            break;
        default:
            goto pool32axf_invalid;
        }
        break;
    case 0x2c:
        switch (minor) {
        case BITSWAP:
            check_insn(ctx, ISA_MIPS_R6);
            gen_bitswap(ctx, OPC_BITSWAP, rs, rt);
            break;
        case SEB:
            gen_bshfl(ctx, OPC_SEB, rs, rt);
            break;
        case SEH:
            gen_bshfl(ctx, OPC_SEH, rs, rt);
            break;
        case CLO:
            mips32_op = OPC_CLO;
            goto do_cl;
        case CLZ:
            mips32_op = OPC_CLZ;
        do_cl:
            check_insn(ctx, ISA_MIPS_R1);
            gen_cl(ctx, mips32_op, rt, rs);
            break;
        case RDHWR:
            check_insn_opc_removed(ctx, ISA_MIPS_R6);
            gen_rdhwr(ctx, rt, rs, 0);
            break;
        case WSBH:
            gen_bshfl(ctx, OPC_WSBH, rs, rt);
            break;
        case MULT:
            check_insn_opc_removed(ctx, ISA_MIPS_R6);
            mips32_op = OPC_MULT;
            goto do_mul;
        case MULTU:
            check_insn_opc_removed(ctx, ISA_MIPS_R6);
            mips32_op = OPC_MULTU;
            goto do_mul;
        case DIV:
            check_insn_opc_removed(ctx, ISA_MIPS_R6);
            mips32_op = OPC_DIV;
            goto do_div;
        case DIVU:
            check_insn_opc_removed(ctx, ISA_MIPS_R6);
            mips32_op = OPC_DIVU;
            goto do_div;
        do_div:
            check_insn(ctx, ISA_MIPS_R1);
            gen_muldiv(ctx, mips32_op, 0, rs, rt);
            break;
        case MADD:
            check_insn_opc_removed(ctx, ISA_MIPS_R6);
            mips32_op = OPC_MADD;
            goto do_mul;
        case MADDU:
            check_insn_opc_removed(ctx, ISA_MIPS_R6);
            mips32_op = OPC_MADDU;
            goto do_mul;
        case MSUB:
            check_insn_opc_removed(ctx, ISA_MIPS_R6);
            mips32_op = OPC_MSUB;
            goto do_mul;
        case MSUBU:
            check_insn_opc_removed(ctx, ISA_MIPS_R6);
            mips32_op = OPC_MSUBU;
        do_mul:
            check_insn(ctx, ISA_MIPS_R1);
            gen_muldiv(ctx, mips32_op, 0, rs, rt);
            break;
        default:
            goto pool32axf_invalid;
        }
        break;
    case 0x34:
        switch (minor) {
        case MFC2:
        case MTC2:
        case MFHC2:
        case MTHC2:
        case CFC2:
        case CTC2:
            generate_exception_err(ctx, EXCP_CpU, 2);
            break;
        default:
            goto pool32axf_invalid;
        }
        break;
    case 0x3c:
        switch (minor) {
        case JALR:    /* JALRC */
        case JALR_HB: /* JALRC_HB */
            if (ctx->insn_flags & ISA_MIPS_R6) {
                /* JALRC, JALRC_HB */
                gen_compute_branch(ctx, OPC_JALR, 4, rs, rt, 0, 0);
            } else {
                /* JALR, JALR_HB */
                gen_compute_branch(ctx, OPC_JALR, 4, rs, rt, 0, 4);
                ctx->hflags |= MIPS_HFLAG_BDS_STRICT;
            }
            break;
        case JALRS:
        case JALRS_HB:
            check_insn_opc_removed(ctx, ISA_MIPS_R6);
            gen_compute_branch(ctx, OPC_JALR, 4, rs, rt, 0, 2);
            ctx->hflags |= MIPS_HFLAG_BDS_STRICT;
            break;
        default:
            goto pool32axf_invalid;
        }
        break;
    case 0x05:
        switch (minor) {
        case RDPGPR:
            check_cp0_enabled(ctx);
            check_insn(ctx, ISA_MIPS_R2);
            gen_load_srsgpr(rs, rt);
            break;
        case WRPGPR:
            check_cp0_enabled(ctx);
            check_insn(ctx, ISA_MIPS_R2);
            gen_store_srsgpr(rs, rt);
            break;
        default:
            goto pool32axf_invalid;
        }
        break;
#ifndef CONFIG_USER_ONLY
    case 0x0d:
        switch (minor) {
        case TLBP:
            mips32_op = OPC_TLBP;
            goto do_cp0;
        case TLBR:
            mips32_op = OPC_TLBR;
            goto do_cp0;
        case TLBWI:
            mips32_op = OPC_TLBWI;
            goto do_cp0;
        case TLBWR:
            mips32_op = OPC_TLBWR;
            goto do_cp0;
        case TLBINV:
            mips32_op = OPC_TLBINV;
            goto do_cp0;
        case TLBINVF:
            mips32_op = OPC_TLBINVF;
            goto do_cp0;
        case WAIT:
            mips32_op = OPC_WAIT;
            goto do_cp0;
        case DERET:
            mips32_op = OPC_DERET;
            goto do_cp0;
        case ERET:
            mips32_op = OPC_ERET;
        do_cp0:
            gen_cp0(env, ctx, mips32_op, rt, rs);
            break;
        default:
            goto pool32axf_invalid;
        }
        break;
    case 0x1d:
        switch (minor) {
        case DI:
            check_cp0_enabled(ctx);
            {
                TCGv t0 = tcg_temp_new();

                save_cpu_state(ctx, 1);
                gen_helper_di(t0, cpu_env);
                gen_store_gpr(t0, rs);
                /*
                 * Stop translation as we may have switched the execution
                 * mode.
                 */
                ctx->base.is_jmp = DISAS_STOP;
            }
            break;
        case EI:
            check_cp0_enabled(ctx);
            {
                TCGv t0 = tcg_temp_new();

                save_cpu_state(ctx, 1);
                gen_helper_ei(t0, cpu_env);
                gen_store_gpr(t0, rs);
                /*
                 * DISAS_STOP isn't sufficient, we need to ensure we break out
                 * of translated code to check for pending interrupts.
                 */
                gen_save_pc(ctx->base.pc_next + 4);
                ctx->base.is_jmp = DISAS_EXIT;
            }
            break;
        default:
            goto pool32axf_invalid;
        }
        break;
#endif
    case 0x2d:
        switch (minor) {
        case SYNC:
            gen_sync(extract32(ctx->opcode, 16, 5));
            break;
        case SYSCALL:
            generate_exception_end(ctx, EXCP_SYSCALL);
            break;
        case SDBBP:
            if (is_uhi(ctx, extract32(ctx->opcode, 16, 10))) {
                ctx->base.is_jmp = DISAS_SEMIHOST;
            } else {
                check_insn(ctx, ISA_MIPS_R1);
                if (ctx->hflags & MIPS_HFLAG_SBRI) {
                    gen_reserved_instruction(ctx);
                } else {
                    generate_exception_end(ctx, EXCP_DBp);
                }
            }
            break;
        default:
            goto pool32axf_invalid;
        }
        break;
    case 0x01:
        switch (minor & 3) {
        case MFHI_ACC:
            gen_HILO(ctx, OPC_MFHI, minor >> 2, rs);
            break;
        case MFLO_ACC:
            gen_HILO(ctx, OPC_MFLO, minor >> 2, rs);
            break;
        case MTHI_ACC:
            gen_HILO(ctx, OPC_MTHI, minor >> 2, rs);
            break;
        case MTLO_ACC:
            gen_HILO(ctx, OPC_MTLO, minor >> 2, rs);
            break;
        default:
            goto pool32axf_invalid;
        }
        break;
    case 0x35:
        check_insn_opc_removed(ctx, ISA_MIPS_R6);
        switch (minor) {
        case MFHI32:
            gen_HILO(ctx, OPC_MFHI, 0, rs);
            break;
        case MFLO32:
            gen_HILO(ctx, OPC_MFLO, 0, rs);
            break;
        case MTHI32:
            gen_HILO(ctx, OPC_MTHI, 0, rs);
            break;
        case MTLO32:
            gen_HILO(ctx, OPC_MTLO, 0, rs);
            break;
        default:
            goto pool32axf_invalid;
        }
        break;
    default:
    pool32axf_invalid:
        MIPS_INVAL("pool32axf");
        gen_reserved_instruction(ctx);
        break;
    }
}

static void gen_pool32fxf(DisasContext *ctx, int rt, int rs)
{
    int extension = (ctx->opcode >> 6) & 0x3ff;
    uint32_t mips32_op;

#define FLOAT_1BIT_FMT(opc, fmt)    ((fmt << 8) | opc)
#define FLOAT_2BIT_FMT(opc, fmt)    ((fmt << 7) | opc)
#define COND_FLOAT_MOV(opc, cond)   ((cond << 7) | opc)

    switch (extension) {
    case FLOAT_1BIT_FMT(CFC1, 0):
        mips32_op = OPC_CFC1;
        goto do_cp1;
    case FLOAT_1BIT_FMT(CTC1, 0):
        mips32_op = OPC_CTC1;
        goto do_cp1;
    case FLOAT_1BIT_FMT(MFC1, 0):
        mips32_op = OPC_MFC1;
        goto do_cp1;
    case FLOAT_1BIT_FMT(MTC1, 0):
        mips32_op = OPC_MTC1;
        goto do_cp1;
    case FLOAT_1BIT_FMT(MFHC1, 0):
        mips32_op = OPC_MFHC1;
        goto do_cp1;
    case FLOAT_1BIT_FMT(MTHC1, 0):
        mips32_op = OPC_MTHC1;
    do_cp1:
        gen_cp1(ctx, mips32_op, rt, rs);
        break;

        /* Reciprocal square root */
    case FLOAT_1BIT_FMT(RSQRT_FMT, FMT_SD_S):
        mips32_op = OPC_RSQRT_S;
        goto do_unaryfp;
    case FLOAT_1BIT_FMT(RSQRT_FMT, FMT_SD_D):
        mips32_op = OPC_RSQRT_D;
        goto do_unaryfp;

        /* Square root */
    case FLOAT_1BIT_FMT(SQRT_FMT, FMT_SD_S):
        mips32_op = OPC_SQRT_S;
        goto do_unaryfp;
    case FLOAT_1BIT_FMT(SQRT_FMT, FMT_SD_D):
        mips32_op = OPC_SQRT_D;
        goto do_unaryfp;

        /* Reciprocal */
    case FLOAT_1BIT_FMT(RECIP_FMT, FMT_SD_S):
        mips32_op = OPC_RECIP_S;
        goto do_unaryfp;
    case FLOAT_1BIT_FMT(RECIP_FMT, FMT_SD_D):
        mips32_op = OPC_RECIP_D;
        goto do_unaryfp;

        /* Floor */
    case FLOAT_1BIT_FMT(FLOOR_L, FMT_SD_S):
        mips32_op = OPC_FLOOR_L_S;
        goto do_unaryfp;
    case FLOAT_1BIT_FMT(FLOOR_L, FMT_SD_D):
        mips32_op = OPC_FLOOR_L_D;
        goto do_unaryfp;
    case FLOAT_1BIT_FMT(FLOOR_W, FMT_SD_S):
        mips32_op = OPC_FLOOR_W_S;
        goto do_unaryfp;
    case FLOAT_1BIT_FMT(FLOOR_W, FMT_SD_D):
        mips32_op = OPC_FLOOR_W_D;
        goto do_unaryfp;

        /* Ceiling */
    case FLOAT_1BIT_FMT(CEIL_L, FMT_SD_S):
        mips32_op = OPC_CEIL_L_S;
        goto do_unaryfp;
    case FLOAT_1BIT_FMT(CEIL_L, FMT_SD_D):
        mips32_op = OPC_CEIL_L_D;
        goto do_unaryfp;
    case FLOAT_1BIT_FMT(CEIL_W, FMT_SD_S):
        mips32_op = OPC_CEIL_W_S;
        goto do_unaryfp;
    case FLOAT_1BIT_FMT(CEIL_W, FMT_SD_D):
        mips32_op = OPC_CEIL_W_D;
        goto do_unaryfp;

        /* Truncation */
    case FLOAT_1BIT_FMT(TRUNC_L, FMT_SD_S):
        mips32_op = OPC_TRUNC_L_S;
        goto do_unaryfp;
    case FLOAT_1BIT_FMT(TRUNC_L, FMT_SD_D):
        mips32_op = OPC_TRUNC_L_D;
        goto do_unaryfp;
    case FLOAT_1BIT_FMT(TRUNC_W, FMT_SD_S):
        mips32_op = OPC_TRUNC_W_S;
        goto do_unaryfp;
    case FLOAT_1BIT_FMT(TRUNC_W, FMT_SD_D):
        mips32_op = OPC_TRUNC_W_D;
        goto do_unaryfp;

        /* Round */
    case FLOAT_1BIT_FMT(ROUND_L, FMT_SD_S):
        mips32_op = OPC_ROUND_L_S;
        goto do_unaryfp;
    case FLOAT_1BIT_FMT(ROUND_L, FMT_SD_D):
        mips32_op = OPC_ROUND_L_D;
        goto do_unaryfp;
    case FLOAT_1BIT_FMT(ROUND_W, FMT_SD_S):
        mips32_op = OPC_ROUND_W_S;
        goto do_unaryfp;
    case FLOAT_1BIT_FMT(ROUND_W, FMT_SD_D):
        mips32_op = OPC_ROUND_W_D;
        goto do_unaryfp;

        /* Integer to floating-point conversion */
    case FLOAT_1BIT_FMT(CVT_L, FMT_SD_S):
        mips32_op = OPC_CVT_L_S;
        goto do_unaryfp;
    case FLOAT_1BIT_FMT(CVT_L, FMT_SD_D):
        mips32_op = OPC_CVT_L_D;
        goto do_unaryfp;
    case FLOAT_1BIT_FMT(CVT_W, FMT_SD_S):
        mips32_op = OPC_CVT_W_S;
        goto do_unaryfp;
    case FLOAT_1BIT_FMT(CVT_W, FMT_SD_D):
        mips32_op = OPC_CVT_W_D;
        goto do_unaryfp;

        /* Paired-foo conversions */
    case FLOAT_1BIT_FMT(CVT_S_PL, 0):
        mips32_op = OPC_CVT_S_PL;
        goto do_unaryfp;
    case FLOAT_1BIT_FMT(CVT_S_PU, 0):
        mips32_op = OPC_CVT_S_PU;
        goto do_unaryfp;
    case FLOAT_1BIT_FMT(CVT_PW_PS, 0):
        mips32_op = OPC_CVT_PW_PS;
        goto do_unaryfp;
    case FLOAT_1BIT_FMT(CVT_PS_PW, 0):
        mips32_op = OPC_CVT_PS_PW;
        goto do_unaryfp;

        /* Floating-point moves */
    case FLOAT_2BIT_FMT(MOV_FMT, FMT_SDPS_S):
        mips32_op = OPC_MOV_S;
        goto do_unaryfp;
    case FLOAT_2BIT_FMT(MOV_FMT, FMT_SDPS_D):
        mips32_op = OPC_MOV_D;
        goto do_unaryfp;
    case FLOAT_2BIT_FMT(MOV_FMT, FMT_SDPS_PS):
        mips32_op = OPC_MOV_PS;
        goto do_unaryfp;

        /* Absolute value */
    case FLOAT_2BIT_FMT(ABS_FMT, FMT_SDPS_S):
        mips32_op = OPC_ABS_S;
        goto do_unaryfp;
    case FLOAT_2BIT_FMT(ABS_FMT, FMT_SDPS_D):
        mips32_op = OPC_ABS_D;
        goto do_unaryfp;
    case FLOAT_2BIT_FMT(ABS_FMT, FMT_SDPS_PS):
        mips32_op = OPC_ABS_PS;
        goto do_unaryfp;

        /* Negation */
    case FLOAT_2BIT_FMT(NEG_FMT, FMT_SDPS_S):
        mips32_op = OPC_NEG_S;
        goto do_unaryfp;
    case FLOAT_2BIT_FMT(NEG_FMT, FMT_SDPS_D):
        mips32_op = OPC_NEG_D;
        goto do_unaryfp;
    case FLOAT_2BIT_FMT(NEG_FMT, FMT_SDPS_PS):
        mips32_op = OPC_NEG_PS;
        goto do_unaryfp;

        /* Reciprocal square root step */
    case FLOAT_2BIT_FMT(RSQRT1_FMT, FMT_SDPS_S):
        mips32_op = OPC_RSQRT1_S;
        goto do_unaryfp;
    case FLOAT_2BIT_FMT(RSQRT1_FMT, FMT_SDPS_D):
        mips32_op = OPC_RSQRT1_D;
        goto do_unaryfp;
    case FLOAT_2BIT_FMT(RSQRT1_FMT, FMT_SDPS_PS):
        mips32_op = OPC_RSQRT1_PS;
        goto do_unaryfp;

        /* Reciprocal step */
    case FLOAT_2BIT_FMT(RECIP1_FMT, FMT_SDPS_S):
        mips32_op = OPC_RECIP1_S;
        goto do_unaryfp;
    case FLOAT_2BIT_FMT(RECIP1_FMT, FMT_SDPS_D):
        mips32_op = OPC_RECIP1_S;
        goto do_unaryfp;
    case FLOAT_2BIT_FMT(RECIP1_FMT, FMT_SDPS_PS):
        mips32_op = OPC_RECIP1_PS;
        goto do_unaryfp;

        /* Conversions from double */
    case FLOAT_2BIT_FMT(CVT_D, FMT_SWL_S):
        mips32_op = OPC_CVT_D_S;
        goto do_unaryfp;
    case FLOAT_2BIT_FMT(CVT_D, FMT_SWL_W):
        mips32_op = OPC_CVT_D_W;
        goto do_unaryfp;
    case FLOAT_2BIT_FMT(CVT_D, FMT_SWL_L):
        mips32_op = OPC_CVT_D_L;
        goto do_unaryfp;

        /* Conversions from single */
    case FLOAT_2BIT_FMT(CVT_S, FMT_DWL_D):
        mips32_op = OPC_CVT_S_D;
        goto do_unaryfp;
    case FLOAT_2BIT_FMT(CVT_S, FMT_DWL_W):
        mips32_op = OPC_CVT_S_W;
        goto do_unaryfp;
    case FLOAT_2BIT_FMT(CVT_S, FMT_DWL_L):
        mips32_op = OPC_CVT_S_L;
    do_unaryfp:
        gen_farith(ctx, mips32_op, -1, rs, rt, 0);
        break;

        /* Conditional moves on floating-point codes */
    case COND_FLOAT_MOV(MOVT, 0):
    case COND_FLOAT_MOV(MOVT, 1):
    case COND_FLOAT_MOV(MOVT, 2):
    case COND_FLOAT_MOV(MOVT, 3):
    case COND_FLOAT_MOV(MOVT, 4):
    case COND_FLOAT_MOV(MOVT, 5):
    case COND_FLOAT_MOV(MOVT, 6):
    case COND_FLOAT_MOV(MOVT, 7):
        check_insn_opc_removed(ctx, ISA_MIPS_R6);
        gen_movci(ctx, rt, rs, (ctx->opcode >> 13) & 0x7, 1);
        break;
    case COND_FLOAT_MOV(MOVF, 0):
    case COND_FLOAT_MOV(MOVF, 1):
    case COND_FLOAT_MOV(MOVF, 2):
    case COND_FLOAT_MOV(MOVF, 3):
    case COND_FLOAT_MOV(MOVF, 4):
    case COND_FLOAT_MOV(MOVF, 5):
    case COND_FLOAT_MOV(MOVF, 6):
    case COND_FLOAT_MOV(MOVF, 7):
        check_insn_opc_removed(ctx, ISA_MIPS_R6);
        gen_movci(ctx, rt, rs, (ctx->opcode >> 13) & 0x7, 0);
        break;
    default:
        MIPS_INVAL("pool32fxf");
        gen_reserved_instruction(ctx);
        break;
    }
}

static void decode_micromips32_opc(CPUMIPSState *env, DisasContext *ctx)
{
    int32_t offset;
    uint16_t insn;
    int rt, rs, rd, rr;
    int16_t imm;
    uint32_t op, minor, minor2, mips32_op;
    uint32_t cond, fmt, cc;

    insn = translator_lduw(env, &ctx->base, ctx->base.pc_next + 2);
    ctx->opcode = (ctx->opcode << 16) | insn;

    rt = (ctx->opcode >> 21) & 0x1f;
    rs = (ctx->opcode >> 16) & 0x1f;
    rd = (ctx->opcode >> 11) & 0x1f;
    rr = (ctx->opcode >> 6) & 0x1f;
    imm = (int16_t) ctx->opcode;

    op = (ctx->opcode >> 26) & 0x3f;
    switch (op) {
    case POOL32A:
        minor = ctx->opcode & 0x3f;
        switch (minor) {
        case 0x00:
            minor = (ctx->opcode >> 6) & 0xf;
            switch (minor) {
            case SLL32:
                mips32_op = OPC_SLL;
                goto do_shifti;
            case SRA:
                mips32_op = OPC_SRA;
                goto do_shifti;
            case SRL32:
                mips32_op = OPC_SRL;
                goto do_shifti;
            case ROTR:
                mips32_op = OPC_ROTR;
            do_shifti:
                gen_shift_imm(ctx, mips32_op, rt, rs, rd);
                break;
            case SELEQZ:
                check_insn(ctx, ISA_MIPS_R6);
                gen_cond_move(ctx, OPC_SELEQZ, rd, rs, rt);
                break;
            case SELNEZ:
                check_insn(ctx, ISA_MIPS_R6);
                gen_cond_move(ctx, OPC_SELNEZ, rd, rs, rt);
                break;
            case R6_RDHWR:
                check_insn(ctx, ISA_MIPS_R6);
                gen_rdhwr(ctx, rt, rs, extract32(ctx->opcode, 11, 3));
                break;
            default:
                goto pool32a_invalid;
            }
            break;
        case 0x10:
            minor = (ctx->opcode >> 6) & 0xf;
            switch (minor) {
                /* Arithmetic */
            case ADD:
                mips32_op = OPC_ADD;
                goto do_arith;
            case ADDU32:
                mips32_op = OPC_ADDU;
                goto do_arith;
            case SUB:
                mips32_op = OPC_SUB;
                goto do_arith;
            case SUBU32:
                mips32_op = OPC_SUBU;
                goto do_arith;
            case MUL:
                check_insn_opc_removed(ctx, ISA_MIPS_R6);
                mips32_op = OPC_MUL;
            do_arith:
                gen_arith(ctx, mips32_op, rd, rs, rt);
                break;
                /* Shifts */
            case SLLV:
                mips32_op = OPC_SLLV;
                goto do_shift;
            case SRLV:
                mips32_op = OPC_SRLV;
                goto do_shift;
            case SRAV:
                mips32_op = OPC_SRAV;
                goto do_shift;
            case ROTRV:
                mips32_op = OPC_ROTRV;
            do_shift:
                gen_shift(ctx, mips32_op, rd, rs, rt);
                break;
                /* Logical operations */
            case AND:
                mips32_op = OPC_AND;
                goto do_logic;
            case OR32:
                mips32_op = OPC_OR;
                goto do_logic;
            case NOR:
                mips32_op = OPC_NOR;
                goto do_logic;
            case XOR32:
                mips32_op = OPC_XOR;
            do_logic:
                gen_logic(ctx, mips32_op, rd, rs, rt);
                break;
                /* Set less than */
            case SLT:
                mips32_op = OPC_SLT;
                goto do_slt;
            case SLTU:
                mips32_op = OPC_SLTU;
            do_slt:
                gen_slt(ctx, mips32_op, rd, rs, rt);
                break;
            default:
                goto pool32a_invalid;
            }
            break;
        case 0x18:
            minor = (ctx->opcode >> 6) & 0xf;
            switch (minor) {
                /* Conditional moves */
            case MOVN: /* MUL */
                if (ctx->insn_flags & ISA_MIPS_R6) {
                    /* MUL */
                    gen_r6_muldiv(ctx, R6_OPC_MUL, rd, rs, rt);
                } else {
                    /* MOVN */
                    gen_cond_move(ctx, OPC_MOVN, rd, rs, rt);
                }
                break;
            case MOVZ: /* MUH */
                if (ctx->insn_flags & ISA_MIPS_R6) {
                    /* MUH */
                    gen_r6_muldiv(ctx, R6_OPC_MUH, rd, rs, rt);
                } else {
                    /* MOVZ */
                    gen_cond_move(ctx, OPC_MOVZ, rd, rs, rt);
                }
                break;
            case MULU:
                check_insn(ctx, ISA_MIPS_R6);
                gen_r6_muldiv(ctx, R6_OPC_MULU, rd, rs, rt);
                break;
            case MUHU:
                check_insn(ctx, ISA_MIPS_R6);
                gen_r6_muldiv(ctx, R6_OPC_MUHU, rd, rs, rt);
                break;
            case LWXS: /* DIV */
                if (ctx->insn_flags & ISA_MIPS_R6) {
                    /* DIV */
                    gen_r6_muldiv(ctx, R6_OPC_DIV, rd, rs, rt);
                } else {
                    /* LWXS */
                    gen_ldxs(ctx, rs, rt, rd);
                }
                break;
            case MOD:
                check_insn(ctx, ISA_MIPS_R6);
                gen_r6_muldiv(ctx, R6_OPC_MOD, rd, rs, rt);
                break;
            case R6_DIVU:
                check_insn(ctx, ISA_MIPS_R6);
                gen_r6_muldiv(ctx, R6_OPC_DIVU, rd, rs, rt);
                break;
            case MODU:
                check_insn(ctx, ISA_MIPS_R6);
                gen_r6_muldiv(ctx, R6_OPC_MODU, rd, rs, rt);
                break;
            default:
                goto pool32a_invalid;
            }
            break;
        case INS:
            gen_bitops(ctx, OPC_INS, rt, rs, rr, rd);
            return;
        case LSA:
            check_insn(ctx, ISA_MIPS_R6);
            gen_lsa(ctx, rd, rt, rs, extract32(ctx->opcode, 9, 2));
            break;
        case ALIGN:
            check_insn(ctx, ISA_MIPS_R6);
            gen_align(ctx, 32, rd, rs, rt, extract32(ctx->opcode, 9, 2));
            break;
        case EXT:
            gen_bitops(ctx, OPC_EXT, rt, rs, rr, rd);
            return;
        case POOL32AXF:
            gen_pool32axf(env, ctx, rt, rs);
            break;
        case BREAK32:
            generate_exception_break(ctx, extract32(ctx->opcode, 6, 20));
            break;
        case SIGRIE:
            check_insn(ctx, ISA_MIPS_R6);
            gen_reserved_instruction(ctx);
            break;
        default:
        pool32a_invalid:
                MIPS_INVAL("pool32a");
                gen_reserved_instruction(ctx);
                break;
        }
        break;
    case POOL32B:
        minor = (ctx->opcode >> 12) & 0xf;
        switch (minor) {
        case CACHE:
            check_cp0_enabled(ctx);
            if (ctx->hflags & MIPS_HFLAG_ITC_CACHE) {
                gen_cache_operation(ctx, rt, rs, imm);
            }
            break;
        case LWC2:
        case SWC2:
            /* COP2: Not implemented. */
            generate_exception_err(ctx, EXCP_CpU, 2);
            break;
#ifdef TARGET_MIPS64
        case LDP:
        case SDP:
            check_insn(ctx, ISA_MIPS3);
            check_mips_64(ctx);
#endif
            /* fall through */
        case LWP:
        case SWP:
            gen_ldst_pair(ctx, minor, rt, rs, SIMM(ctx->opcode, 0, 12));
            break;
#ifdef TARGET_MIPS64
        case LDM:
        case SDM:
            check_insn(ctx, ISA_MIPS3);
            check_mips_64(ctx);
#endif
            /* fall through */
        case LWM32:
        case SWM32:
            gen_ldst_multiple(ctx, minor, rt, rs, SIMM(ctx->opcode, 0, 12));
            break;
        default:
            MIPS_INVAL("pool32b");
            gen_reserved_instruction(ctx);
            break;
        }
        break;
    case POOL32F:
        if (ctx->CP0_Config1 & (1 << CP0C1_FP)) {
            minor = ctx->opcode & 0x3f;
            check_cp1_enabled(ctx);
            switch (minor) {
            case ALNV_PS:
                check_insn_opc_removed(ctx, ISA_MIPS_R6);
                mips32_op = OPC_ALNV_PS;
                goto do_madd;
            case MADD_S:
                check_insn_opc_removed(ctx, ISA_MIPS_R6);
                mips32_op = OPC_MADD_S;
                goto do_madd;
            case MADD_D:
                check_insn_opc_removed(ctx, ISA_MIPS_R6);
                mips32_op = OPC_MADD_D;
                goto do_madd;
            case MADD_PS:
                check_insn_opc_removed(ctx, ISA_MIPS_R6);
                mips32_op = OPC_MADD_PS;
                goto do_madd;
            case MSUB_S:
                check_insn_opc_removed(ctx, ISA_MIPS_R6);
                mips32_op = OPC_MSUB_S;
                goto do_madd;
            case MSUB_D:
                check_insn_opc_removed(ctx, ISA_MIPS_R6);
                mips32_op = OPC_MSUB_D;
                goto do_madd;
            case MSUB_PS:
                check_insn_opc_removed(ctx, ISA_MIPS_R6);
                mips32_op = OPC_MSUB_PS;
                goto do_madd;
            case NMADD_S:
                check_insn_opc_removed(ctx, ISA_MIPS_R6);
                mips32_op = OPC_NMADD_S;
                goto do_madd;
            case NMADD_D:
                check_insn_opc_removed(ctx, ISA_MIPS_R6);
                mips32_op = OPC_NMADD_D;
                goto do_madd;
            case NMADD_PS:
                check_insn_opc_removed(ctx, ISA_MIPS_R6);
                mips32_op = OPC_NMADD_PS;
                goto do_madd;
            case NMSUB_S:
                check_insn_opc_removed(ctx, ISA_MIPS_R6);
                mips32_op = OPC_NMSUB_S;
                goto do_madd;
            case NMSUB_D:
                check_insn_opc_removed(ctx, ISA_MIPS_R6);
                mips32_op = OPC_NMSUB_D;
                goto do_madd;
            case NMSUB_PS:
                check_insn_opc_removed(ctx, ISA_MIPS_R6);
                mips32_op = OPC_NMSUB_PS;
            do_madd:
                gen_flt3_arith(ctx, mips32_op, rd, rr, rs, rt);
                break;
            case CABS_COND_FMT:
                check_insn_opc_removed(ctx, ISA_MIPS_R6);
                cond = (ctx->opcode >> 6) & 0xf;
                cc = (ctx->opcode >> 13) & 0x7;
                fmt = (ctx->opcode >> 10) & 0x3;
                switch (fmt) {
                case 0x0:
                    gen_cmpabs_s(ctx, cond, rt, rs, cc);
                    break;
                case 0x1:
                    gen_cmpabs_d(ctx, cond, rt, rs, cc);
                    break;
                case 0x2:
                    gen_cmpabs_ps(ctx, cond, rt, rs, cc);
                    break;
                default:
                    goto pool32f_invalid;
                }
                break;
            case C_COND_FMT:
                check_insn_opc_removed(ctx, ISA_MIPS_R6);
                cond = (ctx->opcode >> 6) & 0xf;
                cc = (ctx->opcode >> 13) & 0x7;
                fmt = (ctx->opcode >> 10) & 0x3;
                switch (fmt) {
                case 0x0:
                    gen_cmp_s(ctx, cond, rt, rs, cc);
                    break;
                case 0x1:
                    gen_cmp_d(ctx, cond, rt, rs, cc);
                    break;
                case 0x2:
                    gen_cmp_ps(ctx, cond, rt, rs, cc);
                    break;
                default:
                    goto pool32f_invalid;
                }
                break;
            case CMP_CONDN_S:
                check_insn(ctx, ISA_MIPS_R6);
                gen_r6_cmp_s(ctx, (ctx->opcode >> 6) & 0x1f, rt, rs, rd);
                break;
            case CMP_CONDN_D:
                check_insn(ctx, ISA_MIPS_R6);
                gen_r6_cmp_d(ctx, (ctx->opcode >> 6) & 0x1f, rt, rs, rd);
                break;
            case POOL32FXF:
                gen_pool32fxf(ctx, rt, rs);
                break;
            case 0x00:
                /* PLL foo */
                switch ((ctx->opcode >> 6) & 0x7) {
                case PLL_PS:
                    mips32_op = OPC_PLL_PS;
                    goto do_ps;
                case PLU_PS:
                    mips32_op = OPC_PLU_PS;
                    goto do_ps;
                case PUL_PS:
                    mips32_op = OPC_PUL_PS;
                    goto do_ps;
                case PUU_PS:
                    mips32_op = OPC_PUU_PS;
                    goto do_ps;
                case CVT_PS_S:
                    check_insn_opc_removed(ctx, ISA_MIPS_R6);
                    mips32_op = OPC_CVT_PS_S;
                do_ps:
                    gen_farith(ctx, mips32_op, rt, rs, rd, 0);
                    break;
                default:
                    goto pool32f_invalid;
                }
                break;
            case MIN_FMT:
                check_insn(ctx, ISA_MIPS_R6);
                switch ((ctx->opcode >> 9) & 0x3) {
                case FMT_SDPS_S:
                    gen_farith(ctx, OPC_MIN_S, rt, rs, rd, 0);
                    break;
                case FMT_SDPS_D:
                    gen_farith(ctx, OPC_MIN_D, rt, rs, rd, 0);
                    break;
                default:
                    goto pool32f_invalid;
                }
                break;
            case 0x08:
                /* [LS][WDU]XC1 */
                switch ((ctx->opcode >> 6) & 0x7) {
                case LWXC1:
                    check_insn_opc_removed(ctx, ISA_MIPS_R6);
                    mips32_op = OPC_LWXC1;
                    goto do_ldst_cp1;
                case SWXC1:
                    check_insn_opc_removed(ctx, ISA_MIPS_R6);
                    mips32_op = OPC_SWXC1;
                    goto do_ldst_cp1;
                case LDXC1:
                    check_insn_opc_removed(ctx, ISA_MIPS_R6);
                    mips32_op = OPC_LDXC1;
                    goto do_ldst_cp1;
                case SDXC1:
                    check_insn_opc_removed(ctx, ISA_MIPS_R6);
                    mips32_op = OPC_SDXC1;
                    goto do_ldst_cp1;
                case LUXC1:
                    check_insn_opc_removed(ctx, ISA_MIPS_R6);
                    mips32_op = OPC_LUXC1;
                    goto do_ldst_cp1;
                case SUXC1:
                    check_insn_opc_removed(ctx, ISA_MIPS_R6);
                    mips32_op = OPC_SUXC1;
                do_ldst_cp1:
                    gen_flt3_ldst(ctx, mips32_op, rd, rd, rt, rs);
                    break;
                default:
                    goto pool32f_invalid;
                }
                break;
            case MAX_FMT:
                check_insn(ctx, ISA_MIPS_R6);
                switch ((ctx->opcode >> 9) & 0x3) {
                case FMT_SDPS_S:
                    gen_farith(ctx, OPC_MAX_S, rt, rs, rd, 0);
                    break;
                case FMT_SDPS_D:
                    gen_farith(ctx, OPC_MAX_D, rt, rs, rd, 0);
                    break;
                default:
                    goto pool32f_invalid;
                }
                break;
            case 0x18:
                /* 3D insns */
                check_insn_opc_removed(ctx, ISA_MIPS_R6);
                fmt = (ctx->opcode >> 9) & 0x3;
                switch ((ctx->opcode >> 6) & 0x7) {
                case RSQRT2_FMT:
                    switch (fmt) {
                    case FMT_SDPS_S:
                        mips32_op = OPC_RSQRT2_S;
                        goto do_3d;
                    case FMT_SDPS_D:
                        mips32_op = OPC_RSQRT2_D;
                        goto do_3d;
                    case FMT_SDPS_PS:
                        mips32_op = OPC_RSQRT2_PS;
                        goto do_3d;
                    default:
                        goto pool32f_invalid;
                    }
                    break;
                case RECIP2_FMT:
                    switch (fmt) {
                    case FMT_SDPS_S:
                        mips32_op = OPC_RECIP2_S;
                        goto do_3d;
                    case FMT_SDPS_D:
                        mips32_op = OPC_RECIP2_D;
                        goto do_3d;
                    case FMT_SDPS_PS:
                        mips32_op = OPC_RECIP2_PS;
                        goto do_3d;
                    default:
                        goto pool32f_invalid;
                    }
                    break;
                case ADDR_PS:
                    mips32_op = OPC_ADDR_PS;
                    goto do_3d;
                case MULR_PS:
                    mips32_op = OPC_MULR_PS;
                do_3d:
                    gen_farith(ctx, mips32_op, rt, rs, rd, 0);
                    break;
                default:
                    goto pool32f_invalid;
                }
                break;
            case 0x20:
                /* MOV[FT].fmt, PREFX, RINT.fmt, CLASS.fmt*/
                cc = (ctx->opcode >> 13) & 0x7;
                fmt = (ctx->opcode >> 9) & 0x3;
                switch ((ctx->opcode >> 6) & 0x7) {
                case MOVF_FMT: /* RINT_FMT */
                    if (ctx->insn_flags & ISA_MIPS_R6) {
                        /* RINT_FMT */
                        switch (fmt) {
                        case FMT_SDPS_S:
                            gen_farith(ctx, OPC_RINT_S, 0, rt, rs, 0);
                            break;
                        case FMT_SDPS_D:
                            gen_farith(ctx, OPC_RINT_D, 0, rt, rs, 0);
                            break;
                        default:
                            goto pool32f_invalid;
                        }
                    } else {
                        /* MOVF_FMT */
                        switch (fmt) {
                        case FMT_SDPS_S:
                            gen_movcf_s(ctx, rs, rt, cc, 0);
                            break;
                        case FMT_SDPS_D:
                            gen_movcf_d(ctx, rs, rt, cc, 0);
                            break;
                        case FMT_SDPS_PS:
                            check_ps(ctx);
                            gen_movcf_ps(ctx, rs, rt, cc, 0);
                            break;
                        default:
                            goto pool32f_invalid;
                        }
                    }
                    break;
                case MOVT_FMT: /* CLASS_FMT */
                    if (ctx->insn_flags & ISA_MIPS_R6) {
                        /* CLASS_FMT */
                        switch (fmt) {
                        case FMT_SDPS_S:
                            gen_farith(ctx, OPC_CLASS_S, 0, rt, rs, 0);
                            break;
                        case FMT_SDPS_D:
                            gen_farith(ctx, OPC_CLASS_D, 0, rt, rs, 0);
                            break;
                        default:
                            goto pool32f_invalid;
                        }
                    } else {
                        /* MOVT_FMT */
                        switch (fmt) {
                        case FMT_SDPS_S:
                            gen_movcf_s(ctx, rs, rt, cc, 1);
                            break;
                        case FMT_SDPS_D:
                            gen_movcf_d(ctx, rs, rt, cc, 1);
                            break;
                        case FMT_SDPS_PS:
                            check_ps(ctx);
                            gen_movcf_ps(ctx, rs, rt, cc, 1);
                            break;
                        default:
                            goto pool32f_invalid;
                        }
                    }
                    break;
                case PREFX:
                    check_insn_opc_removed(ctx, ISA_MIPS_R6);
                    break;
                default:
                    goto pool32f_invalid;
                }
                break;
#define FINSN_3ARG_SDPS(prfx)                           \
                switch ((ctx->opcode >> 8) & 0x3) {     \
                case FMT_SDPS_S:                        \
                    mips32_op = OPC_##prfx##_S;         \
                    goto do_fpop;                       \
                case FMT_SDPS_D:                        \
                    mips32_op = OPC_##prfx##_D;         \
                    goto do_fpop;                       \
                case FMT_SDPS_PS:                       \
                    check_ps(ctx);                      \
                    mips32_op = OPC_##prfx##_PS;        \
                    goto do_fpop;                       \
                default:                                \
                    goto pool32f_invalid;               \
                }
            case MINA_FMT:
                check_insn(ctx, ISA_MIPS_R6);
                switch ((ctx->opcode >> 9) & 0x3) {
                case FMT_SDPS_S:
                    gen_farith(ctx, OPC_MINA_S, rt, rs, rd, 0);
                    break;
                case FMT_SDPS_D:
                    gen_farith(ctx, OPC_MINA_D, rt, rs, rd, 0);
                    break;
                default:
                    goto pool32f_invalid;
                }
                break;
            case MAXA_FMT:
                check_insn(ctx, ISA_MIPS_R6);
                switch ((ctx->opcode >> 9) & 0x3) {
                case FMT_SDPS_S:
                    gen_farith(ctx, OPC_MAXA_S, rt, rs, rd, 0);
                    break;
                case FMT_SDPS_D:
                    gen_farith(ctx, OPC_MAXA_D, rt, rs, rd, 0);
                    break;
                default:
                    goto pool32f_invalid;
                }
                break;
            case 0x30:
                /* regular FP ops */
                switch ((ctx->opcode >> 6) & 0x3) {
                case ADD_FMT:
                    FINSN_3ARG_SDPS(ADD);
                    break;
                case SUB_FMT:
                    FINSN_3ARG_SDPS(SUB);
                    break;
                case MUL_FMT:
                    FINSN_3ARG_SDPS(MUL);
                    break;
                case DIV_FMT:
                    fmt = (ctx->opcode >> 8) & 0x3;
                    if (fmt == 1) {
                        mips32_op = OPC_DIV_D;
                    } else if (fmt == 0) {
                        mips32_op = OPC_DIV_S;
                    } else {
                        goto pool32f_invalid;
                    }
                    goto do_fpop;
                default:
                    goto pool32f_invalid;
                }
                break;
            case 0x38:
                /* cmovs */
                switch ((ctx->opcode >> 6) & 0x7) {
                case MOVN_FMT: /* SELEQZ_FMT */
                    if (ctx->insn_flags & ISA_MIPS_R6) {
                        /* SELEQZ_FMT */
                        switch ((ctx->opcode >> 9) & 0x3) {
                        case FMT_SDPS_S:
                            gen_sel_s(ctx, OPC_SELEQZ_S, rd, rt, rs);
                            break;
                        case FMT_SDPS_D:
                            gen_sel_d(ctx, OPC_SELEQZ_D, rd, rt, rs);
                            break;
                        default:
                            goto pool32f_invalid;
                        }
                    } else {
                        /* MOVN_FMT */
                        FINSN_3ARG_SDPS(MOVN);
                    }
                    break;
                case MOVN_FMT_04:
                    check_insn_opc_removed(ctx, ISA_MIPS_R6);
                    FINSN_3ARG_SDPS(MOVN);
                    break;
                case MOVZ_FMT: /* SELNEZ_FMT */
                    if (ctx->insn_flags & ISA_MIPS_R6) {
                        /* SELNEZ_FMT */
                        switch ((ctx->opcode >> 9) & 0x3) {
                        case FMT_SDPS_S:
                            gen_sel_s(ctx, OPC_SELNEZ_S, rd, rt, rs);
                            break;
                        case FMT_SDPS_D:
                            gen_sel_d(ctx, OPC_SELNEZ_D, rd, rt, rs);
                            break;
                        default:
                            goto pool32f_invalid;
                        }
                    } else {
                        /* MOVZ_FMT */
                        FINSN_3ARG_SDPS(MOVZ);
                    }
                    break;
                case MOVZ_FMT_05:
                    check_insn_opc_removed(ctx, ISA_MIPS_R6);
                    FINSN_3ARG_SDPS(MOVZ);
                    break;
                case SEL_FMT:
                    check_insn(ctx, ISA_MIPS_R6);
                    switch ((ctx->opcode >> 9) & 0x3) {
                    case FMT_SDPS_S:
                        gen_sel_s(ctx, OPC_SEL_S, rd, rt, rs);
                        break;
                    case FMT_SDPS_D:
                        gen_sel_d(ctx, OPC_SEL_D, rd, rt, rs);
                        break;
                    default:
                        goto pool32f_invalid;
                    }
                    break;
                case MADDF_FMT:
                    check_insn(ctx, ISA_MIPS_R6);
                    switch ((ctx->opcode >> 9) & 0x3) {
                    case FMT_SDPS_S:
                        mips32_op = OPC_MADDF_S;
                        goto do_fpop;
                    case FMT_SDPS_D:
                        mips32_op = OPC_MADDF_D;
                        goto do_fpop;
                    default:
                        goto pool32f_invalid;
                    }
                    break;
                case MSUBF_FMT:
                    check_insn(ctx, ISA_MIPS_R6);
                    switch ((ctx->opcode >> 9) & 0x3) {
                    case FMT_SDPS_S:
                        mips32_op = OPC_MSUBF_S;
                        goto do_fpop;
                    case FMT_SDPS_D:
                        mips32_op = OPC_MSUBF_D;
                        goto do_fpop;
                    default:
                        goto pool32f_invalid;
                    }
                    break;
                default:
                    goto pool32f_invalid;
                }
                break;
            do_fpop:
                gen_farith(ctx, mips32_op, rt, rs, rd, 0);
                break;
            default:
            pool32f_invalid:
                MIPS_INVAL("pool32f");
                gen_reserved_instruction(ctx);
                break;
            }
        } else {
            generate_exception_err(ctx, EXCP_CpU, 1);
        }
        break;
    case POOL32I:
        minor = (ctx->opcode >> 21) & 0x1f;
        switch (minor) {
        case BLTZ:
            check_insn_opc_removed(ctx, ISA_MIPS_R6);
            gen_compute_branch(ctx, OPC_BLTZ, 4, rs, -1, imm << 1, 4);
            break;
        case BLTZAL:
            check_insn_opc_removed(ctx, ISA_MIPS_R6);
            gen_compute_branch(ctx, OPC_BLTZAL, 4, rs, -1, imm << 1, 4);
            ctx->hflags |= MIPS_HFLAG_BDS_STRICT;
            break;
        case BLTZALS:
            check_insn_opc_removed(ctx, ISA_MIPS_R6);
            gen_compute_branch(ctx, OPC_BLTZAL, 4, rs, -1, imm << 1, 2);
            ctx->hflags |= MIPS_HFLAG_BDS_STRICT;
            break;
        case BGEZ:
            check_insn_opc_removed(ctx, ISA_MIPS_R6);
            gen_compute_branch(ctx, OPC_BGEZ, 4, rs, -1, imm << 1, 4);
            break;
        case BGEZAL:
            check_insn_opc_removed(ctx, ISA_MIPS_R6);
            gen_compute_branch(ctx, OPC_BGEZAL, 4, rs, -1, imm << 1, 4);
            ctx->hflags |= MIPS_HFLAG_BDS_STRICT;
            break;
        case BGEZALS:
            check_insn_opc_removed(ctx, ISA_MIPS_R6);
            gen_compute_branch(ctx, OPC_BGEZAL, 4, rs, -1, imm << 1, 2);
            ctx->hflags |= MIPS_HFLAG_BDS_STRICT;
            break;
        case BLEZ:
            check_insn_opc_removed(ctx, ISA_MIPS_R6);
            gen_compute_branch(ctx, OPC_BLEZ, 4, rs, -1, imm << 1, 4);
            break;
        case BGTZ:
            check_insn_opc_removed(ctx, ISA_MIPS_R6);
            gen_compute_branch(ctx, OPC_BGTZ, 4, rs, -1, imm << 1, 4);
            break;

            /* Traps */
        case TLTI: /* BC1EQZC */
            if (ctx->insn_flags & ISA_MIPS_R6) {
                /* BC1EQZC */
                check_cp1_enabled(ctx);
                gen_compute_branch1_r6(ctx, OPC_BC1EQZ, rs, imm << 1, 0);
            } else {
                /* TLTI */
                mips32_op = OPC_TLTI;
                goto do_trapi;
            }
            break;
        case TGEI: /* BC1NEZC */
            if (ctx->insn_flags & ISA_MIPS_R6) {
                /* BC1NEZC */
                check_cp1_enabled(ctx);
                gen_compute_branch1_r6(ctx, OPC_BC1NEZ, rs, imm << 1, 0);
            } else {
                /* TGEI */
                mips32_op = OPC_TGEI;
                goto do_trapi;
            }
            break;
        case TLTIU:
            check_insn_opc_removed(ctx, ISA_MIPS_R6);
            mips32_op = OPC_TLTIU;
            goto do_trapi;
        case TGEIU:
            check_insn_opc_removed(ctx, ISA_MIPS_R6);
            mips32_op = OPC_TGEIU;
            goto do_trapi;
        case TNEI: /* SYNCI */
            if (ctx->insn_flags & ISA_MIPS_R6) {
                /* SYNCI */
                /*
                 * Break the TB to be able to sync copied instructions
                 * immediately.
                 */
                ctx->base.is_jmp = DISAS_STOP;
            } else {
                /* TNEI */
                mips32_op = OPC_TNEI;
                goto do_trapi;
            }
            break;
        case TEQI:
            check_insn_opc_removed(ctx, ISA_MIPS_R6);
            mips32_op = OPC_TEQI;
        do_trapi:
            gen_trap(ctx, mips32_op, rs, -1, imm, 0);
            break;

        case BNEZC:
        case BEQZC:
            check_insn_opc_removed(ctx, ISA_MIPS_R6);
            gen_compute_branch(ctx, minor == BNEZC ? OPC_BNE : OPC_BEQ,
                               4, rs, 0, imm << 1, 0);
            /*
             * Compact branches don't have a delay slot, so just let
             * the normal delay slot handling take us to the branch
             * target.
             */
            break;
        case LUI:
            check_insn_opc_removed(ctx, ISA_MIPS_R6);
            gen_logic_imm(ctx, OPC_LUI, rs, 0, imm);
            break;
        case SYNCI:
            check_insn_opc_removed(ctx, ISA_MIPS_R6);
            /*
             * Break the TB to be able to sync copied instructions
             * immediately.
             */
            ctx->base.is_jmp = DISAS_STOP;
            break;
        case BC2F:
        case BC2T:
            check_insn_opc_removed(ctx, ISA_MIPS_R6);
            /* COP2: Not implemented. */
            generate_exception_err(ctx, EXCP_CpU, 2);
            break;
        case BC1F:
            check_insn_opc_removed(ctx, ISA_MIPS_R6);
            mips32_op = (ctx->opcode & (1 << 16)) ? OPC_BC1FANY2 : OPC_BC1F;
            goto do_cp1branch;
        case BC1T:
            check_insn_opc_removed(ctx, ISA_MIPS_R6);
            mips32_op = (ctx->opcode & (1 << 16)) ? OPC_BC1TANY2 : OPC_BC1T;
            goto do_cp1branch;
        case BC1ANY4F:
            check_insn_opc_removed(ctx, ISA_MIPS_R6);
            mips32_op = OPC_BC1FANY4;
            goto do_cp1mips3d;
        case BC1ANY4T:
            check_insn_opc_removed(ctx, ISA_MIPS_R6);
            mips32_op = OPC_BC1TANY4;
        do_cp1mips3d:
            check_cop1x(ctx);
            check_insn(ctx, ASE_MIPS3D);
            /* Fall through */
        do_cp1branch:
            if (env->CP0_Config1 & (1 << CP0C1_FP)) {
                check_cp1_enabled(ctx);
                gen_compute_branch1(ctx, mips32_op,
                                    (ctx->opcode >> 18) & 0x7, imm << 1);
            } else {
                generate_exception_err(ctx, EXCP_CpU, 1);
            }
            break;
        default:
            MIPS_INVAL("pool32i");
            gen_reserved_instruction(ctx);
            break;
        }
        break;
    case POOL32C:
        minor = (ctx->opcode >> 12) & 0xf;
        offset = sextract32(ctx->opcode, 0,
                            (ctx->insn_flags & ISA_MIPS_R6) ? 9 : 12);
        switch (minor) {
        case LWL:
            check_insn_opc_removed(ctx, ISA_MIPS_R6);
            mips32_op = OPC_LWL;
            goto do_ld_lr;
        case SWL:
            check_insn_opc_removed(ctx, ISA_MIPS_R6);
            mips32_op = OPC_SWL;
            goto do_st_lr;
        case LWR:
            check_insn_opc_removed(ctx, ISA_MIPS_R6);
            mips32_op = OPC_LWR;
            goto do_ld_lr;
        case SWR:
            check_insn_opc_removed(ctx, ISA_MIPS_R6);
            mips32_op = OPC_SWR;
            goto do_st_lr;
#if defined(TARGET_MIPS64)
        case LDL:
            check_insn(ctx, ISA_MIPS3);
            check_mips_64(ctx);
            check_insn_opc_removed(ctx, ISA_MIPS_R6);
            mips32_op = OPC_LDL;
            goto do_ld_lr;
        case SDL:
            check_insn(ctx, ISA_MIPS3);
            check_mips_64(ctx);
            check_insn_opc_removed(ctx, ISA_MIPS_R6);
            mips32_op = OPC_SDL;
            goto do_st_lr;
        case LDR:
            check_insn(ctx, ISA_MIPS3);
            check_mips_64(ctx);
            check_insn_opc_removed(ctx, ISA_MIPS_R6);
            mips32_op = OPC_LDR;
            goto do_ld_lr;
        case SDR:
            check_insn(ctx, ISA_MIPS3);
            check_mips_64(ctx);
            check_insn_opc_removed(ctx, ISA_MIPS_R6);
            mips32_op = OPC_SDR;
            goto do_st_lr;
        case LWU:
            check_insn(ctx, ISA_MIPS3);
            check_mips_64(ctx);
            mips32_op = OPC_LWU;
            goto do_ld_lr;
        case LLD:
            check_insn(ctx, ISA_MIPS3);
            check_mips_64(ctx);
            mips32_op = OPC_LLD;
            goto do_ld_lr;
#endif
        case LL:
            mips32_op = OPC_LL;
            goto do_ld_lr;
        do_ld_lr:
            gen_ld(ctx, mips32_op, rt, rs, offset);
            break;
        do_st_lr:
            gen_st(ctx, mips32_op, rt, rs, offset);
            break;
        case SC:
            gen_st_cond(ctx, rt, rs, offset, MO_TESL, false);
            break;
#if defined(TARGET_MIPS64)
        case SCD:
            check_insn(ctx, ISA_MIPS3);
            check_mips_64(ctx);
            gen_st_cond(ctx, rt, rs, offset, MO_TEUQ, false);
            break;
#endif
        case LD_EVA:
            if (!ctx->eva) {
                MIPS_INVAL("pool32c ld-eva");
                gen_reserved_instruction(ctx);
                break;
            }
            check_cp0_enabled(ctx);

            minor2 = (ctx->opcode >> 9) & 0x7;
            offset = sextract32(ctx->opcode, 0, 9);
            switch (minor2) {
            case LBUE:
                mips32_op = OPC_LBUE;
                goto do_ld_lr;
            case LHUE:
                mips32_op = OPC_LHUE;
                goto do_ld_lr;
            case LWLE:
                check_insn_opc_removed(ctx, ISA_MIPS_R6);
                mips32_op = OPC_LWLE;
                goto do_ld_lr;
            case LWRE:
                check_insn_opc_removed(ctx, ISA_MIPS_R6);
                mips32_op = OPC_LWRE;
                goto do_ld_lr;
            case LBE:
                mips32_op = OPC_LBE;
                goto do_ld_lr;
            case LHE:
                mips32_op = OPC_LHE;
                goto do_ld_lr;
            case LLE:
                mips32_op = OPC_LLE;
                goto do_ld_lr;
            case LWE:
                mips32_op = OPC_LWE;
                goto do_ld_lr;
            };
            break;
        case ST_EVA:
            if (!ctx->eva) {
                MIPS_INVAL("pool32c st-eva");
                gen_reserved_instruction(ctx);
                break;
            }
            check_cp0_enabled(ctx);

            minor2 = (ctx->opcode >> 9) & 0x7;
            offset = sextract32(ctx->opcode, 0, 9);
            switch (minor2) {
            case SWLE:
                check_insn_opc_removed(ctx, ISA_MIPS_R6);
                mips32_op = OPC_SWLE;
                goto do_st_lr;
            case SWRE:
                check_insn_opc_removed(ctx, ISA_MIPS_R6);
                mips32_op = OPC_SWRE;
                goto do_st_lr;
            case PREFE:
                /* Treat as no-op */
                if ((ctx->insn_flags & ISA_MIPS_R6) && (rt >= 24)) {
                    /* hint codes 24-31 are reserved and signal RI */
                    generate_exception(ctx, EXCP_RI);
                }
                break;
            case CACHEE:
                /* Treat as no-op */
                if (ctx->hflags & MIPS_HFLAG_ITC_CACHE) {
                    gen_cache_operation(ctx, rt, rs, offset);
                }
                break;
            case SBE:
                mips32_op = OPC_SBE;
                goto do_st_lr;
            case SHE:
                mips32_op = OPC_SHE;
                goto do_st_lr;
            case SCE:
                gen_st_cond(ctx, rt, rs, offset, MO_TESL, true);
                break;
            case SWE:
                mips32_op = OPC_SWE;
                goto do_st_lr;
            };
            break;
        case PREF:
            /* Treat as no-op */
            if ((ctx->insn_flags & ISA_MIPS_R6) && (rt >= 24)) {
                /* hint codes 24-31 are reserved and signal RI */
                generate_exception(ctx, EXCP_RI);
            }
            break;
        default:
            MIPS_INVAL("pool32c");
            gen_reserved_instruction(ctx);
            break;
        }
        break;
    case ADDI32: /* AUI, LUI */
        if (ctx->insn_flags & ISA_MIPS_R6) {
            /* AUI, LUI */
            gen_logic_imm(ctx, OPC_LUI, rt, rs, imm);
        } else {
            /* ADDI32 */
            mips32_op = OPC_ADDI;
            goto do_addi;
        }
        break;
    case ADDIU32:
        mips32_op = OPC_ADDIU;
    do_addi:
        gen_arith_imm(ctx, mips32_op, rt, rs, imm);
        break;

        /* Logical operations */
    case ORI32:
        mips32_op = OPC_ORI;
        goto do_logici;
    case XORI32:
        mips32_op = OPC_XORI;
        goto do_logici;
    case ANDI32:
        mips32_op = OPC_ANDI;
    do_logici:
        gen_logic_imm(ctx, mips32_op, rt, rs, imm);
        break;

        /* Set less than immediate */
    case SLTI32:
        mips32_op = OPC_SLTI;
        goto do_slti;
    case SLTIU32:
        mips32_op = OPC_SLTIU;
    do_slti:
        gen_slt_imm(ctx, mips32_op, rt, rs, imm);
        break;
    case JALX32:
        check_insn_opc_removed(ctx, ISA_MIPS_R6);
        offset = (int32_t)(ctx->opcode & 0x3FFFFFF) << 2;
        gen_compute_branch(ctx, OPC_JALX, 4, rt, rs, offset, 4);
        ctx->hflags |= MIPS_HFLAG_BDS_STRICT;
        break;
    case JALS32: /* BOVC, BEQC, BEQZALC */
        if (ctx->insn_flags & ISA_MIPS_R6) {
            if (rs >= rt) {
                /* BOVC */
                mips32_op = OPC_BOVC;
            } else if (rs < rt && rs == 0) {
                /* BEQZALC */
                mips32_op = OPC_BEQZALC;
            } else {
                /* BEQC */
                mips32_op = OPC_BEQC;
            }
            gen_compute_compact_branch(ctx, mips32_op, rs, rt, imm << 1);
        } else {
            /* JALS32 */
            offset = (int32_t)(ctx->opcode & 0x3FFFFFF) << 1;
            gen_compute_branch(ctx, OPC_JAL, 4, rt, rs, offset, 2);
            ctx->hflags |= MIPS_HFLAG_BDS_STRICT;
        }
        break;
    case BEQ32: /* BC */
        if (ctx->insn_flags & ISA_MIPS_R6) {
            /* BC */
            gen_compute_compact_branch(ctx, OPC_BC, 0, 0,
                                       sextract32(ctx->opcode << 1, 0, 27));
        } else {
            /* BEQ32 */
            gen_compute_branch(ctx, OPC_BEQ, 4, rt, rs, imm << 1, 4);
        }
        break;
    case BNE32: /* BALC */
        if (ctx->insn_flags & ISA_MIPS_R6) {
            /* BALC */
            gen_compute_compact_branch(ctx, OPC_BALC, 0, 0,
                                       sextract32(ctx->opcode << 1, 0, 27));
        } else {
            /* BNE32 */
            gen_compute_branch(ctx, OPC_BNE, 4, rt, rs, imm << 1, 4);
        }
        break;
    case J32: /* BGTZC, BLTZC, BLTC */
        if (ctx->insn_flags & ISA_MIPS_R6) {
            if (rs == 0 && rt != 0) {
                /* BGTZC */
                mips32_op = OPC_BGTZC;
            } else if (rs != 0 && rt != 0 && rs == rt) {
                /* BLTZC */
                mips32_op = OPC_BLTZC;
            } else {
                /* BLTC */
                mips32_op = OPC_BLTC;
            }
            gen_compute_compact_branch(ctx, mips32_op, rs, rt, imm << 1);
        } else {
            /* J32 */
            gen_compute_branch(ctx, OPC_J, 4, rt, rs,
                               (int32_t)(ctx->opcode & 0x3FFFFFF) << 1, 4);
        }
        break;
    case JAL32: /* BLEZC, BGEZC, BGEC */
        if (ctx->insn_flags & ISA_MIPS_R6) {
            if (rs == 0 && rt != 0) {
                /* BLEZC */
                mips32_op = OPC_BLEZC;
            } else if (rs != 0 && rt != 0 && rs == rt) {
                /* BGEZC */
                mips32_op = OPC_BGEZC;
            } else {
                /* BGEC */
                mips32_op = OPC_BGEC;
            }
            gen_compute_compact_branch(ctx, mips32_op, rs, rt, imm << 1);
        } else {
            /* JAL32 */
            gen_compute_branch(ctx, OPC_JAL, 4, rt, rs,
                               (int32_t)(ctx->opcode & 0x3FFFFFF) << 1, 4);
            ctx->hflags |= MIPS_HFLAG_BDS_STRICT;
        }
        break;
        /* Floating point (COP1) */
    case LWC132:
        mips32_op = OPC_LWC1;
        goto do_cop1;
    case LDC132:
        mips32_op = OPC_LDC1;
        goto do_cop1;
    case SWC132:
        mips32_op = OPC_SWC1;
        goto do_cop1;
    case SDC132:
        mips32_op = OPC_SDC1;
    do_cop1:
        gen_cop1_ldst(ctx, mips32_op, rt, rs, imm);
        break;
    case ADDIUPC: /* PCREL: ADDIUPC, AUIPC, ALUIPC, LWPC */
        if (ctx->insn_flags & ISA_MIPS_R6) {
            /* PCREL: ADDIUPC, AUIPC, ALUIPC, LWPC */
            switch ((ctx->opcode >> 16) & 0x1f) {
            case ADDIUPC_00:
            case ADDIUPC_01:
            case ADDIUPC_02:
            case ADDIUPC_03:
            case ADDIUPC_04:
            case ADDIUPC_05:
            case ADDIUPC_06:
            case ADDIUPC_07:
                gen_pcrel(ctx, OPC_ADDIUPC, ctx->base.pc_next & ~0x3, rt);
                break;
            case AUIPC:
                gen_pcrel(ctx, OPC_AUIPC, ctx->base.pc_next, rt);
                break;
            case ALUIPC:
                gen_pcrel(ctx, OPC_ALUIPC, ctx->base.pc_next, rt);
                break;
            case LWPC_08:
            case LWPC_09:
            case LWPC_0A:
            case LWPC_0B:
            case LWPC_0C:
            case LWPC_0D:
            case LWPC_0E:
            case LWPC_0F:
                gen_pcrel(ctx, R6_OPC_LWPC, ctx->base.pc_next & ~0x3, rt);
                break;
            default:
                generate_exception(ctx, EXCP_RI);
                break;
            }
        } else {
            /* ADDIUPC */
            int reg = mmreg(ZIMM(ctx->opcode, 23, 3));
            offset = SIMM(ctx->opcode, 0, 23) << 2;

            gen_addiupc(ctx, reg, offset, 0, 0);
        }
        break;
    case BNVC: /* BNEC, BNEZALC */
        check_insn(ctx, ISA_MIPS_R6);
        if (rs >= rt) {
            /* BNVC */
            mips32_op = OPC_BNVC;
        } else if (rs < rt && rs == 0) {
            /* BNEZALC */
            mips32_op = OPC_BNEZALC;
        } else {
            /* BNEC */
            mips32_op = OPC_BNEC;
        }
        gen_compute_compact_branch(ctx, mips32_op, rs, rt, imm << 1);
        break;
    case R6_BNEZC: /* JIALC */
        check_insn(ctx, ISA_MIPS_R6);
        if (rt != 0) {
            /* BNEZC */
            gen_compute_compact_branch(ctx, OPC_BNEZC, rt, 0,
                                       sextract32(ctx->opcode << 1, 0, 22));
        } else {
            /* JIALC */
            gen_compute_compact_branch(ctx, OPC_JIALC, 0, rs, imm);
        }
        break;
    case R6_BEQZC: /* JIC */
        check_insn(ctx, ISA_MIPS_R6);
        if (rt != 0) {
            /* BEQZC */
            gen_compute_compact_branch(ctx, OPC_BEQZC, rt, 0,
                                       sextract32(ctx->opcode << 1, 0, 22));
        } else {
            /* JIC */
            gen_compute_compact_branch(ctx, OPC_JIC, 0, rs, imm);
        }
        break;
    case BLEZALC: /* BGEZALC, BGEUC */
        check_insn(ctx, ISA_MIPS_R6);
        if (rs == 0 && rt != 0) {
            /* BLEZALC */
            mips32_op = OPC_BLEZALC;
        } else if (rs != 0 && rt != 0 && rs == rt) {
            /* BGEZALC */
            mips32_op = OPC_BGEZALC;
        } else {
            /* BGEUC */
            mips32_op = OPC_BGEUC;
        }
        gen_compute_compact_branch(ctx, mips32_op, rs, rt, imm << 1);
        break;
    case BGTZALC: /* BLTZALC, BLTUC */
        check_insn(ctx, ISA_MIPS_R6);
        if (rs == 0 && rt != 0) {
            /* BGTZALC */
            mips32_op = OPC_BGTZALC;
        } else if (rs != 0 && rt != 0 && rs == rt) {
            /* BLTZALC */
            mips32_op = OPC_BLTZALC;
        } else {
            /* BLTUC */
            mips32_op = OPC_BLTUC;
        }
        gen_compute_compact_branch(ctx, mips32_op, rs, rt, imm << 1);
        break;
        /* Loads and stores */
    case LB32:
        mips32_op = OPC_LB;
        goto do_ld;
    case LBU32:
        mips32_op = OPC_LBU;
        goto do_ld;
    case LH32:
        mips32_op = OPC_LH;
        goto do_ld;
    case LHU32:
        mips32_op = OPC_LHU;
        goto do_ld;
    case LW32:
        mips32_op = OPC_LW;
        goto do_ld;
#ifdef TARGET_MIPS64
    case LD32:
        check_insn(ctx, ISA_MIPS3);
        check_mips_64(ctx);
        mips32_op = OPC_LD;
        goto do_ld;
    case SD32:
        check_insn(ctx, ISA_MIPS3);
        check_mips_64(ctx);
        mips32_op = OPC_SD;
        goto do_st;
#endif
    case SB32:
        mips32_op = OPC_SB;
        goto do_st;
    case SH32:
        mips32_op = OPC_SH;
        goto do_st;
    case SW32:
        mips32_op = OPC_SW;
        goto do_st;
    do_ld:
        gen_ld(ctx, mips32_op, rt, rs, imm);
        break;
    do_st:
        gen_st(ctx, mips32_op, rt, rs, imm);
        break;
    default:
        gen_reserved_instruction(ctx);
        break;
    }
}

static int decode_isa_micromips(CPUMIPSState *env, DisasContext *ctx)
{
    uint32_t op;

    /* make sure instructions are on a halfword boundary */
    if (ctx->base.pc_next & 0x1) {
        env->CP0_BadVAddr = ctx->base.pc_next;
        generate_exception_end(ctx, EXCP_AdEL);
        return 2;
    }

    op = (ctx->opcode >> 10) & 0x3f;
    /* Enforce properly-sized instructions in a delay slot */
    if (ctx->hflags & MIPS_HFLAG_BDS_STRICT) {
        switch (op & 0x7) { /* MSB-3..MSB-5 */
        case 0:
        /* POOL32A, POOL32B, POOL32I, POOL32C */
        case 4:
        /* ADDI32, ADDIU32, ORI32, XORI32, SLTI32, SLTIU32, ANDI32, JALX32 */
        case 5:
        /* LBU32, LHU32, POOL32F, JALS32, BEQ32, BNE32, J32, JAL32 */
        case 6:
        /* SB32, SH32, ADDIUPC, SWC132, SDC132, SW32 */
        case 7:
        /* LB32, LH32, LWC132, LDC132, LW32 */
            if (ctx->hflags & MIPS_HFLAG_BDS16) {
                gen_reserved_instruction(ctx);
                return 2;
            }
            break;
        case 1:
        /* POOL16A, POOL16B, POOL16C, LWGP16, POOL16F */
        case 2:
        /* LBU16, LHU16, LWSP16, LW16, SB16, SH16, SWSP16, SW16 */
        case 3:
        /* MOVE16, ANDI16, POOL16D, POOL16E, BEQZ16, BNEZ16, B16, LI16 */
            if (ctx->hflags & MIPS_HFLAG_BDS32) {
                gen_reserved_instruction(ctx);
                return 2;
            }
            break;
        }
    }

    switch (op) {
    case POOL16A:
        {
            int rd = mmreg(uMIPS_RD(ctx->opcode));
            int rs1 = mmreg(uMIPS_RS1(ctx->opcode));
            int rs2 = mmreg(uMIPS_RS2(ctx->opcode));
            uint32_t opc = 0;

            switch (ctx->opcode & 0x1) {
            case ADDU16:
                opc = OPC_ADDU;
                break;
            case SUBU16:
                opc = OPC_SUBU;
                break;
            }
            if (ctx->insn_flags & ISA_MIPS_R6) {
                /*
                 * In the Release 6, the register number location in
                 * the instruction encoding has changed.
                 */
                gen_arith(ctx, opc, rs1, rd, rs2);
            } else {
                gen_arith(ctx, opc, rd, rs1, rs2);
            }
        }
        break;
    case POOL16B:
        {
            int rd = mmreg(uMIPS_RD(ctx->opcode));
            int rs = mmreg(uMIPS_RS(ctx->opcode));
            int amount = (ctx->opcode >> 1) & 0x7;
            uint32_t opc = 0;
            amount = amount == 0 ? 8 : amount;

            switch (ctx->opcode & 0x1) {
            case SLL16:
                opc = OPC_SLL;
                break;
            case SRL16:
                opc = OPC_SRL;
                break;
            }

            gen_shift_imm(ctx, opc, rd, rs, amount);
        }
        break;
    case POOL16C:
        if (ctx->insn_flags & ISA_MIPS_R6) {
            gen_pool16c_r6_insn(ctx);
        } else {
            gen_pool16c_insn(ctx);
        }
        break;
    case LWGP16:
        {
            int rd = mmreg(uMIPS_RD(ctx->opcode));
            int rb = 28;            /* GP */
            int16_t offset = SIMM(ctx->opcode, 0, 7) << 2;

            gen_ld(ctx, OPC_LW, rd, rb, offset);
        }
        break;
    case POOL16F:
        check_insn_opc_removed(ctx, ISA_MIPS_R6);
        if (ctx->opcode & 1) {
            gen_reserved_instruction(ctx);
        } else {
            /* MOVEP */
            int enc_dest = uMIPS_RD(ctx->opcode);
            int enc_rt = uMIPS_RS2(ctx->opcode);
            int enc_rs = uMIPS_RS1(ctx->opcode);
            gen_movep(ctx, enc_dest, enc_rt, enc_rs);
        }
        break;
    case LBU16:
        {
            int rd = mmreg(uMIPS_RD(ctx->opcode));
            int rb = mmreg(uMIPS_RS(ctx->opcode));
            int16_t offset = ZIMM(ctx->opcode, 0, 4);
            offset = (offset == 0xf ? -1 : offset);

            gen_ld(ctx, OPC_LBU, rd, rb, offset);
        }
        break;
    case LHU16:
        {
            int rd = mmreg(uMIPS_RD(ctx->opcode));
            int rb = mmreg(uMIPS_RS(ctx->opcode));
            int16_t offset = ZIMM(ctx->opcode, 0, 4) << 1;

            gen_ld(ctx, OPC_LHU, rd, rb, offset);
        }
        break;
    case LWSP16:
        {
            int rd = (ctx->opcode >> 5) & 0x1f;
            int rb = 29;            /* SP */
            int16_t offset = ZIMM(ctx->opcode, 0, 5) << 2;

            gen_ld(ctx, OPC_LW, rd, rb, offset);
        }
        break;
    case LW16:
        {
            int rd = mmreg(uMIPS_RD(ctx->opcode));
            int rb = mmreg(uMIPS_RS(ctx->opcode));
            int16_t offset = ZIMM(ctx->opcode, 0, 4) << 2;

            gen_ld(ctx, OPC_LW, rd, rb, offset);
        }
        break;
    case SB16:
        {
            int rd = mmreg2(uMIPS_RD(ctx->opcode));
            int rb = mmreg(uMIPS_RS(ctx->opcode));
            int16_t offset = ZIMM(ctx->opcode, 0, 4);

            gen_st(ctx, OPC_SB, rd, rb, offset);
        }
        break;
    case SH16:
        {
            int rd = mmreg2(uMIPS_RD(ctx->opcode));
            int rb = mmreg(uMIPS_RS(ctx->opcode));
            int16_t offset = ZIMM(ctx->opcode, 0, 4) << 1;

            gen_st(ctx, OPC_SH, rd, rb, offset);
        }
        break;
    case SWSP16:
        {
            int rd = (ctx->opcode >> 5) & 0x1f;
            int rb = 29;            /* SP */
            int16_t offset = ZIMM(ctx->opcode, 0, 5) << 2;

            gen_st(ctx, OPC_SW, rd, rb, offset);
        }
        break;
    case SW16:
        {
            int rd = mmreg2(uMIPS_RD(ctx->opcode));
            int rb = mmreg(uMIPS_RS(ctx->opcode));
            int16_t offset = ZIMM(ctx->opcode, 0, 4) << 2;

            gen_st(ctx, OPC_SW, rd, rb, offset);
        }
        break;
    case MOVE16:
        {
            int rd = uMIPS_RD5(ctx->opcode);
            int rs = uMIPS_RS5(ctx->opcode);

            gen_arith(ctx, OPC_ADDU, rd, rs, 0);
        }
        break;
    case ANDI16:
        gen_andi16(ctx);
        break;
    case POOL16D:
        switch (ctx->opcode & 0x1) {
        case ADDIUS5:
            gen_addius5(ctx);
            break;
        case ADDIUSP:
            gen_addiusp(ctx);
            break;
        }
        break;
    case POOL16E:
        switch (ctx->opcode & 0x1) {
        case ADDIUR2:
            gen_addiur2(ctx);
            break;
        case ADDIUR1SP:
            gen_addiur1sp(ctx);
            break;
        }
        break;
    case B16: /* BC16 */
        gen_compute_branch(ctx, OPC_BEQ, 2, 0, 0,
                           sextract32(ctx->opcode, 0, 10) << 1,
                           (ctx->insn_flags & ISA_MIPS_R6) ? 0 : 4);
        break;
    case BNEZ16: /* BNEZC16 */
    case BEQZ16: /* BEQZC16 */
        gen_compute_branch(ctx, op == BNEZ16 ? OPC_BNE : OPC_BEQ, 2,
                           mmreg(uMIPS_RD(ctx->opcode)),
                           0, sextract32(ctx->opcode, 0, 7) << 1,
                           (ctx->insn_flags & ISA_MIPS_R6) ? 0 : 4);

        break;
    case LI16:
        {
            int reg = mmreg(uMIPS_RD(ctx->opcode));
            int imm = ZIMM(ctx->opcode, 0, 7);

            imm = (imm == 0x7f ? -1 : imm);
            tcg_gen_movi_tl(cpu_gpr[reg], imm);
        }
        break;
    case RES_29:
    case RES_31:
    case RES_39:
        gen_reserved_instruction(ctx);
        break;
    default:
        decode_micromips32_opc(env, ctx);
        return 4;
    }

    return 2;
}

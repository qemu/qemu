/*
 *  MIPS emulation for QEMU - nanoMIPS translation routines
 *
 *  Copyright (c) 2004-2005 Jocelyn Mayer
 *  Copyright (c) 2006 Marius Groeger (FPU operations)
 *  Copyright (c) 2006 Thiemo Seufer (MIPS32R2 support)
 *  Copyright (c) 2009 CodeSourcery (MIPS16 and microMIPS support)
 *  Copyright (c) 2012 Jia Liu & Dongxue Zhang (MIPS ASE DSP support)
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/* MAJOR, P16, and P32 pools opcodes */
enum {
    NM_P_ADDIU      = 0x00,
    NM_ADDIUPC      = 0x01,
    NM_MOVE_BALC    = 0x02,
    NM_P16_MV       = 0x04,
    NM_LW16         = 0x05,
    NM_BC16         = 0x06,
    NM_P16_SR       = 0x07,

    NM_POOL32A      = 0x08,
    NM_P_BAL        = 0x0a,
    NM_P16_SHIFT    = 0x0c,
    NM_LWSP16       = 0x0d,
    NM_BALC16       = 0x0e,
    NM_P16_4X4      = 0x0f,

    NM_P_GP_W       = 0x10,
    NM_P_GP_BH      = 0x11,
    NM_P_J          = 0x12,
    NM_P16C         = 0x14,
    NM_LWGP16       = 0x15,
    NM_P16_LB       = 0x17,

    NM_P48I         = 0x18,
    NM_P16_A1       = 0x1c,
    NM_LW4X4        = 0x1d,
    NM_P16_LH       = 0x1f,

    NM_P_U12        = 0x20,
    NM_P_LS_U12     = 0x21,
    NM_P_BR1        = 0x22,
    NM_P16_A2       = 0x24,
    NM_SW16         = 0x25,
    NM_BEQZC16      = 0x26,

    NM_POOL32F      = 0x28,
    NM_P_LS_S9      = 0x29,
    NM_P_BR2        = 0x2a,

    NM_P16_ADDU     = 0x2c,
    NM_SWSP16       = 0x2d,
    NM_BNEZC16      = 0x2e,
    NM_MOVEP        = 0x2f,

    NM_POOL32S      = 0x30,
    NM_P_BRI        = 0x32,
    NM_LI16         = 0x34,
    NM_SWGP16       = 0x35,
    NM_P16_BR       = 0x36,

    NM_P_LUI        = 0x38,
    NM_ANDI16       = 0x3c,
    NM_SW4X4        = 0x3d,
    NM_MOVEPREV     = 0x3f,
};

/* POOL32A instruction pool */
enum {
    NM_POOL32A0    = 0x00,
    NM_SPECIAL2    = 0x01,
    NM_COP2_1      = 0x02,
    NM_UDI         = 0x03,
    NM_POOL32A5    = 0x05,
    NM_POOL32A7    = 0x07,
};

/* P.GP.W instruction pool */
enum {
    NM_ADDIUGP_W = 0x00,
    NM_LWGP      = 0x02,
    NM_SWGP      = 0x03,
};

/* P48I instruction pool */
enum {
    NM_LI48        = 0x00,
    NM_ADDIU48     = 0x01,
    NM_ADDIUGP48   = 0x02,
    NM_ADDIUPC48   = 0x03,
    NM_LWPC48      = 0x0b,
    NM_SWPC48      = 0x0f,
};

/* P.U12 instruction pool */
enum {
    NM_ORI      = 0x00,
    NM_XORI     = 0x01,
    NM_ANDI     = 0x02,
    NM_P_SR     = 0x03,
    NM_SLTI     = 0x04,
    NM_SLTIU    = 0x05,
    NM_SEQI     = 0x06,
    NM_ADDIUNEG = 0x08,
    NM_P_SHIFT  = 0x0c,
    NM_P_ROTX   = 0x0d,
    NM_P_INS    = 0x0e,
    NM_P_EXT    = 0x0f,
};

/* POOL32F instruction pool */
enum {
    NM_POOL32F_0   = 0x00,
    NM_POOL32F_3   = 0x03,
    NM_POOL32F_5   = 0x05,
};

/* POOL32S instruction pool */
enum {
    NM_POOL32S_0   = 0x00,
    NM_POOL32S_4   = 0x04,
};

/* P.LUI instruction pool */
enum {
    NM_LUI      = 0x00,
    NM_ALUIPC   = 0x01,
};

/* P.GP.BH instruction pool */
enum {
    NM_LBGP      = 0x00,
    NM_SBGP      = 0x01,
    NM_LBUGP     = 0x02,
    NM_ADDIUGP_B = 0x03,
    NM_P_GP_LH   = 0x04,
    NM_P_GP_SH   = 0x05,
    NM_P_GP_CP1  = 0x06,
};

/* P.LS.U12 instruction pool */
enum {
    NM_LB        = 0x00,
    NM_SB        = 0x01,
    NM_LBU       = 0x02,
    NM_P_PREFU12 = 0x03,
    NM_LH        = 0x04,
    NM_SH        = 0x05,
    NM_LHU       = 0x06,
    NM_LWU       = 0x07,
    NM_LW        = 0x08,
    NM_SW        = 0x09,
    NM_LWC1      = 0x0a,
    NM_SWC1      = 0x0b,
    NM_LDC1      = 0x0e,
    NM_SDC1      = 0x0f,
};

/* P.LS.S9 instruction pool */
enum {
    NM_P_LS_S0         = 0x00,
    NM_P_LS_S1         = 0x01,
    NM_P_LS_E0         = 0x02,
    NM_P_LS_WM         = 0x04,
    NM_P_LS_UAWM       = 0x05,
};

/* P.BAL instruction pool */
enum {
    NM_BC       = 0x00,
    NM_BALC     = 0x01,
};

/* P.J instruction pool */
enum {
    NM_JALRC    = 0x00,
    NM_JALRC_HB = 0x01,
    NM_P_BALRSC = 0x08,
};

/* P.BR1 instruction pool */
enum {
    NM_BEQC     = 0x00,
    NM_P_BR3A   = 0x01,
    NM_BGEC     = 0x02,
    NM_BGEUC    = 0x03,
};

/* P.BR2 instruction pool */
enum {
    NM_BNEC     = 0x00,
    NM_BLTC     = 0x02,
    NM_BLTUC    = 0x03,
};

/* P.BRI instruction pool */
enum {
    NM_BEQIC    = 0x00,
    NM_BBEQZC   = 0x01,
    NM_BGEIC    = 0x02,
    NM_BGEIUC   = 0x03,
    NM_BNEIC    = 0x04,
    NM_BBNEZC   = 0x05,
    NM_BLTIC    = 0x06,
    NM_BLTIUC   = 0x07,
};

/* P16.SHIFT instruction pool */
enum {
    NM_SLL16    = 0x00,
    NM_SRL16    = 0x01,
};

/* POOL16C instruction pool */
enum {
    NM_POOL16C_0  = 0x00,
    NM_LWXS16     = 0x01,
};

/* P16.A1 instruction pool */
enum {
    NM_ADDIUR1SP = 0x01,
};

/* P16.A2 instruction pool */
enum {
    NM_ADDIUR2  = 0x00,
    NM_P_ADDIURS5  = 0x01,
};

/* P16.ADDU instruction pool */
enum {
    NM_ADDU16     = 0x00,
    NM_SUBU16     = 0x01,
};

/* P16.SR instruction pool */
enum {
    NM_SAVE16        = 0x00,
    NM_RESTORE_JRC16 = 0x01,
};

/* P16.4X4 instruction pool */
enum {
    NM_ADDU4X4      = 0x00,
    NM_MUL4X4       = 0x01,
};

/* P16.LB instruction pool */
enum {
    NM_LB16       = 0x00,
    NM_SB16       = 0x01,
    NM_LBU16      = 0x02,
};

/* P16.LH  instruction pool */
enum {
    NM_LH16     = 0x00,
    NM_SH16     = 0x01,
    NM_LHU16    = 0x02,
};

/* P.RI instruction pool */
enum {
    NM_SIGRIE       = 0x00,
    NM_P_SYSCALL    = 0x01,
    NM_BREAK        = 0x02,
    NM_SDBBP        = 0x03,
};

/* POOL32A0 instruction pool */
enum {
    NM_P_TRAP   = 0x00,
    NM_SEB      = 0x01,
    NM_SLLV     = 0x02,
    NM_MUL      = 0x03,
    NM_MFC0     = 0x06,
    NM_MFHC0    = 0x07,
    NM_SEH      = 0x09,
    NM_SRLV     = 0x0a,
    NM_MUH      = 0x0b,
    NM_MTC0     = 0x0e,
    NM_MTHC0    = 0x0f,
    NM_SRAV     = 0x12,
    NM_MULU     = 0x13,
    NM_ROTRV    = 0x1a,
    NM_MUHU     = 0x1b,
    NM_ADD      = 0x22,
    NM_DIV      = 0x23,
    NM_ADDU     = 0x2a,
    NM_MOD      = 0x2b,
    NM_SUB      = 0x32,
    NM_DIVU     = 0x33,
    NM_RDHWR    = 0x38,
    NM_SUBU     = 0x3a,
    NM_MODU     = 0x3b,
    NM_P_CMOVE  = 0x42,
    NM_FORK     = 0x45,
    NM_MFTR     = 0x46,
    NM_MFHTR    = 0x47,
    NM_AND      = 0x4a,
    NM_YIELD    = 0x4d,
    NM_MTTR     = 0x4e,
    NM_MTHTR    = 0x4f,
    NM_OR       = 0x52,
    NM_D_E_MT_VPE = 0x56,
    NM_NOR      = 0x5a,
    NM_XOR      = 0x62,
    NM_SLT      = 0x6a,
    NM_P_SLTU   = 0x72,
    NM_SOV      = 0x7a,
};

/* CRC32 instruction pool */
enum {
    NM_CRC32B   = 0x00,
    NM_CRC32H   = 0x01,
    NM_CRC32W   = 0x02,
    NM_CRC32CB  = 0x04,
    NM_CRC32CH  = 0x05,
    NM_CRC32CW  = 0x06,
};

/* POOL32A5 instruction pool */
enum {
    NM_CMP_EQ_PH        = 0x00,
    NM_CMP_LT_PH        = 0x08,
    NM_CMP_LE_PH        = 0x10,
    NM_CMPGU_EQ_QB      = 0x18,
    NM_CMPGU_LT_QB      = 0x20,
    NM_CMPGU_LE_QB      = 0x28,
    NM_CMPGDU_EQ_QB     = 0x30,
    NM_CMPGDU_LT_QB     = 0x38,
    NM_CMPGDU_LE_QB     = 0x40,
    NM_CMPU_EQ_QB       = 0x48,
    NM_CMPU_LT_QB       = 0x50,
    NM_CMPU_LE_QB       = 0x58,
    NM_ADDQ_S_W         = 0x60,
    NM_SUBQ_S_W         = 0x68,
    NM_ADDSC            = 0x70,
    NM_ADDWC            = 0x78,

    NM_ADDQ_S_PH   = 0x01,
    NM_ADDQH_R_PH  = 0x09,
    NM_ADDQH_R_W   = 0x11,
    NM_ADDU_S_QB   = 0x19,
    NM_ADDU_S_PH   = 0x21,
    NM_ADDUH_R_QB  = 0x29,
    NM_SHRAV_R_PH  = 0x31,
    NM_SHRAV_R_QB  = 0x39,
    NM_SUBQ_S_PH   = 0x41,
    NM_SUBQH_R_PH  = 0x49,
    NM_SUBQH_R_W   = 0x51,
    NM_SUBU_S_QB   = 0x59,
    NM_SUBU_S_PH   = 0x61,
    NM_SUBUH_R_QB  = 0x69,
    NM_SHLLV_S_PH  = 0x71,
    NM_PRECR_SRA_R_PH_W = 0x79,

    NM_MULEU_S_PH_QBL   = 0x12,
    NM_MULEU_S_PH_QBR   = 0x1a,
    NM_MULQ_RS_PH       = 0x22,
    NM_MULQ_S_PH        = 0x2a,
    NM_MULQ_RS_W        = 0x32,
    NM_MULQ_S_W         = 0x3a,
    NM_APPEND           = 0x42,
    NM_MODSUB           = 0x52,
    NM_SHRAV_R_W        = 0x5a,
    NM_SHRLV_PH         = 0x62,
    NM_SHRLV_QB         = 0x6a,
    NM_SHLLV_QB         = 0x72,
    NM_SHLLV_S_W        = 0x7a,

    NM_SHILO            = 0x03,

    NM_MULEQ_S_W_PHL    = 0x04,
    NM_MULEQ_S_W_PHR    = 0x0c,

    NM_MUL_S_PH         = 0x05,
    NM_PRECR_QB_PH      = 0x0d,
    NM_PRECRQ_QB_PH     = 0x15,
    NM_PRECRQ_PH_W      = 0x1d,
    NM_PRECRQ_RS_PH_W   = 0x25,
    NM_PRECRQU_S_QB_PH  = 0x2d,
    NM_PACKRL_PH        = 0x35,
    NM_PICK_QB          = 0x3d,
    NM_PICK_PH          = 0x45,

    NM_SHRA_R_W         = 0x5e,
    NM_SHRA_R_PH        = 0x66,
    NM_SHLL_S_PH        = 0x76,
    NM_SHLL_S_W         = 0x7e,

    NM_REPL_PH          = 0x07
};

/* POOL32A7 instruction pool */
enum {
    NM_P_LSX        = 0x00,
    NM_LSA          = 0x01,
    NM_EXTW         = 0x03,
    NM_POOL32AXF    = 0x07,
};

/* P.SR instruction pool */
enum {
    NM_PP_SR           = 0x00,
    NM_P_SR_F          = 0x01,
};

/* P.SHIFT instruction pool */
enum {
    NM_P_SLL        = 0x00,
    NM_SRL          = 0x02,
    NM_SRA          = 0x04,
    NM_ROTR         = 0x06,
};

/* P.ROTX instruction pool */
enum {
    NM_ROTX         = 0x00,
};

/* P.INS instruction pool */
enum {
    NM_INS          = 0x00,
};

/* P.EXT instruction pool */
enum {
    NM_EXT          = 0x00,
};

/* POOL32F_0 (fmt) instruction pool */
enum {
    NM_RINT_S              = 0x04,
    NM_RINT_D              = 0x44,
    NM_ADD_S               = 0x06,
    NM_SELEQZ_S            = 0x07,
    NM_SELEQZ_D            = 0x47,
    NM_CLASS_S             = 0x0c,
    NM_CLASS_D             = 0x4c,
    NM_SUB_S               = 0x0e,
    NM_SELNEZ_S            = 0x0f,
    NM_SELNEZ_D            = 0x4f,
    NM_MUL_S               = 0x16,
    NM_SEL_S               = 0x17,
    NM_SEL_D               = 0x57,
    NM_DIV_S               = 0x1e,
    NM_ADD_D               = 0x26,
    NM_SUB_D               = 0x2e,
    NM_MUL_D               = 0x36,
    NM_MADDF_S             = 0x37,
    NM_MADDF_D             = 0x77,
    NM_DIV_D               = 0x3e,
    NM_MSUBF_S             = 0x3f,
    NM_MSUBF_D             = 0x7f,
};

/* POOL32F_3  instruction pool */
enum {
    NM_MIN_FMT         = 0x00,
    NM_MAX_FMT         = 0x01,
    NM_MINA_FMT        = 0x04,
    NM_MAXA_FMT        = 0x05,
    NM_POOL32FXF       = 0x07,
};

/* POOL32F_5  instruction pool */
enum {
    NM_CMP_CONDN_S     = 0x00,
    NM_CMP_CONDN_D     = 0x02,
};

/* P.GP.LH instruction pool */
enum {
    NM_LHGP    = 0x00,
    NM_LHUGP   = 0x01,
};

/* P.GP.SH instruction pool */
enum {
    NM_SHGP    = 0x00,
};

/* P.GP.CP1 instruction pool */
enum {
    NM_LWC1GP       = 0x00,
    NM_SWC1GP       = 0x01,
    NM_LDC1GP       = 0x02,
    NM_SDC1GP       = 0x03,
};

/* P.LS.S0 instruction pool */
enum {
    NM_LBS9     = 0x00,
    NM_LHS9     = 0x04,
    NM_LWS9     = 0x08,
    NM_LDS9     = 0x0c,

    NM_SBS9     = 0x01,
    NM_SHS9     = 0x05,
    NM_SWS9     = 0x09,
    NM_SDS9     = 0x0d,

    NM_LBUS9    = 0x02,
    NM_LHUS9    = 0x06,
    NM_LWC1S9   = 0x0a,
    NM_LDC1S9   = 0x0e,

    NM_P_PREFS9 = 0x03,
    NM_LWUS9    = 0x07,
    NM_SWC1S9   = 0x0b,
    NM_SDC1S9   = 0x0f,
};

/* P.LS.S1 instruction pool */
enum {
    NM_ASET_ACLR = 0x02,
    NM_UALH      = 0x04,
    NM_UASH      = 0x05,
    NM_CACHE     = 0x07,
    NM_P_LL      = 0x0a,
    NM_P_SC      = 0x0b,
};

/* P.LS.E0 instruction pool */
enum {
    NM_LBE      = 0x00,
    NM_SBE      = 0x01,
    NM_LBUE     = 0x02,
    NM_P_PREFE  = 0x03,
    NM_LHE      = 0x04,
    NM_SHE      = 0x05,
    NM_LHUE     = 0x06,
    NM_CACHEE   = 0x07,
    NM_LWE      = 0x08,
    NM_SWE      = 0x09,
    NM_P_LLE    = 0x0a,
    NM_P_SCE    = 0x0b,
};

/* P.PREFE instruction pool */
enum {
    NM_SYNCIE   = 0x00,
    NM_PREFE    = 0x01,
};

/* P.LLE instruction pool */
enum {
    NM_LLE      = 0x00,
    NM_LLWPE    = 0x01,
};

/* P.SCE instruction pool */
enum {
    NM_SCE      = 0x00,
    NM_SCWPE    = 0x01,
};

/* P.LS.WM instruction pool */
enum {
    NM_LWM       = 0x00,
    NM_SWM       = 0x01,
};

/* P.LS.UAWM instruction pool */
enum {
    NM_UALWM       = 0x00,
    NM_UASWM       = 0x01,
};

/* P.BR3A instruction pool */
enum {
    NM_BC1EQZC          = 0x00,
    NM_BC1NEZC          = 0x01,
    NM_BC2EQZC          = 0x02,
    NM_BC2NEZC          = 0x03,
    NM_BPOSGE32C        = 0x04,
};

/* P16.RI instruction pool */
enum {
    NM_P16_SYSCALL  = 0x01,
    NM_BREAK16      = 0x02,
    NM_SDBBP16      = 0x03,
};

/* POOL16C_0 instruction pool */
enum {
    NM_POOL16C_00      = 0x00,
};

/* P16.JRC instruction pool */
enum {
    NM_JRC          = 0x00,
    NM_JALRC16      = 0x01,
};

/* P.SYSCALL instruction pool */
enum {
    NM_SYSCALL      = 0x00,
    NM_HYPCALL      = 0x01,
};

/* P.TRAP instruction pool */
enum {
    NM_TEQ          = 0x00,
    NM_TNE          = 0x01,
};

/* P.CMOVE instruction pool */
enum {
    NM_MOVZ            = 0x00,
    NM_MOVN            = 0x01,
};

/* POOL32Axf instruction pool */
enum {
    NM_POOL32AXF_1 = 0x01,
    NM_POOL32AXF_2 = 0x02,
    NM_POOL32AXF_4 = 0x04,
    NM_POOL32AXF_5 = 0x05,
    NM_POOL32AXF_7 = 0x07,
};

/* POOL32Axf_1 instruction pool */
enum {
    NM_POOL32AXF_1_0 = 0x00,
    NM_POOL32AXF_1_1 = 0x01,
    NM_POOL32AXF_1_3 = 0x03,
    NM_POOL32AXF_1_4 = 0x04,
    NM_POOL32AXF_1_5 = 0x05,
    NM_POOL32AXF_1_7 = 0x07,
};

/* POOL32Axf_2 instruction pool */
enum {
    NM_POOL32AXF_2_0_7     = 0x00,
    NM_POOL32AXF_2_8_15    = 0x01,
    NM_POOL32AXF_2_16_23   = 0x02,
    NM_POOL32AXF_2_24_31   = 0x03,
};

/* POOL32Axf_7 instruction pool */
enum {
    NM_SHRA_R_QB    = 0x0,
    NM_SHRL_PH      = 0x1,
    NM_REPL_QB      = 0x2,
};

/* POOL32Axf_1_0 instruction pool */
enum {
    NM_MFHI = 0x0,
    NM_MFLO = 0x1,
    NM_MTHI = 0x2,
    NM_MTLO = 0x3,
};

/* POOL32Axf_1_1 instruction pool */
enum {
    NM_MTHLIP = 0x0,
    NM_SHILOV = 0x1,
};

/* POOL32Axf_1_3 instruction pool */
enum {
    NM_RDDSP    = 0x0,
    NM_WRDSP    = 0x1,
    NM_EXTP     = 0x2,
    NM_EXTPDP   = 0x3,
};

/* POOL32Axf_1_4 instruction pool */
enum {
    NM_SHLL_QB  = 0x0,
    NM_SHRL_QB  = 0x1,
};

/* POOL32Axf_1_5 instruction pool */
enum {
    NM_MAQ_S_W_PHR   = 0x0,
    NM_MAQ_S_W_PHL   = 0x1,
    NM_MAQ_SA_W_PHR  = 0x2,
    NM_MAQ_SA_W_PHL  = 0x3,
};

/* POOL32Axf_1_7 instruction pool */
enum {
    NM_EXTR_W       = 0x0,
    NM_EXTR_R_W     = 0x1,
    NM_EXTR_RS_W    = 0x2,
    NM_EXTR_S_H     = 0x3,
};

/* POOL32Axf_2_0_7 instruction pool */
enum {
    NM_DPA_W_PH     = 0x0,
    NM_DPAQ_S_W_PH  = 0x1,
    NM_DPS_W_PH     = 0x2,
    NM_DPSQ_S_W_PH  = 0x3,
    NM_BALIGN       = 0x4,
    NM_MADD         = 0x5,
    NM_MULT         = 0x6,
    NM_EXTRV_W      = 0x7,
};

/* POOL32Axf_2_8_15 instruction pool */
enum {
    NM_DPAX_W_PH    = 0x0,
    NM_DPAQ_SA_L_W  = 0x1,
    NM_DPSX_W_PH    = 0x2,
    NM_DPSQ_SA_L_W  = 0x3,
    NM_MADDU        = 0x5,
    NM_MULTU        = 0x6,
    NM_EXTRV_R_W    = 0x7,
};

/* POOL32Axf_2_16_23 instruction pool */
enum {
    NM_DPAU_H_QBL       = 0x0,
    NM_DPAQX_S_W_PH     = 0x1,
    NM_DPSU_H_QBL       = 0x2,
    NM_DPSQX_S_W_PH     = 0x3,
    NM_EXTPV            = 0x4,
    NM_MSUB             = 0x5,
    NM_MULSA_W_PH       = 0x6,
    NM_EXTRV_RS_W       = 0x7,
};

/* POOL32Axf_2_24_31 instruction pool */
enum {
    NM_DPAU_H_QBR       = 0x0,
    NM_DPAQX_SA_W_PH    = 0x1,
    NM_DPSU_H_QBR       = 0x2,
    NM_DPSQX_SA_W_PH    = 0x3,
    NM_EXTPDPV          = 0x4,
    NM_MSUBU            = 0x5,
    NM_MULSAQ_S_W_PH    = 0x6,
    NM_EXTRV_S_H        = 0x7,
};

/* POOL32Axf_{4, 5} instruction pool */
enum {
    NM_CLO      = 0x25,
    NM_CLZ      = 0x2d,

    NM_TLBP     = 0x01,
    NM_TLBR     = 0x09,
    NM_TLBWI    = 0x11,
    NM_TLBWR    = 0x19,
    NM_TLBINV   = 0x03,
    NM_TLBINVF  = 0x0b,
    NM_DI       = 0x23,
    NM_EI       = 0x2b,
    NM_RDPGPR   = 0x70,
    NM_WRPGPR   = 0x78,
    NM_WAIT     = 0x61,
    NM_DERET    = 0x71,
    NM_ERETX    = 0x79,

    /* nanoMIPS DSP instructions */
    NM_ABSQ_S_QB        = 0x00,
    NM_ABSQ_S_PH        = 0x08,
    NM_ABSQ_S_W         = 0x10,
    NM_PRECEQ_W_PHL     = 0x28,
    NM_PRECEQ_W_PHR     = 0x30,
    NM_PRECEQU_PH_QBL   = 0x38,
    NM_PRECEQU_PH_QBR   = 0x48,
    NM_PRECEU_PH_QBL    = 0x58,
    NM_PRECEU_PH_QBR    = 0x68,
    NM_PRECEQU_PH_QBLA  = 0x39,
    NM_PRECEQU_PH_QBRA  = 0x49,
    NM_PRECEU_PH_QBLA   = 0x59,
    NM_PRECEU_PH_QBRA   = 0x69,
    NM_REPLV_PH         = 0x01,
    NM_REPLV_QB         = 0x09,
    NM_BITREV           = 0x18,
    NM_INSV             = 0x20,
    NM_RADDU_W_QB       = 0x78,

    NM_BITSWAP          = 0x05,
    NM_WSBH             = 0x3d,
};

/* PP.SR instruction pool */
enum {
    NM_SAVE         = 0x00,
    NM_RESTORE      = 0x02,
    NM_RESTORE_JRC  = 0x03,
};

/* P.SR.F instruction pool */
enum {
    NM_SAVEF        = 0x00,
    NM_RESTOREF     = 0x01,
};

/* P16.SYSCALL  instruction pool */
enum {
    NM_SYSCALL16     = 0x00,
    NM_HYPCALL16     = 0x01,
};

/* POOL16C_00 instruction pool */
enum {
    NM_NOT16           = 0x00,
    NM_XOR16           = 0x01,
    NM_AND16           = 0x02,
    NM_OR16            = 0x03,
};

/* PP.LSX and PP.LSXS instruction pool */
enum {
    NM_LBX      = 0x00,
    NM_LHX      = 0x04,
    NM_LWX      = 0x08,
    NM_LDX      = 0x0c,

    NM_SBX      = 0x01,
    NM_SHX      = 0x05,
    NM_SWX      = 0x09,
    NM_SDX      = 0x0d,

    NM_LBUX     = 0x02,
    NM_LHUX     = 0x06,
    NM_LWC1X    = 0x0a,
    NM_LDC1X    = 0x0e,

    NM_LWUX     = 0x07,
    NM_SWC1X    = 0x0b,
    NM_SDC1X    = 0x0f,

    NM_LHXS     = 0x04,
    NM_LWXS     = 0x08,
    NM_LDXS     = 0x0c,

    NM_SHXS     = 0x05,
    NM_SWXS     = 0x09,
    NM_SDXS     = 0x0d,

    NM_LHUXS    = 0x06,
    NM_LWC1XS   = 0x0a,
    NM_LDC1XS   = 0x0e,

    NM_LWUXS    = 0x07,
    NM_SWC1XS   = 0x0b,
    NM_SDC1XS   = 0x0f,
};

/* ERETx instruction pool */
enum {
    NM_ERET     = 0x00,
    NM_ERETNC   = 0x01,
};

/* POOL32FxF_{0, 1} insturction pool */
enum {
    NM_CFC1     = 0x40,
    NM_CTC1     = 0x60,
    NM_MFC1     = 0x80,
    NM_MTC1     = 0xa0,
    NM_MFHC1    = 0xc0,
    NM_MTHC1    = 0xe0,

    NM_CVT_S_PL = 0x84,
    NM_CVT_S_PU = 0xa4,

    NM_CVT_L_S     = 0x004,
    NM_CVT_L_D     = 0x104,
    NM_CVT_W_S     = 0x024,
    NM_CVT_W_D     = 0x124,

    NM_RSQRT_S     = 0x008,
    NM_RSQRT_D     = 0x108,

    NM_SQRT_S      = 0x028,
    NM_SQRT_D      = 0x128,

    NM_RECIP_S     = 0x048,
    NM_RECIP_D     = 0x148,

    NM_FLOOR_L_S   = 0x00c,
    NM_FLOOR_L_D   = 0x10c,

    NM_FLOOR_W_S   = 0x02c,
    NM_FLOOR_W_D   = 0x12c,

    NM_CEIL_L_S    = 0x04c,
    NM_CEIL_L_D    = 0x14c,
    NM_CEIL_W_S    = 0x06c,
    NM_CEIL_W_D    = 0x16c,
    NM_TRUNC_L_S   = 0x08c,
    NM_TRUNC_L_D   = 0x18c,
    NM_TRUNC_W_S   = 0x0ac,
    NM_TRUNC_W_D   = 0x1ac,
    NM_ROUND_L_S   = 0x0cc,
    NM_ROUND_L_D   = 0x1cc,
    NM_ROUND_W_S   = 0x0ec,
    NM_ROUND_W_D   = 0x1ec,

    NM_MOV_S       = 0x01,
    NM_MOV_D       = 0x81,
    NM_ABS_S       = 0x0d,
    NM_ABS_D       = 0x8d,
    NM_NEG_S       = 0x2d,
    NM_NEG_D       = 0xad,
    NM_CVT_D_S     = 0x04d,
    NM_CVT_D_W     = 0x0cd,
    NM_CVT_D_L     = 0x14d,
    NM_CVT_S_D     = 0x06d,
    NM_CVT_S_W     = 0x0ed,
    NM_CVT_S_L     = 0x16d,
};

/* P.LL instruction pool */
enum {
    NM_LL       = 0x00,
    NM_LLWP     = 0x01,
};

/* P.SC instruction pool */
enum {
    NM_SC       = 0x00,
    NM_SCWP     = 0x01,
};

/* P.DVP instruction pool */
enum {
    NM_DVP      = 0x00,
    NM_EVP      = 0x01,
};


/*
 *
 * nanoMIPS decoding engine
 *
 */


/* extraction utilities */

#define NANOMIPS_EXTRACT_RT3(op) ((op >> 7) & 0x7)
#define NANOMIPS_EXTRACT_RS3(op) ((op >> 4) & 0x7)
#define NANOMIPS_EXTRACT_RD3(op) ((op >> 1) & 0x7)
#define NANOMIPS_EXTRACT_RD5(op) ((op >> 5) & 0x1f)
#define NANOMIPS_EXTRACT_RS5(op) (op & 0x1f)

/* Implement nanoMIPS pseudocode decode_gpr(encoded_gpr, 'gpr3'). */
static inline int decode_gpr_gpr3(int r)
{
    static const int map[] = { 16, 17, 18, 19,  4,  5,  6,  7 };

    return map[r & 0x7];
}

/* Implement nanoMIPS pseudocode decode_gpr(encoded_gpr, 'gpr3.src.store'). */
static inline int decode_gpr_gpr3_src_store(int r)
{
    static const int map[] = {  0, 17, 18, 19,  4,  5,  6,  7 };

    return map[r & 0x7];
}

/* Implement nanoMIPS pseudocode decode_gpr(encoded_gpr, 'gpr4'). */
static inline int decode_gpr_gpr4(int r)
{
    static const int map[] = {  8,  9, 10, 11,  4,  5,  6,  7,
                               16, 17, 18, 19, 20, 21, 22, 23 };

    return map[r & 0xf];
}

/* Implement nanoMIPS pseudocode decode_gpr(encoded_gpr, 'gpr4.zero'). */
static inline int decode_gpr_gpr4_zero(int r)
{
    static const int map[] = {  8,  9, 10,  0,  4,  5,  6,  7,
                               16, 17, 18, 19, 20, 21, 22, 23 };

    return map[r & 0xf];
}

static void gen_ext(DisasContext *ctx, int wordsz, int rd, int rs, int rt,
                    int shift)
{
    gen_align_bits(ctx, wordsz, rd, rs, rt, wordsz - shift);
}

static void gen_llwp(DisasContext *ctx, uint32_t base, int16_t offset,
                    uint32_t reg1, uint32_t reg2)
{
    TCGv taddr = tcg_temp_new();
    TCGv_i64 tval = tcg_temp_new_i64();
    TCGv tmp1 = tcg_temp_new();
    TCGv tmp2 = tcg_temp_new();

    gen_base_offset_addr(ctx, taddr, base, offset);
    tcg_gen_qemu_ld64(tval, taddr, ctx->mem_idx);
    if (cpu_is_bigendian(ctx)) {
        tcg_gen_extr_i64_tl(tmp2, tmp1, tval);
    } else {
        tcg_gen_extr_i64_tl(tmp1, tmp2, tval);
    }
    gen_store_gpr(tmp1, reg1);
    tcg_temp_free(tmp1);
    gen_store_gpr(tmp2, reg2);
    tcg_temp_free(tmp2);
    tcg_gen_st_i64(tval, cpu_env, offsetof(CPUMIPSState, llval_wp));
    tcg_temp_free_i64(tval);
    tcg_gen_st_tl(taddr, cpu_env, offsetof(CPUMIPSState, lladdr));
    tcg_temp_free(taddr);
}

static void gen_scwp(DisasContext *ctx, uint32_t base, int16_t offset,
                    uint32_t reg1, uint32_t reg2, bool eva)
{
    TCGv taddr = tcg_temp_local_new();
    TCGv lladdr = tcg_temp_local_new();
    TCGv_i64 tval = tcg_temp_new_i64();
    TCGv_i64 llval = tcg_temp_new_i64();
    TCGv_i64 val = tcg_temp_new_i64();
    TCGv tmp1 = tcg_temp_new();
    TCGv tmp2 = tcg_temp_new();
    TCGLabel *lab_fail = gen_new_label();
    TCGLabel *lab_done = gen_new_label();

    gen_base_offset_addr(ctx, taddr, base, offset);

    tcg_gen_ld_tl(lladdr, cpu_env, offsetof(CPUMIPSState, lladdr));
    tcg_gen_brcond_tl(TCG_COND_NE, taddr, lladdr, lab_fail);

    gen_load_gpr(tmp1, reg1);
    gen_load_gpr(tmp2, reg2);

    if (cpu_is_bigendian(ctx)) {
        tcg_gen_concat_tl_i64(tval, tmp2, tmp1);
    } else {
        tcg_gen_concat_tl_i64(tval, tmp1, tmp2);
    }

    tcg_gen_ld_i64(llval, cpu_env, offsetof(CPUMIPSState, llval_wp));
    tcg_gen_atomic_cmpxchg_i64(val, taddr, llval, tval,
                               eva ? MIPS_HFLAG_UM : ctx->mem_idx, MO_64);
    if (reg1 != 0) {
        tcg_gen_movi_tl(cpu_gpr[reg1], 1);
    }
    tcg_gen_brcond_i64(TCG_COND_EQ, val, llval, lab_done);

    gen_set_label(lab_fail);

    if (reg1 != 0) {
        tcg_gen_movi_tl(cpu_gpr[reg1], 0);
    }
    gen_set_label(lab_done);
    tcg_gen_movi_tl(lladdr, -1);
    tcg_gen_st_tl(lladdr, cpu_env, offsetof(CPUMIPSState, lladdr));
}

static void gen_adjust_sp(DisasContext *ctx, int u)
{
    gen_op_addr_addi(ctx, cpu_gpr[29], cpu_gpr[29], u);
}

static void gen_save(DisasContext *ctx, uint8_t rt, uint8_t count,
                     uint8_t gp, uint16_t u)
{
    int counter = 0;
    TCGv va = tcg_temp_new();
    TCGv t0 = tcg_temp_new();

    while (counter != count) {
        bool use_gp = gp && (counter == count - 1);
        int this_rt = use_gp ? 28 : (rt & 0x10) | ((rt + counter) & 0x1f);
        int this_offset = -((counter + 1) << 2);
        gen_base_offset_addr(ctx, va, 29, this_offset);
        gen_load_gpr(t0, this_rt);
        tcg_gen_qemu_st_tl(t0, va, ctx->mem_idx,
                           (MO_TEUL | ctx->default_tcg_memop_mask));
        counter++;
    }

    /* adjust stack pointer */
    gen_adjust_sp(ctx, -u);

    tcg_temp_free(t0);
    tcg_temp_free(va);
}

static void gen_restore(DisasContext *ctx, uint8_t rt, uint8_t count,
                        uint8_t gp, uint16_t u)
{
    int counter = 0;
    TCGv va = tcg_temp_new();
    TCGv t0 = tcg_temp_new();

    while (counter != count) {
        bool use_gp = gp && (counter == count - 1);
        int this_rt = use_gp ? 28 : (rt & 0x10) | ((rt + counter) & 0x1f);
        int this_offset = u - ((counter + 1) << 2);
        gen_base_offset_addr(ctx, va, 29, this_offset);
        tcg_gen_qemu_ld_tl(t0, va, ctx->mem_idx, MO_TESL |
                        ctx->default_tcg_memop_mask);
        tcg_gen_ext32s_tl(t0, t0);
        gen_store_gpr(t0, this_rt);
        counter++;
    }

    /* adjust stack pointer */
    gen_adjust_sp(ctx, u);

    tcg_temp_free(t0);
    tcg_temp_free(va);
}

static void gen_compute_branch_nm(DisasContext *ctx, uint32_t opc,
                                  int insn_bytes,
                                  int rs, int rt, int32_t offset)
{
    target_ulong btgt = -1;
    int bcond_compute = 0;
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();

    /* Load needed operands */
    switch (opc) {
    case OPC_BEQ:
    case OPC_BNE:
        /* Compare two registers */
        if (rs != rt) {
            gen_load_gpr(t0, rs);
            gen_load_gpr(t1, rt);
            bcond_compute = 1;
        }
        btgt = ctx->base.pc_next + insn_bytes + offset;
        break;
    case OPC_BGEZAL:
        /* Compare to zero */
        if (rs != 0) {
            gen_load_gpr(t0, rs);
            bcond_compute = 1;
        }
        btgt = ctx->base.pc_next + insn_bytes + offset;
        break;
    case OPC_BPOSGE32:
        tcg_gen_andi_tl(t0, cpu_dspctrl, 0x3F);
        bcond_compute = 1;
        btgt = ctx->base.pc_next + insn_bytes + offset;
        break;
    case OPC_JR:
    case OPC_JALR:
        /* Jump to register */
        if (offset != 0 && offset != 16) {
            /*
             * Hint = 0 is JR/JALR, hint 16 is JR.HB/JALR.HB, the
             * others are reserved.
             */
            MIPS_INVAL("jump hint");
            gen_reserved_instruction(ctx);
            goto out;
        }
        gen_load_gpr(btarget, rs);
        break;
    default:
        MIPS_INVAL("branch/jump");
        gen_reserved_instruction(ctx);
        goto out;
    }
    if (bcond_compute == 0) {
        /* No condition to be computed */
        switch (opc) {
        case OPC_BEQ:     /* rx == rx        */
            /* Always take */
            ctx->hflags |= MIPS_HFLAG_B;
            break;
        case OPC_BGEZAL:  /* 0 >= 0          */
            /* Always take and link */
            tcg_gen_movi_tl(cpu_gpr[31],
                            ctx->base.pc_next + insn_bytes);
            ctx->hflags |= MIPS_HFLAG_B;
            break;
        case OPC_BNE:     /* rx != rx        */
            tcg_gen_movi_tl(cpu_gpr[31], ctx->base.pc_next + 8);
            /* Skip the instruction in the delay slot */
            ctx->base.pc_next += 4;
            goto out;
        case OPC_JR:
            ctx->hflags |= MIPS_HFLAG_BR;
            break;
        case OPC_JALR:
            if (rt > 0) {
                tcg_gen_movi_tl(cpu_gpr[rt],
                                ctx->base.pc_next + insn_bytes);
            }
            ctx->hflags |= MIPS_HFLAG_BR;
            break;
        default:
            MIPS_INVAL("branch/jump");
            gen_reserved_instruction(ctx);
            goto out;
        }
    } else {
        switch (opc) {
        case OPC_BEQ:
            tcg_gen_setcond_tl(TCG_COND_EQ, bcond, t0, t1);
            goto not_likely;
        case OPC_BNE:
            tcg_gen_setcond_tl(TCG_COND_NE, bcond, t0, t1);
            goto not_likely;
        case OPC_BGEZAL:
            tcg_gen_setcondi_tl(TCG_COND_GE, bcond, t0, 0);
            tcg_gen_movi_tl(cpu_gpr[31],
                            ctx->base.pc_next + insn_bytes);
            goto not_likely;
        case OPC_BPOSGE32:
            tcg_gen_setcondi_tl(TCG_COND_GE, bcond, t0, 32);
        not_likely:
            ctx->hflags |= MIPS_HFLAG_BC;
            break;
        default:
            MIPS_INVAL("conditional branch/jump");
            gen_reserved_instruction(ctx);
            goto out;
        }
    }

    ctx->btarget = btgt;

 out:
    if (insn_bytes == 2) {
        ctx->hflags |= MIPS_HFLAG_B16;
    }
    tcg_temp_free(t0);
    tcg_temp_free(t1);
}

static void gen_pool16c_nanomips_insn(DisasContext *ctx)
{
    int rt = decode_gpr_gpr3(NANOMIPS_EXTRACT_RT3(ctx->opcode));
    int rs = decode_gpr_gpr3(NANOMIPS_EXTRACT_RS3(ctx->opcode));

    switch (extract32(ctx->opcode, 2, 2)) {
    case NM_NOT16:
        gen_logic(ctx, OPC_NOR, rt, rs, 0);
        break;
    case NM_AND16:
        gen_logic(ctx, OPC_AND, rt, rt, rs);
        break;
    case NM_XOR16:
        gen_logic(ctx, OPC_XOR, rt, rt, rs);
        break;
    case NM_OR16:
        gen_logic(ctx, OPC_OR, rt, rt, rs);
        break;
    }
}

static void gen_pool32a0_nanomips_insn(CPUMIPSState *env, DisasContext *ctx)
{
    int rt = extract32(ctx->opcode, 21, 5);
    int rs = extract32(ctx->opcode, 16, 5);
    int rd = extract32(ctx->opcode, 11, 5);

    switch (extract32(ctx->opcode, 3, 7)) {
    case NM_P_TRAP:
        switch (extract32(ctx->opcode, 10, 1)) {
        case NM_TEQ:
            check_nms(ctx);
            gen_trap(ctx, OPC_TEQ, rs, rt, -1);
            break;
        case NM_TNE:
            check_nms(ctx);
            gen_trap(ctx, OPC_TNE, rs, rt, -1);
            break;
        }
        break;
    case NM_RDHWR:
        check_nms(ctx);
        gen_rdhwr(ctx, rt, rs, extract32(ctx->opcode, 11, 3));
        break;
    case NM_SEB:
        check_nms(ctx);
        gen_bshfl(ctx, OPC_SEB, rs, rt);
        break;
    case NM_SEH:
        gen_bshfl(ctx, OPC_SEH, rs, rt);
        break;
    case NM_SLLV:
        gen_shift(ctx, OPC_SLLV, rd, rt, rs);
        break;
    case NM_SRLV:
        gen_shift(ctx, OPC_SRLV, rd, rt, rs);
        break;
    case NM_SRAV:
        gen_shift(ctx, OPC_SRAV, rd, rt, rs);
        break;
    case NM_ROTRV:
        gen_shift(ctx, OPC_ROTRV, rd, rt, rs);
        break;
    case NM_ADD:
        gen_arith(ctx, OPC_ADD, rd, rs, rt);
        break;
    case NM_ADDU:
        gen_arith(ctx, OPC_ADDU, rd, rs, rt);
        break;
    case NM_SUB:
        check_nms(ctx);
        gen_arith(ctx, OPC_SUB, rd, rs, rt);
        break;
    case NM_SUBU:
        gen_arith(ctx, OPC_SUBU, rd, rs, rt);
        break;
    case NM_P_CMOVE:
        switch (extract32(ctx->opcode, 10, 1)) {
        case NM_MOVZ:
            gen_cond_move(ctx, OPC_MOVZ, rd, rs, rt);
            break;
        case NM_MOVN:
            gen_cond_move(ctx, OPC_MOVN, rd, rs, rt);
            break;
        }
        break;
    case NM_AND:
        gen_logic(ctx, OPC_AND, rd, rs, rt);
        break;
    case NM_OR:
        gen_logic(ctx, OPC_OR, rd, rs, rt);
        break;
    case NM_NOR:
        gen_logic(ctx, OPC_NOR, rd, rs, rt);
        break;
    case NM_XOR:
        gen_logic(ctx, OPC_XOR, rd, rs, rt);
        break;
    case NM_SLT:
        gen_slt(ctx, OPC_SLT, rd, rs, rt);
        break;
    case NM_P_SLTU:
        if (rd == 0) {
            /* P_DVP */
#ifndef CONFIG_USER_ONLY
            TCGv t0 = tcg_temp_new();
            switch (extract32(ctx->opcode, 10, 1)) {
            case NM_DVP:
                if (ctx->vp) {
                    check_cp0_enabled(ctx);
                    gen_helper_dvp(t0, cpu_env);
                    gen_store_gpr(t0, rt);
                }
                break;
            case NM_EVP:
                if (ctx->vp) {
                    check_cp0_enabled(ctx);
                    gen_helper_evp(t0, cpu_env);
                    gen_store_gpr(t0, rt);
                }
                break;
            }
            tcg_temp_free(t0);
#endif
        } else {
            gen_slt(ctx, OPC_SLTU, rd, rs, rt);
        }
        break;
    case NM_SOV:
        {
            TCGv t0 = tcg_temp_new();
            TCGv t1 = tcg_temp_new();
            TCGv t2 = tcg_temp_new();

            gen_load_gpr(t1, rs);
            gen_load_gpr(t2, rt);
            tcg_gen_add_tl(t0, t1, t2);
            tcg_gen_ext32s_tl(t0, t0);
            tcg_gen_xor_tl(t1, t1, t2);
            tcg_gen_xor_tl(t2, t0, t2);
            tcg_gen_andc_tl(t1, t2, t1);

            /* operands of same sign, result different sign */
            tcg_gen_setcondi_tl(TCG_COND_LT, t0, t1, 0);
            gen_store_gpr(t0, rd);

            tcg_temp_free(t0);
            tcg_temp_free(t1);
            tcg_temp_free(t2);
        }
        break;
    case NM_MUL:
        gen_r6_muldiv(ctx, R6_OPC_MUL, rd, rs, rt);
        break;
    case NM_MUH:
        gen_r6_muldiv(ctx, R6_OPC_MUH, rd, rs, rt);
        break;
    case NM_MULU:
        gen_r6_muldiv(ctx, R6_OPC_MULU, rd, rs, rt);
        break;
    case NM_MUHU:
        gen_r6_muldiv(ctx, R6_OPC_MUHU, rd, rs, rt);
        break;
    case NM_DIV:
        gen_r6_muldiv(ctx, R6_OPC_DIV, rd, rs, rt);
        break;
    case NM_MOD:
        gen_r6_muldiv(ctx, R6_OPC_MOD, rd, rs, rt);
        break;
    case NM_DIVU:
        gen_r6_muldiv(ctx, R6_OPC_DIVU, rd, rs, rt);
        break;
    case NM_MODU:
        gen_r6_muldiv(ctx, R6_OPC_MODU, rd, rs, rt);
        break;
#ifndef CONFIG_USER_ONLY
    case NM_MFC0:
        check_cp0_enabled(ctx);
        if (rt == 0) {
            /* Treat as NOP. */
            break;
        }
        gen_mfc0(ctx, cpu_gpr[rt], rs, extract32(ctx->opcode, 11, 3));
        break;
    case NM_MTC0:
        check_cp0_enabled(ctx);
        {
            TCGv t0 = tcg_temp_new();

            gen_load_gpr(t0, rt);
            gen_mtc0(ctx, t0, rs, extract32(ctx->opcode, 11, 3));
            tcg_temp_free(t0);
        }
        break;
    case NM_D_E_MT_VPE:
        {
            uint8_t sc = extract32(ctx->opcode, 10, 1);
            TCGv t0 = tcg_temp_new();

            switch (sc) {
            case 0:
                if (rs == 1) {
                    /* DMT */
                    check_cp0_mt(ctx);
                    gen_helper_dmt(t0);
                    gen_store_gpr(t0, rt);
                } else if (rs == 0) {
                    /* DVPE */
                    check_cp0_mt(ctx);
                    gen_helper_dvpe(t0, cpu_env);
                    gen_store_gpr(t0, rt);
                } else {
                    gen_reserved_instruction(ctx);
                }
                break;
            case 1:
                if (rs == 1) {
                    /* EMT */
                    check_cp0_mt(ctx);
                    gen_helper_emt(t0);
                    gen_store_gpr(t0, rt);
                } else if (rs == 0) {
                    /* EVPE */
                    check_cp0_mt(ctx);
                    gen_helper_evpe(t0, cpu_env);
                    gen_store_gpr(t0, rt);
                } else {
                    gen_reserved_instruction(ctx);
                }
                break;
            }

            tcg_temp_free(t0);
        }
        break;
    case NM_FORK:
        check_mt(ctx);
        {
            TCGv t0 = tcg_temp_new();
            TCGv t1 = tcg_temp_new();

            gen_load_gpr(t0, rt);
            gen_load_gpr(t1, rs);
            gen_helper_fork(t0, t1);
            tcg_temp_free(t0);
            tcg_temp_free(t1);
        }
        break;
    case NM_MFTR:
    case NM_MFHTR:
        check_cp0_enabled(ctx);
        if (rd == 0) {
            /* Treat as NOP. */
            return;
        }
        gen_mftr(env, ctx, rs, rt, extract32(ctx->opcode, 10, 1),
                 extract32(ctx->opcode, 11, 5), extract32(ctx->opcode, 3, 1));
        break;
    case NM_MTTR:
    case NM_MTHTR:
        check_cp0_enabled(ctx);
        gen_mttr(env, ctx, rs, rt, extract32(ctx->opcode, 10, 1),
                 extract32(ctx->opcode, 11, 5), extract32(ctx->opcode, 3, 1));
        break;
    case NM_YIELD:
        check_mt(ctx);
        {
            TCGv t0 = tcg_temp_new();

            gen_load_gpr(t0, rs);
            gen_helper_yield(t0, cpu_env, t0);
            gen_store_gpr(t0, rt);
            tcg_temp_free(t0);
        }
        break;
#endif
    default:
        gen_reserved_instruction(ctx);
        break;
    }
}

/* dsp */
static void gen_pool32axf_1_5_nanomips_insn(DisasContext *ctx, uint32_t opc,
                                            int ret, int v1, int v2)
{
    TCGv_i32 t0;
    TCGv v0_t;
    TCGv v1_t;

    t0 = tcg_temp_new_i32();

    v0_t = tcg_temp_new();
    v1_t = tcg_temp_new();

    tcg_gen_movi_i32(t0, v2 >> 3);

    gen_load_gpr(v0_t, ret);
    gen_load_gpr(v1_t, v1);

    switch (opc) {
    case NM_MAQ_S_W_PHR:
        check_dsp(ctx);
        gen_helper_maq_s_w_phr(t0, v1_t, v0_t, cpu_env);
        break;
    case NM_MAQ_S_W_PHL:
        check_dsp(ctx);
        gen_helper_maq_s_w_phl(t0, v1_t, v0_t, cpu_env);
        break;
    case NM_MAQ_SA_W_PHR:
        check_dsp(ctx);
        gen_helper_maq_sa_w_phr(t0, v1_t, v0_t, cpu_env);
        break;
    case NM_MAQ_SA_W_PHL:
        check_dsp(ctx);
        gen_helper_maq_sa_w_phl(t0, v1_t, v0_t, cpu_env);
        break;
    default:
        gen_reserved_instruction(ctx);
        break;
    }

    tcg_temp_free_i32(t0);

    tcg_temp_free(v0_t);
    tcg_temp_free(v1_t);
}


static void gen_pool32axf_1_nanomips_insn(DisasContext *ctx, uint32_t opc,
                                    int ret, int v1, int v2)
{
    int16_t imm;
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    TCGv v0_t = tcg_temp_new();

    gen_load_gpr(v0_t, v1);

    switch (opc) {
    case NM_POOL32AXF_1_0:
        check_dsp(ctx);
        switch (extract32(ctx->opcode, 12, 2)) {
        case NM_MFHI:
            gen_HILO(ctx, OPC_MFHI, v2 >> 3, ret);
            break;
        case NM_MFLO:
            gen_HILO(ctx, OPC_MFLO, v2 >> 3, ret);
            break;
        case NM_MTHI:
            gen_HILO(ctx, OPC_MTHI, v2 >> 3, v1);
            break;
        case NM_MTLO:
            gen_HILO(ctx, OPC_MTLO, v2 >> 3, v1);
            break;
        }
        break;
    case NM_POOL32AXF_1_1:
        check_dsp(ctx);
        switch (extract32(ctx->opcode, 12, 2)) {
        case NM_MTHLIP:
            tcg_gen_movi_tl(t0, v2);
            gen_helper_mthlip(t0, v0_t, cpu_env);
            break;
        case NM_SHILOV:
            tcg_gen_movi_tl(t0, v2 >> 3);
            gen_helper_shilo(t0, v0_t, cpu_env);
            break;
        default:
            gen_reserved_instruction(ctx);
            break;
        }
        break;
    case NM_POOL32AXF_1_3:
        check_dsp(ctx);
        imm = extract32(ctx->opcode, 14, 7);
        switch (extract32(ctx->opcode, 12, 2)) {
        case NM_RDDSP:
            tcg_gen_movi_tl(t0, imm);
            gen_helper_rddsp(t0, t0, cpu_env);
            gen_store_gpr(t0, ret);
            break;
        case NM_WRDSP:
            gen_load_gpr(t0, ret);
            tcg_gen_movi_tl(t1, imm);
            gen_helper_wrdsp(t0, t1, cpu_env);
            break;
        case NM_EXTP:
            tcg_gen_movi_tl(t0, v2 >> 3);
            tcg_gen_movi_tl(t1, v1);
            gen_helper_extp(t0, t0, t1, cpu_env);
            gen_store_gpr(t0, ret);
            break;
        case NM_EXTPDP:
            tcg_gen_movi_tl(t0, v2 >> 3);
            tcg_gen_movi_tl(t1, v1);
            gen_helper_extpdp(t0, t0, t1, cpu_env);
            gen_store_gpr(t0, ret);
            break;
        }
        break;
    case NM_POOL32AXF_1_4:
        check_dsp(ctx);
        tcg_gen_movi_tl(t0, v2 >> 2);
        switch (extract32(ctx->opcode, 12, 1)) {
        case NM_SHLL_QB:
            gen_helper_shll_qb(t0, t0, v0_t, cpu_env);
            gen_store_gpr(t0, ret);
            break;
        case NM_SHRL_QB:
            gen_helper_shrl_qb(t0, t0, v0_t);
            gen_store_gpr(t0, ret);
            break;
        }
        break;
    case NM_POOL32AXF_1_5:
        opc = extract32(ctx->opcode, 12, 2);
        gen_pool32axf_1_5_nanomips_insn(ctx, opc, ret, v1, v2);
        break;
    case NM_POOL32AXF_1_7:
        check_dsp(ctx);
        tcg_gen_movi_tl(t0, v2 >> 3);
        tcg_gen_movi_tl(t1, v1);
        switch (extract32(ctx->opcode, 12, 2)) {
        case NM_EXTR_W:
            gen_helper_extr_w(t0, t0, t1, cpu_env);
            gen_store_gpr(t0, ret);
            break;
        case NM_EXTR_R_W:
            gen_helper_extr_r_w(t0, t0, t1, cpu_env);
            gen_store_gpr(t0, ret);
            break;
        case NM_EXTR_RS_W:
            gen_helper_extr_rs_w(t0, t0, t1, cpu_env);
            gen_store_gpr(t0, ret);
            break;
        case NM_EXTR_S_H:
            gen_helper_extr_s_h(t0, t0, t1, cpu_env);
            gen_store_gpr(t0, ret);
            break;
        }
        break;
    default:
        gen_reserved_instruction(ctx);
        break;
    }

    tcg_temp_free(t0);
    tcg_temp_free(t1);
    tcg_temp_free(v0_t);
}

static void gen_pool32axf_2_multiply(DisasContext *ctx, uint32_t opc,
                                    TCGv v0, TCGv v1, int rd)
{
    TCGv_i32 t0;

    t0 = tcg_temp_new_i32();

    tcg_gen_movi_i32(t0, rd >> 3);

    switch (opc) {
    case NM_POOL32AXF_2_0_7:
        switch (extract32(ctx->opcode, 9, 3)) {
        case NM_DPA_W_PH:
            check_dsp_r2(ctx);
            gen_helper_dpa_w_ph(t0, v1, v0, cpu_env);
            break;
        case NM_DPAQ_S_W_PH:
            check_dsp(ctx);
            gen_helper_dpaq_s_w_ph(t0, v1, v0, cpu_env);
            break;
        case NM_DPS_W_PH:
            check_dsp_r2(ctx);
            gen_helper_dps_w_ph(t0, v1, v0, cpu_env);
            break;
        case NM_DPSQ_S_W_PH:
            check_dsp(ctx);
            gen_helper_dpsq_s_w_ph(t0, v1, v0, cpu_env);
            break;
        default:
            gen_reserved_instruction(ctx);
            break;
        }
        break;
    case NM_POOL32AXF_2_8_15:
        switch (extract32(ctx->opcode, 9, 3)) {
        case NM_DPAX_W_PH:
            check_dsp_r2(ctx);
            gen_helper_dpax_w_ph(t0, v0, v1, cpu_env);
            break;
        case NM_DPAQ_SA_L_W:
            check_dsp(ctx);
            gen_helper_dpaq_sa_l_w(t0, v0, v1, cpu_env);
            break;
        case NM_DPSX_W_PH:
            check_dsp_r2(ctx);
            gen_helper_dpsx_w_ph(t0, v0, v1, cpu_env);
            break;
        case NM_DPSQ_SA_L_W:
            check_dsp(ctx);
            gen_helper_dpsq_sa_l_w(t0, v0, v1, cpu_env);
            break;
        default:
            gen_reserved_instruction(ctx);
            break;
        }
        break;
    case NM_POOL32AXF_2_16_23:
        switch (extract32(ctx->opcode, 9, 3)) {
        case NM_DPAU_H_QBL:
            check_dsp(ctx);
            gen_helper_dpau_h_qbl(t0, v0, v1, cpu_env);
            break;
        case NM_DPAQX_S_W_PH:
            check_dsp_r2(ctx);
            gen_helper_dpaqx_s_w_ph(t0, v0, v1, cpu_env);
            break;
        case NM_DPSU_H_QBL:
            check_dsp(ctx);
            gen_helper_dpsu_h_qbl(t0, v0, v1, cpu_env);
            break;
        case NM_DPSQX_S_W_PH:
            check_dsp_r2(ctx);
            gen_helper_dpsqx_s_w_ph(t0, v0, v1, cpu_env);
            break;
        case NM_MULSA_W_PH:
            check_dsp_r2(ctx);
            gen_helper_mulsa_w_ph(t0, v0, v1, cpu_env);
            break;
        default:
            gen_reserved_instruction(ctx);
            break;
        }
        break;
    case NM_POOL32AXF_2_24_31:
        switch (extract32(ctx->opcode, 9, 3)) {
        case NM_DPAU_H_QBR:
            check_dsp(ctx);
            gen_helper_dpau_h_qbr(t0, v1, v0, cpu_env);
            break;
        case NM_DPAQX_SA_W_PH:
            check_dsp_r2(ctx);
            gen_helper_dpaqx_sa_w_ph(t0, v1, v0, cpu_env);
            break;
        case NM_DPSU_H_QBR:
            check_dsp(ctx);
            gen_helper_dpsu_h_qbr(t0, v1, v0, cpu_env);
            break;
        case NM_DPSQX_SA_W_PH:
            check_dsp_r2(ctx);
            gen_helper_dpsqx_sa_w_ph(t0, v1, v0, cpu_env);
            break;
        case NM_MULSAQ_S_W_PH:
            check_dsp(ctx);
            gen_helper_mulsaq_s_w_ph(t0, v1, v0, cpu_env);
            break;
        default:
            gen_reserved_instruction(ctx);
            break;
        }
        break;
    default:
        gen_reserved_instruction(ctx);
        break;
    }

    tcg_temp_free_i32(t0);
}

static void gen_pool32axf_2_nanomips_insn(DisasContext *ctx, uint32_t opc,
                                          int rt, int rs, int rd)
{
    int ret = rt;
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    TCGv v0_t = tcg_temp_new();
    TCGv v1_t = tcg_temp_new();

    gen_load_gpr(v0_t, rt);
    gen_load_gpr(v1_t, rs);

    switch (opc) {
    case NM_POOL32AXF_2_0_7:
        switch (extract32(ctx->opcode, 9, 3)) {
        case NM_DPA_W_PH:
        case NM_DPAQ_S_W_PH:
        case NM_DPS_W_PH:
        case NM_DPSQ_S_W_PH:
            gen_pool32axf_2_multiply(ctx, opc, v0_t, v1_t, rd);
            break;
        case NM_BALIGN:
            check_dsp_r2(ctx);
            if (rt != 0) {
                gen_load_gpr(t0, rs);
                rd &= 3;
                if (rd != 0 && rd != 2) {
                    tcg_gen_shli_tl(cpu_gpr[ret], cpu_gpr[ret], 8 * rd);
                    tcg_gen_ext32u_tl(t0, t0);
                    tcg_gen_shri_tl(t0, t0, 8 * (4 - rd));
                    tcg_gen_or_tl(cpu_gpr[ret], cpu_gpr[ret], t0);
                }
                tcg_gen_ext32s_tl(cpu_gpr[ret], cpu_gpr[ret]);
            }
            break;
        case NM_MADD:
            check_dsp(ctx);
            {
                int acc = extract32(ctx->opcode, 14, 2);
                TCGv_i64 t2 = tcg_temp_new_i64();
                TCGv_i64 t3 = tcg_temp_new_i64();

                gen_load_gpr(t0, rt);
                gen_load_gpr(t1, rs);
                tcg_gen_ext_tl_i64(t2, t0);
                tcg_gen_ext_tl_i64(t3, t1);
                tcg_gen_mul_i64(t2, t2, t3);
                tcg_gen_concat_tl_i64(t3, cpu_LO[acc], cpu_HI[acc]);
                tcg_gen_add_i64(t2, t2, t3);
                tcg_temp_free_i64(t3);
                gen_move_low32(cpu_LO[acc], t2);
                gen_move_high32(cpu_HI[acc], t2);
                tcg_temp_free_i64(t2);
            }
            break;
        case NM_MULT:
            check_dsp(ctx);
            {
                int acc = extract32(ctx->opcode, 14, 2);
                TCGv_i32 t2 = tcg_temp_new_i32();
                TCGv_i32 t3 = tcg_temp_new_i32();

                gen_load_gpr(t0, rs);
                gen_load_gpr(t1, rt);
                tcg_gen_trunc_tl_i32(t2, t0);
                tcg_gen_trunc_tl_i32(t3, t1);
                tcg_gen_muls2_i32(t2, t3, t2, t3);
                tcg_gen_ext_i32_tl(cpu_LO[acc], t2);
                tcg_gen_ext_i32_tl(cpu_HI[acc], t3);
                tcg_temp_free_i32(t2);
                tcg_temp_free_i32(t3);
            }
            break;
        case NM_EXTRV_W:
            check_dsp(ctx);
            gen_load_gpr(v1_t, rs);
            tcg_gen_movi_tl(t0, rd >> 3);
            gen_helper_extr_w(t0, t0, v1_t, cpu_env);
            gen_store_gpr(t0, ret);
            break;
        }
        break;
    case NM_POOL32AXF_2_8_15:
        switch (extract32(ctx->opcode, 9, 3)) {
        case NM_DPAX_W_PH:
        case NM_DPAQ_SA_L_W:
        case NM_DPSX_W_PH:
        case NM_DPSQ_SA_L_W:
            gen_pool32axf_2_multiply(ctx, opc, v0_t, v1_t, rd);
            break;
        case NM_MADDU:
            check_dsp(ctx);
            {
                int acc = extract32(ctx->opcode, 14, 2);
                TCGv_i64 t2 = tcg_temp_new_i64();
                TCGv_i64 t3 = tcg_temp_new_i64();

                gen_load_gpr(t0, rs);
                gen_load_gpr(t1, rt);
                tcg_gen_ext32u_tl(t0, t0);
                tcg_gen_ext32u_tl(t1, t1);
                tcg_gen_extu_tl_i64(t2, t0);
                tcg_gen_extu_tl_i64(t3, t1);
                tcg_gen_mul_i64(t2, t2, t3);
                tcg_gen_concat_tl_i64(t3, cpu_LO[acc], cpu_HI[acc]);
                tcg_gen_add_i64(t2, t2, t3);
                tcg_temp_free_i64(t3);
                gen_move_low32(cpu_LO[acc], t2);
                gen_move_high32(cpu_HI[acc], t2);
                tcg_temp_free_i64(t2);
            }
            break;
        case NM_MULTU:
            check_dsp(ctx);
            {
                int acc = extract32(ctx->opcode, 14, 2);
                TCGv_i32 t2 = tcg_temp_new_i32();
                TCGv_i32 t3 = tcg_temp_new_i32();

                gen_load_gpr(t0, rs);
                gen_load_gpr(t1, rt);
                tcg_gen_trunc_tl_i32(t2, t0);
                tcg_gen_trunc_tl_i32(t3, t1);
                tcg_gen_mulu2_i32(t2, t3, t2, t3);
                tcg_gen_ext_i32_tl(cpu_LO[acc], t2);
                tcg_gen_ext_i32_tl(cpu_HI[acc], t3);
                tcg_temp_free_i32(t2);
                tcg_temp_free_i32(t3);
            }
            break;
        case NM_EXTRV_R_W:
            check_dsp(ctx);
            tcg_gen_movi_tl(t0, rd >> 3);
            gen_helper_extr_r_w(t0, t0, v1_t, cpu_env);
            gen_store_gpr(t0, ret);
            break;
        default:
            gen_reserved_instruction(ctx);
            break;
        }
        break;
    case NM_POOL32AXF_2_16_23:
        switch (extract32(ctx->opcode, 9, 3)) {
        case NM_DPAU_H_QBL:
        case NM_DPAQX_S_W_PH:
        case NM_DPSU_H_QBL:
        case NM_DPSQX_S_W_PH:
        case NM_MULSA_W_PH:
            gen_pool32axf_2_multiply(ctx, opc, v0_t, v1_t, rd);
            break;
        case NM_EXTPV:
            check_dsp(ctx);
            tcg_gen_movi_tl(t0, rd >> 3);
            gen_helper_extp(t0, t0, v1_t, cpu_env);
            gen_store_gpr(t0, ret);
            break;
        case NM_MSUB:
            check_dsp(ctx);
            {
                int acc = extract32(ctx->opcode, 14, 2);
                TCGv_i64 t2 = tcg_temp_new_i64();
                TCGv_i64 t3 = tcg_temp_new_i64();

                gen_load_gpr(t0, rs);
                gen_load_gpr(t1, rt);
                tcg_gen_ext_tl_i64(t2, t0);
                tcg_gen_ext_tl_i64(t3, t1);
                tcg_gen_mul_i64(t2, t2, t3);
                tcg_gen_concat_tl_i64(t3, cpu_LO[acc], cpu_HI[acc]);
                tcg_gen_sub_i64(t2, t3, t2);
                tcg_temp_free_i64(t3);
                gen_move_low32(cpu_LO[acc], t2);
                gen_move_high32(cpu_HI[acc], t2);
                tcg_temp_free_i64(t2);
            }
            break;
        case NM_EXTRV_RS_W:
            check_dsp(ctx);
            tcg_gen_movi_tl(t0, rd >> 3);
            gen_helper_extr_rs_w(t0, t0, v1_t, cpu_env);
            gen_store_gpr(t0, ret);
            break;
        }
        break;
    case NM_POOL32AXF_2_24_31:
        switch (extract32(ctx->opcode, 9, 3)) {
        case NM_DPAU_H_QBR:
        case NM_DPAQX_SA_W_PH:
        case NM_DPSU_H_QBR:
        case NM_DPSQX_SA_W_PH:
        case NM_MULSAQ_S_W_PH:
            gen_pool32axf_2_multiply(ctx, opc, v0_t, v1_t, rd);
            break;
        case NM_EXTPDPV:
            check_dsp(ctx);
            tcg_gen_movi_tl(t0, rd >> 3);
            gen_helper_extpdp(t0, t0, v1_t, cpu_env);
            gen_store_gpr(t0, ret);
            break;
        case NM_MSUBU:
            check_dsp(ctx);
            {
                int acc = extract32(ctx->opcode, 14, 2);
                TCGv_i64 t2 = tcg_temp_new_i64();
                TCGv_i64 t3 = tcg_temp_new_i64();

                gen_load_gpr(t0, rs);
                gen_load_gpr(t1, rt);
                tcg_gen_ext32u_tl(t0, t0);
                tcg_gen_ext32u_tl(t1, t1);
                tcg_gen_extu_tl_i64(t2, t0);
                tcg_gen_extu_tl_i64(t3, t1);
                tcg_gen_mul_i64(t2, t2, t3);
                tcg_gen_concat_tl_i64(t3, cpu_LO[acc], cpu_HI[acc]);
                tcg_gen_sub_i64(t2, t3, t2);
                tcg_temp_free_i64(t3);
                gen_move_low32(cpu_LO[acc], t2);
                gen_move_high32(cpu_HI[acc], t2);
                tcg_temp_free_i64(t2);
            }
            break;
        case NM_EXTRV_S_H:
            check_dsp(ctx);
            tcg_gen_movi_tl(t0, rd >> 3);
            gen_helper_extr_s_h(t0, t0, v0_t, cpu_env);
            gen_store_gpr(t0, ret);
            break;
        }
        break;
    default:
        gen_reserved_instruction(ctx);
        break;
    }

    tcg_temp_free(t0);
    tcg_temp_free(t1);

    tcg_temp_free(v0_t);
    tcg_temp_free(v1_t);
}

static void gen_pool32axf_4_nanomips_insn(DisasContext *ctx, uint32_t opc,
                                          int rt, int rs)
{
    int ret = rt;
    TCGv t0 = tcg_temp_new();
    TCGv v0_t = tcg_temp_new();

    gen_load_gpr(v0_t, rs);

    switch (opc) {
    case NM_ABSQ_S_QB:
        check_dsp_r2(ctx);
        gen_helper_absq_s_qb(v0_t, v0_t, cpu_env);
        gen_store_gpr(v0_t, ret);
        break;
    case NM_ABSQ_S_PH:
        check_dsp(ctx);
        gen_helper_absq_s_ph(v0_t, v0_t, cpu_env);
        gen_store_gpr(v0_t, ret);
        break;
    case NM_ABSQ_S_W:
        check_dsp(ctx);
        gen_helper_absq_s_w(v0_t, v0_t, cpu_env);
        gen_store_gpr(v0_t, ret);
        break;
    case NM_PRECEQ_W_PHL:
        check_dsp(ctx);
        tcg_gen_andi_tl(v0_t, v0_t, 0xFFFF0000);
        tcg_gen_ext32s_tl(v0_t, v0_t);
        gen_store_gpr(v0_t, ret);
        break;
    case NM_PRECEQ_W_PHR:
        check_dsp(ctx);
        tcg_gen_andi_tl(v0_t, v0_t, 0x0000FFFF);
        tcg_gen_shli_tl(v0_t, v0_t, 16);
        tcg_gen_ext32s_tl(v0_t, v0_t);
        gen_store_gpr(v0_t, ret);
        break;
    case NM_PRECEQU_PH_QBL:
        check_dsp(ctx);
        gen_helper_precequ_ph_qbl(v0_t, v0_t);
        gen_store_gpr(v0_t, ret);
        break;
    case NM_PRECEQU_PH_QBR:
        check_dsp(ctx);
        gen_helper_precequ_ph_qbr(v0_t, v0_t);
        gen_store_gpr(v0_t, ret);
        break;
    case NM_PRECEQU_PH_QBLA:
        check_dsp(ctx);
        gen_helper_precequ_ph_qbla(v0_t, v0_t);
        gen_store_gpr(v0_t, ret);
        break;
    case NM_PRECEQU_PH_QBRA:
        check_dsp(ctx);
        gen_helper_precequ_ph_qbra(v0_t, v0_t);
        gen_store_gpr(v0_t, ret);
        break;
    case NM_PRECEU_PH_QBL:
        check_dsp(ctx);
        gen_helper_preceu_ph_qbl(v0_t, v0_t);
        gen_store_gpr(v0_t, ret);
        break;
    case NM_PRECEU_PH_QBR:
        check_dsp(ctx);
        gen_helper_preceu_ph_qbr(v0_t, v0_t);
        gen_store_gpr(v0_t, ret);
        break;
    case NM_PRECEU_PH_QBLA:
        check_dsp(ctx);
        gen_helper_preceu_ph_qbla(v0_t, v0_t);
        gen_store_gpr(v0_t, ret);
        break;
    case NM_PRECEU_PH_QBRA:
        check_dsp(ctx);
        gen_helper_preceu_ph_qbra(v0_t, v0_t);
        gen_store_gpr(v0_t, ret);
        break;
    case NM_REPLV_PH:
        check_dsp(ctx);
        tcg_gen_ext16u_tl(v0_t, v0_t);
        tcg_gen_shli_tl(t0, v0_t, 16);
        tcg_gen_or_tl(v0_t, v0_t, t0);
        tcg_gen_ext32s_tl(v0_t, v0_t);
        gen_store_gpr(v0_t, ret);
        break;
    case NM_REPLV_QB:
        check_dsp(ctx);
        tcg_gen_ext8u_tl(v0_t, v0_t);
        tcg_gen_shli_tl(t0, v0_t, 8);
        tcg_gen_or_tl(v0_t, v0_t, t0);
        tcg_gen_shli_tl(t0, v0_t, 16);
        tcg_gen_or_tl(v0_t, v0_t, t0);
        tcg_gen_ext32s_tl(v0_t, v0_t);
        gen_store_gpr(v0_t, ret);
        break;
    case NM_BITREV:
        check_dsp(ctx);
        gen_helper_bitrev(v0_t, v0_t);
        gen_store_gpr(v0_t, ret);
        break;
    case NM_INSV:
        check_dsp(ctx);
        {
            TCGv tv0 = tcg_temp_new();

            gen_load_gpr(tv0, rt);
            gen_helper_insv(v0_t, cpu_env, v0_t, tv0);
            gen_store_gpr(v0_t, ret);
            tcg_temp_free(tv0);
        }
        break;
    case NM_RADDU_W_QB:
        check_dsp(ctx);
        gen_helper_raddu_w_qb(v0_t, v0_t);
        gen_store_gpr(v0_t, ret);
        break;
    case NM_BITSWAP:
        gen_bitswap(ctx, OPC_BITSWAP, ret, rs);
        break;
    case NM_CLO:
        check_nms(ctx);
        gen_cl(ctx, OPC_CLO, ret, rs);
        break;
    case NM_CLZ:
        check_nms(ctx);
        gen_cl(ctx, OPC_CLZ, ret, rs);
        break;
    case NM_WSBH:
        gen_bshfl(ctx, OPC_WSBH, ret, rs);
        break;
    default:
        gen_reserved_instruction(ctx);
        break;
    }

    tcg_temp_free(v0_t);
    tcg_temp_free(t0);
}

static void gen_pool32axf_7_nanomips_insn(DisasContext *ctx, uint32_t opc,
                                          int rt, int rs, int rd)
{
    TCGv t0 = tcg_temp_new();
    TCGv rs_t = tcg_temp_new();

    gen_load_gpr(rs_t, rs);

    switch (opc) {
    case NM_SHRA_R_QB:
        check_dsp_r2(ctx);
        tcg_gen_movi_tl(t0, rd >> 2);
        switch (extract32(ctx->opcode, 12, 1)) {
        case 0:
            /* NM_SHRA_QB */
            gen_helper_shra_qb(t0, t0, rs_t);
            gen_store_gpr(t0, rt);
            break;
        case 1:
            /* NM_SHRA_R_QB */
            gen_helper_shra_r_qb(t0, t0, rs_t);
            gen_store_gpr(t0, rt);
            break;
        }
        break;
    case NM_SHRL_PH:
        check_dsp_r2(ctx);
        tcg_gen_movi_tl(t0, rd >> 1);
        gen_helper_shrl_ph(t0, t0, rs_t);
        gen_store_gpr(t0, rt);
        break;
    case NM_REPL_QB:
        check_dsp(ctx);
        {
            int16_t imm;
            target_long result;
            imm = extract32(ctx->opcode, 13, 8);
            result = (uint32_t)imm << 24 |
                     (uint32_t)imm << 16 |
                     (uint32_t)imm << 8  |
                     (uint32_t)imm;
            result = (int32_t)result;
            tcg_gen_movi_tl(t0, result);
            gen_store_gpr(t0, rt);
        }
        break;
    default:
        gen_reserved_instruction(ctx);
        break;
    }
    tcg_temp_free(t0);
    tcg_temp_free(rs_t);
}


static void gen_pool32axf_nanomips_insn(CPUMIPSState *env, DisasContext *ctx)
{
    int rt = extract32(ctx->opcode, 21, 5);
    int rs = extract32(ctx->opcode, 16, 5);
    int rd = extract32(ctx->opcode, 11, 5);

    switch (extract32(ctx->opcode, 6, 3)) {
    case NM_POOL32AXF_1:
        {
            int32_t op1 = extract32(ctx->opcode, 9, 3);
            gen_pool32axf_1_nanomips_insn(ctx, op1, rt, rs, rd);
        }
        break;
    case NM_POOL32AXF_2:
        {
            int32_t op1 = extract32(ctx->opcode, 12, 2);
            gen_pool32axf_2_nanomips_insn(ctx, op1, rt, rs, rd);
        }
        break;
    case NM_POOL32AXF_4:
        {
            int32_t op1 = extract32(ctx->opcode, 9, 7);
            gen_pool32axf_4_nanomips_insn(ctx, op1, rt, rs);
        }
        break;
    case NM_POOL32AXF_5:
        switch (extract32(ctx->opcode, 9, 7)) {
#ifndef CONFIG_USER_ONLY
        case NM_TLBP:
            gen_cp0(env, ctx, OPC_TLBP, 0, 0);
            break;
        case NM_TLBR:
            gen_cp0(env, ctx, OPC_TLBR, 0, 0);
            break;
        case NM_TLBWI:
            gen_cp0(env, ctx, OPC_TLBWI, 0, 0);
            break;
        case NM_TLBWR:
            gen_cp0(env, ctx, OPC_TLBWR, 0, 0);
            break;
        case NM_TLBINV:
            gen_cp0(env, ctx, OPC_TLBINV, 0, 0);
            break;
        case NM_TLBINVF:
            gen_cp0(env, ctx, OPC_TLBINVF, 0, 0);
            break;
        case NM_DI:
            check_cp0_enabled(ctx);
            {
                TCGv t0 = tcg_temp_new();

                save_cpu_state(ctx, 1);
                gen_helper_di(t0, cpu_env);
                gen_store_gpr(t0, rt);
            /* Stop translation as we may have switched the execution mode */
                ctx->base.is_jmp = DISAS_STOP;
                tcg_temp_free(t0);
            }
            break;
        case NM_EI:
            check_cp0_enabled(ctx);
            {
                TCGv t0 = tcg_temp_new();

                save_cpu_state(ctx, 1);
                gen_helper_ei(t0, cpu_env);
                gen_store_gpr(t0, rt);
            /* Stop translation as we may have switched the execution mode */
                ctx->base.is_jmp = DISAS_STOP;
                tcg_temp_free(t0);
            }
            break;
        case NM_RDPGPR:
            check_cp0_enabled(ctx);
            gen_load_srsgpr(rs, rt);
            break;
        case NM_WRPGPR:
            check_cp0_enabled(ctx);
            gen_store_srsgpr(rs, rt);
            break;
        case NM_WAIT:
            gen_cp0(env, ctx, OPC_WAIT, 0, 0);
            break;
        case NM_DERET:
            gen_cp0(env, ctx, OPC_DERET, 0, 0);
            break;
        case NM_ERETX:
            gen_cp0(env, ctx, OPC_ERET, 0, 0);
            break;
#endif
        default:
            gen_reserved_instruction(ctx);
            break;
        }
        break;
    case NM_POOL32AXF_7:
        {
            int32_t op1 = extract32(ctx->opcode, 9, 3);
            gen_pool32axf_7_nanomips_insn(ctx, op1, rt, rs, rd);
        }
        break;
    default:
        gen_reserved_instruction(ctx);
        break;
    }
}

/* Immediate Value Compact Branches */
static void gen_compute_imm_branch(DisasContext *ctx, uint32_t opc,
                                   int rt, int32_t imm, int32_t offset)
{
    TCGCond cond = TCG_COND_ALWAYS;
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();

    gen_load_gpr(t0, rt);
    tcg_gen_movi_tl(t1, imm);
    ctx->btarget = addr_add(ctx, ctx->base.pc_next + 4, offset);

    /* Load needed operands and calculate btarget */
    switch (opc) {
    case NM_BEQIC:
        if (rt == 0 && imm == 0) {
            /* Unconditional branch */
        } else if (rt == 0 && imm != 0) {
            /* Treat as NOP */
            goto out;
        } else {
            cond = TCG_COND_EQ;
        }
        break;
    case NM_BBEQZC:
    case NM_BBNEZC:
        check_nms(ctx);
        if (imm >= 32 && !(ctx->hflags & MIPS_HFLAG_64)) {
            gen_reserved_instruction(ctx);
            goto out;
        } else if (rt == 0 && opc == NM_BBEQZC) {
            /* Unconditional branch */
        } else if (rt == 0 && opc == NM_BBNEZC) {
            /* Treat as NOP */
            goto out;
        } else {
            tcg_gen_shri_tl(t0, t0, imm);
            tcg_gen_andi_tl(t0, t0, 1);
            tcg_gen_movi_tl(t1, 0);
            if (opc == NM_BBEQZC) {
                cond = TCG_COND_EQ;
            } else {
                cond = TCG_COND_NE;
            }
        }
        break;
    case NM_BNEIC:
        if (rt == 0 && imm == 0) {
            /* Treat as NOP */
            goto out;
        } else if (rt == 0 && imm != 0) {
            /* Unconditional branch */
        } else {
            cond = TCG_COND_NE;
        }
        break;
    case NM_BGEIC:
        if (rt == 0 && imm == 0) {
            /* Unconditional branch */
        } else  {
            cond = TCG_COND_GE;
        }
        break;
    case NM_BLTIC:
        cond = TCG_COND_LT;
        break;
    case NM_BGEIUC:
        if (rt == 0 && imm == 0) {
            /* Unconditional branch */
        } else  {
            cond = TCG_COND_GEU;
        }
        break;
    case NM_BLTIUC:
        cond = TCG_COND_LTU;
        break;
    default:
        MIPS_INVAL("Immediate Value Compact branch");
        gen_reserved_instruction(ctx);
        goto out;
    }

    /* branch completion */
    clear_branch_hflags(ctx);
    ctx->base.is_jmp = DISAS_NORETURN;

    if (cond == TCG_COND_ALWAYS) {
        /* Uncoditional compact branch */
        gen_goto_tb(ctx, 0, ctx->btarget);
    } else {
        /* Conditional compact branch */
        TCGLabel *fs = gen_new_label();

        tcg_gen_brcond_tl(tcg_invert_cond(cond), t0, t1, fs);

        gen_goto_tb(ctx, 1, ctx->btarget);
        gen_set_label(fs);

        gen_goto_tb(ctx, 0, ctx->base.pc_next + 4);
    }

out:
    tcg_temp_free(t0);
    tcg_temp_free(t1);
}

/* P.BALRSC type nanoMIPS R6 branches: BALRSC and BRSC */
static void gen_compute_nanomips_pbalrsc_branch(DisasContext *ctx, int rs,
                                                int rt)
{
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();

    /* load rs */
    gen_load_gpr(t0, rs);

    /* link */
    if (rt != 0) {
        tcg_gen_movi_tl(cpu_gpr[rt], ctx->base.pc_next + 4);
    }

    /* calculate btarget */
    tcg_gen_shli_tl(t0, t0, 1);
    tcg_gen_movi_tl(t1, ctx->base.pc_next + 4);
    gen_op_addr_add(ctx, btarget, t1, t0);

    /* branch completion */
    clear_branch_hflags(ctx);
    ctx->base.is_jmp = DISAS_NORETURN;

    /* unconditional branch to register */
    tcg_gen_mov_tl(cpu_PC, btarget);
    tcg_gen_lookup_and_goto_ptr();

    tcg_temp_free(t0);
    tcg_temp_free(t1);
}

/* nanoMIPS Branches */
static void gen_compute_compact_branch_nm(DisasContext *ctx, uint32_t opc,
                                       int rs, int rt, int32_t offset)
{
    int bcond_compute = 0;
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();

    /* Load needed operands and calculate btarget */
    switch (opc) {
    /* compact branch */
    case OPC_BGEC:
    case OPC_BLTC:
        gen_load_gpr(t0, rs);
        gen_load_gpr(t1, rt);
        bcond_compute = 1;
        ctx->btarget = addr_add(ctx, ctx->base.pc_next + 4, offset);
        break;
    case OPC_BGEUC:
    case OPC_BLTUC:
        if (rs == 0 || rs == rt) {
            /* OPC_BLEZALC, OPC_BGEZALC */
            /* OPC_BGTZALC, OPC_BLTZALC */
            tcg_gen_movi_tl(cpu_gpr[31], ctx->base.pc_next + 4);
        }
        gen_load_gpr(t0, rs);
        gen_load_gpr(t1, rt);
        bcond_compute = 1;
        ctx->btarget = addr_add(ctx, ctx->base.pc_next + 4, offset);
        break;
    case OPC_BC:
        ctx->btarget = addr_add(ctx, ctx->base.pc_next + 4, offset);
        break;
    case OPC_BEQZC:
        if (rs != 0) {
            /* OPC_BEQZC, OPC_BNEZC */
            gen_load_gpr(t0, rs);
            bcond_compute = 1;
            ctx->btarget = addr_add(ctx, ctx->base.pc_next + 4, offset);
        } else {
            /* OPC_JIC, OPC_JIALC */
            TCGv tbase = tcg_temp_new();
            TCGv toffset = tcg_temp_new();

            gen_load_gpr(tbase, rt);
            tcg_gen_movi_tl(toffset, offset);
            gen_op_addr_add(ctx, btarget, tbase, toffset);
            tcg_temp_free(tbase);
            tcg_temp_free(toffset);
        }
        break;
    default:
        MIPS_INVAL("Compact branch/jump");
        gen_reserved_instruction(ctx);
        goto out;
    }

    if (bcond_compute == 0) {
        /* Uncoditional compact branch */
        switch (opc) {
        case OPC_BC:
            gen_goto_tb(ctx, 0, ctx->btarget);
            break;
        default:
            MIPS_INVAL("Compact branch/jump");
            gen_reserved_instruction(ctx);
            goto out;
        }
    } else {
        /* Conditional compact branch */
        TCGLabel *fs = gen_new_label();

        switch (opc) {
        case OPC_BGEUC:
            if (rs == 0 && rt != 0) {
                /* OPC_BLEZALC */
                tcg_gen_brcondi_tl(tcg_invert_cond(TCG_COND_LE), t1, 0, fs);
            } else if (rs != 0 && rt != 0 && rs == rt) {
                /* OPC_BGEZALC */
                tcg_gen_brcondi_tl(tcg_invert_cond(TCG_COND_GE), t1, 0, fs);
            } else {
                /* OPC_BGEUC */
                tcg_gen_brcond_tl(tcg_invert_cond(TCG_COND_GEU), t0, t1, fs);
            }
            break;
        case OPC_BLTUC:
            if (rs == 0 && rt != 0) {
                /* OPC_BGTZALC */
                tcg_gen_brcondi_tl(tcg_invert_cond(TCG_COND_GT), t1, 0, fs);
            } else if (rs != 0 && rt != 0 && rs == rt) {
                /* OPC_BLTZALC */
                tcg_gen_brcondi_tl(tcg_invert_cond(TCG_COND_LT), t1, 0, fs);
            } else {
                /* OPC_BLTUC */
                tcg_gen_brcond_tl(tcg_invert_cond(TCG_COND_LTU), t0, t1, fs);
            }
            break;
        case OPC_BGEC:
            if (rs == 0 && rt != 0) {
                /* OPC_BLEZC */
                tcg_gen_brcondi_tl(tcg_invert_cond(TCG_COND_LE), t1, 0, fs);
            } else if (rs != 0 && rt != 0 && rs == rt) {
                /* OPC_BGEZC */
                tcg_gen_brcondi_tl(tcg_invert_cond(TCG_COND_GE), t1, 0, fs);
            } else {
                /* OPC_BGEC */
                tcg_gen_brcond_tl(tcg_invert_cond(TCG_COND_GE), t0, t1, fs);
            }
            break;
        case OPC_BLTC:
            if (rs == 0 && rt != 0) {
                /* OPC_BGTZC */
                tcg_gen_brcondi_tl(tcg_invert_cond(TCG_COND_GT), t1, 0, fs);
            } else if (rs != 0 && rt != 0 && rs == rt) {
                /* OPC_BLTZC */
                tcg_gen_brcondi_tl(tcg_invert_cond(TCG_COND_LT), t1, 0, fs);
            } else {
                /* OPC_BLTC */
                tcg_gen_brcond_tl(tcg_invert_cond(TCG_COND_LT), t0, t1, fs);
            }
            break;
        case OPC_BEQZC:
            tcg_gen_brcondi_tl(tcg_invert_cond(TCG_COND_EQ), t0, 0, fs);
            break;
        default:
            MIPS_INVAL("Compact conditional branch/jump");
            gen_reserved_instruction(ctx);
            goto out;
        }

        /* branch completion */
        clear_branch_hflags(ctx);
        ctx->base.is_jmp = DISAS_NORETURN;

        /* Generating branch here as compact branches don't have delay slot */
        gen_goto_tb(ctx, 1, ctx->btarget);
        gen_set_label(fs);

        gen_goto_tb(ctx, 0, ctx->base.pc_next + 4);
    }

out:
    tcg_temp_free(t0);
    tcg_temp_free(t1);
}


/* nanoMIPS CP1 Branches */
static void gen_compute_branch_cp1_nm(DisasContext *ctx, uint32_t op,
                                   int32_t ft, int32_t offset)
{
    target_ulong btarget;
    TCGv_i64 t0 = tcg_temp_new_i64();

    gen_load_fpr64(ctx, t0, ft);
    tcg_gen_andi_i64(t0, t0, 1);

    btarget = addr_add(ctx, ctx->base.pc_next + 4, offset);

    switch (op) {
    case NM_BC1EQZC:
        tcg_gen_xori_i64(t0, t0, 1);
        ctx->hflags |= MIPS_HFLAG_BC;
        break;
    case NM_BC1NEZC:
        /* t0 already set */
        ctx->hflags |= MIPS_HFLAG_BC;
        break;
    default:
        MIPS_INVAL("cp1 cond branch");
        gen_reserved_instruction(ctx);
        goto out;
    }

    tcg_gen_trunc_i64_tl(bcond, t0);

    ctx->btarget = btarget;

out:
    tcg_temp_free_i64(t0);
}


static void gen_p_lsx(DisasContext *ctx, int rd, int rs, int rt)
{
    TCGv t0, t1;
    t0 = tcg_temp_new();
    t1 = tcg_temp_new();

    gen_load_gpr(t0, rs);
    gen_load_gpr(t1, rt);

    if ((extract32(ctx->opcode, 6, 1)) == 1) {
        /* PP.LSXS instructions require shifting */
        switch (extract32(ctx->opcode, 7, 4)) {
        case NM_SHXS:
            check_nms(ctx);
            /* fall through */
        case NM_LHXS:
        case NM_LHUXS:
            tcg_gen_shli_tl(t0, t0, 1);
            break;
        case NM_SWXS:
            check_nms(ctx);
            /* fall through */
        case NM_LWXS:
        case NM_LWC1XS:
        case NM_SWC1XS:
            tcg_gen_shli_tl(t0, t0, 2);
            break;
        case NM_LDC1XS:
        case NM_SDC1XS:
            tcg_gen_shli_tl(t0, t0, 3);
            break;
        }
    }
    gen_op_addr_add(ctx, t0, t0, t1);

    switch (extract32(ctx->opcode, 7, 4)) {
    case NM_LBX:
        tcg_gen_qemu_ld_tl(t0, t0, ctx->mem_idx,
                           MO_SB);
        gen_store_gpr(t0, rd);
        break;
    case NM_LHX:
    /*case NM_LHXS:*/
        tcg_gen_qemu_ld_tl(t0, t0, ctx->mem_idx,
                           MO_TESW);
        gen_store_gpr(t0, rd);
        break;
    case NM_LWX:
    /*case NM_LWXS:*/
        tcg_gen_qemu_ld_tl(t0, t0, ctx->mem_idx,
                           MO_TESL);
        gen_store_gpr(t0, rd);
        break;
    case NM_LBUX:
        tcg_gen_qemu_ld_tl(t0, t0, ctx->mem_idx,
                           MO_UB);
        gen_store_gpr(t0, rd);
        break;
    case NM_LHUX:
    /*case NM_LHUXS:*/
        tcg_gen_qemu_ld_tl(t0, t0, ctx->mem_idx,
                           MO_TEUW);
        gen_store_gpr(t0, rd);
        break;
    case NM_SBX:
        check_nms(ctx);
        gen_load_gpr(t1, rd);
        tcg_gen_qemu_st_tl(t1, t0, ctx->mem_idx,
                           MO_8);
        break;
    case NM_SHX:
    /*case NM_SHXS:*/
        check_nms(ctx);
        gen_load_gpr(t1, rd);
        tcg_gen_qemu_st_tl(t1, t0, ctx->mem_idx,
                           MO_TEUW);
        break;
    case NM_SWX:
    /*case NM_SWXS:*/
        check_nms(ctx);
        gen_load_gpr(t1, rd);
        tcg_gen_qemu_st_tl(t1, t0, ctx->mem_idx,
                           MO_TEUL);
        break;
    case NM_LWC1X:
    /*case NM_LWC1XS:*/
    case NM_LDC1X:
    /*case NM_LDC1XS:*/
    case NM_SWC1X:
    /*case NM_SWC1XS:*/
    case NM_SDC1X:
    /*case NM_SDC1XS:*/
        if (ctx->CP0_Config1 & (1 << CP0C1_FP)) {
            check_cp1_enabled(ctx);
            switch (extract32(ctx->opcode, 7, 4)) {
            case NM_LWC1X:
            /*case NM_LWC1XS:*/
                gen_flt_ldst(ctx, OPC_LWC1, rd, t0);
                break;
            case NM_LDC1X:
            /*case NM_LDC1XS:*/
                gen_flt_ldst(ctx, OPC_LDC1, rd, t0);
                break;
            case NM_SWC1X:
            /*case NM_SWC1XS:*/
                gen_flt_ldst(ctx, OPC_SWC1, rd, t0);
                break;
            case NM_SDC1X:
            /*case NM_SDC1XS:*/
                gen_flt_ldst(ctx, OPC_SDC1, rd, t0);
                break;
            }
        } else {
            generate_exception_err(ctx, EXCP_CpU, 1);
        }
        break;
    default:
        gen_reserved_instruction(ctx);
        break;
    }

    tcg_temp_free(t0);
    tcg_temp_free(t1);
}

static void gen_pool32f_nanomips_insn(DisasContext *ctx)
{
    int rt, rs, rd;

    rt = extract32(ctx->opcode, 21, 5);
    rs = extract32(ctx->opcode, 16, 5);
    rd = extract32(ctx->opcode, 11, 5);

    if (!(ctx->CP0_Config1 & (1 << CP0C1_FP))) {
        gen_reserved_instruction(ctx);
        return;
    }
    check_cp1_enabled(ctx);
    switch (extract32(ctx->opcode, 0, 3)) {
    case NM_POOL32F_0:
        switch (extract32(ctx->opcode, 3, 7)) {
        case NM_RINT_S:
            gen_farith(ctx, OPC_RINT_S, 0, rt, rs, 0);
            break;
        case NM_RINT_D:
            gen_farith(ctx, OPC_RINT_D, 0, rt, rs, 0);
            break;
        case NM_CLASS_S:
            gen_farith(ctx, OPC_CLASS_S, 0, rt, rs, 0);
            break;
        case NM_CLASS_D:
            gen_farith(ctx, OPC_CLASS_D, 0, rt, rs, 0);
            break;
        case NM_ADD_S:
            gen_farith(ctx, OPC_ADD_S, rt, rs, rd, 0);
            break;
        case NM_ADD_D:
            gen_farith(ctx, OPC_ADD_D, rt, rs, rd, 0);
            break;
        case NM_SUB_S:
            gen_farith(ctx, OPC_SUB_S, rt, rs, rd, 0);
            break;
        case NM_SUB_D:
            gen_farith(ctx, OPC_SUB_D, rt, rs, rd, 0);
            break;
        case NM_MUL_S:
            gen_farith(ctx, OPC_MUL_S, rt, rs, rd, 0);
            break;
        case NM_MUL_D:
            gen_farith(ctx, OPC_MUL_D, rt, rs, rd, 0);
            break;
        case NM_DIV_S:
            gen_farith(ctx, OPC_DIV_S, rt, rs, rd, 0);
            break;
        case NM_DIV_D:
            gen_farith(ctx, OPC_DIV_D, rt, rs, rd, 0);
            break;
        case NM_SELEQZ_S:
            gen_sel_s(ctx, OPC_SELEQZ_S, rd, rt, rs);
            break;
        case NM_SELEQZ_D:
            gen_sel_d(ctx, OPC_SELEQZ_D, rd, rt, rs);
            break;
        case NM_SELNEZ_S:
            gen_sel_s(ctx, OPC_SELNEZ_S, rd, rt, rs);
            break;
        case NM_SELNEZ_D:
            gen_sel_d(ctx, OPC_SELNEZ_D, rd, rt, rs);
            break;
        case NM_SEL_S:
            gen_sel_s(ctx, OPC_SEL_S, rd, rt, rs);
            break;
        case NM_SEL_D:
            gen_sel_d(ctx, OPC_SEL_D, rd, rt, rs);
            break;
        case NM_MADDF_S:
            gen_farith(ctx, OPC_MADDF_S, rt, rs, rd, 0);
            break;
        case NM_MADDF_D:
            gen_farith(ctx, OPC_MADDF_D, rt, rs, rd, 0);
            break;
        case NM_MSUBF_S:
            gen_farith(ctx, OPC_MSUBF_S, rt, rs, rd, 0);
            break;
        case NM_MSUBF_D:
            gen_farith(ctx, OPC_MSUBF_D, rt, rs, rd, 0);
            break;
        default:
            gen_reserved_instruction(ctx);
            break;
        }
        break;
    case NM_POOL32F_3:
        switch (extract32(ctx->opcode, 3, 3)) {
        case NM_MIN_FMT:
            switch (extract32(ctx->opcode, 9, 1)) {
            case FMT_SDPS_S:
                gen_farith(ctx, OPC_MIN_S, rt, rs, rd, 0);
                break;
            case FMT_SDPS_D:
                gen_farith(ctx, OPC_MIN_D, rt, rs, rd, 0);
                break;
            }
            break;
        case NM_MAX_FMT:
            switch (extract32(ctx->opcode, 9, 1)) {
            case FMT_SDPS_S:
                gen_farith(ctx, OPC_MAX_S, rt, rs, rd, 0);
                break;
            case FMT_SDPS_D:
                gen_farith(ctx, OPC_MAX_D, rt, rs, rd, 0);
                break;
            }
            break;
        case NM_MINA_FMT:
            switch (extract32(ctx->opcode, 9, 1)) {
            case FMT_SDPS_S:
                gen_farith(ctx, OPC_MINA_S, rt, rs, rd, 0);
                break;
            case FMT_SDPS_D:
                gen_farith(ctx, OPC_MINA_D, rt, rs, rd, 0);
                break;
            }
            break;
        case NM_MAXA_FMT:
            switch (extract32(ctx->opcode, 9, 1)) {
            case FMT_SDPS_S:
                gen_farith(ctx, OPC_MAXA_S, rt, rs, rd, 0);
                break;
            case FMT_SDPS_D:
                gen_farith(ctx, OPC_MAXA_D, rt, rs, rd, 0);
                break;
            }
            break;
        case NM_POOL32FXF:
            switch (extract32(ctx->opcode, 6, 8)) {
            case NM_CFC1:
                gen_cp1(ctx, OPC_CFC1, rt, rs);
                break;
            case NM_CTC1:
                gen_cp1(ctx, OPC_CTC1, rt, rs);
                break;
            case NM_MFC1:
                gen_cp1(ctx, OPC_MFC1, rt, rs);
                break;
            case NM_MTC1:
                gen_cp1(ctx, OPC_MTC1, rt, rs);
                break;
            case NM_MFHC1:
                gen_cp1(ctx, OPC_MFHC1, rt, rs);
                break;
            case NM_MTHC1:
                gen_cp1(ctx, OPC_MTHC1, rt, rs);
                break;
            case NM_CVT_S_PL:
                gen_farith(ctx, OPC_CVT_S_PL, -1, rs, rt, 0);
                break;
            case NM_CVT_S_PU:
                gen_farith(ctx, OPC_CVT_S_PU, -1, rs, rt, 0);
                break;
            default:
                switch (extract32(ctx->opcode, 6, 9)) {
                case NM_CVT_L_S:
                    gen_farith(ctx, OPC_CVT_L_S, -1, rs, rt, 0);
                    break;
                case NM_CVT_L_D:
                    gen_farith(ctx, OPC_CVT_L_D, -1, rs, rt, 0);
                    break;
                case NM_CVT_W_S:
                    gen_farith(ctx, OPC_CVT_W_S, -1, rs, rt, 0);
                    break;
                case NM_CVT_W_D:
                    gen_farith(ctx, OPC_CVT_W_D, -1, rs, rt, 0);
                    break;
                case NM_RSQRT_S:
                    gen_farith(ctx, OPC_RSQRT_S, -1, rs, rt, 0);
                    break;
                case NM_RSQRT_D:
                    gen_farith(ctx, OPC_RSQRT_D, -1, rs, rt, 0);
                    break;
                case NM_SQRT_S:
                    gen_farith(ctx, OPC_SQRT_S, -1, rs, rt, 0);
                    break;
                case NM_SQRT_D:
                    gen_farith(ctx, OPC_SQRT_D, -1, rs, rt, 0);
                    break;
                case NM_RECIP_S:
                    gen_farith(ctx, OPC_RECIP_S, -1, rs, rt, 0);
                    break;
                case NM_RECIP_D:
                    gen_farith(ctx, OPC_RECIP_D, -1, rs, rt, 0);
                    break;
                case NM_FLOOR_L_S:
                    gen_farith(ctx, OPC_FLOOR_L_S, -1, rs, rt, 0);
                    break;
                case NM_FLOOR_L_D:
                    gen_farith(ctx, OPC_FLOOR_L_D, -1, rs, rt, 0);
                    break;
                case NM_FLOOR_W_S:
                    gen_farith(ctx, OPC_FLOOR_W_S, -1, rs, rt, 0);
                    break;
                case NM_FLOOR_W_D:
                    gen_farith(ctx, OPC_FLOOR_W_D, -1, rs, rt, 0);
                    break;
                case NM_CEIL_L_S:
                    gen_farith(ctx, OPC_CEIL_L_S, -1, rs, rt, 0);
                    break;
                case NM_CEIL_L_D:
                    gen_farith(ctx, OPC_CEIL_L_D, -1, rs, rt, 0);
                    break;
                case NM_CEIL_W_S:
                    gen_farith(ctx, OPC_CEIL_W_S, -1, rs, rt, 0);
                    break;
                case NM_CEIL_W_D:
                    gen_farith(ctx, OPC_CEIL_W_D, -1, rs, rt, 0);
                    break;
                case NM_TRUNC_L_S:
                    gen_farith(ctx, OPC_TRUNC_L_S, -1, rs, rt, 0);
                    break;
                case NM_TRUNC_L_D:
                    gen_farith(ctx, OPC_TRUNC_L_D, -1, rs, rt, 0);
                    break;
                case NM_TRUNC_W_S:
                    gen_farith(ctx, OPC_TRUNC_W_S, -1, rs, rt, 0);
                    break;
                case NM_TRUNC_W_D:
                    gen_farith(ctx, OPC_TRUNC_W_D, -1, rs, rt, 0);
                    break;
                case NM_ROUND_L_S:
                    gen_farith(ctx, OPC_ROUND_L_S, -1, rs, rt, 0);
                    break;
                case NM_ROUND_L_D:
                    gen_farith(ctx, OPC_ROUND_L_D, -1, rs, rt, 0);
                    break;
                case NM_ROUND_W_S:
                    gen_farith(ctx, OPC_ROUND_W_S, -1, rs, rt, 0);
                    break;
                case NM_ROUND_W_D:
                    gen_farith(ctx, OPC_ROUND_W_D, -1, rs, rt, 0);
                    break;
                case NM_MOV_S:
                    gen_farith(ctx, OPC_MOV_S, -1, rs, rt, 0);
                    break;
                case NM_MOV_D:
                    gen_farith(ctx, OPC_MOV_D, -1, rs, rt, 0);
                    break;
                case NM_ABS_S:
                    gen_farith(ctx, OPC_ABS_S, -1, rs, rt, 0);
                    break;
                case NM_ABS_D:
                    gen_farith(ctx, OPC_ABS_D, -1, rs, rt, 0);
                    break;
                case NM_NEG_S:
                    gen_farith(ctx, OPC_NEG_S, -1, rs, rt, 0);
                    break;
                case NM_NEG_D:
                    gen_farith(ctx, OPC_NEG_D, -1, rs, rt, 0);
                    break;
                case NM_CVT_D_S:
                    gen_farith(ctx, OPC_CVT_D_S, -1, rs, rt, 0);
                    break;
                case NM_CVT_D_W:
                    gen_farith(ctx, OPC_CVT_D_W, -1, rs, rt, 0);
                    break;
                case NM_CVT_D_L:
                    gen_farith(ctx, OPC_CVT_D_L, -1, rs, rt, 0);
                    break;
                case NM_CVT_S_D:
                    gen_farith(ctx, OPC_CVT_S_D, -1, rs, rt, 0);
                    break;
                case NM_CVT_S_W:
                    gen_farith(ctx, OPC_CVT_S_W, -1, rs, rt, 0);
                    break;
                case NM_CVT_S_L:
                    gen_farith(ctx, OPC_CVT_S_L, -1, rs, rt, 0);
                    break;
                default:
                    gen_reserved_instruction(ctx);
                    break;
                }
                break;
            }
            break;
        }
        break;
    case NM_POOL32F_5:
        switch (extract32(ctx->opcode, 3, 3)) {
        case NM_CMP_CONDN_S:
            gen_r6_cmp_s(ctx, extract32(ctx->opcode, 6, 5), rt, rs, rd);
            break;
        case NM_CMP_CONDN_D:
            gen_r6_cmp_d(ctx, extract32(ctx->opcode, 6, 5), rt, rs, rd);
            break;
        default:
            gen_reserved_instruction(ctx);
            break;
        }
        break;
    default:
        gen_reserved_instruction(ctx);
        break;
    }
}

static void gen_pool32a5_nanomips_insn(DisasContext *ctx, int opc,
                                       int rd, int rs, int rt)
{
    int ret = rd;
    TCGv t0 = tcg_temp_new();
    TCGv v1_t = tcg_temp_new();
    TCGv v2_t = tcg_temp_new();

    gen_load_gpr(v1_t, rs);
    gen_load_gpr(v2_t, rt);

    switch (opc) {
    case NM_CMP_EQ_PH:
        check_dsp(ctx);
        gen_helper_cmp_eq_ph(v1_t, v2_t, cpu_env);
        break;
    case NM_CMP_LT_PH:
        check_dsp(ctx);
        gen_helper_cmp_lt_ph(v1_t, v2_t, cpu_env);
        break;
    case NM_CMP_LE_PH:
        check_dsp(ctx);
        gen_helper_cmp_le_ph(v1_t, v2_t, cpu_env);
        break;
    case NM_CMPU_EQ_QB:
        check_dsp(ctx);
        gen_helper_cmpu_eq_qb(v1_t, v2_t, cpu_env);
        break;
    case NM_CMPU_LT_QB:
        check_dsp(ctx);
        gen_helper_cmpu_lt_qb(v1_t, v2_t, cpu_env);
        break;
    case NM_CMPU_LE_QB:
        check_dsp(ctx);
        gen_helper_cmpu_le_qb(v1_t, v2_t, cpu_env);
        break;
    case NM_CMPGU_EQ_QB:
        check_dsp(ctx);
        gen_helper_cmpgu_eq_qb(v1_t, v1_t, v2_t);
        gen_store_gpr(v1_t, ret);
        break;
    case NM_CMPGU_LT_QB:
        check_dsp(ctx);
        gen_helper_cmpgu_lt_qb(v1_t, v1_t, v2_t);
        gen_store_gpr(v1_t, ret);
        break;
    case NM_CMPGU_LE_QB:
        check_dsp(ctx);
        gen_helper_cmpgu_le_qb(v1_t, v1_t, v2_t);
        gen_store_gpr(v1_t, ret);
        break;
    case NM_CMPGDU_EQ_QB:
        check_dsp_r2(ctx);
        gen_helper_cmpgu_eq_qb(v1_t, v1_t, v2_t);
        tcg_gen_deposit_tl(cpu_dspctrl, cpu_dspctrl, v1_t, 24, 4);
        gen_store_gpr(v1_t, ret);
        break;
    case NM_CMPGDU_LT_QB:
        check_dsp_r2(ctx);
        gen_helper_cmpgu_lt_qb(v1_t, v1_t, v2_t);
        tcg_gen_deposit_tl(cpu_dspctrl, cpu_dspctrl, v1_t, 24, 4);
        gen_store_gpr(v1_t, ret);
        break;
    case NM_CMPGDU_LE_QB:
        check_dsp_r2(ctx);
        gen_helper_cmpgu_le_qb(v1_t, v1_t, v2_t);
        tcg_gen_deposit_tl(cpu_dspctrl, cpu_dspctrl, v1_t, 24, 4);
        gen_store_gpr(v1_t, ret);
        break;
    case NM_PACKRL_PH:
        check_dsp(ctx);
        gen_helper_packrl_ph(v1_t, v1_t, v2_t);
        gen_store_gpr(v1_t, ret);
        break;
    case NM_PICK_QB:
        check_dsp(ctx);
        gen_helper_pick_qb(v1_t, v1_t, v2_t, cpu_env);
        gen_store_gpr(v1_t, ret);
        break;
    case NM_PICK_PH:
        check_dsp(ctx);
        gen_helper_pick_ph(v1_t, v1_t, v2_t, cpu_env);
        gen_store_gpr(v1_t, ret);
        break;
    case NM_ADDQ_S_W:
        check_dsp(ctx);
        gen_helper_addq_s_w(v1_t, v1_t, v2_t, cpu_env);
        gen_store_gpr(v1_t, ret);
        break;
    case NM_SUBQ_S_W:
        check_dsp(ctx);
        gen_helper_subq_s_w(v1_t, v1_t, v2_t, cpu_env);
        gen_store_gpr(v1_t, ret);
        break;
    case NM_ADDSC:
        check_dsp(ctx);
        gen_helper_addsc(v1_t, v1_t, v2_t, cpu_env);
        gen_store_gpr(v1_t, ret);
        break;
    case NM_ADDWC:
        check_dsp(ctx);
        gen_helper_addwc(v1_t, v1_t, v2_t, cpu_env);
        gen_store_gpr(v1_t, ret);
        break;
    case NM_ADDQ_S_PH:
        check_dsp(ctx);
        switch (extract32(ctx->opcode, 10, 1)) {
        case 0:
            /* ADDQ_PH */
            gen_helper_addq_ph(v1_t, v1_t, v2_t, cpu_env);
            gen_store_gpr(v1_t, ret);
            break;
        case 1:
            /* ADDQ_S_PH */
            gen_helper_addq_s_ph(v1_t, v1_t, v2_t, cpu_env);
            gen_store_gpr(v1_t, ret);
            break;
        }
        break;
    case NM_ADDQH_R_PH:
        check_dsp_r2(ctx);
        switch (extract32(ctx->opcode, 10, 1)) {
        case 0:
            /* ADDQH_PH */
            gen_helper_addqh_ph(v1_t, v1_t, v2_t);
            gen_store_gpr(v1_t, ret);
            break;
        case 1:
            /* ADDQH_R_PH */
            gen_helper_addqh_r_ph(v1_t, v1_t, v2_t);
            gen_store_gpr(v1_t, ret);
            break;
        }
        break;
    case NM_ADDQH_R_W:
        check_dsp_r2(ctx);
        switch (extract32(ctx->opcode, 10, 1)) {
        case 0:
            /* ADDQH_W */
            gen_helper_addqh_w(v1_t, v1_t, v2_t);
            gen_store_gpr(v1_t, ret);
            break;
        case 1:
            /* ADDQH_R_W */
            gen_helper_addqh_r_w(v1_t, v1_t, v2_t);
            gen_store_gpr(v1_t, ret);
            break;
        }
        break;
    case NM_ADDU_S_QB:
        check_dsp(ctx);
        switch (extract32(ctx->opcode, 10, 1)) {
        case 0:
            /* ADDU_QB */
            gen_helper_addu_qb(v1_t, v1_t, v2_t, cpu_env);
            gen_store_gpr(v1_t, ret);
            break;
        case 1:
            /* ADDU_S_QB */
            gen_helper_addu_s_qb(v1_t, v1_t, v2_t, cpu_env);
            gen_store_gpr(v1_t, ret);
            break;
        }
        break;
    case NM_ADDU_S_PH:
        check_dsp_r2(ctx);
        switch (extract32(ctx->opcode, 10, 1)) {
        case 0:
            /* ADDU_PH */
            gen_helper_addu_ph(v1_t, v1_t, v2_t, cpu_env);
            gen_store_gpr(v1_t, ret);
            break;
        case 1:
            /* ADDU_S_PH */
            gen_helper_addu_s_ph(v1_t, v1_t, v2_t, cpu_env);
            gen_store_gpr(v1_t, ret);
            break;
        }
        break;
    case NM_ADDUH_R_QB:
        check_dsp_r2(ctx);
        switch (extract32(ctx->opcode, 10, 1)) {
        case 0:
            /* ADDUH_QB */
            gen_helper_adduh_qb(v1_t, v1_t, v2_t);
            gen_store_gpr(v1_t, ret);
            break;
        case 1:
            /* ADDUH_R_QB */
            gen_helper_adduh_r_qb(v1_t, v1_t, v2_t);
            gen_store_gpr(v1_t, ret);
            break;
        }
        break;
    case NM_SHRAV_R_PH:
        check_dsp(ctx);
        switch (extract32(ctx->opcode, 10, 1)) {
        case 0:
            /* SHRAV_PH */
            gen_helper_shra_ph(v1_t, v1_t, v2_t);
            gen_store_gpr(v1_t, ret);
            break;
        case 1:
            /* SHRAV_R_PH */
            gen_helper_shra_r_ph(v1_t, v1_t, v2_t);
            gen_store_gpr(v1_t, ret);
            break;
        }
        break;
    case NM_SHRAV_R_QB:
        check_dsp_r2(ctx);
        switch (extract32(ctx->opcode, 10, 1)) {
        case 0:
            /* SHRAV_QB */
            gen_helper_shra_qb(v1_t, v1_t, v2_t);
            gen_store_gpr(v1_t, ret);
            break;
        case 1:
            /* SHRAV_R_QB */
            gen_helper_shra_r_qb(v1_t, v1_t, v2_t);
            gen_store_gpr(v1_t, ret);
            break;
        }
        break;
    case NM_SUBQ_S_PH:
        check_dsp(ctx);
        switch (extract32(ctx->opcode, 10, 1)) {
        case 0:
            /* SUBQ_PH */
            gen_helper_subq_ph(v1_t, v1_t, v2_t, cpu_env);
            gen_store_gpr(v1_t, ret);
            break;
        case 1:
            /* SUBQ_S_PH */
            gen_helper_subq_s_ph(v1_t, v1_t, v2_t, cpu_env);
            gen_store_gpr(v1_t, ret);
            break;
        }
        break;
    case NM_SUBQH_R_PH:
        check_dsp_r2(ctx);
        switch (extract32(ctx->opcode, 10, 1)) {
        case 0:
            /* SUBQH_PH */
            gen_helper_subqh_ph(v1_t, v1_t, v2_t);
            gen_store_gpr(v1_t, ret);
            break;
        case 1:
            /* SUBQH_R_PH */
            gen_helper_subqh_r_ph(v1_t, v1_t, v2_t);
            gen_store_gpr(v1_t, ret);
            break;
        }
        break;
    case NM_SUBQH_R_W:
        check_dsp_r2(ctx);
        switch (extract32(ctx->opcode, 10, 1)) {
        case 0:
            /* SUBQH_W */
            gen_helper_subqh_w(v1_t, v1_t, v2_t);
            gen_store_gpr(v1_t, ret);
            break;
        case 1:
            /* SUBQH_R_W */
            gen_helper_subqh_r_w(v1_t, v1_t, v2_t);
            gen_store_gpr(v1_t, ret);
            break;
        }
        break;
    case NM_SUBU_S_QB:
        check_dsp(ctx);
        switch (extract32(ctx->opcode, 10, 1)) {
        case 0:
            /* SUBU_QB */
            gen_helper_subu_qb(v1_t, v1_t, v2_t, cpu_env);
            gen_store_gpr(v1_t, ret);
            break;
        case 1:
            /* SUBU_S_QB */
            gen_helper_subu_s_qb(v1_t, v1_t, v2_t, cpu_env);
            gen_store_gpr(v1_t, ret);
            break;
        }
        break;
    case NM_SUBU_S_PH:
        check_dsp_r2(ctx);
        switch (extract32(ctx->opcode, 10, 1)) {
        case 0:
            /* SUBU_PH */
            gen_helper_subu_ph(v1_t, v1_t, v2_t, cpu_env);
            gen_store_gpr(v1_t, ret);
            break;
        case 1:
            /* SUBU_S_PH */
            gen_helper_subu_s_ph(v1_t, v1_t, v2_t, cpu_env);
            gen_store_gpr(v1_t, ret);
            break;
        }
        break;
    case NM_SUBUH_R_QB:
        check_dsp_r2(ctx);
        switch (extract32(ctx->opcode, 10, 1)) {
        case 0:
            /* SUBUH_QB */
            gen_helper_subuh_qb(v1_t, v1_t, v2_t);
            gen_store_gpr(v1_t, ret);
            break;
        case 1:
            /* SUBUH_R_QB */
            gen_helper_subuh_r_qb(v1_t, v1_t, v2_t);
            gen_store_gpr(v1_t, ret);
            break;
        }
        break;
    case NM_SHLLV_S_PH:
        check_dsp(ctx);
        switch (extract32(ctx->opcode, 10, 1)) {
        case 0:
            /* SHLLV_PH */
            gen_helper_shll_ph(v1_t, v1_t, v2_t, cpu_env);
            gen_store_gpr(v1_t, ret);
            break;
        case 1:
            /* SHLLV_S_PH */
            gen_helper_shll_s_ph(v1_t, v1_t, v2_t, cpu_env);
            gen_store_gpr(v1_t, ret);
            break;
        }
        break;
    case NM_PRECR_SRA_R_PH_W:
        check_dsp_r2(ctx);
        switch (extract32(ctx->opcode, 10, 1)) {
        case 0:
            /* PRECR_SRA_PH_W */
            {
                TCGv_i32 sa_t = tcg_const_i32(rd);
                gen_helper_precr_sra_ph_w(v1_t, sa_t, v1_t,
                                          cpu_gpr[rt]);
                gen_store_gpr(v1_t, rt);
                tcg_temp_free_i32(sa_t);
            }
            break;
        case 1:
            /* PRECR_SRA_R_PH_W */
            {
                TCGv_i32 sa_t = tcg_const_i32(rd);
                gen_helper_precr_sra_r_ph_w(v1_t, sa_t, v1_t,
                                            cpu_gpr[rt]);
                gen_store_gpr(v1_t, rt);
                tcg_temp_free_i32(sa_t);
            }
            break;
       }
        break;
    case NM_MULEU_S_PH_QBL:
        check_dsp(ctx);
        gen_helper_muleu_s_ph_qbl(v1_t, v1_t, v2_t, cpu_env);
        gen_store_gpr(v1_t, ret);
        break;
    case NM_MULEU_S_PH_QBR:
        check_dsp(ctx);
        gen_helper_muleu_s_ph_qbr(v1_t, v1_t, v2_t, cpu_env);
        gen_store_gpr(v1_t, ret);
        break;
    case NM_MULQ_RS_PH:
        check_dsp(ctx);
        gen_helper_mulq_rs_ph(v1_t, v1_t, v2_t, cpu_env);
        gen_store_gpr(v1_t, ret);
        break;
    case NM_MULQ_S_PH:
        check_dsp_r2(ctx);
        gen_helper_mulq_s_ph(v1_t, v1_t, v2_t, cpu_env);
        gen_store_gpr(v1_t, ret);
        break;
    case NM_MULQ_RS_W:
        check_dsp_r2(ctx);
        gen_helper_mulq_rs_w(v1_t, v1_t, v2_t, cpu_env);
        gen_store_gpr(v1_t, ret);
        break;
    case NM_MULQ_S_W:
        check_dsp_r2(ctx);
        gen_helper_mulq_s_w(v1_t, v1_t, v2_t, cpu_env);
        gen_store_gpr(v1_t, ret);
        break;
    case NM_APPEND:
        check_dsp_r2(ctx);
        gen_load_gpr(t0, rs);
        if (rd != 0) {
            tcg_gen_deposit_tl(cpu_gpr[rt], t0, cpu_gpr[rt], rd, 32 - rd);
        }
        tcg_gen_ext32s_tl(cpu_gpr[rt], cpu_gpr[rt]);
        break;
    case NM_MODSUB:
        check_dsp(ctx);
        gen_helper_modsub(v1_t, v1_t, v2_t);
        gen_store_gpr(v1_t, ret);
        break;
    case NM_SHRAV_R_W:
        check_dsp(ctx);
        gen_helper_shra_r_w(v1_t, v1_t, v2_t);
        gen_store_gpr(v1_t, ret);
        break;
    case NM_SHRLV_PH:
        check_dsp_r2(ctx);
        gen_helper_shrl_ph(v1_t, v1_t, v2_t);
        gen_store_gpr(v1_t, ret);
        break;
    case NM_SHRLV_QB:
        check_dsp(ctx);
        gen_helper_shrl_qb(v1_t, v1_t, v2_t);
        gen_store_gpr(v1_t, ret);
        break;
    case NM_SHLLV_QB:
        check_dsp(ctx);
        gen_helper_shll_qb(v1_t, v1_t, v2_t, cpu_env);
        gen_store_gpr(v1_t, ret);
        break;
    case NM_SHLLV_S_W:
        check_dsp(ctx);
        gen_helper_shll_s_w(v1_t, v1_t, v2_t, cpu_env);
        gen_store_gpr(v1_t, ret);
        break;
    case NM_SHILO:
        check_dsp(ctx);
        {
            TCGv tv0 = tcg_temp_new();
            TCGv tv1 = tcg_temp_new();
            int16_t imm = extract32(ctx->opcode, 16, 7);

            tcg_gen_movi_tl(tv0, rd >> 3);
            tcg_gen_movi_tl(tv1, imm);
            gen_helper_shilo(tv0, tv1, cpu_env);
            tcg_temp_free(tv1);
            tcg_temp_free(tv0);
        }
        break;
    case NM_MULEQ_S_W_PHL:
        check_dsp(ctx);
        gen_helper_muleq_s_w_phl(v1_t, v1_t, v2_t, cpu_env);
        gen_store_gpr(v1_t, ret);
        break;
    case NM_MULEQ_S_W_PHR:
        check_dsp(ctx);
        gen_helper_muleq_s_w_phr(v1_t, v1_t, v2_t, cpu_env);
        gen_store_gpr(v1_t, ret);
        break;
    case NM_MUL_S_PH:
        check_dsp_r2(ctx);
        switch (extract32(ctx->opcode, 10, 1)) {
        case 0:
            /* MUL_PH */
            gen_helper_mul_ph(v1_t, v1_t, v2_t, cpu_env);
            gen_store_gpr(v1_t, ret);
            break;
        case 1:
            /* MUL_S_PH */
            gen_helper_mul_s_ph(v1_t, v1_t, v2_t, cpu_env);
            gen_store_gpr(v1_t, ret);
            break;
        }
        break;
    case NM_PRECR_QB_PH:
        check_dsp_r2(ctx);
        gen_helper_precr_qb_ph(v1_t, v1_t, v2_t);
        gen_store_gpr(v1_t, ret);
        break;
    case NM_PRECRQ_QB_PH:
        check_dsp(ctx);
        gen_helper_precrq_qb_ph(v1_t, v1_t, v2_t);
        gen_store_gpr(v1_t, ret);
        break;
    case NM_PRECRQ_PH_W:
        check_dsp(ctx);
        gen_helper_precrq_ph_w(v1_t, v1_t, v2_t);
        gen_store_gpr(v1_t, ret);
        break;
    case NM_PRECRQ_RS_PH_W:
        check_dsp(ctx);
        gen_helper_precrq_rs_ph_w(v1_t, v1_t, v2_t, cpu_env);
        gen_store_gpr(v1_t, ret);
        break;
    case NM_PRECRQU_S_QB_PH:
        check_dsp(ctx);
        gen_helper_precrqu_s_qb_ph(v1_t, v1_t, v2_t, cpu_env);
        gen_store_gpr(v1_t, ret);
        break;
    case NM_SHRA_R_W:
        check_dsp(ctx);
        tcg_gen_movi_tl(t0, rd);
        gen_helper_shra_r_w(v1_t, t0, v1_t);
        gen_store_gpr(v1_t, rt);
        break;
    case NM_SHRA_R_PH:
        check_dsp(ctx);
        tcg_gen_movi_tl(t0, rd >> 1);
        switch (extract32(ctx->opcode, 10, 1)) {
        case 0:
            /* SHRA_PH */
            gen_helper_shra_ph(v1_t, t0, v1_t);
            gen_store_gpr(v1_t, rt);
            break;
        case 1:
            /* SHRA_R_PH */
            gen_helper_shra_r_ph(v1_t, t0, v1_t);
            gen_store_gpr(v1_t, rt);
            break;
        }
        break;
    case NM_SHLL_S_PH:
        check_dsp(ctx);
        tcg_gen_movi_tl(t0, rd >> 1);
        switch (extract32(ctx->opcode, 10, 2)) {
        case 0:
            /* SHLL_PH */
            gen_helper_shll_ph(v1_t, t0, v1_t, cpu_env);
            gen_store_gpr(v1_t, rt);
            break;
        case 2:
            /* SHLL_S_PH */
            gen_helper_shll_s_ph(v1_t, t0, v1_t, cpu_env);
            gen_store_gpr(v1_t, rt);
            break;
        default:
            gen_reserved_instruction(ctx);
            break;
        }
        break;
    case NM_SHLL_S_W:
        check_dsp(ctx);
        tcg_gen_movi_tl(t0, rd);
        gen_helper_shll_s_w(v1_t, t0, v1_t, cpu_env);
        gen_store_gpr(v1_t, rt);
        break;
    case NM_REPL_PH:
        check_dsp(ctx);
        {
            int16_t imm;
            imm = sextract32(ctx->opcode, 11, 11);
            imm = (int16_t)(imm << 6) >> 6;
            if (rt != 0) {
                tcg_gen_movi_tl(cpu_gpr[rt], dup_const(MO_16, imm));
            }
        }
        break;
    default:
        gen_reserved_instruction(ctx);
        break;
    }

    tcg_temp_free(v2_t);
    tcg_temp_free(v1_t);
    tcg_temp_free(t0);
}

static int decode_nanomips_32_48_opc(CPUMIPSState *env, DisasContext *ctx)
{
    uint16_t insn;
    uint32_t op;
    int rt, rs, rd;
    int offset;
    int imm;

    insn = translator_lduw(env, &ctx->base, ctx->base.pc_next + 2);
    ctx->opcode = (ctx->opcode << 16) | insn;

    rt = extract32(ctx->opcode, 21, 5);
    rs = extract32(ctx->opcode, 16, 5);
    rd = extract32(ctx->opcode, 11, 5);

    op = extract32(ctx->opcode, 26, 6);
    switch (op) {
    case NM_P_ADDIU:
        if (rt == 0) {
            /* P.RI */
            switch (extract32(ctx->opcode, 19, 2)) {
            case NM_SIGRIE:
            default:
                gen_reserved_instruction(ctx);
                break;
            case NM_P_SYSCALL:
                if ((extract32(ctx->opcode, 18, 1)) == NM_SYSCALL) {
                    generate_exception_end(ctx, EXCP_SYSCALL);
                } else {
                    gen_reserved_instruction(ctx);
                }
                break;
            case NM_BREAK:
                generate_exception_end(ctx, EXCP_BREAK);
                break;
            case NM_SDBBP:
                if (is_uhi(extract32(ctx->opcode, 0, 19))) {
                    gen_helper_do_semihosting(cpu_env);
                } else {
                    if (ctx->hflags & MIPS_HFLAG_SBRI) {
                        gen_reserved_instruction(ctx);
                    } else {
                        generate_exception_end(ctx, EXCP_DBp);
                    }
                }
                break;
            }
        } else {
            /* NM_ADDIU */
            imm = extract32(ctx->opcode, 0, 16);
            if (rs != 0) {
                tcg_gen_addi_tl(cpu_gpr[rt], cpu_gpr[rs], imm);
            } else {
                tcg_gen_movi_tl(cpu_gpr[rt], imm);
            }
            tcg_gen_ext32s_tl(cpu_gpr[rt], cpu_gpr[rt]);
        }
        break;
    case NM_ADDIUPC:
        if (rt != 0) {
            offset = sextract32(ctx->opcode, 0, 1) << 21 |
                     extract32(ctx->opcode, 1, 20) << 1;
            target_long addr = addr_add(ctx, ctx->base.pc_next + 4, offset);
            tcg_gen_movi_tl(cpu_gpr[rt], addr);
        }
        break;
    case NM_POOL32A:
        switch (ctx->opcode & 0x07) {
        case NM_POOL32A0:
            gen_pool32a0_nanomips_insn(env, ctx);
            break;
        case NM_POOL32A5:
            {
                int32_t op1 = extract32(ctx->opcode, 3, 7);
                gen_pool32a5_nanomips_insn(ctx, op1, rd, rs, rt);
            }
            break;
        case NM_POOL32A7:
            switch (extract32(ctx->opcode, 3, 3)) {
            case NM_P_LSX:
                gen_p_lsx(ctx, rd, rs, rt);
                break;
            case NM_LSA:
                /*
                 * In nanoMIPS, the shift field directly encodes the shift
                 * amount, meaning that the supported shift values are in
                 * the range 0 to 3 (instead of 1 to 4 in MIPSR6).
                 */
                gen_lsa(ctx, rd, rt, rs, extract32(ctx->opcode, 9, 2) - 1);
                break;
            case NM_EXTW:
                gen_ext(ctx, 32, rd, rs, rt, extract32(ctx->opcode, 6, 5));
                break;
            case NM_POOL32AXF:
                gen_pool32axf_nanomips_insn(env, ctx);
                break;
            default:
                gen_reserved_instruction(ctx);
                break;
            }
            break;
        default:
            gen_reserved_instruction(ctx);
            break;
        }
        break;
    case NM_P_GP_W:
        switch (ctx->opcode & 0x03) {
        case NM_ADDIUGP_W:
            if (rt != 0) {
                offset = extract32(ctx->opcode, 0, 21);
                gen_op_addr_addi(ctx, cpu_gpr[rt], cpu_gpr[28], offset);
            }
            break;
        case NM_LWGP:
            gen_ld(ctx, OPC_LW, rt, 28, extract32(ctx->opcode, 2, 19) << 2);
            break;
        case NM_SWGP:
            gen_st(ctx, OPC_SW, rt, 28, extract32(ctx->opcode, 2, 19) << 2);
            break;
        default:
            gen_reserved_instruction(ctx);
            break;
        }
        break;
    case NM_P48I:
        {
            insn = translator_lduw(env, &ctx->base, ctx->base.pc_next + 4);
            target_long addr_off = extract32(ctx->opcode, 0, 16) | insn << 16;
            switch (extract32(ctx->opcode, 16, 5)) {
            case NM_LI48:
                check_nms(ctx);
                if (rt != 0) {
                    tcg_gen_movi_tl(cpu_gpr[rt], addr_off);
                }
                break;
            case NM_ADDIU48:
                check_nms(ctx);
                if (rt != 0) {
                    tcg_gen_addi_tl(cpu_gpr[rt], cpu_gpr[rt], addr_off);
                    tcg_gen_ext32s_tl(cpu_gpr[rt], cpu_gpr[rt]);
                }
                break;
            case NM_ADDIUGP48:
                check_nms(ctx);
                if (rt != 0) {
                    gen_op_addr_addi(ctx, cpu_gpr[rt], cpu_gpr[28], addr_off);
                }
                break;
            case NM_ADDIUPC48:
                check_nms(ctx);
                if (rt != 0) {
                    target_long addr = addr_add(ctx, ctx->base.pc_next + 6,
                                                addr_off);

                    tcg_gen_movi_tl(cpu_gpr[rt], addr);
                }
                break;
            case NM_LWPC48:
                check_nms(ctx);
                if (rt != 0) {
                    TCGv t0;
                    t0 = tcg_temp_new();

                    target_long addr = addr_add(ctx, ctx->base.pc_next + 6,
                                                addr_off);

                    tcg_gen_movi_tl(t0, addr);
                    tcg_gen_qemu_ld_tl(cpu_gpr[rt], t0, ctx->mem_idx, MO_TESL);
                    tcg_temp_free(t0);
                }
                break;
            case NM_SWPC48:
                check_nms(ctx);
                {
                    TCGv t0, t1;
                    t0 = tcg_temp_new();
                    t1 = tcg_temp_new();

                    target_long addr = addr_add(ctx, ctx->base.pc_next + 6,
                                                addr_off);

                    tcg_gen_movi_tl(t0, addr);
                    gen_load_gpr(t1, rt);

                    tcg_gen_qemu_st_tl(t1, t0, ctx->mem_idx, MO_TEUL);

                    tcg_temp_free(t0);
                    tcg_temp_free(t1);
                }
                break;
            default:
                gen_reserved_instruction(ctx);
                break;
            }
            return 6;
        }
    case NM_P_U12:
        switch (extract32(ctx->opcode, 12, 4)) {
        case NM_ORI:
            gen_logic_imm(ctx, OPC_ORI, rt, rs, extract32(ctx->opcode, 0, 12));
            break;
        case NM_XORI:
            gen_logic_imm(ctx, OPC_XORI, rt, rs, extract32(ctx->opcode, 0, 12));
            break;
        case NM_ANDI:
            gen_logic_imm(ctx, OPC_ANDI, rt, rs, extract32(ctx->opcode, 0, 12));
            break;
        case NM_P_SR:
            switch (extract32(ctx->opcode, 20, 1)) {
            case NM_PP_SR:
                switch (ctx->opcode & 3) {
                case NM_SAVE:
                    gen_save(ctx, rt, extract32(ctx->opcode, 16, 4),
                             extract32(ctx->opcode, 2, 1),
                             extract32(ctx->opcode, 3, 9) << 3);
                    break;
                case NM_RESTORE:
                case NM_RESTORE_JRC:
                    gen_restore(ctx, rt, extract32(ctx->opcode, 16, 4),
                                extract32(ctx->opcode, 2, 1),
                                extract32(ctx->opcode, 3, 9) << 3);
                    if ((ctx->opcode & 3) == NM_RESTORE_JRC) {
                        gen_compute_branch_nm(ctx, OPC_JR, 2, 31, 0, 0);
                    }
                    break;
                default:
                    gen_reserved_instruction(ctx);
                    break;
                }
                break;
            case NM_P_SR_F:
                gen_reserved_instruction(ctx);
                break;
            }
            break;
        case NM_SLTI:
            gen_slt_imm(ctx, OPC_SLTI, rt, rs, extract32(ctx->opcode, 0, 12));
            break;
        case NM_SLTIU:
            gen_slt_imm(ctx, OPC_SLTIU, rt, rs, extract32(ctx->opcode, 0, 12));
            break;
        case NM_SEQI:
            {
                TCGv t0 = tcg_temp_new();

                imm = extract32(ctx->opcode, 0, 12);
                gen_load_gpr(t0, rs);
                tcg_gen_setcondi_tl(TCG_COND_EQ, t0, t0, imm);
                gen_store_gpr(t0, rt);

                tcg_temp_free(t0);
            }
            break;
        case NM_ADDIUNEG:
            imm = (int16_t) extract32(ctx->opcode, 0, 12);
            gen_arith_imm(ctx, OPC_ADDIU, rt, rs, -imm);
            break;
        case NM_P_SHIFT:
            {
                int shift = extract32(ctx->opcode, 0, 5);
                switch (extract32(ctx->opcode, 5, 4)) {
                case NM_P_SLL:
                    if (rt == 0 && shift == 0) {
                        /* NOP */
                    } else if (rt == 0 && shift == 3) {
                        /* EHB - treat as NOP */
                    } else if (rt == 0 && shift == 5) {
                        /* PAUSE - treat as NOP */
                    } else if (rt == 0 && shift == 6) {
                        /* SYNC */
                        gen_sync(extract32(ctx->opcode, 16, 5));
                    } else {
                        /* SLL */
                        gen_shift_imm(ctx, OPC_SLL, rt, rs,
                                      extract32(ctx->opcode, 0, 5));
                    }
                    break;
                case NM_SRL:
                    gen_shift_imm(ctx, OPC_SRL, rt, rs,
                                  extract32(ctx->opcode, 0, 5));
                    break;
                case NM_SRA:
                    gen_shift_imm(ctx, OPC_SRA, rt, rs,
                                  extract32(ctx->opcode, 0, 5));
                    break;
                case NM_ROTR:
                    gen_shift_imm(ctx, OPC_ROTR, rt, rs,
                                  extract32(ctx->opcode, 0, 5));
                    break;
                }
            }
            break;
        case NM_P_ROTX:
            check_nms(ctx);
            if (rt != 0) {
                TCGv t0 = tcg_temp_new();
                TCGv_i32 shift = tcg_const_i32(extract32(ctx->opcode, 0, 5));
                TCGv_i32 shiftx = tcg_const_i32(extract32(ctx->opcode, 7, 4)
                                                << 1);
                TCGv_i32 stripe = tcg_const_i32(extract32(ctx->opcode, 6, 1));

                gen_load_gpr(t0, rs);
                gen_helper_rotx(cpu_gpr[rt], t0, shift, shiftx, stripe);
                tcg_temp_free(t0);

                tcg_temp_free_i32(shift);
                tcg_temp_free_i32(shiftx);
                tcg_temp_free_i32(stripe);
            }
            break;
        case NM_P_INS:
            switch (((ctx->opcode >> 10) & 2) |
                    (extract32(ctx->opcode, 5, 1))) {
            case NM_INS:
                check_nms(ctx);
                gen_bitops(ctx, OPC_INS, rt, rs, extract32(ctx->opcode, 0, 5),
                           extract32(ctx->opcode, 6, 5));
                break;
            default:
                gen_reserved_instruction(ctx);
                break;
            }
            break;
        case NM_P_EXT:
            switch (((ctx->opcode >> 10) & 2) |
                    (extract32(ctx->opcode, 5, 1))) {
            case NM_EXT:
                check_nms(ctx);
                gen_bitops(ctx, OPC_EXT, rt, rs, extract32(ctx->opcode, 0, 5),
                           extract32(ctx->opcode, 6, 5));
                break;
            default:
                gen_reserved_instruction(ctx);
                break;
            }
            break;
        default:
            gen_reserved_instruction(ctx);
            break;
        }
        break;
    case NM_POOL32F:
        gen_pool32f_nanomips_insn(ctx);
        break;
    case NM_POOL32S:
        break;
    case NM_P_LUI:
        switch (extract32(ctx->opcode, 1, 1)) {
        case NM_LUI:
            if (rt != 0) {
                tcg_gen_movi_tl(cpu_gpr[rt],
                                sextract32(ctx->opcode, 0, 1) << 31 |
                                extract32(ctx->opcode, 2, 10) << 21 |
                                extract32(ctx->opcode, 12, 9) << 12);
            }
            break;
        case NM_ALUIPC:
            if (rt != 0) {
                offset = sextract32(ctx->opcode, 0, 1) << 31 |
                         extract32(ctx->opcode, 2, 10) << 21 |
                         extract32(ctx->opcode, 12, 9) << 12;
                target_long addr;
                addr = ~0xFFF & addr_add(ctx, ctx->base.pc_next + 4, offset);
                tcg_gen_movi_tl(cpu_gpr[rt], addr);
            }
            break;
        }
        break;
    case NM_P_GP_BH:
        {
            uint32_t u = extract32(ctx->opcode, 0, 18);

            switch (extract32(ctx->opcode, 18, 3)) {
            case NM_LBGP:
                gen_ld(ctx, OPC_LB, rt, 28, u);
                break;
            case NM_SBGP:
                gen_st(ctx, OPC_SB, rt, 28, u);
                break;
            case NM_LBUGP:
                gen_ld(ctx, OPC_LBU, rt, 28, u);
                break;
            case NM_ADDIUGP_B:
                if (rt != 0) {
                    gen_op_addr_addi(ctx, cpu_gpr[rt], cpu_gpr[28], u);
                }
                break;
            case NM_P_GP_LH:
                u &= ~1;
                switch (ctx->opcode & 1) {
                case NM_LHGP:
                    gen_ld(ctx, OPC_LH, rt, 28, u);
                    break;
                case NM_LHUGP:
                    gen_ld(ctx, OPC_LHU, rt, 28, u);
                    break;
                }
                break;
            case NM_P_GP_SH:
                u &= ~1;
                switch (ctx->opcode & 1) {
                case NM_SHGP:
                    gen_st(ctx, OPC_SH, rt, 28, u);
                    break;
                default:
                    gen_reserved_instruction(ctx);
                    break;
                }
                break;
            case NM_P_GP_CP1:
                u &= ~0x3;
                switch (ctx->opcode & 0x3) {
                case NM_LWC1GP:
                    gen_cop1_ldst(ctx, OPC_LWC1, rt, 28, u);
                    break;
                case NM_LDC1GP:
                    gen_cop1_ldst(ctx, OPC_LDC1, rt, 28, u);
                    break;
                case NM_SWC1GP:
                    gen_cop1_ldst(ctx, OPC_SWC1, rt, 28, u);
                    break;
                case NM_SDC1GP:
                    gen_cop1_ldst(ctx, OPC_SDC1, rt, 28, u);
                    break;
                }
                break;
            default:
                gen_reserved_instruction(ctx);
                break;
            }
        }
        break;
    case NM_P_LS_U12:
        {
            uint32_t u = extract32(ctx->opcode, 0, 12);

            switch (extract32(ctx->opcode, 12, 4)) {
            case NM_P_PREFU12:
                if (rt == 31) {
                    /* SYNCI */
                    /*
                     * Break the TB to be able to sync copied instructions
                     * immediately.
                     */
                    ctx->base.is_jmp = DISAS_STOP;
                } else {
                    /* PREF */
                    /* Treat as NOP. */
                }
                break;
            case NM_LB:
                gen_ld(ctx, OPC_LB, rt, rs, u);
                break;
            case NM_LH:
                gen_ld(ctx, OPC_LH, rt, rs, u);
                break;
            case NM_LW:
                gen_ld(ctx, OPC_LW, rt, rs, u);
                break;
            case NM_LBU:
                gen_ld(ctx, OPC_LBU, rt, rs, u);
                break;
            case NM_LHU:
                gen_ld(ctx, OPC_LHU, rt, rs, u);
                break;
            case NM_SB:
                gen_st(ctx, OPC_SB, rt, rs, u);
                break;
            case NM_SH:
                gen_st(ctx, OPC_SH, rt, rs, u);
                break;
            case NM_SW:
                gen_st(ctx, OPC_SW, rt, rs, u);
                break;
            case NM_LWC1:
                gen_cop1_ldst(ctx, OPC_LWC1, rt, rs, u);
                break;
            case NM_LDC1:
                gen_cop1_ldst(ctx, OPC_LDC1, rt, rs, u);
                break;
            case NM_SWC1:
                gen_cop1_ldst(ctx, OPC_SWC1, rt, rs, u);
                break;
            case NM_SDC1:
                gen_cop1_ldst(ctx, OPC_SDC1, rt, rs, u);
                break;
            default:
                gen_reserved_instruction(ctx);
                break;
            }
        }
        break;
    case NM_P_LS_S9:
        {
            int32_t s = (sextract32(ctx->opcode, 15, 1) << 8) |
                        extract32(ctx->opcode, 0, 8);

            switch (extract32(ctx->opcode, 8, 3)) {
            case NM_P_LS_S0:
                switch (extract32(ctx->opcode, 11, 4)) {
                case NM_LBS9:
                    gen_ld(ctx, OPC_LB, rt, rs, s);
                    break;
                case NM_LHS9:
                    gen_ld(ctx, OPC_LH, rt, rs, s);
                    break;
                case NM_LWS9:
                    gen_ld(ctx, OPC_LW, rt, rs, s);
                    break;
                case NM_LBUS9:
                    gen_ld(ctx, OPC_LBU, rt, rs, s);
                    break;
                case NM_LHUS9:
                    gen_ld(ctx, OPC_LHU, rt, rs, s);
                    break;
                case NM_SBS9:
                    gen_st(ctx, OPC_SB, rt, rs, s);
                    break;
                case NM_SHS9:
                    gen_st(ctx, OPC_SH, rt, rs, s);
                    break;
                case NM_SWS9:
                    gen_st(ctx, OPC_SW, rt, rs, s);
                    break;
                case NM_LWC1S9:
                    gen_cop1_ldst(ctx, OPC_LWC1, rt, rs, s);
                    break;
                case NM_LDC1S9:
                    gen_cop1_ldst(ctx, OPC_LDC1, rt, rs, s);
                    break;
                case NM_SWC1S9:
                    gen_cop1_ldst(ctx, OPC_SWC1, rt, rs, s);
                    break;
                case NM_SDC1S9:
                    gen_cop1_ldst(ctx, OPC_SDC1, rt, rs, s);
                    break;
                case NM_P_PREFS9:
                    if (rt == 31) {
                        /* SYNCI */
                        /*
                         * Break the TB to be able to sync copied instructions
                         * immediately.
                         */
                        ctx->base.is_jmp = DISAS_STOP;
                    } else {
                        /* PREF */
                        /* Treat as NOP. */
                    }
                    break;
                default:
                    gen_reserved_instruction(ctx);
                    break;
                }
                break;
            case NM_P_LS_S1:
                switch (extract32(ctx->opcode, 11, 4)) {
                case NM_UALH:
                case NM_UASH:
                    check_nms(ctx);
                    {
                        TCGv t0 = tcg_temp_new();
                        TCGv t1 = tcg_temp_new();

                        gen_base_offset_addr(ctx, t0, rs, s);

                        switch (extract32(ctx->opcode, 11, 4)) {
                        case NM_UALH:
                            tcg_gen_qemu_ld_tl(t0, t0, ctx->mem_idx, MO_TESW |
                                               MO_UNALN);
                            gen_store_gpr(t0, rt);
                            break;
                        case NM_UASH:
                            gen_load_gpr(t1, rt);
                            tcg_gen_qemu_st_tl(t1, t0, ctx->mem_idx, MO_TEUW |
                                               MO_UNALN);
                            break;
                        }
                        tcg_temp_free(t0);
                        tcg_temp_free(t1);
                    }
                    break;
                case NM_P_LL:
                    switch (ctx->opcode & 0x03) {
                    case NM_LL:
                        gen_ld(ctx, OPC_LL, rt, rs, s);
                        break;
                    case NM_LLWP:
                        check_xnp(ctx);
                        gen_llwp(ctx, rs, 0, rt, extract32(ctx->opcode, 3, 5));
                        break;
                    }
                    break;
                case NM_P_SC:
                    switch (ctx->opcode & 0x03) {
                    case NM_SC:
                        gen_st_cond(ctx, rt, rs, s, MO_TESL, false);
                        break;
                    case NM_SCWP:
                        check_xnp(ctx);
                        gen_scwp(ctx, rs, 0, rt, extract32(ctx->opcode, 3, 5),
                                 false);
                        break;
                    }
                    break;
                case NM_CACHE:
                    check_cp0_enabled(ctx);
                    if (ctx->hflags & MIPS_HFLAG_ITC_CACHE) {
                        gen_cache_operation(ctx, rt, rs, s);
                    }
                    break;
                }
                break;
            case NM_P_LS_E0:
                switch (extract32(ctx->opcode, 11, 4)) {
                case NM_LBE:
                    check_eva(ctx);
                    check_cp0_enabled(ctx);
                    gen_ld(ctx, OPC_LBE, rt, rs, s);
                    break;
                case NM_SBE:
                    check_eva(ctx);
                    check_cp0_enabled(ctx);
                    gen_st(ctx, OPC_SBE, rt, rs, s);
                    break;
                case NM_LBUE:
                    check_eva(ctx);
                    check_cp0_enabled(ctx);
                    gen_ld(ctx, OPC_LBUE, rt, rs, s);
                    break;
                case NM_P_PREFE:
                    if (rt == 31) {
                        /* case NM_SYNCIE */
                        check_eva(ctx);
                        check_cp0_enabled(ctx);
                        /*
                         * Break the TB to be able to sync copied instructions
                         * immediately.
                         */
                        ctx->base.is_jmp = DISAS_STOP;
                    } else {
                        /* case NM_PREFE */
                        check_eva(ctx);
                        check_cp0_enabled(ctx);
                        /* Treat as NOP. */
                    }
                    break;
                case NM_LHE:
                    check_eva(ctx);
                    check_cp0_enabled(ctx);
                    gen_ld(ctx, OPC_LHE, rt, rs, s);
                    break;
                case NM_SHE:
                    check_eva(ctx);
                    check_cp0_enabled(ctx);
                    gen_st(ctx, OPC_SHE, rt, rs, s);
                    break;
                case NM_LHUE:
                    check_eva(ctx);
                    check_cp0_enabled(ctx);
                    gen_ld(ctx, OPC_LHUE, rt, rs, s);
                    break;
                case NM_CACHEE:
                    check_eva(ctx);
                    check_cp0_enabled(ctx);
                    check_nms_dl_il_sl_tl_l2c(ctx);
                    gen_cache_operation(ctx, rt, rs, s);
                    break;
                case NM_LWE:
                    check_eva(ctx);
                    check_cp0_enabled(ctx);
                    gen_ld(ctx, OPC_LWE, rt, rs, s);
                    break;
                case NM_SWE:
                    check_eva(ctx);
                    check_cp0_enabled(ctx);
                    gen_st(ctx, OPC_SWE, rt, rs, s);
                    break;
                case NM_P_LLE:
                    switch (extract32(ctx->opcode, 2, 2)) {
                    case NM_LLE:
                        check_xnp(ctx);
                        check_eva(ctx);
                        check_cp0_enabled(ctx);
                        gen_ld(ctx, OPC_LLE, rt, rs, s);
                        break;
                    case NM_LLWPE:
                        check_xnp(ctx);
                        check_eva(ctx);
                        check_cp0_enabled(ctx);
                        gen_llwp(ctx, rs, 0, rt, extract32(ctx->opcode, 3, 5));
                        break;
                    default:
                        gen_reserved_instruction(ctx);
                        break;
                    }
                    break;
                case NM_P_SCE:
                    switch (extract32(ctx->opcode, 2, 2)) {
                    case NM_SCE:
                        check_xnp(ctx);
                        check_eva(ctx);
                        check_cp0_enabled(ctx);
                        gen_st_cond(ctx, rt, rs, s, MO_TESL, true);
                        break;
                    case NM_SCWPE:
                        check_xnp(ctx);
                        check_eva(ctx);
                        check_cp0_enabled(ctx);
                        gen_scwp(ctx, rs, 0, rt, extract32(ctx->opcode, 3, 5),
                                 true);
                        break;
                    default:
                        gen_reserved_instruction(ctx);
                        break;
                    }
                    break;
                }
                break;
            case NM_P_LS_WM:
            case NM_P_LS_UAWM:
                check_nms(ctx);
                {
                    int count = extract32(ctx->opcode, 12, 3);
                    int counter = 0;

                    offset = sextract32(ctx->opcode, 15, 1) << 8 |
                             extract32(ctx->opcode, 0, 8);
                    TCGv va = tcg_temp_new();
                    TCGv t1 = tcg_temp_new();
                    MemOp memop = (extract32(ctx->opcode, 8, 3)) ==
                                      NM_P_LS_UAWM ? MO_UNALN : 0;

                    count = (count == 0) ? 8 : count;
                    while (counter != count) {
                        int this_rt = ((rt + counter) & 0x1f) | (rt & 0x10);
                        int this_offset = offset + (counter << 2);

                        gen_base_offset_addr(ctx, va, rs, this_offset);

                        switch (extract32(ctx->opcode, 11, 1)) {
                        case NM_LWM:
                            tcg_gen_qemu_ld_tl(t1, va, ctx->mem_idx,
                                               memop | MO_TESL);
                            gen_store_gpr(t1, this_rt);
                            if ((this_rt == rs) &&
                                (counter != (count - 1))) {
                                /* UNPREDICTABLE */
                            }
                            break;
                        case NM_SWM:
                            this_rt = (rt == 0) ? 0 : this_rt;
                            gen_load_gpr(t1, this_rt);
                            tcg_gen_qemu_st_tl(t1, va, ctx->mem_idx,
                                               memop | MO_TEUL);
                            break;
                        }
                        counter++;
                    }
                    tcg_temp_free(va);
                    tcg_temp_free(t1);
                }
                break;
            default:
                gen_reserved_instruction(ctx);
                break;
            }
        }
        break;
    case NM_MOVE_BALC:
        check_nms(ctx);
        {
            TCGv t0 = tcg_temp_new();
            int32_t s = sextract32(ctx->opcode, 0, 1) << 21 |
                        extract32(ctx->opcode, 1, 20) << 1;
            rd = (extract32(ctx->opcode, 24, 1)) == 0 ? 4 : 5;
            rt = decode_gpr_gpr4_zero(extract32(ctx->opcode, 25, 1) << 3 |
                            extract32(ctx->opcode, 21, 3));
            gen_load_gpr(t0, rt);
            tcg_gen_mov_tl(cpu_gpr[rd], t0);
            gen_compute_branch_nm(ctx, OPC_BGEZAL, 4, 0, 0, s);
            tcg_temp_free(t0);
        }
        break;
    case NM_P_BAL:
        {
            int32_t s = sextract32(ctx->opcode, 0, 1) << 25 |
                        extract32(ctx->opcode, 1, 24) << 1;

            if ((extract32(ctx->opcode, 25, 1)) == 0) {
                /* BC */
                gen_compute_branch_nm(ctx, OPC_BEQ, 4, 0, 0, s);
            } else {
                /* BALC */
                gen_compute_branch_nm(ctx, OPC_BGEZAL, 4, 0, 0, s);
            }
        }
        break;
    case NM_P_J:
        switch (extract32(ctx->opcode, 12, 4)) {
        case NM_JALRC:
        case NM_JALRC_HB:
            gen_compute_branch_nm(ctx, OPC_JALR, 4, rs, rt, 0);
            break;
        case NM_P_BALRSC:
            gen_compute_nanomips_pbalrsc_branch(ctx, rs, rt);
            break;
        default:
            gen_reserved_instruction(ctx);
            break;
        }
        break;
    case NM_P_BR1:
        {
            int32_t s = sextract32(ctx->opcode, 0, 1) << 14 |
                        extract32(ctx->opcode, 1, 13) << 1;
            switch (extract32(ctx->opcode, 14, 2)) {
            case NM_BEQC:
                check_nms(ctx);
                gen_compute_branch_nm(ctx, OPC_BEQ, 4, rs, rt, s);
                break;
            case NM_P_BR3A:
                s = sextract32(ctx->opcode, 0, 1) << 14 |
                    extract32(ctx->opcode, 1, 13) << 1;
                check_cp1_enabled(ctx);
                switch (extract32(ctx->opcode, 16, 5)) {
                case NM_BC1EQZC:
                    gen_compute_branch_cp1_nm(ctx, OPC_BC1EQZ, rt, s);
                    break;
                case NM_BC1NEZC:
                    gen_compute_branch_cp1_nm(ctx, OPC_BC1NEZ, rt, s);
                    break;
                case NM_BPOSGE32C:
                    check_dsp_r3(ctx);
                    {
                        int32_t imm = extract32(ctx->opcode, 1, 13) |
                                      extract32(ctx->opcode, 0, 1) << 13;

                        gen_compute_branch_nm(ctx, OPC_BPOSGE32, 4, -1, -2,
                                              imm << 1);
                    }
                    break;
                default:
                    gen_reserved_instruction(ctx);
                    break;
                }
                break;
            case NM_BGEC:
                if (rs == rt) {
                    gen_compute_compact_branch_nm(ctx, OPC_BC, rs, rt, s);
                } else {
                    gen_compute_compact_branch_nm(ctx, OPC_BGEC, rs, rt, s);
                }
                break;
            case NM_BGEUC:
                if (rs == rt || rt == 0) {
                    gen_compute_compact_branch_nm(ctx, OPC_BC, 0, 0, s);
                } else if (rs == 0) {
                    gen_compute_compact_branch_nm(ctx, OPC_BEQZC, rt, 0, s);
                } else {
                    gen_compute_compact_branch_nm(ctx, OPC_BGEUC, rs, rt, s);
                }
                break;
            }
        }
        break;
    case NM_P_BR2:
        {
            int32_t s = sextract32(ctx->opcode, 0, 1) << 14 |
                        extract32(ctx->opcode, 1, 13) << 1;
            switch (extract32(ctx->opcode, 14, 2)) {
            case NM_BNEC:
                check_nms(ctx);
                gen_compute_branch_nm(ctx, OPC_BNE, 4, rs, rt, s);
                break;
            case NM_BLTC:
                if (rs != 0 && rt != 0 && rs == rt) {
                    /* NOP */
                    ctx->hflags |= MIPS_HFLAG_FBNSLOT;
                } else {
                    gen_compute_compact_branch_nm(ctx, OPC_BLTC, rs, rt, s);
                }
                break;
            case NM_BLTUC:
                if (rs == 0 || rs == rt) {
                    /* NOP */
                    ctx->hflags |= MIPS_HFLAG_FBNSLOT;
                } else {
                    gen_compute_compact_branch_nm(ctx, OPC_BLTUC, rs, rt, s);
                }
                break;
            default:
                gen_reserved_instruction(ctx);
                break;
            }
        }
        break;
    case NM_P_BRI:
        {
            int32_t s = sextract32(ctx->opcode, 0, 1) << 11 |
                        extract32(ctx->opcode, 1, 10) << 1;
            uint32_t u = extract32(ctx->opcode, 11, 7);

            gen_compute_imm_branch(ctx, extract32(ctx->opcode, 18, 3),
                                   rt, u, s);
        }
        break;
    default:
        gen_reserved_instruction(ctx);
        break;
    }
    return 4;
}

static int decode_isa_nanomips(CPUMIPSState *env, DisasContext *ctx)
{
    uint32_t op;
    int rt = decode_gpr_gpr3(NANOMIPS_EXTRACT_RT3(ctx->opcode));
    int rs = decode_gpr_gpr3(NANOMIPS_EXTRACT_RS3(ctx->opcode));
    int rd = decode_gpr_gpr3(NANOMIPS_EXTRACT_RD3(ctx->opcode));
    int offset;
    int imm;

    /* make sure instructions are on a halfword boundary */
    if (ctx->base.pc_next & 0x1) {
        TCGv tmp = tcg_const_tl(ctx->base.pc_next);
        tcg_gen_st_tl(tmp, cpu_env, offsetof(CPUMIPSState, CP0_BadVAddr));
        tcg_temp_free(tmp);
        generate_exception_end(ctx, EXCP_AdEL);
        return 2;
    }

    op = extract32(ctx->opcode, 10, 6);
    switch (op) {
    case NM_P16_MV:
        rt = NANOMIPS_EXTRACT_RD5(ctx->opcode);
        if (rt != 0) {
            /* MOVE */
            rs = NANOMIPS_EXTRACT_RS5(ctx->opcode);
            gen_arith(ctx, OPC_ADDU, rt, rs, 0);
        } else {
            /* P16.RI */
            switch (extract32(ctx->opcode, 3, 2)) {
            case NM_P16_SYSCALL:
                if (extract32(ctx->opcode, 2, 1) == 0) {
                    generate_exception_end(ctx, EXCP_SYSCALL);
                } else {
                    gen_reserved_instruction(ctx);
                }
                break;
            case NM_BREAK16:
                generate_exception_end(ctx, EXCP_BREAK);
                break;
            case NM_SDBBP16:
                if (is_uhi(extract32(ctx->opcode, 0, 3))) {
                    gen_helper_do_semihosting(cpu_env);
                } else {
                    if (ctx->hflags & MIPS_HFLAG_SBRI) {
                        gen_reserved_instruction(ctx);
                    } else {
                        generate_exception_end(ctx, EXCP_DBp);
                    }
                }
                break;
            default:
                gen_reserved_instruction(ctx);
                break;
            }
        }
        break;
    case NM_P16_SHIFT:
        {
            int shift = extract32(ctx->opcode, 0, 3);
            uint32_t opc = 0;
            shift = (shift == 0) ? 8 : shift;

            switch (extract32(ctx->opcode, 3, 1)) {
            case NM_SLL16:
                opc = OPC_SLL;
                break;
            case NM_SRL16:
                opc = OPC_SRL;
                break;
            }
            gen_shift_imm(ctx, opc, rt, rs, shift);
        }
        break;
    case NM_P16C:
        switch (ctx->opcode & 1) {
        case NM_POOL16C_0:
            gen_pool16c_nanomips_insn(ctx);
            break;
        case NM_LWXS16:
            gen_ldxs(ctx, rt, rs, rd);
            break;
        }
        break;
    case NM_P16_A1:
        switch (extract32(ctx->opcode, 6, 1)) {
        case NM_ADDIUR1SP:
            imm = extract32(ctx->opcode, 0, 6) << 2;
            gen_arith_imm(ctx, OPC_ADDIU, rt, 29, imm);
            break;
        default:
            gen_reserved_instruction(ctx);
            break;
        }
        break;
    case NM_P16_A2:
        switch (extract32(ctx->opcode, 3, 1)) {
        case NM_ADDIUR2:
            imm = extract32(ctx->opcode, 0, 3) << 2;
            gen_arith_imm(ctx, OPC_ADDIU, rt, rs, imm);
            break;
        case NM_P_ADDIURS5:
            rt = extract32(ctx->opcode, 5, 5);
            if (rt != 0) {
                /* imm = sign_extend(s[3] . s[2:0] , from_nbits = 4) */
                imm = (sextract32(ctx->opcode, 4, 1) << 3) |
                      (extract32(ctx->opcode, 0, 3));
                gen_arith_imm(ctx, OPC_ADDIU, rt, rt, imm);
            }
            break;
        }
        break;
    case NM_P16_ADDU:
        switch (ctx->opcode & 0x1) {
        case NM_ADDU16:
            gen_arith(ctx, OPC_ADDU, rd, rs, rt);
            break;
        case NM_SUBU16:
            gen_arith(ctx, OPC_SUBU, rd, rs, rt);
            break;
        }
        break;
    case NM_P16_4X4:
        rt = (extract32(ctx->opcode, 9, 1) << 3) |
              extract32(ctx->opcode, 5, 3);
        rs = (extract32(ctx->opcode, 4, 1) << 3) |
              extract32(ctx->opcode, 0, 3);
        rt = decode_gpr_gpr4(rt);
        rs = decode_gpr_gpr4(rs);
        switch ((extract32(ctx->opcode, 7, 2) & 0x2) |
                (extract32(ctx->opcode, 3, 1))) {
        case NM_ADDU4X4:
            check_nms(ctx);
            gen_arith(ctx, OPC_ADDU, rt, rs, rt);
            break;
        case NM_MUL4X4:
            check_nms(ctx);
            gen_r6_muldiv(ctx, R6_OPC_MUL, rt, rs, rt);
            break;
        default:
            gen_reserved_instruction(ctx);
            break;
        }
        break;
    case NM_LI16:
        {
            int imm = extract32(ctx->opcode, 0, 7);
            imm = (imm == 0x7f ? -1 : imm);
            if (rt != 0) {
                tcg_gen_movi_tl(cpu_gpr[rt], imm);
            }
        }
        break;
    case NM_ANDI16:
        {
            uint32_t u = extract32(ctx->opcode, 0, 4);
            u = (u == 12) ? 0xff :
                (u == 13) ? 0xffff : u;
            gen_logic_imm(ctx, OPC_ANDI, rt, rs, u);
        }
        break;
    case NM_P16_LB:
        offset = extract32(ctx->opcode, 0, 2);
        switch (extract32(ctx->opcode, 2, 2)) {
        case NM_LB16:
            gen_ld(ctx, OPC_LB, rt, rs, offset);
            break;
        case NM_SB16:
            rt = decode_gpr_gpr3_src_store(
                     NANOMIPS_EXTRACT_RT3(ctx->opcode));
            gen_st(ctx, OPC_SB, rt, rs, offset);
            break;
        case NM_LBU16:
            gen_ld(ctx, OPC_LBU, rt, rs, offset);
            break;
        default:
            gen_reserved_instruction(ctx);
            break;
        }
        break;
    case NM_P16_LH:
        offset = extract32(ctx->opcode, 1, 2) << 1;
        switch ((extract32(ctx->opcode, 3, 1) << 1) | (ctx->opcode & 1)) {
        case NM_LH16:
            gen_ld(ctx, OPC_LH, rt, rs, offset);
            break;
        case NM_SH16:
            rt = decode_gpr_gpr3_src_store(
                     NANOMIPS_EXTRACT_RT3(ctx->opcode));
            gen_st(ctx, OPC_SH, rt, rs, offset);
            break;
        case NM_LHU16:
            gen_ld(ctx, OPC_LHU, rt, rs, offset);
            break;
        default:
            gen_reserved_instruction(ctx);
            break;
        }
        break;
    case NM_LW16:
        offset = extract32(ctx->opcode, 0, 4) << 2;
        gen_ld(ctx, OPC_LW, rt, rs, offset);
        break;
    case NM_LWSP16:
        rt = NANOMIPS_EXTRACT_RD5(ctx->opcode);
        offset = extract32(ctx->opcode, 0, 5) << 2;
        gen_ld(ctx, OPC_LW, rt, 29, offset);
        break;
    case NM_LW4X4:
        check_nms(ctx);
        rt = (extract32(ctx->opcode, 9, 1) << 3) |
             extract32(ctx->opcode, 5, 3);
        rs = (extract32(ctx->opcode, 4, 1) << 3) |
             extract32(ctx->opcode, 0, 3);
        offset = (extract32(ctx->opcode, 3, 1) << 3) |
                 (extract32(ctx->opcode, 8, 1) << 2);
        rt = decode_gpr_gpr4(rt);
        rs = decode_gpr_gpr4(rs);
        gen_ld(ctx, OPC_LW, rt, rs, offset);
        break;
    case NM_SW4X4:
        check_nms(ctx);
        rt = (extract32(ctx->opcode, 9, 1) << 3) |
             extract32(ctx->opcode, 5, 3);
        rs = (extract32(ctx->opcode, 4, 1) << 3) |
             extract32(ctx->opcode, 0, 3);
        offset = (extract32(ctx->opcode, 3, 1) << 3) |
                 (extract32(ctx->opcode, 8, 1) << 2);
        rt = decode_gpr_gpr4_zero(rt);
        rs = decode_gpr_gpr4(rs);
        gen_st(ctx, OPC_SW, rt, rs, offset);
        break;
    case NM_LWGP16:
        offset = extract32(ctx->opcode, 0, 7) << 2;
        gen_ld(ctx, OPC_LW, rt, 28, offset);
        break;
    case NM_SWSP16:
        rt = NANOMIPS_EXTRACT_RD5(ctx->opcode);
        offset = extract32(ctx->opcode, 0, 5) << 2;
        gen_st(ctx, OPC_SW, rt, 29, offset);
        break;
    case NM_SW16:
        rt = decode_gpr_gpr3_src_store(
                 NANOMIPS_EXTRACT_RT3(ctx->opcode));
        rs = decode_gpr_gpr3(NANOMIPS_EXTRACT_RS3(ctx->opcode));
        offset = extract32(ctx->opcode, 0, 4) << 2;
        gen_st(ctx, OPC_SW, rt, rs, offset);
        break;
    case NM_SWGP16:
        rt = decode_gpr_gpr3_src_store(
                 NANOMIPS_EXTRACT_RT3(ctx->opcode));
        offset = extract32(ctx->opcode, 0, 7) << 2;
        gen_st(ctx, OPC_SW, rt, 28, offset);
        break;
    case NM_BC16:
        gen_compute_branch_nm(ctx, OPC_BEQ, 2, 0, 0,
                              (sextract32(ctx->opcode, 0, 1) << 10) |
                              (extract32(ctx->opcode, 1, 9) << 1));
        break;
    case NM_BALC16:
        gen_compute_branch_nm(ctx, OPC_BGEZAL, 2, 0, 0,
                              (sextract32(ctx->opcode, 0, 1) << 10) |
                              (extract32(ctx->opcode, 1, 9) << 1));
        break;
    case NM_BEQZC16:
        gen_compute_branch_nm(ctx, OPC_BEQ, 2, rt, 0,
                              (sextract32(ctx->opcode, 0, 1) << 7) |
                              (extract32(ctx->opcode, 1, 6) << 1));
        break;
    case NM_BNEZC16:
        gen_compute_branch_nm(ctx, OPC_BNE, 2, rt, 0,
                              (sextract32(ctx->opcode, 0, 1) << 7) |
                              (extract32(ctx->opcode, 1, 6) << 1));
        break;
    case NM_P16_BR:
        switch (ctx->opcode & 0xf) {
        case 0:
            /* P16.JRC */
            switch (extract32(ctx->opcode, 4, 1)) {
            case NM_JRC:
                gen_compute_branch_nm(ctx, OPC_JR, 2,
                                      extract32(ctx->opcode, 5, 5), 0, 0);
                break;
            case NM_JALRC16:
                gen_compute_branch_nm(ctx, OPC_JALR, 2,
                                      extract32(ctx->opcode, 5, 5), 31, 0);
                break;
            }
            break;
        default:
            {
                /* P16.BRI */
                uint32_t opc = extract32(ctx->opcode, 4, 3) <
                               extract32(ctx->opcode, 7, 3) ? OPC_BEQ : OPC_BNE;
                gen_compute_branch_nm(ctx, opc, 2, rs, rt,
                                      extract32(ctx->opcode, 0, 4) << 1);
            }
            break;
        }
        break;
    case NM_P16_SR:
        {
            int count = extract32(ctx->opcode, 0, 4);
            int u = extract32(ctx->opcode, 4, 4) << 4;

            rt = 30 + extract32(ctx->opcode, 9, 1);
            switch (extract32(ctx->opcode, 8, 1)) {
            case NM_SAVE16:
                gen_save(ctx, rt, count, 0, u);
                break;
            case NM_RESTORE_JRC16:
                gen_restore(ctx, rt, count, 0, u);
                gen_compute_branch_nm(ctx, OPC_JR, 2, 31, 0, 0);
                break;
            }
        }
        break;
    case NM_MOVEP:
    case NM_MOVEPREV:
        check_nms(ctx);
        {
            static const int gpr2reg1[] = {4, 5, 6, 7};
            static const int gpr2reg2[] = {5, 6, 7, 8};
            int re;
            int rd2 = extract32(ctx->opcode, 3, 1) << 1 |
                      extract32(ctx->opcode, 8, 1);
            int r1 = gpr2reg1[rd2];
            int r2 = gpr2reg2[rd2];
            int r3 = extract32(ctx->opcode, 4, 1) << 3 |
                     extract32(ctx->opcode, 0, 3);
            int r4 = extract32(ctx->opcode, 9, 1) << 3 |
                     extract32(ctx->opcode, 5, 3);
            TCGv t0 = tcg_temp_new();
            TCGv t1 = tcg_temp_new();
            if (op == NM_MOVEP) {
                rd = r1;
                re = r2;
                rs = decode_gpr_gpr4_zero(r3);
                rt = decode_gpr_gpr4_zero(r4);
            } else {
                rd = decode_gpr_gpr4(r3);
                re = decode_gpr_gpr4(r4);
                rs = r1;
                rt = r2;
            }
            gen_load_gpr(t0, rs);
            gen_load_gpr(t1, rt);
            tcg_gen_mov_tl(cpu_gpr[rd], t0);
            tcg_gen_mov_tl(cpu_gpr[re], t1);
            tcg_temp_free(t0);
            tcg_temp_free(t1);
        }
        break;
    default:
        return decode_nanomips_32_48_opc(env, ctx);
    }

    return 2;
}

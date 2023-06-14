/*
 *  Copyright (c) 2012-2014 Bastian Koppelmann C-Lab/University Paderborn
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef TARGET_TRICORE_TRICORE_OPCODES_H
#define TARGET_TRICORE_TRICORE_OPCODES_H

/*
 * Opcode Masks for Tricore
 * Format MASK_OP_InstrFormatName_Field
 */

/* This creates a mask with bits start .. end set to 1 and applies it to op */
#define MASK_BITS_SHIFT(op, start, end) (extract32(op, (start), \
                                        (end) - (start) + 1))
#define MASK_BITS_SHIFT_SEXT(op, start, end) (sextract32(op, (start),\
                                             (end) - (start) + 1))

/* new opcode masks */

#define MASK_OP_MAJOR(op)      MASK_BITS_SHIFT(op, 0, 7)

/* 16-Bit Formats */
#define MASK_OP_SB_DISP8(op)   MASK_BITS_SHIFT(op, 8, 15)
#define MASK_OP_SB_DISP8_SEXT(op) MASK_BITS_SHIFT_SEXT(op, 8, 15)

#define MASK_OP_SBC_CONST4(op) MASK_BITS_SHIFT(op, 12, 15)
#define MASK_OP_SBC_CONST4_SEXT(op) MASK_BITS_SHIFT_SEXT(op, 12, 15)
#define MASK_OP_SBC_DISP4(op)  MASK_BITS_SHIFT(op, 8, 11)

#define MASK_OP_SBR_S2(op)     MASK_BITS_SHIFT(op, 12, 15)
#define MASK_OP_SBR_DISP4(op)  MASK_BITS_SHIFT(op, 8, 11)

#define MASK_OP_SBRN_N(op)     MASK_BITS_SHIFT(op, 12, 15)
#define MASK_OP_SBRN_DISP4(op) MASK_BITS_SHIFT(op, 8, 11)

#define MASK_OP_SC_CONST8(op)  MASK_BITS_SHIFT(op, 8, 15)

#define MASK_OP_SLR_S2(op)     MASK_BITS_SHIFT(op, 12, 15)
#define MASK_OP_SLR_D(op)      MASK_BITS_SHIFT(op, 8, 11)

#define MASK_OP_SLRO_OFF4(op)  MASK_BITS_SHIFT(op, 12, 15)
#define MASK_OP_SLRO_D(op)     MASK_BITS_SHIFT(op, 8, 11)

#define MASK_OP_SR_OP2(op)     MASK_BITS_SHIFT(op, 12, 15)
#define MASK_OP_SR_S1D(op)     MASK_BITS_SHIFT(op, 8, 11)

#define MASK_OP_SRC_CONST4(op) MASK_BITS_SHIFT(op, 12, 15)
#define MASK_OP_SRC_CONST4_SEXT(op) MASK_BITS_SHIFT_SEXT(op, 12, 15)
#define MASK_OP_SRC_S1D(op)    MASK_BITS_SHIFT(op, 8, 11)

#define MASK_OP_SRO_S2(op)     MASK_BITS_SHIFT(op, 12, 15)
#define MASK_OP_SRO_OFF4(op)   MASK_BITS_SHIFT(op, 8, 11)

#define MASK_OP_SRR_S2(op)     MASK_BITS_SHIFT(op, 12, 15)
#define MASK_OP_SRR_S1D(op)    MASK_BITS_SHIFT(op, 8, 11)

#define MASK_OP_SRRS_S2(op)    MASK_BITS_SHIFT(op, 12, 15)
#define MASK_OP_SRRS_S1D(op)   MASK_BITS_SHIFT(op, 8, 11)
#define MASK_OP_SRRS_N(op)     MASK_BITS_SHIFT(op, 6, 7)

#define MASK_OP_SSR_S2(op)     MASK_BITS_SHIFT(op, 12, 15)
#define MASK_OP_SSR_S1(op)     MASK_BITS_SHIFT(op, 8, 11)

#define MASK_OP_SSRO_OFF4(op)  MASK_BITS_SHIFT(op, 12, 15)
#define MASK_OP_SSRO_S1(op)    MASK_BITS_SHIFT(op, 8, 11)

/* 32-Bit Formats */

/* ABS Format */
#define MASK_OP_ABS_OFF18(op)  (MASK_BITS_SHIFT(op, 16, 21) +       \
                               (MASK_BITS_SHIFT(op, 28, 31) << 6) + \
                               (MASK_BITS_SHIFT(op, 22, 25) << 10) +\
                               (MASK_BITS_SHIFT(op, 12, 15) << 14))
#define MASK_OP_ABS_OP2(op)    MASK_BITS_SHIFT(op, 26, 27)
#define MASK_OP_ABS_S1D(op)    MASK_BITS_SHIFT(op, 8, 11)

/* ABSB Format */
#define MASK_OP_ABSB_OFF18(op) MASK_OP_ABS_OFF18(op)
#define MASK_OP_ABSB_OP2(op)   MASK_BITS_SHIFT(op, 26, 27)
#define MASK_OP_ABSB_B(op)     MASK_BITS_SHIFT(op, 11, 11)
#define MASK_OP_ABSB_BPOS(op)  MASK_BITS_SHIFT(op, 8, 10)

/* B Format   */
#define MASK_OP_B_DISP24(op)   (MASK_BITS_SHIFT(op, 16, 31) + \
                               (MASK_BITS_SHIFT(op, 8, 15) << 16))
#define MASK_OP_B_DISP24_SEXT(op)   (MASK_BITS_SHIFT(op, 16, 31) + \
                                    (MASK_BITS_SHIFT_SEXT(op, 8, 15) << 16))
/* BIT Format */
#define MASK_OP_BIT_D(op)      MASK_BITS_SHIFT(op, 28, 31)
#define MASK_OP_BIT_POS2(op)   MASK_BITS_SHIFT(op, 23, 27)
#define MASK_OP_BIT_OP2(op)    MASK_BITS_SHIFT(op, 21, 22)
#define MASK_OP_BIT_POS1(op)   MASK_BITS_SHIFT(op, 16, 20)
#define MASK_OP_BIT_S2(op)     MASK_BITS_SHIFT(op, 12, 15)
#define MASK_OP_BIT_S1(op)     MASK_BITS_SHIFT(op, 8, 11)

/* BO Format */
#define MASK_OP_BO_OFF10(op)   (MASK_BITS_SHIFT(op, 16, 21) + \
                               (MASK_BITS_SHIFT(op, 28, 31) << 6))
#define MASK_OP_BO_OFF10_SEXT(op)   (MASK_BITS_SHIFT(op, 16, 21) + \
                                    (MASK_BITS_SHIFT_SEXT(op, 28, 31) << 6))
#define MASK_OP_BO_OP2(op)     MASK_BITS_SHIFT(op, 22, 27)
#define MASK_OP_BO_S2(op)      MASK_BITS_SHIFT(op, 12, 15)
#define MASK_OP_BO_S1D(op)     MASK_BITS_SHIFT(op, 8, 11)

/* BOL Format */
#define MASK_OP_BOL_OFF16(op)  ((MASK_BITS_SHIFT(op, 16, 21) +        \
                               (MASK_BITS_SHIFT(op, 28, 31) << 6)) + \
                               (MASK_BITS_SHIFT(op, 22, 27) << 10))
#define MASK_OP_BOL_OFF16_SEXT(op)  ((MASK_BITS_SHIFT(op, 16, 21) +        \
                                    (MASK_BITS_SHIFT(op, 28, 31) << 6)) + \
                                    (MASK_BITS_SHIFT_SEXT(op, 22, 27) << 10))
#define MASK_OP_BOL_S2(op)     MASK_BITS_SHIFT(op, 12, 15)
#define MASK_OP_BOL_S1D(op)    MASK_BITS_SHIFT(op, 8, 11)

/* BRC Format */
#define MASK_OP_BRC_OP2(op)    MASK_BITS_SHIFT(op, 31, 31)
#define MASK_OP_BRC_DISP15(op) MASK_BITS_SHIFT(op, 16, 30)
#define MASK_OP_BRC_DISP15_SEXT(op) MASK_BITS_SHIFT_SEXT(op, 16, 30)
#define MASK_OP_BRC_CONST4(op) MASK_BITS_SHIFT(op, 12, 15)
#define MASK_OP_BRC_CONST4_SEXT(op) MASK_BITS_SHIFT_SEXT(op, 12, 15)
#define MASK_OP_BRC_S1(op)     MASK_BITS_SHIFT(op, 8, 11)

/* BRN Format */
#define MASK_OP_BRN_OP2(op)    MASK_BITS_SHIFT(op, 31, 31)
#define MASK_OP_BRN_DISP15(op) MASK_BITS_SHIFT(op, 16, 30)
#define MASK_OP_BRN_DISP15_SEXT(op) MASK_BITS_SHIFT_SEXT(op, 16, 30)
#define MASK_OP_BRN_N(op)      (MASK_BITS_SHIFT(op, 12, 15) + \
                               (MASK_BITS_SHIFT(op, 7, 7) << 4))
#define MASK_OP_BRN_S1(op)     MASK_BITS_SHIFT(op, 8, 11)
/* BRR Format */
#define MASK_OP_BRR_OP2(op)    MASK_BITS_SHIFT(op, 31, 31)
#define MASK_OP_BRR_DISP15(op) MASK_BITS_SHIFT(op, 16, 30)
#define MASK_OP_BRR_DISP15_SEXT(op) MASK_BITS_SHIFT_SEXT(op, 16, 30)
#define MASK_OP_BRR_S2(op)     MASK_BITS_SHIFT(op, 12, 15)
#define MASK_OP_BRR_S1(op)     MASK_BITS_SHIFT(op, 8, 11)

/* META MASK for similar instr Formats */
#define MASK_OP_META_D(op)     MASK_BITS_SHIFT(op, 28, 31)
#define MASK_OP_META_S1(op)    MASK_BITS_SHIFT(op, 8, 11)

/* RC Format */
#define MASK_OP_RC_D(op)       MASK_OP_META_D(op)
#define MASK_OP_RC_OP2(op)     MASK_BITS_SHIFT(op, 21, 27)
#define MASK_OP_RC_CONST9(op)  MASK_BITS_SHIFT(op, 12, 20)
#define MASK_OP_RC_CONST9_SEXT(op)  MASK_BITS_SHIFT_SEXT(op, 12, 20)
#define MASK_OP_RC_S1(op)      MASK_OP_META_S1(op)

/* RCPW Format */

#define MASK_OP_RCPW_D(op)      MASK_OP_META_D(op)
#define MASK_OP_RCPW_POS(op)    MASK_BITS_SHIFT(op, 23, 27)
#define MASK_OP_RCPW_OP2(op)    MASK_BITS_SHIFT(op, 21, 22)
#define MASK_OP_RCPW_WIDTH(op)  MASK_BITS_SHIFT(op, 16, 20)
#define MASK_OP_RCPW_CONST4(op) MASK_BITS_SHIFT(op, 12, 15)
#define MASK_OP_RCPW_S1(op)     MASK_OP_META_S1(op)

/* RCR Format */

#define MASK_OP_RCR_D(op)      MASK_OP_META_D(op)
#define MASK_OP_RCR_S3(op)     MASK_BITS_SHIFT(op, 24, 27)
#define MASK_OP_RCR_OP2(op)    MASK_BITS_SHIFT(op, 21, 23)
#define MASK_OP_RCR_CONST9(op) MASK_BITS_SHIFT(op, 12, 20)
#define MASK_OP_RCR_CONST9_SEXT(op) MASK_BITS_SHIFT_SEXT(op, 12, 20)
#define MASK_OP_RCR_S1(op)     MASK_OP_META_S1(op)

/* RCRR Format */

#define MASK_OP_RCRR_D(op)      MASK_OP_META_D(op)
#define MASK_OP_RCRR_S3(op)     MASK_BITS_SHIFT(op, 24, 27)
#define MASK_OP_RCRR_OP2(op)    MASK_BITS_SHIFT(op, 21, 23)
#define MASK_OP_RCRR_CONST4(op) MASK_BITS_SHIFT(op, 12, 15)
#define MASK_OP_RCRR_S1(op)     MASK_OP_META_S1(op)

/* RCRW Format */

#define MASK_OP_RCRW_D(op)      MASK_OP_META_D(op)
#define MASK_OP_RCRW_S3(op)     MASK_BITS_SHIFT(op, 24, 27)
#define MASK_OP_RCRW_OP2(op)    MASK_BITS_SHIFT(op, 21, 23)
#define MASK_OP_RCRW_WIDTH(op)  MASK_BITS_SHIFT(op, 16, 20)
#define MASK_OP_RCRW_CONST4(op) MASK_BITS_SHIFT(op, 12, 15)
#define MASK_OP_RCRW_S1(op)     MASK_OP_META_S1(op)

/* RLC Format */

#define MASK_OP_RLC_D(op)       MASK_OP_META_D(op)
#define MASK_OP_RLC_CONST16(op) MASK_BITS_SHIFT(op, 12, 27)
#define MASK_OP_RLC_CONST16_SEXT(op) MASK_BITS_SHIFT_SEXT(op, 12, 27)
#define MASK_OP_RLC_S1(op)      MASK_OP_META_S1(op)

/* RR  Format */
#define MASK_OP_RR_D(op)        MASK_OP_META_D(op)
#define MASK_OP_RR_OP2(op)      MASK_BITS_SHIFT(op, 20, 27)
#define MASK_OP_RR_N(op)        MASK_BITS_SHIFT(op, 16, 17)
#define MASK_OP_RR_S2(op)       MASK_BITS_SHIFT(op, 12, 15)
#define MASK_OP_RR_S1(op)       MASK_OP_META_S1(op)

/* RR1  Format */
#define MASK_OP_RR1_D(op)       MASK_OP_META_D(op)
#define MASK_OP_RR1_OP2(op)     MASK_BITS_SHIFT(op, 18, 27)
#define MASK_OP_RR1_N(op)       MASK_BITS_SHIFT(op, 16, 17)
#define MASK_OP_RR1_S2(op)      MASK_BITS_SHIFT(op, 12, 15)
#define MASK_OP_RR1_S1(op)      MASK_OP_META_S1(op)

/* RR2  Format */
#define MASK_OP_RR2_D(op)       MASK_OP_META_D(op)
#define MASK_OP_RR2_OP2(op)     MASK_BITS_SHIFT(op, 16, 27)
#define MASK_OP_RR2_S2(op)      MASK_BITS_SHIFT(op, 12, 15)
#define MASK_OP_RR2_S1(op)      MASK_OP_META_S1(op)

/* RRPW  Format */
#define MASK_OP_RRPW_D(op)      MASK_OP_META_D(op)
#define MASK_OP_RRPW_POS(op)    MASK_BITS_SHIFT(op, 23, 27)
#define MASK_OP_RRPW_OP2(op)    MASK_BITS_SHIFT(op, 21, 22)
#define MASK_OP_RRPW_WIDTH(op)  MASK_BITS_SHIFT(op, 16, 20)
#define MASK_OP_RRPW_S2(op)     MASK_BITS_SHIFT(op, 12, 15)
#define MASK_OP_RRPW_S1(op)     MASK_OP_META_S1(op)

/* RRR  Format */
#define MASK_OP_RRR_D(op)       MASK_OP_META_D(op)
#define MASK_OP_RRR_S3(op)      MASK_BITS_SHIFT(op, 24, 27)
#define MASK_OP_RRR_OP2(op)     MASK_BITS_SHIFT(op, 20, 23)
#define MASK_OP_RRR_N(op)       MASK_BITS_SHIFT(op, 16, 17)
#define MASK_OP_RRR_S2(op)      MASK_BITS_SHIFT(op, 12, 15)
#define MASK_OP_RRR_S1(op)      MASK_OP_META_S1(op)

/* RRR1  Format */
#define MASK_OP_RRR1_D(op)      MASK_OP_META_D(op)
#define MASK_OP_RRR1_S3(op)     MASK_BITS_SHIFT(op, 24, 27)
#define MASK_OP_RRR1_OP2(op)    MASK_BITS_SHIFT(op, 18, 23)
#define MASK_OP_RRR1_N(op)      MASK_BITS_SHIFT(op, 16, 17)
#define MASK_OP_RRR1_S2(op)     MASK_BITS_SHIFT(op, 12, 15)
#define MASK_OP_RRR1_S1(op)     MASK_OP_META_S1(op)

/* RRR2  Format */
#define MASK_OP_RRR2_D(op)      MASK_OP_META_D(op)
#define MASK_OP_RRR2_S3(op)     MASK_BITS_SHIFT(op, 24, 27)
#define MASK_OP_RRR2_OP2(op)    MASK_BITS_SHIFT(op, 16, 23)
#define MASK_OP_RRR2_S2(op)     MASK_BITS_SHIFT(op, 12, 15)
#define MASK_OP_RRR2_S1(op)     MASK_OP_META_S1(op)

/* RRRR  Format */
#define MASK_OP_RRRR_D(op)      MASK_OP_META_D(op)
#define MASK_OP_RRRR_S3(op)     MASK_BITS_SHIFT(op, 24, 27)
#define MASK_OP_RRRR_OP2(op)    MASK_BITS_SHIFT(op, 21, 23)
#define MASK_OP_RRRR_S2(op)     MASK_BITS_SHIFT(op, 12, 15)
#define MASK_OP_RRRR_S1(op)     MASK_OP_META_S1(op)

/* RRRW  Format */
#define MASK_OP_RRRW_D(op)      MASK_OP_META_D(op)
#define MASK_OP_RRRW_S3(op)     MASK_BITS_SHIFT(op, 24, 27)
#define MASK_OP_RRRW_OP2(op)    MASK_BITS_SHIFT(op, 21, 23)
#define MASK_OP_RRRW_WIDTH(op)  MASK_BITS_SHIFT(op, 16, 20)
#define MASK_OP_RRRW_S2(op)     MASK_BITS_SHIFT(op, 12, 15)
#define MASK_OP_RRRW_S1(op)     MASK_OP_META_S1(op)

/* SYS Format */
#define MASK_OP_SYS_OP2(op)     MASK_BITS_SHIFT(op, 22, 27)
#define MASK_OP_SYS_S1D(op)     MASK_OP_META_S1(op)



/*
 * Tricore Opcodes Enums
 *
 * Format: OPC(1|2|M)_InstrLen_Name
 * OPC1 = only op1 field is used
 * OPC2 = op1 and op2 field used part of OPCM
 * OPCM = op1 field used to group Instr
 * InstrLen = 16|32
 * Name = Name of Instr
 */

/* 16-Bit */
enum {

    OPCM_16_SR_SYSTEM                                = 0x00,
    OPCM_16_SR_ACCU                                  = 0x32,

    OPC1_16_SRC_ADD                                  = 0xc2,
    OPC1_16_SRC_ADD_A15                              = 0x92,
    OPC1_16_SRC_ADD_15A                              = 0x9a,
    OPC1_16_SRR_ADD                                  = 0x42,
    OPC1_16_SRR_ADD_A15                              = 0x12,
    OPC1_16_SRR_ADD_15A                              = 0x1a,
    OPC1_16_SRC_ADD_A                                = 0xb0,
    OPC1_16_SRR_ADD_A                                = 0x30,
    OPC1_16_SRR_ADDS                                 = 0x22,
    OPC1_16_SRRS_ADDSC_A                             = 0x10,
    OPC1_16_SC_AND                                   = 0x16,
    OPC1_16_SRR_AND                                  = 0x26,
    OPC1_16_SC_BISR                                  = 0xe0,
    OPC1_16_SRC_CADD                                 = 0x8a,
    OPC1_16_SRC_CADDN                                = 0xca,
    OPC1_16_SB_CALL                                  = 0x5c,
    OPC1_16_SRC_CMOV                                 = 0xaa,
    OPC1_16_SRR_CMOV                                 = 0x2a,
    OPC1_16_SRC_CMOVN                                = 0xea,
    OPC1_16_SRR_CMOVN                                = 0x6a,
    OPC1_16_SRC_EQ                                   = 0xba,
    OPC1_16_SRR_EQ                                   = 0x3a,
    OPC1_16_SB_J                                     = 0x3c,
    OPC1_16_SBC_JEQ                                  = 0x1e,
    OPC1_16_SBC_JEQ2                                 = 0x9e,
    OPC1_16_SBR_JEQ                                  = 0x3e,
    OPC1_16_SBR_JEQ2                                 = 0xbe,
    OPC1_16_SBR_JGEZ                                 = 0xce,
    OPC1_16_SBR_JGTZ                                 = 0x4e,
    OPC1_16_SR_JI                                    = 0xdc,
    OPC1_16_SBR_JLEZ                                 = 0x8e,
    OPC1_16_SBR_JLTZ                                 = 0x0e,
    OPC1_16_SBC_JNE                                  = 0x5e,
    OPC1_16_SBC_JNE2                                 = 0xde,
    OPC1_16_SBR_JNE                                  = 0x7e,
    OPC1_16_SBR_JNE2                                 = 0xfe,
    OPC1_16_SB_JNZ                                   = 0xee,
    OPC1_16_SBR_JNZ                                  = 0xf6,
    OPC1_16_SBR_JNZ_A                                = 0x7c,
    OPC1_16_SBRN_JNZ_T                               = 0xae,
    OPC1_16_SB_JZ                                    = 0x6e,
    OPC1_16_SBR_JZ                                   = 0x76,
    OPC1_16_SBR_JZ_A                                 = 0xbc,
    OPC1_16_SBRN_JZ_T                                = 0x2e,
    OPC1_16_SC_LD_A                                  = 0xd8,
    OPC1_16_SLR_LD_A                                 = 0xd4,
    OPC1_16_SLR_LD_A_POSTINC                         = 0xc4,
    OPC1_16_SLRO_LD_A                                = 0xc8,
    OPC1_16_SRO_LD_A                                 = 0xcc,
    OPC1_16_SLR_LD_BU                                = 0x14,
    OPC1_16_SLR_LD_BU_POSTINC                        = 0x04,
    OPC1_16_SLRO_LD_BU                               = 0x08,
    OPC1_16_SRO_LD_BU                                = 0x0c,
    OPC1_16_SLR_LD_H                                 = 0x94,
    OPC1_16_SLR_LD_H_POSTINC                         = 0x84,
    OPC1_16_SLRO_LD_H                                = 0x88,
    OPC1_16_SRO_LD_H                                 = 0x8c,
    OPC1_16_SC_LD_W                                  = 0x58,
    OPC1_16_SLR_LD_W                                 = 0x54,
    OPC1_16_SLR_LD_W_POSTINC                         = 0x44,
    OPC1_16_SLRO_LD_W                                = 0x48,
    OPC1_16_SRO_LD_W                                 = 0x4c,
    OPC1_16_SBR_LOOP                                 = 0xfc,
    OPC1_16_SRC_LT                                   = 0xfa,
    OPC1_16_SRR_LT                                   = 0x7a,
    OPC1_16_SC_MOV                                   = 0xda,
    OPC1_16_SRC_MOV                                  = 0x82,
    OPC1_16_SRR_MOV                                  = 0x02,
    OPC1_16_SRC_MOV_E                                = 0xd2,/* 1.6 only */
    OPC1_16_SRC_MOV_A                                = 0xa0,
    OPC1_16_SRR_MOV_A                                = 0x60,
    OPC1_16_SRR_MOV_AA                               = 0x40,
    OPC1_16_SRR_MOV_D                                = 0x80,
    OPC1_16_SRR_MUL                                  = 0xe2,
    OPC1_16_SR_NOT                                   = 0x46,
    OPC1_16_SC_OR                                    = 0x96,
    OPC1_16_SRR_OR                                   = 0xa6,
    OPC1_16_SRC_SH                                   = 0x06,
    OPC1_16_SRC_SHA                                  = 0x86,
    OPC1_16_SC_ST_A                                  = 0xf8,
    OPC1_16_SRO_ST_A                                 = 0xec,
    OPC1_16_SSR_ST_A                                 = 0xf4,
    OPC1_16_SSR_ST_A_POSTINC                         = 0xe4,
    OPC1_16_SSRO_ST_A                                = 0xe8,
    OPC1_16_SRO_ST_B                                 = 0x2c,
    OPC1_16_SSR_ST_B                                 = 0x34,
    OPC1_16_SSR_ST_B_POSTINC                         = 0x24,
    OPC1_16_SSRO_ST_B                                = 0x28,
    OPC1_16_SRO_ST_H                                 = 0xac,
    OPC1_16_SSR_ST_H                                 = 0xb4,
    OPC1_16_SSR_ST_H_POSTINC                         = 0xa4,
    OPC1_16_SSRO_ST_H                                = 0xa8,
    OPC1_16_SC_ST_W                                  = 0x78,
    OPC1_16_SRO_ST_W                                 = 0x6c,
    OPC1_16_SSR_ST_W                                 = 0x74,
    OPC1_16_SSR_ST_W_POSTINC                         = 0x64,
    OPC1_16_SSRO_ST_W                                = 0x68,
    OPC1_16_SRR_SUB                                  = 0xa2,
    OPC1_16_SRR_SUB_A15B                             = 0x52,
    OPC1_16_SRR_SUB_15AB                             = 0x5a,
    OPC1_16_SC_SUB_A                                 = 0x20,
    OPC1_16_SRR_SUBS                                 = 0x62,
    OPC1_16_SRR_XOR                                  = 0xc6,

};

/*
 * SR Format
 */
/* OPCM_16_SR_SYSTEM                                 */
enum {

    OPC2_16_SR_NOP                                   = 0x00,
    OPC2_16_SR_RET                                   = 0x09,
    OPC2_16_SR_RFE                                   = 0x08,
    OPC2_16_SR_DEBUG                                 = 0x0a,
    OPC2_16_SR_FRET                                  = 0x07,
};
/* OPCM_16_SR_ACCU                                   */
enum {
    OPC2_16_SR_RSUB                                  = 0x05,
    OPC2_16_SR_SAT_B                                 = 0x00,
    OPC2_16_SR_SAT_BU                                = 0x01,
    OPC2_16_SR_SAT_H                                 = 0x02,
    OPC2_16_SR_SAT_HU                                = 0x03,

};

/* 32-Bit */

enum {
/* ABS Format 1, M */
    OPCM_32_ABS_LDW                                  = 0x85,
    OPCM_32_ABS_LDB                                  = 0x05,
    OPCM_32_ABS_LDMST_SWAP                           = 0xe5,
    OPCM_32_ABS_LDST_CONTEXT                         = 0x15,
    OPCM_32_ABS_STORE                                = 0xa5,
    OPCM_32_ABS_STOREB_H                             = 0x25,
    OPC1_32_ABS_STOREQ                               = 0x65,
    OPC1_32_ABS_LD_Q                                 = 0x45,
    OPCM_32_ABS_LEA_LHA                              = 0xc5,
/* ABSB Format */
    OPC1_32_ABSB_ST_T                                = 0xd5,
/* B Format */
    OPC1_32_B_CALL                                   = 0x6d,
    OPC1_32_B_CALLA                                  = 0xed,
    OPC1_32_B_FCALL                                  = 0x61,
    OPC1_32_B_FCALLA                                 = 0xe1,
    OPC1_32_B_J                                      = 0x1d,
    OPC1_32_B_JA                                     = 0x9d,
    OPC1_32_B_JL                                     = 0x5d,
    OPC1_32_B_JLA                                    = 0xdd,
/* Bit Format */
    OPCM_32_BIT_ANDACC                               = 0x47,
    OPCM_32_BIT_LOGICAL_T1                           = 0x87,
    OPCM_32_BIT_INSERT                               = 0x67,
    OPCM_32_BIT_LOGICAL_T2                           = 0x07,
    OPCM_32_BIT_ORAND                                = 0xc7,
    OPCM_32_BIT_SH_LOGIC1                            = 0x27,
    OPCM_32_BIT_SH_LOGIC2                            = 0xa7,
/* BO Format */
    OPCM_32_BO_ADDRMODE_POST_PRE_BASE                = 0x89,
    OPCM_32_BO_ADDRMODE_BITREVERSE_CIRCULAR          = 0xa9,
    OPCM_32_BO_ADDRMODE_LD_POST_PRE_BASE             = 0x09,
    OPCM_32_BO_ADDRMODE_LD_BITREVERSE_CIRCULAR       = 0x29,
    OPCM_32_BO_ADDRMODE_STCTX_POST_PRE_BASE          = 0x49,
    OPCM_32_BO_ADDRMODE_LDMST_BITREVERSE_CIRCULAR    = 0x69,
/* BOL Format */
    OPC1_32_BOL_LD_A_LONGOFF                         = 0x99,
    OPC1_32_BOL_LD_W_LONGOFF                         = 0x19,
    OPC1_32_BOL_LEA_LONGOFF                          = 0xd9,
    OPC1_32_BOL_ST_W_LONGOFF                         = 0x59,
    OPC1_32_BOL_ST_A_LONGOFF                         = 0xb5, /* 1.6 only */
    OPC1_32_BOL_LD_B_LONGOFF                         = 0x79, /* 1.6 only */
    OPC1_32_BOL_LD_BU_LONGOFF                        = 0x39, /* 1.6 only */
    OPC1_32_BOL_LD_H_LONGOFF                         = 0xc9, /* 1.6 only */
    OPC1_32_BOL_LD_HU_LONGOFF                        = 0xb9, /* 1.6 only */
    OPC1_32_BOL_ST_B_LONGOFF                         = 0xe9, /* 1.6 only */
    OPC1_32_BOL_ST_H_LONGOFF                         = 0xf9, /* 1.6 only */
/* BRC Format */
    OPCM_32_BRC_EQ_NEQ                               = 0xdf,
    OPCM_32_BRC_GE                                   = 0xff,
    OPCM_32_BRC_JLT                                  = 0xbf,
    OPCM_32_BRC_JNE                                  = 0x9f,
/* BRN Format */
    OPCM_32_BRN_JTT                                  = 0x6f,
/* BRR Format */
    OPCM_32_BRR_EQ_NEQ                               = 0x5f,
    OPCM_32_BRR_ADDR_EQ_NEQ                          = 0x7d,
    OPCM_32_BRR_GE                                   = 0x7f,
    OPCM_32_BRR_JLT                                  = 0x3f,
    OPCM_32_BRR_JNE                                  = 0x1f,
    OPCM_32_BRR_JNZ                                  = 0xbd,
    OPCM_32_BRR_LOOP                                 = 0xfd,
/* RC Format */
    OPCM_32_RC_LOGICAL_SHIFT                         = 0x8f,
    OPCM_32_RC_ACCUMULATOR                           = 0x8b,
    OPCM_32_RC_SERVICEROUTINE                        = 0xad,
    OPCM_32_RC_MUL                                   = 0x53,
/* RCPW Format */
    OPCM_32_RCPW_MASK_INSERT                         = 0xb7,
/* RCR Format */
    OPCM_32_RCR_COND_SELECT                          = 0xab,
    OPCM_32_RCR_MADD                                 = 0x13,
    OPCM_32_RCR_MSUB                                 = 0x33,
/* RCRR Format */
    OPC1_32_RCRR_INSERT                              = 0x97,
/* RCRW Format */
    OPCM_32_RCRW_MASK_INSERT                         = 0xd7,
/* RLC Format */
    OPC1_32_RLC_ADDI                                 = 0x1b,
    OPC1_32_RLC_ADDIH                                = 0x9b,
    OPC1_32_RLC_ADDIH_A                              = 0x11,
    OPC1_32_RLC_MFCR                                 = 0x4d,
    OPC1_32_RLC_MOV                                  = 0x3b,
    OPC1_32_RLC_MOV_64                               = 0xfb, /* 1.6 only */
    OPC1_32_RLC_MOV_U                                = 0xbb,
    OPC1_32_RLC_MOV_H                                = 0x7b,
    OPC1_32_RLC_MOVH_A                               = 0x91,
    OPC1_32_RLC_MTCR                                 = 0xcd,
/* RR Format */
    OPCM_32_RR_LOGICAL_SHIFT                         = 0x0f,
    OPCM_32_RR_ACCUMULATOR                           = 0x0b,
    OPCM_32_RR_ADDRESS                               = 0x01,
    OPCM_32_RR_DIVIDE                                = 0x4b,
    OPCM_32_RR_IDIRECT                               = 0x2d,
/* RR1 Format */
    OPCM_32_RR1_MUL                                  = 0xb3,
    OPCM_32_RR1_MULQ                                 = 0x93,
/* RR2 Format */
    OPCM_32_RR2_MUL                                  = 0x73,
/* RRPW Format */
    OPCM_32_RRPW_EXTRACT_INSERT                      = 0x37,
    OPC1_32_RRPW_DEXTR                               = 0x77,
/* RRR Format */
    OPCM_32_RRR_COND_SELECT                          = 0x2b,
    OPCM_32_RRR_DIVIDE                               = 0x6b,
/* RRR1 Format */
    OPCM_32_RRR1_MADD                                = 0x83,
    OPCM_32_RRR1_MADDQ_H                             = 0x43,
    OPCM_32_RRR1_MADDSU_H                            = 0xc3,
    OPCM_32_RRR1_MSUB_H                              = 0xa3,
    OPCM_32_RRR1_MSUB_Q                              = 0x63,
    OPCM_32_RRR1_MSUBAD_H                            = 0xe3,
/* RRR2 Format */
    OPCM_32_RRR2_MADD                                = 0x03,
    OPCM_32_RRR2_MSUB                                = 0x23,
/* RRRR Format */
    OPCM_32_RRRR_EXTRACT_INSERT                      = 0x17,
/* RRRW Format */
    OPCM_32_RRRW_EXTRACT_INSERT                      = 0x57,
/* SYS Format */
    OPCM_32_SYS_INTERRUPTS                           = 0x0d,
    OPC1_32_SYS_RSTV                                 = 0x2f,
};



/*
 * ABS Format
 */

/* OPCM_32_ABS_LDW  */
enum {

    OPC2_32_ABS_LD_A                             = 0x02,
    OPC2_32_ABS_LD_D                             = 0x01,
    OPC2_32_ABS_LD_DA                            = 0x03,
    OPC2_32_ABS_LD_W                             = 0x00,
};

/* OPCM_32_ABS_LDB */
enum {
    OPC2_32_ABS_LD_B                             = 0x00,
    OPC2_32_ABS_LD_BU                            = 0x01,
    OPC2_32_ABS_LD_H                             = 0x02,
    OPC2_32_ABS_LD_HU                            = 0x03,
};
/* OPCM_32_ABS_LDMST_SWAP       */
enum {
    OPC2_32_ABS_LDMST                            = 0x01,
    OPC2_32_ABS_SWAP_W                           = 0x00,
};
/* OPCM_32_ABS_LDST_CONTEXT     */
enum {
    OPC2_32_ABS_LDLCX                            = 0x02,
    OPC2_32_ABS_LDUCX                            = 0x03,
    OPC2_32_ABS_STLCX                            = 0x00,
    OPC2_32_ABS_STUCX                            = 0x01,
};
/* OPCM_32_ABS_STORE            */
enum {
    OPC2_32_ABS_ST_A                             = 0x02,
    OPC2_32_ABS_ST_D                             = 0x01,
    OPC2_32_ABS_ST_DA                            = 0x03,
    OPC2_32_ABS_ST_W                             = 0x00,
};
/* OPCM_32_ABS_STOREB_H */
enum {
    OPC2_32_ABS_ST_B                             = 0x00,
    OPC2_32_ABS_ST_H                             = 0x02,
};

/* OPCM_32_ABS_LEA_LHA */
enum {
    OPC2_32_ABS_LEA                              = 0x00,
    OPC2_32_ABS_LHA                              = 0x01,
};

/*
 * Bit Format
 */
/* OPCM_32_BIT_ANDACC              */
enum {
    OPC2_32_BIT_AND_AND_T                        = 0x00,
    OPC2_32_BIT_AND_ANDN_T                       = 0x03,
    OPC2_32_BIT_AND_NOR_T                        = 0x02,
    OPC2_32_BIT_AND_OR_T                         = 0x01,
};
/* OPCM_32_BIT_LOGICAL_T                       */
enum {
    OPC2_32_BIT_AND_T                            = 0x00,
    OPC2_32_BIT_ANDN_T                           = 0x03,
    OPC2_32_BIT_NOR_T                            = 0x02,
    OPC2_32_BIT_OR_T                             = 0x01,
};
/* OPCM_32_BIT_INSERT                   */
enum {
    OPC2_32_BIT_INS_T                            = 0x00,
    OPC2_32_BIT_INSN_T                           = 0x01,
};
/* OPCM_32_BIT_LOGICAL_T2              */
enum {
    OPC2_32_BIT_NAND_T                           = 0x00,
    OPC2_32_BIT_ORN_T                            = 0x01,
    OPC2_32_BIT_XNOR_T                           = 0x02,
    OPC2_32_BIT_XOR_T                            = 0x03,
};
/* OPCM_32_BIT_ORAND                    */
enum {
    OPC2_32_BIT_OR_AND_T                         = 0x00,
    OPC2_32_BIT_OR_ANDN_T                        = 0x03,
    OPC2_32_BIT_OR_NOR_T                         = 0x02,
    OPC2_32_BIT_OR_OR_T                          = 0x01,
};
/*OPCM_32_BIT_SH_LOGIC1                 */
enum {
    OPC2_32_BIT_SH_AND_T                         = 0x00,
    OPC2_32_BIT_SH_ANDN_T                        = 0x03,
    OPC2_32_BIT_SH_NOR_T                         = 0x02,
    OPC2_32_BIT_SH_OR_T                          = 0x01,
};
/* OPCM_32_BIT_SH_LOGIC2              */
enum {
    OPC2_32_BIT_SH_NAND_T                        = 0x00,
    OPC2_32_BIT_SH_ORN_T                         = 0x01,
    OPC2_32_BIT_SH_XNOR_T                        = 0x02,
    OPC2_32_BIT_SH_XOR_T                         = 0x03,
};
/*
 * BO Format
 */
/* OPCM_32_BO_ADDRMODE_POST_PRE_BASE     */
enum {
    OPC2_32_BO_CACHEA_I_SHORTOFF                 = 0x2e,
    OPC2_32_BO_CACHEA_I_POSTINC                  = 0x0e,
    OPC2_32_BO_CACHEA_I_PREINC                   = 0x1e,
    OPC2_32_BO_CACHEA_W_SHORTOFF                 = 0x2c,
    OPC2_32_BO_CACHEA_W_POSTINC                  = 0x0c,
    OPC2_32_BO_CACHEA_W_PREINC                   = 0x1c,
    OPC2_32_BO_CACHEA_WI_SHORTOFF                = 0x2d,
    OPC2_32_BO_CACHEA_WI_POSTINC                 = 0x0d,
    OPC2_32_BO_CACHEA_WI_PREINC                  = 0x1d,
    /* 1.3.1 only */
    OPC2_32_BO_CACHEI_W_SHORTOFF                 = 0x2b,
    OPC2_32_BO_CACHEI_W_POSTINC                  = 0x0b,
    OPC2_32_BO_CACHEI_W_PREINC                   = 0x1b,
    OPC2_32_BO_CACHEI_WI_SHORTOFF                = 0x2f,
    OPC2_32_BO_CACHEI_WI_POSTINC                 = 0x0f,
    OPC2_32_BO_CACHEI_WI_PREINC                  = 0x1f,
    /* end 1.3.1 only */
    OPC2_32_BO_ST_A_SHORTOFF                     = 0x26,
    OPC2_32_BO_ST_A_POSTINC                      = 0x06,
    OPC2_32_BO_ST_A_PREINC                       = 0x16,
    OPC2_32_BO_ST_B_SHORTOFF                     = 0x20,
    OPC2_32_BO_ST_B_POSTINC                      = 0x00,
    OPC2_32_BO_ST_B_PREINC                       = 0x10,
    OPC2_32_BO_ST_D_SHORTOFF                     = 0x25,
    OPC2_32_BO_ST_D_POSTINC                      = 0x05,
    OPC2_32_BO_ST_D_PREINC                       = 0x15,
    OPC2_32_BO_ST_DA_SHORTOFF                    = 0x27,
    OPC2_32_BO_ST_DA_POSTINC                     = 0x07,
    OPC2_32_BO_ST_DA_PREINC                      = 0x17,
    OPC2_32_BO_ST_H_SHORTOFF                     = 0x22,
    OPC2_32_BO_ST_H_POSTINC                      = 0x02,
    OPC2_32_BO_ST_H_PREINC                       = 0x12,
    OPC2_32_BO_ST_Q_SHORTOFF                     = 0x28,
    OPC2_32_BO_ST_Q_POSTINC                      = 0x08,
    OPC2_32_BO_ST_Q_PREINC                       = 0x18,
    OPC2_32_BO_ST_W_SHORTOFF                     = 0x24,
    OPC2_32_BO_ST_W_POSTINC                      = 0x04,
    OPC2_32_BO_ST_W_PREINC                       = 0x14,
};
/* OPCM_32_BO_ADDRMODE_BITREVERSE_CIRCULAR   */
enum {
    OPC2_32_BO_CACHEA_I_BR                       = 0x0e,
    OPC2_32_BO_CACHEA_I_CIRC                     = 0x1e,
    OPC2_32_BO_CACHEA_W_BR                       = 0x0c,
    OPC2_32_BO_CACHEA_W_CIRC                     = 0x1c,
    OPC2_32_BO_CACHEA_WI_BR                      = 0x0d,
    OPC2_32_BO_CACHEA_WI_CIRC                    = 0x1d,
    OPC2_32_BO_ST_A_BR                           = 0x06,
    OPC2_32_BO_ST_A_CIRC                         = 0x16,
    OPC2_32_BO_ST_B_BR                           = 0x00,
    OPC2_32_BO_ST_B_CIRC                         = 0x10,
    OPC2_32_BO_ST_D_BR                           = 0x05,
    OPC2_32_BO_ST_D_CIRC                         = 0x15,
    OPC2_32_BO_ST_DA_BR                          = 0x07,
    OPC2_32_BO_ST_DA_CIRC                        = 0x17,
    OPC2_32_BO_ST_H_BR                           = 0x02,
    OPC2_32_BO_ST_H_CIRC                         = 0x12,
    OPC2_32_BO_ST_Q_BR                           = 0x08,
    OPC2_32_BO_ST_Q_CIRC                         = 0x18,
    OPC2_32_BO_ST_W_BR                           = 0x04,
    OPC2_32_BO_ST_W_CIRC                         = 0x14,
};
/*    OPCM_32_BO_ADDRMODE_LD_POST_PRE_BASE   */
enum {
    OPC2_32_BO_LD_A_SHORTOFF                     = 0x26,
    OPC2_32_BO_LD_A_POSTINC                      = 0x06,
    OPC2_32_BO_LD_A_PREINC                       = 0x16,
    OPC2_32_BO_LD_B_SHORTOFF                     = 0x20,
    OPC2_32_BO_LD_B_POSTINC                      = 0x00,
    OPC2_32_BO_LD_B_PREINC                       = 0x10,
    OPC2_32_BO_LD_BU_SHORTOFF                    = 0x21,
    OPC2_32_BO_LD_BU_POSTINC                     = 0x01,
    OPC2_32_BO_LD_BU_PREINC                      = 0x11,
    OPC2_32_BO_LD_D_SHORTOFF                     = 0x25,
    OPC2_32_BO_LD_D_POSTINC                      = 0x05,
    OPC2_32_BO_LD_D_PREINC                       = 0x15,
    OPC2_32_BO_LD_DA_SHORTOFF                    = 0x27,
    OPC2_32_BO_LD_DA_POSTINC                     = 0x07,
    OPC2_32_BO_LD_DA_PREINC                      = 0x17,
    OPC2_32_BO_LD_H_SHORTOFF                     = 0x22,
    OPC2_32_BO_LD_H_POSTINC                      = 0x02,
    OPC2_32_BO_LD_H_PREINC                       = 0x12,
    OPC2_32_BO_LD_HU_SHORTOFF                    = 0x23,
    OPC2_32_BO_LD_HU_POSTINC                     = 0x03,
    OPC2_32_BO_LD_HU_PREINC                      = 0x13,
    OPC2_32_BO_LD_Q_SHORTOFF                     = 0x28,
    OPC2_32_BO_LD_Q_POSTINC                      = 0x08,
    OPC2_32_BO_LD_Q_PREINC                       = 0x18,
    OPC2_32_BO_LD_W_SHORTOFF                     = 0x24,
    OPC2_32_BO_LD_W_POSTINC                      = 0x04,
    OPC2_32_BO_LD_W_PREINC                       = 0x14,
};
/* OPCM_32_BO_ADDRMODE_LD_BITREVERSE_CIRCULAR  */
enum {
    OPC2_32_BO_LD_A_BR                           = 0x06,
    OPC2_32_BO_LD_A_CIRC                         = 0x16,
    OPC2_32_BO_LD_B_BR                           = 0x00,
    OPC2_32_BO_LD_B_CIRC                         = 0x10,
    OPC2_32_BO_LD_BU_BR                          = 0x01,
    OPC2_32_BO_LD_BU_CIRC                        = 0x11,
    OPC2_32_BO_LD_D_BR                           = 0x05,
    OPC2_32_BO_LD_D_CIRC                         = 0x15,
    OPC2_32_BO_LD_DA_BR                          = 0x07,
    OPC2_32_BO_LD_DA_CIRC                        = 0x17,
    OPC2_32_BO_LD_H_BR                           = 0x02,
    OPC2_32_BO_LD_H_CIRC                         = 0x12,
    OPC2_32_BO_LD_HU_BR                          = 0x03,
    OPC2_32_BO_LD_HU_CIRC                        = 0x13,
    OPC2_32_BO_LD_Q_BR                           = 0x08,
    OPC2_32_BO_LD_Q_CIRC                         = 0x18,
    OPC2_32_BO_LD_W_BR                           = 0x04,
    OPC2_32_BO_LD_W_CIRC                         = 0x14,
};
/* OPCM_32_BO_ADDRMODE_STCTX_POST_PRE_BASE    */
enum {
    OPC2_32_BO_LDLCX_SHORTOFF                    = 0x24,
    OPC2_32_BO_LDMST_SHORTOFF                    = 0x21,
    OPC2_32_BO_LDMST_POSTINC                     = 0x01,
    OPC2_32_BO_LDMST_PREINC                      = 0x11,
    OPC2_32_BO_LDUCX_SHORTOFF                    = 0x25,
    OPC2_32_BO_LEA_SHORTOFF                      = 0x28,
    OPC2_32_BO_STLCX_SHORTOFF                    = 0x26,
    OPC2_32_BO_STUCX_SHORTOFF                    = 0x27,
    OPC2_32_BO_SWAP_W_SHORTOFF                   = 0x20,
    OPC2_32_BO_SWAP_W_POSTINC                    = 0x00,
    OPC2_32_BO_SWAP_W_PREINC                     = 0x10,
    OPC2_32_BO_CMPSWAP_W_SHORTOFF                = 0x23,
    OPC2_32_BO_CMPSWAP_W_POSTINC                 = 0x03,
    OPC2_32_BO_CMPSWAP_W_PREINC                  = 0x13,
    OPC2_32_BO_SWAPMSK_W_SHORTOFF                = 0x22,
    OPC2_32_BO_SWAPMSK_W_POSTINC                 = 0x02,
    OPC2_32_BO_SWAPMSK_W_PREINC                  = 0x12,
};
/*OPCM_32_BO_ADDRMODE_LDMST_BITREVERSE_CIRCULAR  */
enum {
    OPC2_32_BO_LDMST_BR                          = 0x01,
    OPC2_32_BO_LDMST_CIRC                        = 0x11,
    OPC2_32_BO_SWAP_W_BR                         = 0x00,
    OPC2_32_BO_SWAP_W_CIRC                       = 0x10,
    OPC2_32_BO_CMPSWAP_W_BR                      = 0x03,
    OPC2_32_BO_CMPSWAP_W_CIRC                    = 0x13,
    OPC2_32_BO_SWAPMSK_W_BR                      = 0x02,
    OPC2_32_BO_SWAPMSK_W_CIRC                    = 0x12,
};
/*
 * BRC Format
 */
/*OPCM_32_BRC_EQ_NEQ                             */
enum {
    OPC2_32_BRC_JEQ                              = 0x00,
    OPC2_32_BRC_JNE                              = 0x01,
};
/* OPCM_32_BRC_GE                                   */
enum {
    OP2_32_BRC_JGE                               = 0x00,
    OPC_32_BRC_JGE_U                             = 0x01,
};
/* OPCM_32_BRC_JLT                                  */
enum {
    OPC2_32_BRC_JLT                              = 0x00,
    OPC2_32_BRC_JLT_U                            = 0x01,
};
/* OPCM_32_BRC_JNE                                  */
enum {
    OPC2_32_BRC_JNED                             = 0x01,
    OPC2_32_BRC_JNEI                             = 0x00,
};
/*
 * BRN Format
 */
/* OPCM_32_BRN_JTT                                  */
enum {
    OPC2_32_BRN_JNZ_T                            = 0x01,
    OPC2_32_BRN_JZ_T                             = 0x00,
};
/*
 * BRR Format
 */
/* OPCM_32_BRR_EQ_NEQ                               */
enum {
    OPC2_32_BRR_JEQ                              = 0x00,
    OPC2_32_BRR_JNE                              = 0x01,
};
/* OPCM_32_BRR_ADDR_EQ_NEQ                        */
enum {
    OPC2_32_BRR_JEQ_A                            = 0x00,
    OPC2_32_BRR_JNE_A                            = 0x01,
};
/*OPCM_32_BRR_GE                                   */
enum {
    OPC2_32_BRR_JGE                              = 0x00,
    OPC2_32_BRR_JGE_U                            = 0x01,
};
/* OPCM_32_BRR_JLT                                  */
enum {
    OPC2_32_BRR_JLT                              = 0x00,
    OPC2_32_BRR_JLT_U                            = 0x01,
};
/* OPCM_32_BRR_JNE                                  */
enum {
    OPC2_32_BRR_JNED                             = 0x01,
    OPC2_32_BRR_JNEI                             = 0x00,
};
/* OPCM_32_BRR_JNZ                                  */
enum {
    OPC2_32_BRR_JNZ_A                            = 0x01,
    OPC2_32_BRR_JZ_A                             = 0x00,
};
/* OPCM_32_BRR_LOOP                                 */
enum {
    OPC2_32_BRR_LOOP                             = 0x00,
    OPC2_32_BRR_LOOPU                            = 0x01,
};
/*
 * RC Format
 */
/* OPCM_32_RC_LOGICAL_SHIFT                         */
enum {
    OPC2_32_RC_AND                               = 0x08,
    OPC2_32_RC_ANDN                              = 0x0e,
    OPC2_32_RC_NAND                              = 0x09,
    OPC2_32_RC_NOR                               = 0x0b,
    OPC2_32_RC_OR                                = 0x0a,
    OPC2_32_RC_ORN                               = 0x0f,
    OPC2_32_RC_SH                                = 0x00,
    OPC2_32_RC_SH_H                              = 0x40,
    OPC2_32_RC_SHA                               = 0x01,
    OPC2_32_RC_SHA_H                             = 0x41,
    OPC2_32_RC_SHAS                              = 0x02,
    OPC2_32_RC_XNOR                              = 0x0d,
    OPC2_32_RC_XOR                               = 0x0c,
    OPC2_32_RC_SHUFFLE                           = 0x07, /* v1.6.2 only */
};
/* OPCM_32_RC_ACCUMULATOR                           */
enum {
    OPC2_32_RC_ABSDIF                            = 0x0e,
    OPC2_32_RC_ABSDIFS                           = 0x0f,
    OPC2_32_RC_ADD                               = 0x00,
    OPC2_32_RC_ADDC                              = 0x05,
    OPC2_32_RC_ADDS                              = 0x02,
    OPC2_32_RC_ADDS_U                            = 0x03,
    OPC2_32_RC_ADDX                              = 0x04,
    OPC2_32_RC_AND_EQ                            = 0x20,
    OPC2_32_RC_AND_GE                            = 0x24,
    OPC2_32_RC_AND_GE_U                          = 0x25,
    OPC2_32_RC_AND_LT                            = 0x22,
    OPC2_32_RC_AND_LT_U                          = 0x23,
    OPC2_32_RC_AND_NE                            = 0x21,
    OPC2_32_RC_EQ                                = 0x10,
    OPC2_32_RC_EQANY_B                           = 0x56,
    OPC2_32_RC_EQANY_H                           = 0x76,
    OPC2_32_RC_GE                                = 0x14,
    OPC2_32_RC_GE_U                              = 0x15,
    OPC2_32_RC_LT                                = 0x12,
    OPC2_32_RC_LT_U                              = 0x13,
    OPC2_32_RC_MAX                               = 0x1a,
    OPC2_32_RC_MAX_U                             = 0x1b,
    OPC2_32_RC_MIN                               = 0x18,
    OPC2_32_RC_MIN_U                             = 0x19,
    OPC2_32_RC_NE                                = 0x11,
    OPC2_32_RC_OR_EQ                             = 0x27,
    OPC2_32_RC_OR_GE                             = 0x2b,
    OPC2_32_RC_OR_GE_U                           = 0x2c,
    OPC2_32_RC_OR_LT                             = 0x29,
    OPC2_32_RC_OR_LT_U                           = 0x2a,
    OPC2_32_RC_OR_NE                             = 0x28,
    OPC2_32_RC_RSUB                              = 0x08,
    OPC2_32_RC_RSUBS                             = 0x0a,
    OPC2_32_RC_RSUBS_U                           = 0x0b,
    OPC2_32_RC_SH_EQ                             = 0x37,
    OPC2_32_RC_SH_GE                             = 0x3b,
    OPC2_32_RC_SH_GE_U                           = 0x3c,
    OPC2_32_RC_SH_LT                             = 0x39,
    OPC2_32_RC_SH_LT_U                           = 0x3a,
    OPC2_32_RC_SH_NE                             = 0x38,
    OPC2_32_RC_XOR_EQ                            = 0x2f,
    OPC2_32_RC_XOR_GE                            = 0x33,
    OPC2_32_RC_XOR_GE_U                          = 0x34,
    OPC2_32_RC_XOR_LT                            = 0x31,
    OPC2_32_RC_XOR_LT_U                          = 0x32,
    OPC2_32_RC_XOR_NE                            = 0x30,
};
/* OPCM_32_RC_SERVICEROUTINE                        */
enum {
    OPC2_32_RC_BISR                              = 0x00,
    OPC2_32_RC_SYSCALL                           = 0x04,
};
/* OPCM_32_RC_MUL                                   */
enum {
    OPC2_32_RC_MUL_32                            = 0x01,
    OPC2_32_RC_MUL_64                            = 0x03,
    OPC2_32_RC_MULS_32                           = 0x05,
    OPC2_32_RC_MUL_U_64                          = 0x02,
    OPC2_32_RC_MULS_U_32                         = 0x04,
};
/*
 * RCPW Format
 */
/* OPCM_32_RCPW_MASK_INSERT                         */
enum {
    OPC2_32_RCPW_IMASK                           = 0x01,
    OPC2_32_RCPW_INSERT                          = 0x00,
};
/*
 * RCR Format
 */
/* OPCM_32_RCR_COND_SELECT                          */
enum {
    OPC2_32_RCR_CADD                             = 0x00,
    OPC2_32_RCR_CADDN                            = 0x01,
    OPC2_32_RCR_SEL                              = 0x04,
    OPC2_32_RCR_SELN                             = 0x05,
};
/* OPCM_32_RCR_MADD                                 */
enum {
    OPC2_32_RCR_MADD_32                          = 0x01,
    OPC2_32_RCR_MADD_64                          = 0x03,
    OPC2_32_RCR_MADDS_32                         = 0x05,
    OPC2_32_RCR_MADDS_64                         = 0x07,
    OPC2_32_RCR_MADD_U_64                        = 0x02,
    OPC2_32_RCR_MADDS_U_32                       = 0x04,
    OPC2_32_RCR_MADDS_U_64                       = 0x06,
};
/* OPCM_32_RCR_MSUB                                 */
enum {
    OPC2_32_RCR_MSUB_32                          = 0x01,
    OPC2_32_RCR_MSUB_64                          = 0x03,
    OPC2_32_RCR_MSUBS_32                         = 0x05,
    OPC2_32_RCR_MSUBS_64                         = 0x07,
    OPC2_32_RCR_MSUB_U_64                        = 0x02,
    OPC2_32_RCR_MSUBS_U_32                       = 0x04,
    OPC2_32_RCR_MSUBS_U_64                       = 0x06,
};
/*
 * RCRW Format
 */
/* OPCM_32_RCRW_MASK_INSERT                         */
enum {
    OPC2_32_RCRW_IMASK                           = 0x01,
    OPC2_32_RCRW_INSERT                          = 0x00,
};

/*
 * RR Format
 */
/* OPCM_32_RR_LOGICAL_SHIFT                         */
enum {
    OPC2_32_RR_AND                               = 0x08,
    OPC2_32_RR_ANDN                              = 0x0e,
    OPC2_32_RR_CLO                               = 0x1c,
    OPC2_32_RR_CLO_H                             = 0x7d,
    OPC2_32_RR_CLS                               = 0x1d,
    OPC2_32_RR_CLS_H                             = 0x7e,
    OPC2_32_RR_CLZ                               = 0x1b,
    OPC2_32_RR_CLZ_H                             = 0x7c,
    OPC2_32_RR_NAND                              = 0x09,
    OPC2_32_RR_NOR                               = 0x0b,
    OPC2_32_RR_OR                                = 0x0a,
    OPC2_32_RR_ORN                               = 0x0f,
    OPC2_32_RR_SH                                = 0x00,
    OPC2_32_RR_SH_H                              = 0x40,
    OPC2_32_RR_SHA                               = 0x01,
    OPC2_32_RR_SHA_H                             = 0x41,
    OPC2_32_RR_SHAS                              = 0x02,
    OPC2_32_RR_XNOR                              = 0x0d,
    OPC2_32_RR_XOR                               = 0x0c,
};
/* OPCM_32_RR_ACCUMULATOR                           */
enum {
    OPC2_32_RR_ABS                               = 0x1c,
    OPC2_32_RR_ABS_B                             = 0x5c,
    OPC2_32_RR_ABS_H                             = 0x7c,
    OPC2_32_RR_ABSDIF                            = 0x0e,
    OPC2_32_RR_ABSDIF_B                          = 0x4e,
    OPC2_32_RR_ABSDIF_H                          = 0x6e,
    OPC2_32_RR_ABSDIFS                           = 0x0f,
    OPC2_32_RR_ABSDIFS_H                         = 0x6f,
    OPC2_32_RR_ABSS                              = 0x1d,
    OPC2_32_RR_ABSS_H                            = 0x7d,
    OPC2_32_RR_ADD                               = 0x00,
    OPC2_32_RR_ADD_B                             = 0x40,
    OPC2_32_RR_ADD_H                             = 0x60,
    OPC2_32_RR_ADDC                              = 0x05,
    OPC2_32_RR_ADDS                              = 0x02,
    OPC2_32_RR_ADDS_H                            = 0x62,
    OPC2_32_RR_ADDS_HU                           = 0x63,
    OPC2_32_RR_ADDS_U                            = 0x03,
    OPC2_32_RR_ADDX                              = 0x04,
    OPC2_32_RR_AND_EQ                            = 0x20,
    OPC2_32_RR_AND_GE                            = 0x24,
    OPC2_32_RR_AND_GE_U                          = 0x25,
    OPC2_32_RR_AND_LT                            = 0x22,
    OPC2_32_RR_AND_LT_U                          = 0x23,
    OPC2_32_RR_AND_NE                            = 0x21,
    OPC2_32_RR_EQ                                = 0x10,
    OPC2_32_RR_EQ_B                              = 0x50,
    OPC2_32_RR_EQ_H                              = 0x70,
    OPC2_32_RR_EQ_W                              = 0x90,
    OPC2_32_RR_EQANY_B                           = 0x56,
    OPC2_32_RR_EQANY_H                           = 0x76,
    OPC2_32_RR_GE                                = 0x14,
    OPC2_32_RR_GE_U                              = 0x15,
    OPC2_32_RR_LT                                = 0x12,
    OPC2_32_RR_LT_U                              = 0x13,
    OPC2_32_RR_LT_B                              = 0x52,
    OPC2_32_RR_LT_BU                             = 0x53,
    OPC2_32_RR_LT_H                              = 0x72,
    OPC2_32_RR_LT_HU                             = 0x73,
    OPC2_32_RR_LT_W                              = 0x92,
    OPC2_32_RR_LT_WU                             = 0x93,
    OPC2_32_RR_MAX                               = 0x1a,
    OPC2_32_RR_MAX_U                             = 0x1b,
    OPC2_32_RR_MAX_B                             = 0x5a,
    OPC2_32_RR_MAX_BU                            = 0x5b,
    OPC2_32_RR_MAX_H                             = 0x7a,
    OPC2_32_RR_MAX_HU                            = 0x7b,
    OPC2_32_RR_MIN                               = 0x18,
    OPC2_32_RR_MIN_U                             = 0x19,
    OPC2_32_RR_MIN_B                             = 0x58,
    OPC2_32_RR_MIN_BU                            = 0x59,
    OPC2_32_RR_MIN_H                             = 0x78,
    OPC2_32_RR_MIN_HU                            = 0x79,
    OPC2_32_RR_MOV                               = 0x1f,
    OPC2_32_RR_MOVS_64                           = 0x80,
    OPC2_32_RR_MOV_64                            = 0x81,
    OPC2_32_RR_NE                                = 0x11,
    OPC2_32_RR_OR_EQ                             = 0x27,
    OPC2_32_RR_OR_GE                             = 0x2b,
    OPC2_32_RR_OR_GE_U                           = 0x2c,
    OPC2_32_RR_OR_LT                             = 0x29,
    OPC2_32_RR_OR_LT_U                           = 0x2a,
    OPC2_32_RR_OR_NE                             = 0x28,
    OPC2_32_RR_SAT_B                             = 0x5e,
    OPC2_32_RR_SAT_BU                            = 0x5f,
    OPC2_32_RR_SAT_H                             = 0x7e,
    OPC2_32_RR_SAT_HU                            = 0x7f,
    OPC2_32_RR_SH_EQ                             = 0x37,
    OPC2_32_RR_SH_GE                             = 0x3b,
    OPC2_32_RR_SH_GE_U                           = 0x3c,
    OPC2_32_RR_SH_LT                             = 0x39,
    OPC2_32_RR_SH_LT_U                           = 0x3a,
    OPC2_32_RR_SH_NE                             = 0x38,
    OPC2_32_RR_SUB                               = 0x08,
    OPC2_32_RR_SUB_B                             = 0x48,
    OPC2_32_RR_SUB_H                             = 0x68,
    OPC2_32_RR_SUBC                              = 0x0d,
    OPC2_32_RR_SUBS                              = 0x0a,
    OPC2_32_RR_SUBS_U                            = 0x0b,
    OPC2_32_RR_SUBS_H                            = 0x6a,
    OPC2_32_RR_SUBS_HU                           = 0x6b,
    OPC2_32_RR_SUBX                              = 0x0c,
    OPC2_32_RR_XOR_EQ                            = 0x2f,
    OPC2_32_RR_XOR_GE                            = 0x33,
    OPC2_32_RR_XOR_GE_U                          = 0x34,
    OPC2_32_RR_XOR_LT                            = 0x31,
    OPC2_32_RR_XOR_LT_U                          = 0x32,
    OPC2_32_RR_XOR_NE                            = 0x30,
};
/* OPCM_32_RR_ADDRESS                               */
enum {
    OPC2_32_RR_ADD_A                             = 0x01,
    OPC2_32_RR_ADDSC_A                           = 0x60,
    OPC2_32_RR_ADDSC_AT                          = 0x62,
    OPC2_32_RR_EQ_A                              = 0x40,
    OPC2_32_RR_EQZ                               = 0x48,
    OPC2_32_RR_GE_A                              = 0x43,
    OPC2_32_RR_LT_A                              = 0x42,
    OPC2_32_RR_MOV_A                             = 0x63,
    OPC2_32_RR_MOV_AA                            = 0x00,
    OPC2_32_RR_MOV_D                             = 0x4c,
    OPC2_32_RR_NE_A                              = 0x41,
    OPC2_32_RR_NEZ_A                             = 0x49,
    OPC2_32_RR_SUB_A                             = 0x02,
};
/* OPCM_32_RR_FLOAT                                 */
enum {
    OPC2_32_RR_BMERGE                            = 0x01,
    OPC2_32_RR_BSPLIT                            = 0x09,
    OPC2_32_RR_DVINIT_B                          = 0x5a,
    OPC2_32_RR_DVINIT_BU                         = 0x4a,
    OPC2_32_RR_DVINIT_H                          = 0x3a,
    OPC2_32_RR_DVINIT_HU                         = 0x2a,
    OPC2_32_RR_DVINIT                            = 0x1a,
    OPC2_32_RR_DVINIT_U                          = 0x0a,
    OPC2_32_RR_PARITY                            = 0x02,
    OPC2_32_RR_UNPACK                            = 0x08,
    OPC2_32_RR_CRC32                             = 0x03, /* CRC32B.W in 1.6.2 */
    OPC2_32_RR_CRC32_B                           = 0x06, /* 1.6.2 only */
    OPC2_32_RR_CRC32L_W                          = 0x07, /* 1.6.2 only */
    OPC2_32_RR_POPCNT_W                          = 0x22, /* 1.6.2 only */
    OPC2_32_RR_DIV                               = 0x20,
    OPC2_32_RR_DIV_U                             = 0x21,
    OPC2_32_RR_MUL_F                             = 0x04,
    OPC2_32_RR_DIV_F                             = 0x05,
    OPC2_32_RR_FTOI                              = 0x10,
    OPC2_32_RR_ITOF                              = 0x14,
    OPC2_32_RR_CMP_F                             = 0x00,
    OPC2_32_RR_FTOIZ                             = 0x13,
    OPC2_32_RR_FTOQ31                            = 0x11,
    OPC2_32_RR_FTOQ31Z                           = 0x18,
    OPC2_32_RR_FTOU                              = 0x12,
    OPC2_32_RR_FTOUZ                             = 0x17,
    OPC2_32_RR_Q31TOF                            = 0x15,
    OPC2_32_RR_QSEED_F                           = 0x19,
    OPC2_32_RR_UPDFL                             = 0x0c,
    OPC2_32_RR_UTOF                              = 0x16,
};
/* OPCM_32_RR_IDIRECT                               */
enum {
    OPC2_32_RR_JI                                = 0x03,
    OPC2_32_RR_JLI                               = 0x02,
    OPC2_32_RR_CALLI                             = 0x00,
    OPC2_32_RR_FCALLI                            = 0x01,
};
/*
 * RR1 Format
 */
/* OPCM_32_RR1_MUL                                  */
enum {
    OPC2_32_RR1_MUL_H_32_LL                      = 0x1a,
    OPC2_32_RR1_MUL_H_32_LU                      = 0x19,
    OPC2_32_RR1_MUL_H_32_UL                      = 0x18,
    OPC2_32_RR1_MUL_H_32_UU                      = 0x1b,
    OPC2_32_RR1_MULM_H_64_LL                     = 0x1e,
    OPC2_32_RR1_MULM_H_64_LU                     = 0x1d,
    OPC2_32_RR1_MULM_H_64_UL                     = 0x1c,
    OPC2_32_RR1_MULM_H_64_UU                     = 0x1f,
    OPC2_32_RR1_MULR_H_16_LL                     = 0x0e,
    OPC2_32_RR1_MULR_H_16_LU                     = 0x0d,
    OPC2_32_RR1_MULR_H_16_UL                     = 0x0c,
    OPC2_32_RR1_MULR_H_16_UU                     = 0x0f,
};
/* OPCM_32_RR1_MULQ                                 */
enum {
    OPC2_32_RR1_MUL_Q_32                         = 0x02,
    OPC2_32_RR1_MUL_Q_64                         = 0x1b,
    OPC2_32_RR1_MUL_Q_32_L                       = 0x01,
    OPC2_32_RR1_MUL_Q_64_L                       = 0x19,
    OPC2_32_RR1_MUL_Q_32_U                       = 0x00,
    OPC2_32_RR1_MUL_Q_64_U                       = 0x18,
    OPC2_32_RR1_MUL_Q_32_LL                      = 0x05,
    OPC2_32_RR1_MUL_Q_32_UU                      = 0x04,
    OPC2_32_RR1_MULR_Q_32_L                      = 0x07,
    OPC2_32_RR1_MULR_Q_32_U                      = 0x06,
};
/*
 * RR2 Format
 */
/* OPCM_32_RR2_MUL                                  */
enum {
    OPC2_32_RR2_MUL_32                           = 0x0a,
    OPC2_32_RR2_MUL_64                           = 0x6a,
    OPC2_32_RR2_MULS_32                          = 0x8a,
    OPC2_32_RR2_MUL_U_64                         = 0x68,
    OPC2_32_RR2_MULS_U_32                        = 0x88,
};
/*
 * RRPW Format
 */
/* OPCM_32_RRPW_EXTRACT_INSERT                      */
enum {

    OPC2_32_RRPW_EXTR                            = 0x02,
    OPC2_32_RRPW_EXTR_U                          = 0x03,
    OPC2_32_RRPW_IMASK                           = 0x01,
    OPC2_32_RRPW_INSERT                          = 0x00,
};
/*
 * RRR Format
 */
/* OPCM_32_RRR_COND_SELECT                          */
enum {
    OPC2_32_RRR_CADD                             = 0x00,
    OPC2_32_RRR_CADDN                            = 0x01,
    OPC2_32_RRR_CSUB                             = 0x02,
    OPC2_32_RRR_CSUBN                            = 0x03,
    OPC2_32_RRR_SEL                              = 0x04,
    OPC2_32_RRR_SELN                             = 0x05,
};
/* OPCM_32_RRR_FLOAT                                */
enum {
    OPC2_32_RRR_DVADJ                            = 0x0d,
    OPC2_32_RRR_DVSTEP                           = 0x0f,
    OPC2_32_RRR_DVSTEP_U                         = 0x0e,
    OPC2_32_RRR_IXMAX                            = 0x0a,
    OPC2_32_RRR_IXMAX_U                          = 0x0b,
    OPC2_32_RRR_IXMIN                            = 0x08,
    OPC2_32_RRR_IXMIN_U                          = 0x09,
    OPC2_32_RRR_PACK                             = 0x00,
    OPC2_32_RRR_ADD_F                            = 0x02,
    OPC2_32_RRR_SUB_F                            = 0x03,
    OPC2_32_RRR_MADD_F                           = 0x06,
    OPC2_32_RRR_MSUB_F                           = 0x07,
};
/*
 * RRR1 Format
 */
/* OPCM_32_RRR1_MADD                                */
enum {
    OPC2_32_RRR1_MADD_H_LL                       = 0x1a,
    OPC2_32_RRR1_MADD_H_LU                       = 0x19,
    OPC2_32_RRR1_MADD_H_UL                       = 0x18,
    OPC2_32_RRR1_MADD_H_UU                       = 0x1b,
    OPC2_32_RRR1_MADDS_H_LL                      = 0x3a,
    OPC2_32_RRR1_MADDS_H_LU                      = 0x39,
    OPC2_32_RRR1_MADDS_H_UL                      = 0x38,
    OPC2_32_RRR1_MADDS_H_UU                      = 0x3b,
    OPC2_32_RRR1_MADDM_H_LL                      = 0x1e,
    OPC2_32_RRR1_MADDM_H_LU                      = 0x1d,
    OPC2_32_RRR1_MADDM_H_UL                      = 0x1c,
    OPC2_32_RRR1_MADDM_H_UU                      = 0x1f,
    OPC2_32_RRR1_MADDMS_H_LL                     = 0x3e,
    OPC2_32_RRR1_MADDMS_H_LU                     = 0x3d,
    OPC2_32_RRR1_MADDMS_H_UL                     = 0x3c,
    OPC2_32_RRR1_MADDMS_H_UU                     = 0x3f,
    OPC2_32_RRR1_MADDR_H_LL                      = 0x0e,
    OPC2_32_RRR1_MADDR_H_LU                      = 0x0d,
    OPC2_32_RRR1_MADDR_H_UL                      = 0x0c,
    OPC2_32_RRR1_MADDR_H_UU                      = 0x0f,
    OPC2_32_RRR1_MADDRS_H_LL                     = 0x2e,
    OPC2_32_RRR1_MADDRS_H_LU                     = 0x2d,
    OPC2_32_RRR1_MADDRS_H_UL                     = 0x2c,
    OPC2_32_RRR1_MADDRS_H_UU                     = 0x2f,
};
/* OPCM_32_RRR1_MADDQ_H                             */
enum {
    OPC2_32_RRR1_MADD_Q_32                       = 0x02,
    OPC2_32_RRR1_MADD_Q_64                       = 0x1b,
    OPC2_32_RRR1_MADD_Q_32_L                     = 0x01,
    OPC2_32_RRR1_MADD_Q_64_L                     = 0x19,
    OPC2_32_RRR1_MADD_Q_32_U                     = 0x00,
    OPC2_32_RRR1_MADD_Q_64_U                     = 0x18,
    OPC2_32_RRR1_MADD_Q_32_LL                    = 0x05,
    OPC2_32_RRR1_MADD_Q_64_LL                    = 0x1d,
    OPC2_32_RRR1_MADD_Q_32_UU                    = 0x04,
    OPC2_32_RRR1_MADD_Q_64_UU                    = 0x1c,
    OPC2_32_RRR1_MADDS_Q_32                      = 0x22,
    OPC2_32_RRR1_MADDS_Q_64                      = 0x3b,
    OPC2_32_RRR1_MADDS_Q_32_L                    = 0x21,
    OPC2_32_RRR1_MADDS_Q_64_L                    = 0x39,
    OPC2_32_RRR1_MADDS_Q_32_U                    = 0x20,
    OPC2_32_RRR1_MADDS_Q_64_U                    = 0x38,
    OPC2_32_RRR1_MADDS_Q_32_LL                   = 0x25,
    OPC2_32_RRR1_MADDS_Q_64_LL                   = 0x3d,
    OPC2_32_RRR1_MADDS_Q_32_UU                   = 0x24,
    OPC2_32_RRR1_MADDS_Q_64_UU                   = 0x3c,
    OPC2_32_RRR1_MADDR_H_64_UL                   = 0x1e,
    OPC2_32_RRR1_MADDRS_H_64_UL                  = 0x3e,
    OPC2_32_RRR1_MADDR_Q_32_LL                   = 0x07,
    OPC2_32_RRR1_MADDR_Q_32_UU                   = 0x06,
    OPC2_32_RRR1_MADDRS_Q_32_LL                  = 0x27,
    OPC2_32_RRR1_MADDRS_Q_32_UU                  = 0x26,
};
/* OPCM_32_RRR1_MADDSU_H                            */
enum {
    OPC2_32_RRR1_MADDSU_H_32_LL                  = 0x1a,
    OPC2_32_RRR1_MADDSU_H_32_LU                  = 0x19,
    OPC2_32_RRR1_MADDSU_H_32_UL                  = 0x18,
    OPC2_32_RRR1_MADDSU_H_32_UU                  = 0x1b,
    OPC2_32_RRR1_MADDSUS_H_32_LL                 = 0x3a,
    OPC2_32_RRR1_MADDSUS_H_32_LU                 = 0x39,
    OPC2_32_RRR1_MADDSUS_H_32_UL                 = 0x38,
    OPC2_32_RRR1_MADDSUS_H_32_UU                 = 0x3b,
    OPC2_32_RRR1_MADDSUM_H_64_LL                 = 0x1e,
    OPC2_32_RRR1_MADDSUM_H_64_LU                 = 0x1d,
    OPC2_32_RRR1_MADDSUM_H_64_UL                 = 0x1c,
    OPC2_32_RRR1_MADDSUM_H_64_UU                 = 0x1f,
    OPC2_32_RRR1_MADDSUMS_H_64_LL                = 0x3e,
    OPC2_32_RRR1_MADDSUMS_H_64_LU                = 0x3d,
    OPC2_32_RRR1_MADDSUMS_H_64_UL                = 0x3c,
    OPC2_32_RRR1_MADDSUMS_H_64_UU                = 0x3f,
    OPC2_32_RRR1_MADDSUR_H_16_LL                 = 0x0e,
    OPC2_32_RRR1_MADDSUR_H_16_LU                 = 0x0d,
    OPC2_32_RRR1_MADDSUR_H_16_UL                 = 0x0c,
    OPC2_32_RRR1_MADDSUR_H_16_UU                 = 0x0f,
    OPC2_32_RRR1_MADDSURS_H_16_LL                = 0x2e,
    OPC2_32_RRR1_MADDSURS_H_16_LU                = 0x2d,
    OPC2_32_RRR1_MADDSURS_H_16_UL                = 0x2c,
    OPC2_32_RRR1_MADDSURS_H_16_UU                = 0x2f,
};
/* OPCM_32_RRR1_MSUB_H                              */
enum {
    OPC2_32_RRR1_MSUB_H_LL                       = 0x1a,
    OPC2_32_RRR1_MSUB_H_LU                       = 0x19,
    OPC2_32_RRR1_MSUB_H_UL                       = 0x18,
    OPC2_32_RRR1_MSUB_H_UU                       = 0x1b,
    OPC2_32_RRR1_MSUBS_H_LL                      = 0x3a,
    OPC2_32_RRR1_MSUBS_H_LU                      = 0x39,
    OPC2_32_RRR1_MSUBS_H_UL                      = 0x38,
    OPC2_32_RRR1_MSUBS_H_UU                      = 0x3b,
    OPC2_32_RRR1_MSUBM_H_LL                      = 0x1e,
    OPC2_32_RRR1_MSUBM_H_LU                      = 0x1d,
    OPC2_32_RRR1_MSUBM_H_UL                      = 0x1c,
    OPC2_32_RRR1_MSUBM_H_UU                      = 0x1f,
    OPC2_32_RRR1_MSUBMS_H_LL                     = 0x3e,
    OPC2_32_RRR1_MSUBMS_H_LU                     = 0x3d,
    OPC2_32_RRR1_MSUBMS_H_UL                     = 0x3c,
    OPC2_32_RRR1_MSUBMS_H_UU                     = 0x3f,
    OPC2_32_RRR1_MSUBR_H_LL                      = 0x0e,
    OPC2_32_RRR1_MSUBR_H_LU                      = 0x0d,
    OPC2_32_RRR1_MSUBR_H_UL                      = 0x0c,
    OPC2_32_RRR1_MSUBR_H_UU                      = 0x0f,
    OPC2_32_RRR1_MSUBRS_H_LL                     = 0x2e,
    OPC2_32_RRR1_MSUBRS_H_LU                     = 0x2d,
    OPC2_32_RRR1_MSUBRS_H_UL                     = 0x2c,
    OPC2_32_RRR1_MSUBRS_H_UU                     = 0x2f,
};
/* OPCM_32_RRR1_MSUB_Q                              */
enum {
    OPC2_32_RRR1_MSUB_Q_32                       = 0x02,
    OPC2_32_RRR1_MSUB_Q_64                       = 0x1b,
    OPC2_32_RRR1_MSUB_Q_32_L                     = 0x01,
    OPC2_32_RRR1_MSUB_Q_64_L                     = 0x19,
    OPC2_32_RRR1_MSUB_Q_32_U                     = 0x00,
    OPC2_32_RRR1_MSUB_Q_64_U                     = 0x18,
    OPC2_32_RRR1_MSUB_Q_32_LL                    = 0x05,
    OPC2_32_RRR1_MSUB_Q_64_LL                    = 0x1d,
    OPC2_32_RRR1_MSUB_Q_32_UU                    = 0x04,
    OPC2_32_RRR1_MSUB_Q_64_UU                    = 0x1c,
    OPC2_32_RRR1_MSUBS_Q_32                      = 0x22,
    OPC2_32_RRR1_MSUBS_Q_64                      = 0x3b,
    OPC2_32_RRR1_MSUBS_Q_32_L                    = 0x21,
    OPC2_32_RRR1_MSUBS_Q_64_L                    = 0x39,
    OPC2_32_RRR1_MSUBS_Q_32_U                    = 0x20,
    OPC2_32_RRR1_MSUBS_Q_64_U                    = 0x38,
    OPC2_32_RRR1_MSUBS_Q_32_LL                   = 0x25,
    OPC2_32_RRR1_MSUBS_Q_64_LL                   = 0x3d,
    OPC2_32_RRR1_MSUBS_Q_32_UU                   = 0x24,
    OPC2_32_RRR1_MSUBS_Q_64_UU                   = 0x3c,
    OPC2_32_RRR1_MSUBR_H_64_UL                   = 0x1e,
    OPC2_32_RRR1_MSUBRS_H_64_UL                  = 0x3e,
    OPC2_32_RRR1_MSUBR_Q_32_LL                   = 0x07,
    OPC2_32_RRR1_MSUBR_Q_32_UU                   = 0x06,
    OPC2_32_RRR1_MSUBRS_Q_32_LL                  = 0x27,
    OPC2_32_RRR1_MSUBRS_Q_32_UU                  = 0x26,
};
/* OPCM_32_RRR1_MSUBADS_H                           */
enum {
    OPC2_32_RRR1_MSUBAD_H_32_LL                  = 0x1a,
    OPC2_32_RRR1_MSUBAD_H_32_LU                  = 0x19,
    OPC2_32_RRR1_MSUBAD_H_32_UL                  = 0x18,
    OPC2_32_RRR1_MSUBAD_H_32_UU                  = 0x1b,
    OPC2_32_RRR1_MSUBADS_H_32_LL                 = 0x3a,
    OPC2_32_RRR1_MSUBADS_H_32_LU                 = 0x39,
    OPC2_32_RRR1_MSUBADS_H_32_UL                 = 0x38,
    OPC2_32_RRR1_MSUBADS_H_32_UU                 = 0x3b,
    OPC2_32_RRR1_MSUBADM_H_64_LL                 = 0x1e,
    OPC2_32_RRR1_MSUBADM_H_64_LU                 = 0x1d,
    OPC2_32_RRR1_MSUBADM_H_64_UL                 = 0x1c,
    OPC2_32_RRR1_MSUBADM_H_64_UU                 = 0x1f,
    OPC2_32_RRR1_MSUBADMS_H_64_LL                = 0x3e,
    OPC2_32_RRR1_MSUBADMS_H_64_LU                = 0x3d,
    OPC2_32_RRR1_MSUBADMS_H_64_UL                = 0x3c,
    OPC2_32_RRR1_MSUBADMS_H_64_UU                = 0x3f,
    OPC2_32_RRR1_MSUBADR_H_16_LL                 = 0x0e,
    OPC2_32_RRR1_MSUBADR_H_16_LU                 = 0x0d,
    OPC2_32_RRR1_MSUBADR_H_16_UL                 = 0x0c,
    OPC2_32_RRR1_MSUBADR_H_16_UU                 = 0x0f,
    OPC2_32_RRR1_MSUBADRS_H_16_LL                = 0x2e,
    OPC2_32_RRR1_MSUBADRS_H_16_LU                = 0x2d,
    OPC2_32_RRR1_MSUBADRS_H_16_UL                = 0x2c,
    OPC2_32_RRR1_MSUBADRS_H_16_UU                = 0x2f,
};
/*
 * RRR2 Format
 */
/* OPCM_32_RRR2_MADD                                */
enum {
    OPC2_32_RRR2_MADD_32                         = 0x0a,
    OPC2_32_RRR2_MADD_64                         = 0x6a,
    OPC2_32_RRR2_MADDS_32                        = 0x8a,
    OPC2_32_RRR2_MADDS_64                        = 0xea,
    OPC2_32_RRR2_MADD_U_64                       = 0x68,
    OPC2_32_RRR2_MADDS_U_32                      = 0x88,
    OPC2_32_RRR2_MADDS_U_64                      = 0xe8,
};
/* OPCM_32_RRR2_MSUB                                */
enum {
    OPC2_32_RRR2_MSUB_32                         = 0x0a,
    OPC2_32_RRR2_MSUB_64                         = 0x6a,
    OPC2_32_RRR2_MSUBS_32                        = 0x8a,
    OPC2_32_RRR2_MSUBS_64                        = 0xea,
    OPC2_32_RRR2_MSUB_U_64                       = 0x68,
    OPC2_32_RRR2_MSUBS_U_32                      = 0x88,
    OPC2_32_RRR2_MSUBS_U_64                      = 0xe8,
};
/*
 * RRRR Format
 */
/* OPCM_32_RRRR_EXTRACT_INSERT                      */
enum {
    OPC2_32_RRRR_DEXTR                           = 0x04,
    OPC2_32_RRRR_EXTR                            = 0x02,
    OPC2_32_RRRR_EXTR_U                          = 0x03,
    OPC2_32_RRRR_INSERT                          = 0x00,
};
/*
 * RRRW Format
 */
/* OPCM_32_RRRW_EXTRACT_INSERT                      */
enum {
    OPC2_32_RRRW_EXTR                            = 0x02,
    OPC2_32_RRRW_EXTR_U                          = 0x03,
    OPC2_32_RRRW_IMASK                           = 0x01,
    OPC2_32_RRRW_INSERT                          = 0x00,
};
/*
 * SYS Format
 */
/* OPCM_32_SYS_INTERRUPTS                           */
enum {
    OPC2_32_SYS_DEBUG                            = 0x04,
    OPC2_32_SYS_DISABLE                          = 0x0d,
    OPC2_32_SYS_DISABLE_D                        = 0x0f, /* 1.6 up */
    OPC2_32_SYS_DSYNC                            = 0x12,
    OPC2_32_SYS_ENABLE                           = 0x0c,
    OPC2_32_SYS_ISYNC                            = 0x13,
    OPC2_32_SYS_NOP                              = 0x00,
    OPC2_32_SYS_RET                              = 0x06,
    OPC2_32_SYS_RFE                              = 0x07,
    OPC2_32_SYS_RFM                              = 0x05,
    OPC2_32_SYS_RSLCX                            = 0x09,
    OPC2_32_SYS_SVLCX                            = 0x08,
    OPC2_32_SYS_TRAPSV                           = 0x15,
    OPC2_32_SYS_TRAPV                            = 0x14,
    OPC2_32_SYS_RESTORE                          = 0x0e,
    OPC2_32_SYS_FRET                             = 0x03,
};

#endif

/*
 *  Header file for nanoMIPS disassembler component of QEMU
 *
 *  Copyright (C) 2018  Wave Computing, Inc.
 *  Copyright (C) 2018  Matthew Fortune <matthew.fortune@mips.com>
 *  Copyright (C) 2018  Aleksandar Markovic <amarkovic@wavecomp.com>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 */

#ifndef DISAS_NANOMIPS_H
#define DISAS_NANOMIPS_H

#include <string>

typedef int64_t int64;
typedef uint64_t uint64;
typedef uint32_t uint32;
typedef uint16_t uint16;
typedef uint64_t img_address;

enum TABLE_ENTRY_TYPE {
    instruction,
    call_instruction,
    branch_instruction,
    return_instruction,
    reserved_block,
    pool,
};

enum TABLE_ATTRIBUTE_TYPE {
    MIPS64_    = 0x00000001,
    XNP_       = 0x00000002,
    XMMS_      = 0x00000004,
    EVA_       = 0x00000008,
    DSP_       = 0x00000010,
    MT_        = 0x00000020,
    EJTAG_     = 0x00000040,
    TLBINV_    = 0x00000080,
    CP0_       = 0x00000100,
    CP1_       = 0x00000200,
    CP2_       = 0x00000400,
    UDI_       = 0x00000800,
    MCU_       = 0x00001000,
    VZ_        = 0x00002000,
    TLB_       = 0x00004000,
    MVH_       = 0x00008000,
    ALL_ATTRIBUTES = 0xffffffffull,
};

typedef struct Dis_info {
  img_address m_pc;
} Dis_info;

typedef bool (*conditional_function)(uint64 instruction);
typedef std::string (*disassembly_function)(uint64 instruction,
                                            Dis_info *info);

class NMD
{
public:

    int Disassemble(const uint16 *data, std::string & dis,
                    TABLE_ENTRY_TYPE & type, Dis_info *info);

private:

    struct Pool {
        TABLE_ENTRY_TYPE     type;
        struct Pool          *next_table;
        int                  next_table_size;
        int                  instructions_size;
        uint64               mask;
        uint64               value;
        disassembly_function disassembly;
        conditional_function condition;
        uint64               attributes;
    };

    uint64 extract_op_code_value(const uint16 *data, int size);
    int Disassemble(const uint16 *data, std::string & dis,
                    TABLE_ENTRY_TYPE & type, const Pool *table, int table_size,
                    Dis_info *info);

    static Pool P_SYSCALL[2];
    static Pool P_RI[4];
    static Pool P_ADDIU[2];
    static Pool P_TRAP[2];
    static Pool P_CMOVE[2];
    static Pool P_D_MT_VPE[2];
    static Pool P_E_MT_VPE[2];
    static Pool _P_MT_VPE[2];
    static Pool P_MT_VPE[8];
    static Pool P_DVP[2];
    static Pool P_SLTU[2];
    static Pool _POOL32A0[128];
    static Pool ADDQ__S__PH[2];
    static Pool MUL__S__PH[2];
    static Pool ADDQH__R__PH[2];
    static Pool ADDQH__R__W[2];
    static Pool ADDU__S__QB[2];
    static Pool ADDU__S__PH[2];
    static Pool ADDUH__R__QB[2];
    static Pool SHRAV__R__PH[2];
    static Pool SHRAV__R__QB[2];
    static Pool SUBQ__S__PH[2];
    static Pool SUBQH__R__PH[2];
    static Pool SUBQH__R__W[2];
    static Pool SUBU__S__QB[2];
    static Pool SUBU__S__PH[2];
    static Pool SHRA__R__PH[2];
    static Pool SUBUH__R__QB[2];
    static Pool SHLLV__S__PH[2];
    static Pool SHLL__S__PH[4];
    static Pool PRECR_SRA__R__PH_W[2];
    static Pool _POOL32A5[128];
    static Pool PP_LSX[16];
    static Pool PP_LSXS[16];
    static Pool P_LSX[2];
    static Pool POOL32Axf_1_0[4];
    static Pool POOL32Axf_1_1[4];
    static Pool POOL32Axf_1_3[4];
    static Pool POOL32Axf_1_4[2];
    static Pool MAQ_S_A__W_PHR[2];
    static Pool MAQ_S_A__W_PHL[2];
    static Pool POOL32Axf_1_5[2];
    static Pool POOL32Axf_1_7[4];
    static Pool POOL32Axf_1[8];
    static Pool POOL32Axf_2_DSP__0_7[8];
    static Pool POOL32Axf_2_DSP__8_15[8];
    static Pool POOL32Axf_2_DSP__16_23[8];
    static Pool POOL32Axf_2_DSP__24_31[8];
    static Pool POOL32Axf_2[4];
    static Pool POOL32Axf_4[128];
    static Pool POOL32Axf_5_group0[32];
    static Pool POOL32Axf_5_group1[32];
    static Pool ERETx[2];
    static Pool POOL32Axf_5_group3[32];
    static Pool POOL32Axf_5[4];
    static Pool SHRA__R__QB[2];
    static Pool POOL32Axf_7[8];
    static Pool POOL32Axf[8];
    static Pool _POOL32A7[8];
    static Pool P32A[8];
    static Pool P_GP_D[2];
    static Pool P_GP_W[4];
    static Pool POOL48I[32];
    static Pool PP_SR[4];
    static Pool P_SR_F[8];
    static Pool P_SR[2];
    static Pool P_SLL[5];
    static Pool P_SHIFT[16];
    static Pool P_ROTX[4];
    static Pool P_INS[4];
    static Pool P_EXT[4];
    static Pool P_U12[16];
    static Pool RINT_fmt[2];
    static Pool ADD_fmt0[2];
    static Pool SELEQZ_fmt[2];
    static Pool CLASS_fmt[2];
    static Pool SUB_fmt0[2];
    static Pool SELNEZ_fmt[2];
    static Pool MUL_fmt0[2];
    static Pool SEL_fmt[2];
    static Pool DIV_fmt0[2];
    static Pool ADD_fmt1[2];
    static Pool SUB_fmt1[2];
    static Pool MUL_fmt1[2];
    static Pool MADDF_fmt[2];
    static Pool DIV_fmt1[2];
    static Pool MSUBF_fmt[2];
    static Pool POOL32F_0[64];
    static Pool MIN_fmt[2];
    static Pool MAX_fmt[2];
    static Pool MINA_fmt[2];
    static Pool MAXA_fmt[2];
    static Pool CVT_L_fmt[2];
    static Pool RSQRT_fmt[2];
    static Pool FLOOR_L_fmt[2];
    static Pool CVT_W_fmt[2];
    static Pool SQRT_fmt[2];
    static Pool FLOOR_W_fmt[2];
    static Pool RECIP_fmt[2];
    static Pool CEIL_L_fmt[2];
    static Pool CEIL_W_fmt[2];
    static Pool TRUNC_L_fmt[2];
    static Pool TRUNC_W_fmt[2];
    static Pool ROUND_L_fmt[2];
    static Pool ROUND_W_fmt[2];
    static Pool POOL32Fxf_0[64];
    static Pool MOV_fmt[4];
    static Pool ABS_fmt[4];
    static Pool NEG_fmt[4];
    static Pool CVT_D_fmt[4];
    static Pool CVT_S_fmt[4];
    static Pool POOL32Fxf_1[32];
    static Pool POOL32Fxf[4];
    static Pool POOL32F_3[8];
    static Pool CMP_condn_S[32];
    static Pool CMP_condn_D[32];
    static Pool POOL32F_5[8];
    static Pool POOL32F[8];
    static Pool POOL32S_0[64];
    static Pool POOL32Sxf_4[128];
    static Pool POOL32Sxf[8];
    static Pool POOL32S_4[8];
    static Pool POOL32S[8];
    static Pool P_LUI[2];
    static Pool P_GP_LH[2];
    static Pool P_GP_SH[2];
    static Pool P_GP_CP1[4];
    static Pool P_GP_M64[4];
    static Pool P_GP_BH[8];
    static Pool P_LS_U12[16];
    static Pool P_PREF_S9_[2];
    static Pool P_LS_S0[16];
    static Pool ASET_ACLR[2];
    static Pool P_LL[4];
    static Pool P_SC[4];
    static Pool P_LLD[8];
    static Pool P_SCD[8];
    static Pool P_LS_S1[16];
    static Pool P_PREFE[2];
    static Pool P_LLE[4];
    static Pool P_SCE[4];
    static Pool P_LS_E0[16];
    static Pool P_LS_WM[2];
    static Pool P_LS_UAWM[2];
    static Pool P_LS_DM[2];
    static Pool P_LS_UADM[2];
    static Pool P_LS_S9[8];
    static Pool P_BAL[2];
    static Pool P_BALRSC[2];
    static Pool P_J[16];
    static Pool P_BR3A[32];
    static Pool P_BR1[4];
    static Pool P_BR2[4];
    static Pool P_BRI[8];
    static Pool P32[32];
    static Pool P16_SYSCALL[2];
    static Pool P16_RI[4];
    static Pool P16_MV[2];
    static Pool P16_SHIFT[2];
    static Pool POOL16C_00[4];
    static Pool POOL16C_0[2];
    static Pool P16C[2];
    static Pool P16_A1[2];
    static Pool P_ADDIU_RS5_[2];
    static Pool P16_A2[2];
    static Pool P16_ADDU[2];
    static Pool P16_JRC[2];
    static Pool P16_BR1[2];
    static Pool P16_BR[2];
    static Pool P16_SR[2];
    static Pool P16_4X4[4];
    static Pool P16_LB[4];
    static Pool P16_LH[4];
    static Pool P16[32];
    static Pool MAJOR[2];

};

#endif

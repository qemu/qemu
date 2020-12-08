/*
 *  MIPS SIMD Architecture (MSA) translation routines
 *
 *  Copyright (c) 2004-2005 Jocelyn Mayer
 *  Copyright (c) 2006 Marius Groeger (FPU operations)
 *  Copyright (c) 2006 Thiemo Seufer (MIPS32R2 support)
 *  Copyright (c) 2009 CodeSourcery (MIPS16 and microMIPS support)
 *  Copyright (c) 2012 Jia Liu & Dongxue Zhang (MIPS ASE DSP support)
 *  Copyright (c) 2020 Philippe Mathieu-Daudé
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include "qemu/osdep.h"
#include "tcg/tcg-op.h"
#include "exec/helper-gen.h"
#include "translate.h"
#include "fpu_helper.h"
#include "internal.h"

/* Include the auto-generated decoder.  */
#include "decode-msa32.c.inc"
#include "decode-msa64.c.inc"

#define OPC_MSA (0x1E << 26)

#define MASK_MSA_MINOR(op)          (MASK_OP_MAJOR(op) | (op & 0x3F))
enum {
    OPC_MSA_I8_00   = 0x00 | OPC_MSA,
    OPC_MSA_I8_01   = 0x01 | OPC_MSA,
    OPC_MSA_I8_02   = 0x02 | OPC_MSA,
    OPC_MSA_I5_06   = 0x06 | OPC_MSA,
    OPC_MSA_I5_07   = 0x07 | OPC_MSA,
    OPC_MSA_BIT_09  = 0x09 | OPC_MSA,
    OPC_MSA_BIT_0A  = 0x0A | OPC_MSA,
    OPC_MSA_3R_0D   = 0x0D | OPC_MSA,
    OPC_MSA_3R_0E   = 0x0E | OPC_MSA,
    OPC_MSA_3R_0F   = 0x0F | OPC_MSA,
    OPC_MSA_3R_10   = 0x10 | OPC_MSA,
    OPC_MSA_3R_11   = 0x11 | OPC_MSA,
    OPC_MSA_3R_12   = 0x12 | OPC_MSA,
    OPC_MSA_3R_13   = 0x13 | OPC_MSA,
    OPC_MSA_3R_14   = 0x14 | OPC_MSA,
    OPC_MSA_3R_15   = 0x15 | OPC_MSA,
    OPC_MSA_ELM     = 0x19 | OPC_MSA,
    OPC_MSA_3RF_1A  = 0x1A | OPC_MSA,
    OPC_MSA_3RF_1B  = 0x1B | OPC_MSA,
    OPC_MSA_3RF_1C  = 0x1C | OPC_MSA,
    OPC_MSA_VEC     = 0x1E | OPC_MSA,

    /* MI10 instruction */
    OPC_LD_B        = (0x20) | OPC_MSA,
    OPC_LD_H        = (0x21) | OPC_MSA,
    OPC_LD_W        = (0x22) | OPC_MSA,
    OPC_LD_D        = (0x23) | OPC_MSA,
    OPC_ST_B        = (0x24) | OPC_MSA,
    OPC_ST_H        = (0x25) | OPC_MSA,
    OPC_ST_W        = (0x26) | OPC_MSA,
    OPC_ST_D        = (0x27) | OPC_MSA,
};

enum {
    /* I5 instruction df(bits 22..21) = _b, _h, _w, _d */
    OPC_ADDVI_df    = (0x0 << 23) | OPC_MSA_I5_06,
    OPC_CEQI_df     = (0x0 << 23) | OPC_MSA_I5_07,
    OPC_SUBVI_df    = (0x1 << 23) | OPC_MSA_I5_06,
    OPC_MAXI_S_df   = (0x2 << 23) | OPC_MSA_I5_06,
    OPC_CLTI_S_df   = (0x2 << 23) | OPC_MSA_I5_07,
    OPC_MAXI_U_df   = (0x3 << 23) | OPC_MSA_I5_06,
    OPC_CLTI_U_df   = (0x3 << 23) | OPC_MSA_I5_07,
    OPC_MINI_S_df   = (0x4 << 23) | OPC_MSA_I5_06,
    OPC_CLEI_S_df   = (0x4 << 23) | OPC_MSA_I5_07,
    OPC_MINI_U_df   = (0x5 << 23) | OPC_MSA_I5_06,
    OPC_CLEI_U_df   = (0x5 << 23) | OPC_MSA_I5_07,
    OPC_LDI_df      = (0x6 << 23) | OPC_MSA_I5_07,

    /* I8 instruction */
    OPC_ANDI_B      = (0x0 << 24) | OPC_MSA_I8_00,
    OPC_BMNZI_B     = (0x0 << 24) | OPC_MSA_I8_01,
    OPC_SHF_B       = (0x0 << 24) | OPC_MSA_I8_02,
    OPC_ORI_B       = (0x1 << 24) | OPC_MSA_I8_00,
    OPC_BMZI_B      = (0x1 << 24) | OPC_MSA_I8_01,
    OPC_SHF_H       = (0x1 << 24) | OPC_MSA_I8_02,
    OPC_NORI_B      = (0x2 << 24) | OPC_MSA_I8_00,
    OPC_BSELI_B     = (0x2 << 24) | OPC_MSA_I8_01,
    OPC_SHF_W       = (0x2 << 24) | OPC_MSA_I8_02,
    OPC_XORI_B      = (0x3 << 24) | OPC_MSA_I8_00,

    /* VEC/2R/2RF instruction */
    OPC_AND_V       = (0x00 << 21) | OPC_MSA_VEC,
    OPC_OR_V        = (0x01 << 21) | OPC_MSA_VEC,
    OPC_NOR_V       = (0x02 << 21) | OPC_MSA_VEC,
    OPC_XOR_V       = (0x03 << 21) | OPC_MSA_VEC,
    OPC_BMNZ_V      = (0x04 << 21) | OPC_MSA_VEC,
    OPC_BMZ_V       = (0x05 << 21) | OPC_MSA_VEC,
    OPC_BSEL_V      = (0x06 << 21) | OPC_MSA_VEC,

    OPC_MSA_2R      = (0x18 << 21) | OPC_MSA_VEC,
    OPC_MSA_2RF     = (0x19 << 21) | OPC_MSA_VEC,

    /* 2R instruction df(bits 17..16) = _b, _h, _w, _d */
    OPC_FILL_df     = (0x00 << 18) | OPC_MSA_2R,
    OPC_PCNT_df     = (0x01 << 18) | OPC_MSA_2R,
    OPC_NLOC_df     = (0x02 << 18) | OPC_MSA_2R,
    OPC_NLZC_df     = (0x03 << 18) | OPC_MSA_2R,

    /* 2RF instruction df(bit 16) = _w, _d */
    OPC_FCLASS_df   = (0x00 << 17) | OPC_MSA_2RF,
    OPC_FTRUNC_S_df = (0x01 << 17) | OPC_MSA_2RF,
    OPC_FTRUNC_U_df = (0x02 << 17) | OPC_MSA_2RF,
    OPC_FSQRT_df    = (0x03 << 17) | OPC_MSA_2RF,
    OPC_FRSQRT_df   = (0x04 << 17) | OPC_MSA_2RF,
    OPC_FRCP_df     = (0x05 << 17) | OPC_MSA_2RF,
    OPC_FRINT_df    = (0x06 << 17) | OPC_MSA_2RF,
    OPC_FLOG2_df    = (0x07 << 17) | OPC_MSA_2RF,
    OPC_FEXUPL_df   = (0x08 << 17) | OPC_MSA_2RF,
    OPC_FEXUPR_df   = (0x09 << 17) | OPC_MSA_2RF,
    OPC_FFQL_df     = (0x0A << 17) | OPC_MSA_2RF,
    OPC_FFQR_df     = (0x0B << 17) | OPC_MSA_2RF,
    OPC_FTINT_S_df  = (0x0C << 17) | OPC_MSA_2RF,
    OPC_FTINT_U_df  = (0x0D << 17) | OPC_MSA_2RF,
    OPC_FFINT_S_df  = (0x0E << 17) | OPC_MSA_2RF,
    OPC_FFINT_U_df  = (0x0F << 17) | OPC_MSA_2RF,

    /* 3R instruction df(bits 22..21) = _b, _h, _w, d */
    OPC_SLL_df      = (0x0 << 23) | OPC_MSA_3R_0D,
    OPC_ADDV_df     = (0x0 << 23) | OPC_MSA_3R_0E,
    OPC_CEQ_df      = (0x0 << 23) | OPC_MSA_3R_0F,
    OPC_ADD_A_df    = (0x0 << 23) | OPC_MSA_3R_10,
    OPC_SUBS_S_df   = (0x0 << 23) | OPC_MSA_3R_11,
    OPC_MULV_df     = (0x0 << 23) | OPC_MSA_3R_12,
    OPC_DOTP_S_df   = (0x0 << 23) | OPC_MSA_3R_13,
    OPC_SLD_df      = (0x0 << 23) | OPC_MSA_3R_14,
    OPC_VSHF_df     = (0x0 << 23) | OPC_MSA_3R_15,
    OPC_SRA_df      = (0x1 << 23) | OPC_MSA_3R_0D,
    OPC_SUBV_df     = (0x1 << 23) | OPC_MSA_3R_0E,
    OPC_ADDS_A_df   = (0x1 << 23) | OPC_MSA_3R_10,
    OPC_SUBS_U_df   = (0x1 << 23) | OPC_MSA_3R_11,
    OPC_MADDV_df    = (0x1 << 23) | OPC_MSA_3R_12,
    OPC_DOTP_U_df   = (0x1 << 23) | OPC_MSA_3R_13,
    OPC_SPLAT_df    = (0x1 << 23) | OPC_MSA_3R_14,
    OPC_SRAR_df     = (0x1 << 23) | OPC_MSA_3R_15,
    OPC_SRL_df      = (0x2 << 23) | OPC_MSA_3R_0D,
    OPC_MAX_S_df    = (0x2 << 23) | OPC_MSA_3R_0E,
    OPC_CLT_S_df    = (0x2 << 23) | OPC_MSA_3R_0F,
    OPC_ADDS_S_df   = (0x2 << 23) | OPC_MSA_3R_10,
    OPC_SUBSUS_U_df = (0x2 << 23) | OPC_MSA_3R_11,
    OPC_MSUBV_df    = (0x2 << 23) | OPC_MSA_3R_12,
    OPC_DPADD_S_df  = (0x2 << 23) | OPC_MSA_3R_13,
    OPC_PCKEV_df    = (0x2 << 23) | OPC_MSA_3R_14,
    OPC_SRLR_df     = (0x2 << 23) | OPC_MSA_3R_15,
    OPC_BCLR_df     = (0x3 << 23) | OPC_MSA_3R_0D,
    OPC_MAX_U_df    = (0x3 << 23) | OPC_MSA_3R_0E,
    OPC_CLT_U_df    = (0x3 << 23) | OPC_MSA_3R_0F,
    OPC_ADDS_U_df   = (0x3 << 23) | OPC_MSA_3R_10,
    OPC_SUBSUU_S_df = (0x3 << 23) | OPC_MSA_3R_11,
    OPC_DPADD_U_df  = (0x3 << 23) | OPC_MSA_3R_13,
    OPC_PCKOD_df    = (0x3 << 23) | OPC_MSA_3R_14,
    OPC_BSET_df     = (0x4 << 23) | OPC_MSA_3R_0D,
    OPC_MIN_S_df    = (0x4 << 23) | OPC_MSA_3R_0E,
    OPC_CLE_S_df    = (0x4 << 23) | OPC_MSA_3R_0F,
    OPC_AVE_S_df    = (0x4 << 23) | OPC_MSA_3R_10,
    OPC_ASUB_S_df   = (0x4 << 23) | OPC_MSA_3R_11,
    OPC_DIV_S_df    = (0x4 << 23) | OPC_MSA_3R_12,
    OPC_DPSUB_S_df  = (0x4 << 23) | OPC_MSA_3R_13,
    OPC_ILVL_df     = (0x4 << 23) | OPC_MSA_3R_14,
    OPC_HADD_S_df   = (0x4 << 23) | OPC_MSA_3R_15,
    OPC_BNEG_df     = (0x5 << 23) | OPC_MSA_3R_0D,
    OPC_MIN_U_df    = (0x5 << 23) | OPC_MSA_3R_0E,
    OPC_CLE_U_df    = (0x5 << 23) | OPC_MSA_3R_0F,
    OPC_AVE_U_df    = (0x5 << 23) | OPC_MSA_3R_10,
    OPC_ASUB_U_df   = (0x5 << 23) | OPC_MSA_3R_11,
    OPC_DIV_U_df    = (0x5 << 23) | OPC_MSA_3R_12,
    OPC_DPSUB_U_df  = (0x5 << 23) | OPC_MSA_3R_13,
    OPC_ILVR_df     = (0x5 << 23) | OPC_MSA_3R_14,
    OPC_HADD_U_df   = (0x5 << 23) | OPC_MSA_3R_15,
    OPC_BINSL_df    = (0x6 << 23) | OPC_MSA_3R_0D,
    OPC_MAX_A_df    = (0x6 << 23) | OPC_MSA_3R_0E,
    OPC_AVER_S_df   = (0x6 << 23) | OPC_MSA_3R_10,
    OPC_MOD_S_df    = (0x6 << 23) | OPC_MSA_3R_12,
    OPC_ILVEV_df    = (0x6 << 23) | OPC_MSA_3R_14,
    OPC_HSUB_S_df   = (0x6 << 23) | OPC_MSA_3R_15,
    OPC_BINSR_df    = (0x7 << 23) | OPC_MSA_3R_0D,
    OPC_MIN_A_df    = (0x7 << 23) | OPC_MSA_3R_0E,
    OPC_AVER_U_df   = (0x7 << 23) | OPC_MSA_3R_10,
    OPC_MOD_U_df    = (0x7 << 23) | OPC_MSA_3R_12,
    OPC_ILVOD_df    = (0x7 << 23) | OPC_MSA_3R_14,
    OPC_HSUB_U_df   = (0x7 << 23) | OPC_MSA_3R_15,

    /* ELM instructions df(bits 21..16) = _b, _h, _w, _d */
    OPC_SLDI_df     = (0x0 << 22) | (0x00 << 16) | OPC_MSA_ELM,
    OPC_CTCMSA      = (0x0 << 22) | (0x3E << 16) | OPC_MSA_ELM,
    OPC_SPLATI_df   = (0x1 << 22) | (0x00 << 16) | OPC_MSA_ELM,
    OPC_CFCMSA      = (0x1 << 22) | (0x3E << 16) | OPC_MSA_ELM,
    OPC_COPY_S_df   = (0x2 << 22) | (0x00 << 16) | OPC_MSA_ELM,
    OPC_MOVE_V      = (0x2 << 22) | (0x3E << 16) | OPC_MSA_ELM,
    OPC_COPY_U_df   = (0x3 << 22) | (0x00 << 16) | OPC_MSA_ELM,
    OPC_INSERT_df   = (0x4 << 22) | (0x00 << 16) | OPC_MSA_ELM,
    OPC_INSVE_df    = (0x5 << 22) | (0x00 << 16) | OPC_MSA_ELM,

    /* 3RF instruction _df(bit 21) = _w, _d */
    OPC_FCAF_df     = (0x0 << 22) | OPC_MSA_3RF_1A,
    OPC_FADD_df     = (0x0 << 22) | OPC_MSA_3RF_1B,
    OPC_FCUN_df     = (0x1 << 22) | OPC_MSA_3RF_1A,
    OPC_FSUB_df     = (0x1 << 22) | OPC_MSA_3RF_1B,
    OPC_FCOR_df     = (0x1 << 22) | OPC_MSA_3RF_1C,
    OPC_FCEQ_df     = (0x2 << 22) | OPC_MSA_3RF_1A,
    OPC_FMUL_df     = (0x2 << 22) | OPC_MSA_3RF_1B,
    OPC_FCUNE_df    = (0x2 << 22) | OPC_MSA_3RF_1C,
    OPC_FCUEQ_df    = (0x3 << 22) | OPC_MSA_3RF_1A,
    OPC_FDIV_df     = (0x3 << 22) | OPC_MSA_3RF_1B,
    OPC_FCNE_df     = (0x3 << 22) | OPC_MSA_3RF_1C,
    OPC_FCLT_df     = (0x4 << 22) | OPC_MSA_3RF_1A,
    OPC_FMADD_df    = (0x4 << 22) | OPC_MSA_3RF_1B,
    OPC_MUL_Q_df    = (0x4 << 22) | OPC_MSA_3RF_1C,
    OPC_FCULT_df    = (0x5 << 22) | OPC_MSA_3RF_1A,
    OPC_FMSUB_df    = (0x5 << 22) | OPC_MSA_3RF_1B,
    OPC_MADD_Q_df   = (0x5 << 22) | OPC_MSA_3RF_1C,
    OPC_FCLE_df     = (0x6 << 22) | OPC_MSA_3RF_1A,
    OPC_MSUB_Q_df   = (0x6 << 22) | OPC_MSA_3RF_1C,
    OPC_FCULE_df    = (0x7 << 22) | OPC_MSA_3RF_1A,
    OPC_FEXP2_df    = (0x7 << 22) | OPC_MSA_3RF_1B,
    OPC_FSAF_df     = (0x8 << 22) | OPC_MSA_3RF_1A,
    OPC_FEXDO_df    = (0x8 << 22) | OPC_MSA_3RF_1B,
    OPC_FSUN_df     = (0x9 << 22) | OPC_MSA_3RF_1A,
    OPC_FSOR_df     = (0x9 << 22) | OPC_MSA_3RF_1C,
    OPC_FSEQ_df     = (0xA << 22) | OPC_MSA_3RF_1A,
    OPC_FTQ_df      = (0xA << 22) | OPC_MSA_3RF_1B,
    OPC_FSUNE_df    = (0xA << 22) | OPC_MSA_3RF_1C,
    OPC_FSUEQ_df    = (0xB << 22) | OPC_MSA_3RF_1A,
    OPC_FSNE_df     = (0xB << 22) | OPC_MSA_3RF_1C,
    OPC_FSLT_df     = (0xC << 22) | OPC_MSA_3RF_1A,
    OPC_FMIN_df     = (0xC << 22) | OPC_MSA_3RF_1B,
    OPC_MULR_Q_df   = (0xC << 22) | OPC_MSA_3RF_1C,
    OPC_FSULT_df    = (0xD << 22) | OPC_MSA_3RF_1A,
    OPC_FMIN_A_df   = (0xD << 22) | OPC_MSA_3RF_1B,
    OPC_MADDR_Q_df  = (0xD << 22) | OPC_MSA_3RF_1C,
    OPC_FSLE_df     = (0xE << 22) | OPC_MSA_3RF_1A,
    OPC_FMAX_df     = (0xE << 22) | OPC_MSA_3RF_1B,
    OPC_MSUBR_Q_df  = (0xE << 22) | OPC_MSA_3RF_1C,
    OPC_FSULE_df    = (0xF << 22) | OPC_MSA_3RF_1A,
    OPC_FMAX_A_df   = (0xF << 22) | OPC_MSA_3RF_1B,

    /* BIT instruction df(bits 22..16) = _B _H _W _D */
    OPC_SLLI_df     = (0x0 << 23) | OPC_MSA_BIT_09,
    OPC_SAT_S_df    = (0x0 << 23) | OPC_MSA_BIT_0A,
    OPC_SRAI_df     = (0x1 << 23) | OPC_MSA_BIT_09,
    OPC_SAT_U_df    = (0x1 << 23) | OPC_MSA_BIT_0A,
    OPC_SRLI_df     = (0x2 << 23) | OPC_MSA_BIT_09,
    OPC_SRARI_df    = (0x2 << 23) | OPC_MSA_BIT_0A,
    OPC_BCLRI_df    = (0x3 << 23) | OPC_MSA_BIT_09,
    OPC_SRLRI_df    = (0x3 << 23) | OPC_MSA_BIT_0A,
    OPC_BSETI_df    = (0x4 << 23) | OPC_MSA_BIT_09,
    OPC_BNEGI_df    = (0x5 << 23) | OPC_MSA_BIT_09,
    OPC_BINSLI_df   = (0x6 << 23) | OPC_MSA_BIT_09,
    OPC_BINSRI_df   = (0x7 << 23) | OPC_MSA_BIT_09,
};

static const char * const msaregnames[] = {
    "w0.d0",  "w0.d1",  "w1.d0",  "w1.d1",
    "w2.d0",  "w2.d1",  "w3.d0",  "w3.d1",
    "w4.d0",  "w4.d1",  "w5.d0",  "w5.d1",
    "w6.d0",  "w6.d1",  "w7.d0",  "w7.d1",
    "w8.d0",  "w8.d1",  "w9.d0",  "w9.d1",
    "w10.d0", "w10.d1", "w11.d0", "w11.d1",
    "w12.d0", "w12.d1", "w13.d0", "w13.d1",
    "w14.d0", "w14.d1", "w15.d0", "w15.d1",
    "w16.d0", "w16.d1", "w17.d0", "w17.d1",
    "w18.d0", "w18.d1", "w19.d0", "w19.d1",
    "w20.d0", "w20.d1", "w21.d0", "w21.d1",
    "w22.d0", "w22.d1", "w23.d0", "w23.d1",
    "w24.d0", "w24.d1", "w25.d0", "w25.d1",
    "w26.d0", "w26.d1", "w27.d0", "w27.d1",
    "w28.d0", "w28.d1", "w29.d0", "w29.d1",
    "w30.d0", "w30.d1", "w31.d0", "w31.d1",
};

static TCGv_i64 msa_wr_d[64];

void msa_translate_init(void)
{
    int i;

    for (i = 0; i < 32; i++) {
        int off = offsetof(CPUMIPSState, active_fpu.fpr[i].wr.d[0]);

        /*
         * The MSA vector registers are mapped on the
         * scalar floating-point unit (FPU) registers.
         */
        msa_wr_d[i * 2] = fpu_f64[i];
        off = offsetof(CPUMIPSState, active_fpu.fpr[i].wr.d[1]);
        msa_wr_d[i * 2 + 1] =
                tcg_global_mem_new_i64(cpu_env, off, msaregnames[i * 2 + 1]);
    }
}

static inline int check_msa_access(DisasContext *ctx)
{
    if (unlikely((ctx->hflags & MIPS_HFLAG_FPU) &&
                 !(ctx->hflags & MIPS_HFLAG_F64))) {
        gen_reserved_instruction(ctx);
        return 0;
    }

    if (unlikely(!(ctx->hflags & MIPS_HFLAG_MSA))) {
        generate_exception_end(ctx, EXCP_MSADIS);
        return 0;
    }
    return 1;
}

static void gen_check_zero_element(TCGv tresult, uint8_t df, uint8_t wt,
                                   TCGCond cond)
{
    /* generates tcg ops to check if any element is 0 */
    /* Note this function only works with MSA_WRLEN = 128 */
    uint64_t eval_zero_or_big = 0;
    uint64_t eval_big = 0;
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    switch (df) {
    case DF_BYTE:
        eval_zero_or_big = 0x0101010101010101ULL;
        eval_big = 0x8080808080808080ULL;
        break;
    case DF_HALF:
        eval_zero_or_big = 0x0001000100010001ULL;
        eval_big = 0x8000800080008000ULL;
        break;
    case DF_WORD:
        eval_zero_or_big = 0x0000000100000001ULL;
        eval_big = 0x8000000080000000ULL;
        break;
    case DF_DOUBLE:
        eval_zero_or_big = 0x0000000000000001ULL;
        eval_big = 0x8000000000000000ULL;
        break;
    }
    tcg_gen_subi_i64(t0, msa_wr_d[wt << 1], eval_zero_or_big);
    tcg_gen_andc_i64(t0, t0, msa_wr_d[wt << 1]);
    tcg_gen_andi_i64(t0, t0, eval_big);
    tcg_gen_subi_i64(t1, msa_wr_d[(wt << 1) + 1], eval_zero_or_big);
    tcg_gen_andc_i64(t1, t1, msa_wr_d[(wt << 1) + 1]);
    tcg_gen_andi_i64(t1, t1, eval_big);
    tcg_gen_or_i64(t0, t0, t1);
    /* if all bits are zero then all elements are not zero */
    /* if some bit is non-zero then some element is zero */
    tcg_gen_setcondi_i64(cond, t0, t0, 0);
    tcg_gen_trunc_i64_tl(tresult, t0);
    tcg_temp_free_i64(t0);
    tcg_temp_free_i64(t1);
}

static bool gen_msa_BxZ_V(DisasContext *ctx, int wt, int s16, TCGCond cond)
{
    TCGv_i64 t0;

    check_msa_access(ctx);

    if (ctx->hflags & MIPS_HFLAG_BMASK) {
        gen_reserved_instruction(ctx);
        return true;
    }
    t0 = tcg_temp_new_i64();
    tcg_gen_or_i64(t0, msa_wr_d[wt << 1], msa_wr_d[(wt << 1) + 1]);
    tcg_gen_setcondi_i64(cond, t0, t0, 0);
    tcg_gen_trunc_i64_tl(bcond, t0);
    tcg_temp_free_i64(t0);

    ctx->btarget = ctx->base.pc_next + (s16 << 2) + 4;

    ctx->hflags |= MIPS_HFLAG_BC;
    ctx->hflags |= MIPS_HFLAG_BDS32;

    return true;
}

static bool trans_BZ_V(DisasContext *ctx, arg_msa_bz *a)
{
    return gen_msa_BxZ_V(ctx, a->wt, a->s16, TCG_COND_EQ);
}

static bool trans_BNZ_V(DisasContext *ctx, arg_msa_bz *a)
{
    return gen_msa_BxZ_V(ctx, a->wt, a->s16, TCG_COND_NE);
}

static bool gen_msa_BxZ(DisasContext *ctx, int df, int wt, int s16, bool if_not)
{
    check_msa_access(ctx);

    if (ctx->hflags & MIPS_HFLAG_BMASK) {
        gen_reserved_instruction(ctx);
        return true;
    }

    gen_check_zero_element(bcond, df, wt, if_not ? TCG_COND_EQ : TCG_COND_NE);

    ctx->btarget = ctx->base.pc_next + (s16 << 2) + 4;
    ctx->hflags |= MIPS_HFLAG_BC;
    ctx->hflags |= MIPS_HFLAG_BDS32;

    return true;
}

static bool trans_BZ_x(DisasContext *ctx, arg_msa_bz *a)
{
    return gen_msa_BxZ(ctx, a->df, a->wt, a->s16, false);
}

static bool trans_BNZ_x(DisasContext *ctx, arg_msa_bz *a)
{
    return gen_msa_BxZ(ctx, a->df, a->wt, a->s16, true);
}

static void gen_msa_i8(DisasContext *ctx)
{
#define MASK_MSA_I8(op)    (MASK_MSA_MINOR(op) | (op & (0x03 << 24)))
    uint8_t i8 = (ctx->opcode >> 16) & 0xff;
    uint8_t ws = (ctx->opcode >> 11) & 0x1f;
    uint8_t wd = (ctx->opcode >> 6) & 0x1f;

    TCGv_i32 twd = tcg_const_i32(wd);
    TCGv_i32 tws = tcg_const_i32(ws);
    TCGv_i32 ti8 = tcg_const_i32(i8);

    switch (MASK_MSA_I8(ctx->opcode)) {
    case OPC_ANDI_B:
        gen_helper_msa_andi_b(cpu_env, twd, tws, ti8);
        break;
    case OPC_ORI_B:
        gen_helper_msa_ori_b(cpu_env, twd, tws, ti8);
        break;
    case OPC_NORI_B:
        gen_helper_msa_nori_b(cpu_env, twd, tws, ti8);
        break;
    case OPC_XORI_B:
        gen_helper_msa_xori_b(cpu_env, twd, tws, ti8);
        break;
    case OPC_BMNZI_B:
        gen_helper_msa_bmnzi_b(cpu_env, twd, tws, ti8);
        break;
    case OPC_BMZI_B:
        gen_helper_msa_bmzi_b(cpu_env, twd, tws, ti8);
        break;
    case OPC_BSELI_B:
        gen_helper_msa_bseli_b(cpu_env, twd, tws, ti8);
        break;
    case OPC_SHF_B:
    case OPC_SHF_H:
    case OPC_SHF_W:
        {
            uint8_t df = (ctx->opcode >> 24) & 0x3;
            if (df == DF_DOUBLE) {
                gen_reserved_instruction(ctx);
            } else {
                TCGv_i32 tdf = tcg_const_i32(df);
                gen_helper_msa_shf_df(cpu_env, tdf, twd, tws, ti8);
                tcg_temp_free_i32(tdf);
            }
        }
        break;
    default:
        MIPS_INVAL("MSA instruction");
        gen_reserved_instruction(ctx);
        break;
    }

    tcg_temp_free_i32(twd);
    tcg_temp_free_i32(tws);
    tcg_temp_free_i32(ti8);
}

static void gen_msa_i5(DisasContext *ctx)
{
#define MASK_MSA_I5(op)    (MASK_MSA_MINOR(op) | (op & (0x7 << 23)))
    uint8_t df = (ctx->opcode >> 21) & 0x3;
    int8_t s5 = (int8_t) sextract32(ctx->opcode, 16, 5);
    uint8_t u5 = (ctx->opcode >> 16) & 0x1f;
    uint8_t ws = (ctx->opcode >> 11) & 0x1f;
    uint8_t wd = (ctx->opcode >> 6) & 0x1f;

    TCGv_i32 tdf = tcg_const_i32(df);
    TCGv_i32 twd = tcg_const_i32(wd);
    TCGv_i32 tws = tcg_const_i32(ws);
    TCGv_i32 timm = tcg_temp_new_i32();
    tcg_gen_movi_i32(timm, u5);

    switch (MASK_MSA_I5(ctx->opcode)) {
    case OPC_ADDVI_df:
        gen_helper_msa_addvi_df(cpu_env, tdf, twd, tws, timm);
        break;
    case OPC_SUBVI_df:
        gen_helper_msa_subvi_df(cpu_env, tdf, twd, tws, timm);
        break;
    case OPC_MAXI_S_df:
        tcg_gen_movi_i32(timm, s5);
        gen_helper_msa_maxi_s_df(cpu_env, tdf, twd, tws, timm);
        break;
    case OPC_MAXI_U_df:
        gen_helper_msa_maxi_u_df(cpu_env, tdf, twd, tws, timm);
        break;
    case OPC_MINI_S_df:
        tcg_gen_movi_i32(timm, s5);
        gen_helper_msa_mini_s_df(cpu_env, tdf, twd, tws, timm);
        break;
    case OPC_MINI_U_df:
        gen_helper_msa_mini_u_df(cpu_env, tdf, twd, tws, timm);
        break;
    case OPC_CEQI_df:
        tcg_gen_movi_i32(timm, s5);
        gen_helper_msa_ceqi_df(cpu_env, tdf, twd, tws, timm);
        break;
    case OPC_CLTI_S_df:
        tcg_gen_movi_i32(timm, s5);
        gen_helper_msa_clti_s_df(cpu_env, tdf, twd, tws, timm);
        break;
    case OPC_CLTI_U_df:
        gen_helper_msa_clti_u_df(cpu_env, tdf, twd, tws, timm);
        break;
    case OPC_CLEI_S_df:
        tcg_gen_movi_i32(timm, s5);
        gen_helper_msa_clei_s_df(cpu_env, tdf, twd, tws, timm);
        break;
    case OPC_CLEI_U_df:
        gen_helper_msa_clei_u_df(cpu_env, tdf, twd, tws, timm);
        break;
    case OPC_LDI_df:
        {
            int32_t s10 = sextract32(ctx->opcode, 11, 10);
            tcg_gen_movi_i32(timm, s10);
            gen_helper_msa_ldi_df(cpu_env, tdf, twd, timm);
        }
        break;
    default:
        MIPS_INVAL("MSA instruction");
        gen_reserved_instruction(ctx);
        break;
    }

    tcg_temp_free_i32(tdf);
    tcg_temp_free_i32(twd);
    tcg_temp_free_i32(tws);
    tcg_temp_free_i32(timm);
}

static void gen_msa_bit(DisasContext *ctx)
{
#define MASK_MSA_BIT(op)    (MASK_MSA_MINOR(op) | (op & (0x7 << 23)))
    uint8_t dfm = (ctx->opcode >> 16) & 0x7f;
    uint32_t df = 0, m = 0;
    uint8_t ws = (ctx->opcode >> 11) & 0x1f;
    uint8_t wd = (ctx->opcode >> 6) & 0x1f;

    TCGv_i32 tdf;
    TCGv_i32 tm;
    TCGv_i32 twd;
    TCGv_i32 tws;

    if ((dfm & 0x40) == 0x00) {
        m = dfm & 0x3f;
        df = DF_DOUBLE;
    } else if ((dfm & 0x60) == 0x40) {
        m = dfm & 0x1f;
        df = DF_WORD;
    } else if ((dfm & 0x70) == 0x60) {
        m = dfm & 0x0f;
        df = DF_HALF;
    } else if ((dfm & 0x78) == 0x70) {
        m = dfm & 0x7;
        df = DF_BYTE;
    } else {
        gen_reserved_instruction(ctx);
        return;
    }

    tdf = tcg_const_i32(df);
    tm  = tcg_const_i32(m);
    twd = tcg_const_i32(wd);
    tws = tcg_const_i32(ws);

    switch (MASK_MSA_BIT(ctx->opcode)) {
    case OPC_SLLI_df:
        gen_helper_msa_slli_df(cpu_env, tdf, twd, tws, tm);
        break;
    case OPC_SRAI_df:
        gen_helper_msa_srai_df(cpu_env, tdf, twd, tws, tm);
        break;
    case OPC_SRLI_df:
        gen_helper_msa_srli_df(cpu_env, tdf, twd, tws, tm);
        break;
    case OPC_BCLRI_df:
        gen_helper_msa_bclri_df(cpu_env, tdf, twd, tws, tm);
        break;
    case OPC_BSETI_df:
        gen_helper_msa_bseti_df(cpu_env, tdf, twd, tws, tm);
        break;
    case OPC_BNEGI_df:
        gen_helper_msa_bnegi_df(cpu_env, tdf, twd, tws, tm);
        break;
    case OPC_BINSLI_df:
        gen_helper_msa_binsli_df(cpu_env, tdf, twd, tws, tm);
        break;
    case OPC_BINSRI_df:
        gen_helper_msa_binsri_df(cpu_env, tdf, twd, tws, tm);
        break;
    case OPC_SAT_S_df:
        gen_helper_msa_sat_s_df(cpu_env, tdf, twd, tws, tm);
        break;
    case OPC_SAT_U_df:
        gen_helper_msa_sat_u_df(cpu_env, tdf, twd, tws, tm);
        break;
    case OPC_SRARI_df:
        gen_helper_msa_srari_df(cpu_env, tdf, twd, tws, tm);
        break;
    case OPC_SRLRI_df:
        gen_helper_msa_srlri_df(cpu_env, tdf, twd, tws, tm);
        break;
    default:
        MIPS_INVAL("MSA instruction");
        gen_reserved_instruction(ctx);
        break;
    }

    tcg_temp_free_i32(tdf);
    tcg_temp_free_i32(tm);
    tcg_temp_free_i32(twd);
    tcg_temp_free_i32(tws);
}

static void gen_msa_3r(DisasContext *ctx)
{
#define MASK_MSA_3R(op)    (MASK_MSA_MINOR(op) | (op & (0x7 << 23)))
    uint8_t df = (ctx->opcode >> 21) & 0x3;
    uint8_t wt = (ctx->opcode >> 16) & 0x1f;
    uint8_t ws = (ctx->opcode >> 11) & 0x1f;
    uint8_t wd = (ctx->opcode >> 6) & 0x1f;

    TCGv_i32 tdf = tcg_const_i32(df);
    TCGv_i32 twd = tcg_const_i32(wd);
    TCGv_i32 tws = tcg_const_i32(ws);
    TCGv_i32 twt = tcg_const_i32(wt);

    switch (MASK_MSA_3R(ctx->opcode)) {
    case OPC_BINSL_df:
        switch (df) {
        case DF_BYTE:
            gen_helper_msa_binsl_b(cpu_env, twd, tws, twt);
            break;
        case DF_HALF:
            gen_helper_msa_binsl_h(cpu_env, twd, tws, twt);
            break;
        case DF_WORD:
            gen_helper_msa_binsl_w(cpu_env, twd, tws, twt);
            break;
        case DF_DOUBLE:
            gen_helper_msa_binsl_d(cpu_env, twd, tws, twt);
            break;
        }
        break;
    case OPC_BINSR_df:
        switch (df) {
        case DF_BYTE:
            gen_helper_msa_binsr_b(cpu_env, twd, tws, twt);
            break;
        case DF_HALF:
            gen_helper_msa_binsr_h(cpu_env, twd, tws, twt);
            break;
        case DF_WORD:
            gen_helper_msa_binsr_w(cpu_env, twd, tws, twt);
            break;
        case DF_DOUBLE:
            gen_helper_msa_binsr_d(cpu_env, twd, tws, twt);
            break;
        }
        break;
    case OPC_BCLR_df:
        switch (df) {
        case DF_BYTE:
            gen_helper_msa_bclr_b(cpu_env, twd, tws, twt);
            break;
        case DF_HALF:
            gen_helper_msa_bclr_h(cpu_env, twd, tws, twt);
            break;
        case DF_WORD:
            gen_helper_msa_bclr_w(cpu_env, twd, tws, twt);
            break;
        case DF_DOUBLE:
            gen_helper_msa_bclr_d(cpu_env, twd, tws, twt);
            break;
        }
        break;
    case OPC_BNEG_df:
        switch (df) {
        case DF_BYTE:
            gen_helper_msa_bneg_b(cpu_env, twd, tws, twt);
            break;
        case DF_HALF:
            gen_helper_msa_bneg_h(cpu_env, twd, tws, twt);
            break;
        case DF_WORD:
            gen_helper_msa_bneg_w(cpu_env, twd, tws, twt);
            break;
        case DF_DOUBLE:
            gen_helper_msa_bneg_d(cpu_env, twd, tws, twt);
            break;
        }
        break;
    case OPC_BSET_df:
        switch (df) {
        case DF_BYTE:
            gen_helper_msa_bset_b(cpu_env, twd, tws, twt);
            break;
        case DF_HALF:
            gen_helper_msa_bset_h(cpu_env, twd, tws, twt);
            break;
        case DF_WORD:
            gen_helper_msa_bset_w(cpu_env, twd, tws, twt);
            break;
        case DF_DOUBLE:
            gen_helper_msa_bset_d(cpu_env, twd, tws, twt);
            break;
        }
        break;
    case OPC_ADD_A_df:
        switch (df) {
        case DF_BYTE:
            gen_helper_msa_add_a_b(cpu_env, twd, tws, twt);
            break;
        case DF_HALF:
            gen_helper_msa_add_a_h(cpu_env, twd, tws, twt);
            break;
        case DF_WORD:
            gen_helper_msa_add_a_w(cpu_env, twd, tws, twt);
            break;
        case DF_DOUBLE:
            gen_helper_msa_add_a_d(cpu_env, twd, tws, twt);
            break;
        }
        break;
    case OPC_ADDS_A_df:
        switch (df) {
        case DF_BYTE:
            gen_helper_msa_adds_a_b(cpu_env, twd, tws, twt);
            break;
        case DF_HALF:
            gen_helper_msa_adds_a_h(cpu_env, twd, tws, twt);
            break;
        case DF_WORD:
            gen_helper_msa_adds_a_w(cpu_env, twd, tws, twt);
            break;
        case DF_DOUBLE:
            gen_helper_msa_adds_a_d(cpu_env, twd, tws, twt);
            break;
        }
        break;
    case OPC_ADDS_S_df:
        switch (df) {
        case DF_BYTE:
            gen_helper_msa_adds_s_b(cpu_env, twd, tws, twt);
            break;
        case DF_HALF:
            gen_helper_msa_adds_s_h(cpu_env, twd, tws, twt);
            break;
        case DF_WORD:
            gen_helper_msa_adds_s_w(cpu_env, twd, tws, twt);
            break;
        case DF_DOUBLE:
            gen_helper_msa_adds_s_d(cpu_env, twd, tws, twt);
            break;
        }
        break;
    case OPC_ADDS_U_df:
        switch (df) {
        case DF_BYTE:
            gen_helper_msa_adds_u_b(cpu_env, twd, tws, twt);
            break;
        case DF_HALF:
            gen_helper_msa_adds_u_h(cpu_env, twd, tws, twt);
            break;
        case DF_WORD:
            gen_helper_msa_adds_u_w(cpu_env, twd, tws, twt);
            break;
        case DF_DOUBLE:
            gen_helper_msa_adds_u_d(cpu_env, twd, tws, twt);
            break;
        }
        break;
    case OPC_ADDV_df:
        switch (df) {
        case DF_BYTE:
            gen_helper_msa_addv_b(cpu_env, twd, tws, twt);
            break;
        case DF_HALF:
            gen_helper_msa_addv_h(cpu_env, twd, tws, twt);
            break;
        case DF_WORD:
            gen_helper_msa_addv_w(cpu_env, twd, tws, twt);
            break;
        case DF_DOUBLE:
            gen_helper_msa_addv_d(cpu_env, twd, tws, twt);
            break;
        }
        break;
    case OPC_AVE_S_df:
        switch (df) {
        case DF_BYTE:
            gen_helper_msa_ave_s_b(cpu_env, twd, tws, twt);
            break;
        case DF_HALF:
            gen_helper_msa_ave_s_h(cpu_env, twd, tws, twt);
            break;
        case DF_WORD:
            gen_helper_msa_ave_s_w(cpu_env, twd, tws, twt);
            break;
        case DF_DOUBLE:
            gen_helper_msa_ave_s_d(cpu_env, twd, tws, twt);
            break;
        }
        break;
    case OPC_AVE_U_df:
        switch (df) {
        case DF_BYTE:
            gen_helper_msa_ave_u_b(cpu_env, twd, tws, twt);
            break;
        case DF_HALF:
            gen_helper_msa_ave_u_h(cpu_env, twd, tws, twt);
            break;
        case DF_WORD:
            gen_helper_msa_ave_u_w(cpu_env, twd, tws, twt);
            break;
        case DF_DOUBLE:
            gen_helper_msa_ave_u_d(cpu_env, twd, tws, twt);
            break;
        }
        break;
    case OPC_AVER_S_df:
        switch (df) {
        case DF_BYTE:
            gen_helper_msa_aver_s_b(cpu_env, twd, tws, twt);
            break;
        case DF_HALF:
            gen_helper_msa_aver_s_h(cpu_env, twd, tws, twt);
            break;
        case DF_WORD:
            gen_helper_msa_aver_s_w(cpu_env, twd, tws, twt);
            break;
        case DF_DOUBLE:
            gen_helper_msa_aver_s_d(cpu_env, twd, tws, twt);
            break;
        }
        break;
    case OPC_AVER_U_df:
        switch (df) {
        case DF_BYTE:
            gen_helper_msa_aver_u_b(cpu_env, twd, tws, twt);
            break;
        case DF_HALF:
            gen_helper_msa_aver_u_h(cpu_env, twd, tws, twt);
            break;
        case DF_WORD:
            gen_helper_msa_aver_u_w(cpu_env, twd, tws, twt);
            break;
        case DF_DOUBLE:
            gen_helper_msa_aver_u_d(cpu_env, twd, tws, twt);
            break;
        }
        break;
    case OPC_CEQ_df:
        switch (df) {
        case DF_BYTE:
            gen_helper_msa_ceq_b(cpu_env, twd, tws, twt);
            break;
        case DF_HALF:
            gen_helper_msa_ceq_h(cpu_env, twd, tws, twt);
            break;
        case DF_WORD:
            gen_helper_msa_ceq_w(cpu_env, twd, tws, twt);
            break;
        case DF_DOUBLE:
            gen_helper_msa_ceq_d(cpu_env, twd, tws, twt);
            break;
        }
        break;
    case OPC_CLE_S_df:
        switch (df) {
        case DF_BYTE:
            gen_helper_msa_cle_s_b(cpu_env, twd, tws, twt);
            break;
        case DF_HALF:
            gen_helper_msa_cle_s_h(cpu_env, twd, tws, twt);
            break;
        case DF_WORD:
            gen_helper_msa_cle_s_w(cpu_env, twd, tws, twt);
            break;
        case DF_DOUBLE:
            gen_helper_msa_cle_s_d(cpu_env, twd, tws, twt);
            break;
        }
        break;
    case OPC_CLE_U_df:
        switch (df) {
        case DF_BYTE:
            gen_helper_msa_cle_u_b(cpu_env, twd, tws, twt);
            break;
        case DF_HALF:
            gen_helper_msa_cle_u_h(cpu_env, twd, tws, twt);
            break;
        case DF_WORD:
            gen_helper_msa_cle_u_w(cpu_env, twd, tws, twt);
            break;
        case DF_DOUBLE:
            gen_helper_msa_cle_u_d(cpu_env, twd, tws, twt);
            break;
        }
        break;
    case OPC_CLT_S_df:
        switch (df) {
        case DF_BYTE:
            gen_helper_msa_clt_s_b(cpu_env, twd, tws, twt);
            break;
        case DF_HALF:
            gen_helper_msa_clt_s_h(cpu_env, twd, tws, twt);
            break;
        case DF_WORD:
            gen_helper_msa_clt_s_w(cpu_env, twd, tws, twt);
            break;
        case DF_DOUBLE:
            gen_helper_msa_clt_s_d(cpu_env, twd, tws, twt);
            break;
        }
        break;
    case OPC_CLT_U_df:
        switch (df) {
        case DF_BYTE:
            gen_helper_msa_clt_u_b(cpu_env, twd, tws, twt);
            break;
        case DF_HALF:
            gen_helper_msa_clt_u_h(cpu_env, twd, tws, twt);
            break;
        case DF_WORD:
            gen_helper_msa_clt_u_w(cpu_env, twd, tws, twt);
            break;
        case DF_DOUBLE:
            gen_helper_msa_clt_u_d(cpu_env, twd, tws, twt);
            break;
        }
        break;
    case OPC_DIV_S_df:
        switch (df) {
        case DF_BYTE:
            gen_helper_msa_div_s_b(cpu_env, twd, tws, twt);
            break;
        case DF_HALF:
            gen_helper_msa_div_s_h(cpu_env, twd, tws, twt);
            break;
        case DF_WORD:
            gen_helper_msa_div_s_w(cpu_env, twd, tws, twt);
            break;
        case DF_DOUBLE:
            gen_helper_msa_div_s_d(cpu_env, twd, tws, twt);
            break;
        }
        break;
    case OPC_DIV_U_df:
        switch (df) {
        case DF_BYTE:
            gen_helper_msa_div_u_b(cpu_env, twd, tws, twt);
            break;
        case DF_HALF:
            gen_helper_msa_div_u_h(cpu_env, twd, tws, twt);
            break;
        case DF_WORD:
            gen_helper_msa_div_u_w(cpu_env, twd, tws, twt);
            break;
        case DF_DOUBLE:
            gen_helper_msa_div_u_d(cpu_env, twd, tws, twt);
            break;
        }
        break;
    case OPC_MAX_A_df:
        switch (df) {
        case DF_BYTE:
            gen_helper_msa_max_a_b(cpu_env, twd, tws, twt);
            break;
        case DF_HALF:
            gen_helper_msa_max_a_h(cpu_env, twd, tws, twt);
            break;
        case DF_WORD:
            gen_helper_msa_max_a_w(cpu_env, twd, tws, twt);
            break;
        case DF_DOUBLE:
            gen_helper_msa_max_a_d(cpu_env, twd, tws, twt);
            break;
        }
        break;
    case OPC_MAX_S_df:
        switch (df) {
        case DF_BYTE:
            gen_helper_msa_max_s_b(cpu_env, twd, tws, twt);
            break;
        case DF_HALF:
            gen_helper_msa_max_s_h(cpu_env, twd, tws, twt);
            break;
        case DF_WORD:
            gen_helper_msa_max_s_w(cpu_env, twd, tws, twt);
            break;
        case DF_DOUBLE:
            gen_helper_msa_max_s_d(cpu_env, twd, tws, twt);
            break;
        }
        break;
    case OPC_MAX_U_df:
        switch (df) {
        case DF_BYTE:
            gen_helper_msa_max_u_b(cpu_env, twd, tws, twt);
            break;
        case DF_HALF:
            gen_helper_msa_max_u_h(cpu_env, twd, tws, twt);
            break;
        case DF_WORD:
            gen_helper_msa_max_u_w(cpu_env, twd, tws, twt);
            break;
        case DF_DOUBLE:
            gen_helper_msa_max_u_d(cpu_env, twd, tws, twt);
            break;
        }
        break;
    case OPC_MIN_A_df:
        switch (df) {
        case DF_BYTE:
            gen_helper_msa_min_a_b(cpu_env, twd, tws, twt);
            break;
        case DF_HALF:
            gen_helper_msa_min_a_h(cpu_env, twd, tws, twt);
            break;
        case DF_WORD:
            gen_helper_msa_min_a_w(cpu_env, twd, tws, twt);
            break;
        case DF_DOUBLE:
            gen_helper_msa_min_a_d(cpu_env, twd, tws, twt);
            break;
        }
        break;
    case OPC_MIN_S_df:
        switch (df) {
        case DF_BYTE:
            gen_helper_msa_min_s_b(cpu_env, twd, tws, twt);
            break;
        case DF_HALF:
            gen_helper_msa_min_s_h(cpu_env, twd, tws, twt);
            break;
        case DF_WORD:
            gen_helper_msa_min_s_w(cpu_env, twd, tws, twt);
            break;
        case DF_DOUBLE:
            gen_helper_msa_min_s_d(cpu_env, twd, tws, twt);
            break;
        }
        break;
    case OPC_MIN_U_df:
        switch (df) {
        case DF_BYTE:
            gen_helper_msa_min_u_b(cpu_env, twd, tws, twt);
            break;
        case DF_HALF:
            gen_helper_msa_min_u_h(cpu_env, twd, tws, twt);
            break;
        case DF_WORD:
            gen_helper_msa_min_u_w(cpu_env, twd, tws, twt);
            break;
        case DF_DOUBLE:
            gen_helper_msa_min_u_d(cpu_env, twd, tws, twt);
            break;
        }
        break;
    case OPC_MOD_S_df:
        switch (df) {
        case DF_BYTE:
            gen_helper_msa_mod_s_b(cpu_env, twd, tws, twt);
            break;
        case DF_HALF:
            gen_helper_msa_mod_s_h(cpu_env, twd, tws, twt);
            break;
        case DF_WORD:
            gen_helper_msa_mod_s_w(cpu_env, twd, tws, twt);
            break;
        case DF_DOUBLE:
            gen_helper_msa_mod_s_d(cpu_env, twd, tws, twt);
            break;
        }
        break;
    case OPC_MOD_U_df:
        switch (df) {
        case DF_BYTE:
            gen_helper_msa_mod_u_b(cpu_env, twd, tws, twt);
            break;
        case DF_HALF:
            gen_helper_msa_mod_u_h(cpu_env, twd, tws, twt);
            break;
        case DF_WORD:
            gen_helper_msa_mod_u_w(cpu_env, twd, tws, twt);
            break;
        case DF_DOUBLE:
            gen_helper_msa_mod_u_d(cpu_env, twd, tws, twt);
            break;
        }
        break;
    case OPC_MADDV_df:
        switch (df) {
        case DF_BYTE:
            gen_helper_msa_maddv_b(cpu_env, twd, tws, twt);
            break;
        case DF_HALF:
            gen_helper_msa_maddv_h(cpu_env, twd, tws, twt);
            break;
        case DF_WORD:
            gen_helper_msa_maddv_w(cpu_env, twd, tws, twt);
            break;
        case DF_DOUBLE:
            gen_helper_msa_maddv_d(cpu_env, twd, tws, twt);
            break;
        }
        break;
    case OPC_MSUBV_df:
        switch (df) {
        case DF_BYTE:
            gen_helper_msa_msubv_b(cpu_env, twd, tws, twt);
            break;
        case DF_HALF:
            gen_helper_msa_msubv_h(cpu_env, twd, tws, twt);
            break;
        case DF_WORD:
            gen_helper_msa_msubv_w(cpu_env, twd, tws, twt);
            break;
        case DF_DOUBLE:
            gen_helper_msa_msubv_d(cpu_env, twd, tws, twt);
            break;
        }
        break;
    case OPC_ASUB_S_df:
        switch (df) {
        case DF_BYTE:
            gen_helper_msa_asub_s_b(cpu_env, twd, tws, twt);
            break;
        case DF_HALF:
            gen_helper_msa_asub_s_h(cpu_env, twd, tws, twt);
            break;
        case DF_WORD:
            gen_helper_msa_asub_s_w(cpu_env, twd, tws, twt);
            break;
        case DF_DOUBLE:
            gen_helper_msa_asub_s_d(cpu_env, twd, tws, twt);
            break;
        }
        break;
    case OPC_ASUB_U_df:
        switch (df) {
        case DF_BYTE:
            gen_helper_msa_asub_u_b(cpu_env, twd, tws, twt);
            break;
        case DF_HALF:
            gen_helper_msa_asub_u_h(cpu_env, twd, tws, twt);
            break;
        case DF_WORD:
            gen_helper_msa_asub_u_w(cpu_env, twd, tws, twt);
            break;
        case DF_DOUBLE:
            gen_helper_msa_asub_u_d(cpu_env, twd, tws, twt);
            break;
        }
        break;
    case OPC_ILVEV_df:
        switch (df) {
        case DF_BYTE:
            gen_helper_msa_ilvev_b(cpu_env, twd, tws, twt);
            break;
        case DF_HALF:
            gen_helper_msa_ilvev_h(cpu_env, twd, tws, twt);
            break;
        case DF_WORD:
            gen_helper_msa_ilvev_w(cpu_env, twd, tws, twt);
            break;
        case DF_DOUBLE:
            gen_helper_msa_ilvev_d(cpu_env, twd, tws, twt);
            break;
        }
        break;
    case OPC_ILVOD_df:
        switch (df) {
        case DF_BYTE:
            gen_helper_msa_ilvod_b(cpu_env, twd, tws, twt);
            break;
        case DF_HALF:
            gen_helper_msa_ilvod_h(cpu_env, twd, tws, twt);
            break;
        case DF_WORD:
            gen_helper_msa_ilvod_w(cpu_env, twd, tws, twt);
            break;
        case DF_DOUBLE:
            gen_helper_msa_ilvod_d(cpu_env, twd, tws, twt);
            break;
        }
        break;
    case OPC_ILVL_df:
        switch (df) {
        case DF_BYTE:
            gen_helper_msa_ilvl_b(cpu_env, twd, tws, twt);
            break;
        case DF_HALF:
            gen_helper_msa_ilvl_h(cpu_env, twd, tws, twt);
            break;
        case DF_WORD:
            gen_helper_msa_ilvl_w(cpu_env, twd, tws, twt);
            break;
        case DF_DOUBLE:
            gen_helper_msa_ilvl_d(cpu_env, twd, tws, twt);
            break;
        }
        break;
    case OPC_ILVR_df:
        switch (df) {
        case DF_BYTE:
            gen_helper_msa_ilvr_b(cpu_env, twd, tws, twt);
            break;
        case DF_HALF:
            gen_helper_msa_ilvr_h(cpu_env, twd, tws, twt);
            break;
        case DF_WORD:
            gen_helper_msa_ilvr_w(cpu_env, twd, tws, twt);
            break;
        case DF_DOUBLE:
            gen_helper_msa_ilvr_d(cpu_env, twd, tws, twt);
            break;
        }
        break;
    case OPC_PCKEV_df:
        switch (df) {
        case DF_BYTE:
            gen_helper_msa_pckev_b(cpu_env, twd, tws, twt);
            break;
        case DF_HALF:
            gen_helper_msa_pckev_h(cpu_env, twd, tws, twt);
            break;
        case DF_WORD:
            gen_helper_msa_pckev_w(cpu_env, twd, tws, twt);
            break;
        case DF_DOUBLE:
            gen_helper_msa_pckev_d(cpu_env, twd, tws, twt);
            break;
        }
        break;
    case OPC_PCKOD_df:
        switch (df) {
        case DF_BYTE:
            gen_helper_msa_pckod_b(cpu_env, twd, tws, twt);
            break;
        case DF_HALF:
            gen_helper_msa_pckod_h(cpu_env, twd, tws, twt);
            break;
        case DF_WORD:
            gen_helper_msa_pckod_w(cpu_env, twd, tws, twt);
            break;
        case DF_DOUBLE:
            gen_helper_msa_pckod_d(cpu_env, twd, tws, twt);
            break;
        }
        break;
    case OPC_SLL_df:
        switch (df) {
        case DF_BYTE:
            gen_helper_msa_sll_b(cpu_env, twd, tws, twt);
            break;
        case DF_HALF:
            gen_helper_msa_sll_h(cpu_env, twd, tws, twt);
            break;
        case DF_WORD:
            gen_helper_msa_sll_w(cpu_env, twd, tws, twt);
            break;
        case DF_DOUBLE:
            gen_helper_msa_sll_d(cpu_env, twd, tws, twt);
            break;
        }
        break;
    case OPC_SRA_df:
        switch (df) {
        case DF_BYTE:
            gen_helper_msa_sra_b(cpu_env, twd, tws, twt);
            break;
        case DF_HALF:
            gen_helper_msa_sra_h(cpu_env, twd, tws, twt);
            break;
        case DF_WORD:
            gen_helper_msa_sra_w(cpu_env, twd, tws, twt);
            break;
        case DF_DOUBLE:
            gen_helper_msa_sra_d(cpu_env, twd, tws, twt);
            break;
        }
        break;
    case OPC_SRAR_df:
        switch (df) {
        case DF_BYTE:
            gen_helper_msa_srar_b(cpu_env, twd, tws, twt);
            break;
        case DF_HALF:
            gen_helper_msa_srar_h(cpu_env, twd, tws, twt);
            break;
        case DF_WORD:
            gen_helper_msa_srar_w(cpu_env, twd, tws, twt);
            break;
        case DF_DOUBLE:
            gen_helper_msa_srar_d(cpu_env, twd, tws, twt);
            break;
        }
        break;
    case OPC_SRL_df:
        switch (df) {
        case DF_BYTE:
            gen_helper_msa_srl_b(cpu_env, twd, tws, twt);
            break;
        case DF_HALF:
            gen_helper_msa_srl_h(cpu_env, twd, tws, twt);
            break;
        case DF_WORD:
            gen_helper_msa_srl_w(cpu_env, twd, tws, twt);
            break;
        case DF_DOUBLE:
            gen_helper_msa_srl_d(cpu_env, twd, tws, twt);
            break;
        }
        break;
    case OPC_SRLR_df:
        switch (df) {
        case DF_BYTE:
            gen_helper_msa_srlr_b(cpu_env, twd, tws, twt);
            break;
        case DF_HALF:
            gen_helper_msa_srlr_h(cpu_env, twd, tws, twt);
            break;
        case DF_WORD:
            gen_helper_msa_srlr_w(cpu_env, twd, tws, twt);
            break;
        case DF_DOUBLE:
            gen_helper_msa_srlr_d(cpu_env, twd, tws, twt);
            break;
        }
        break;
    case OPC_SUBS_S_df:
        switch (df) {
        case DF_BYTE:
            gen_helper_msa_subs_s_b(cpu_env, twd, tws, twt);
            break;
        case DF_HALF:
            gen_helper_msa_subs_s_h(cpu_env, twd, tws, twt);
            break;
        case DF_WORD:
            gen_helper_msa_subs_s_w(cpu_env, twd, tws, twt);
            break;
        case DF_DOUBLE:
            gen_helper_msa_subs_s_d(cpu_env, twd, tws, twt);
            break;
        }
        break;
    case OPC_MULV_df:
        switch (df) {
        case DF_BYTE:
            gen_helper_msa_mulv_b(cpu_env, twd, tws, twt);
            break;
        case DF_HALF:
            gen_helper_msa_mulv_h(cpu_env, twd, tws, twt);
            break;
        case DF_WORD:
            gen_helper_msa_mulv_w(cpu_env, twd, tws, twt);
            break;
        case DF_DOUBLE:
            gen_helper_msa_mulv_d(cpu_env, twd, tws, twt);
            break;
        }
        break;
    case OPC_SLD_df:
        gen_helper_msa_sld_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_VSHF_df:
        gen_helper_msa_vshf_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_SUBV_df:
        switch (df) {
        case DF_BYTE:
            gen_helper_msa_subv_b(cpu_env, twd, tws, twt);
            break;
        case DF_HALF:
            gen_helper_msa_subv_h(cpu_env, twd, tws, twt);
            break;
        case DF_WORD:
            gen_helper_msa_subv_w(cpu_env, twd, tws, twt);
            break;
        case DF_DOUBLE:
            gen_helper_msa_subv_d(cpu_env, twd, tws, twt);
            break;
        }
        break;
    case OPC_SUBS_U_df:
        switch (df) {
        case DF_BYTE:
            gen_helper_msa_subs_u_b(cpu_env, twd, tws, twt);
            break;
        case DF_HALF:
            gen_helper_msa_subs_u_h(cpu_env, twd, tws, twt);
            break;
        case DF_WORD:
            gen_helper_msa_subs_u_w(cpu_env, twd, tws, twt);
            break;
        case DF_DOUBLE:
            gen_helper_msa_subs_u_d(cpu_env, twd, tws, twt);
            break;
        }
        break;
    case OPC_SPLAT_df:
        gen_helper_msa_splat_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_SUBSUS_U_df:
        switch (df) {
        case DF_BYTE:
            gen_helper_msa_subsus_u_b(cpu_env, twd, tws, twt);
            break;
        case DF_HALF:
            gen_helper_msa_subsus_u_h(cpu_env, twd, tws, twt);
            break;
        case DF_WORD:
            gen_helper_msa_subsus_u_w(cpu_env, twd, tws, twt);
            break;
        case DF_DOUBLE:
            gen_helper_msa_subsus_u_d(cpu_env, twd, tws, twt);
            break;
        }
        break;
    case OPC_SUBSUU_S_df:
        switch (df) {
        case DF_BYTE:
            gen_helper_msa_subsuu_s_b(cpu_env, twd, tws, twt);
            break;
        case DF_HALF:
            gen_helper_msa_subsuu_s_h(cpu_env, twd, tws, twt);
            break;
        case DF_WORD:
            gen_helper_msa_subsuu_s_w(cpu_env, twd, tws, twt);
            break;
        case DF_DOUBLE:
            gen_helper_msa_subsuu_s_d(cpu_env, twd, tws, twt);
            break;
        }
        break;

    case OPC_DOTP_S_df:
    case OPC_DOTP_U_df:
    case OPC_DPADD_S_df:
    case OPC_DPADD_U_df:
    case OPC_DPSUB_S_df:
    case OPC_HADD_S_df:
    case OPC_DPSUB_U_df:
    case OPC_HADD_U_df:
    case OPC_HSUB_S_df:
    case OPC_HSUB_U_df:
        if (df == DF_BYTE) {
            gen_reserved_instruction(ctx);
            break;
        }
        switch (MASK_MSA_3R(ctx->opcode)) {
        case OPC_HADD_S_df:
            switch (df) {
            case DF_HALF:
                gen_helper_msa_hadd_s_h(cpu_env, twd, tws, twt);
                break;
            case DF_WORD:
                gen_helper_msa_hadd_s_w(cpu_env, twd, tws, twt);
                break;
            case DF_DOUBLE:
                gen_helper_msa_hadd_s_d(cpu_env, twd, tws, twt);
                break;
            }
            break;
        case OPC_HADD_U_df:
            switch (df) {
            case DF_HALF:
                gen_helper_msa_hadd_u_h(cpu_env, twd, tws, twt);
                break;
            case DF_WORD:
                gen_helper_msa_hadd_u_w(cpu_env, twd, tws, twt);
                break;
            case DF_DOUBLE:
                gen_helper_msa_hadd_u_d(cpu_env, twd, tws, twt);
                break;
            }
            break;
        case OPC_HSUB_S_df:
            switch (df) {
            case DF_HALF:
                gen_helper_msa_hsub_s_h(cpu_env, twd, tws, twt);
                break;
            case DF_WORD:
                gen_helper_msa_hsub_s_w(cpu_env, twd, tws, twt);
                break;
            case DF_DOUBLE:
                gen_helper_msa_hsub_s_d(cpu_env, twd, tws, twt);
                break;
            }
            break;
        case OPC_HSUB_U_df:
            switch (df) {
            case DF_HALF:
                gen_helper_msa_hsub_u_h(cpu_env, twd, tws, twt);
                break;
            case DF_WORD:
                gen_helper_msa_hsub_u_w(cpu_env, twd, tws, twt);
                break;
            case DF_DOUBLE:
                gen_helper_msa_hsub_u_d(cpu_env, twd, tws, twt);
                break;
            }
            break;
        case OPC_DOTP_S_df:
            switch (df) {
            case DF_HALF:
                gen_helper_msa_dotp_s_h(cpu_env, twd, tws, twt);
                break;
            case DF_WORD:
                gen_helper_msa_dotp_s_w(cpu_env, twd, tws, twt);
                break;
            case DF_DOUBLE:
                gen_helper_msa_dotp_s_d(cpu_env, twd, tws, twt);
                break;
            }
            break;
        case OPC_DOTP_U_df:
            switch (df) {
            case DF_HALF:
                gen_helper_msa_dotp_u_h(cpu_env, twd, tws, twt);
                break;
            case DF_WORD:
                gen_helper_msa_dotp_u_w(cpu_env, twd, tws, twt);
                break;
            case DF_DOUBLE:
                gen_helper_msa_dotp_u_d(cpu_env, twd, tws, twt);
                break;
            }
            break;
        case OPC_DPADD_S_df:
            switch (df) {
            case DF_HALF:
                gen_helper_msa_dpadd_s_h(cpu_env, twd, tws, twt);
                break;
            case DF_WORD:
                gen_helper_msa_dpadd_s_w(cpu_env, twd, tws, twt);
                break;
            case DF_DOUBLE:
                gen_helper_msa_dpadd_s_d(cpu_env, twd, tws, twt);
                break;
            }
            break;
        case OPC_DPADD_U_df:
            switch (df) {
            case DF_HALF:
                gen_helper_msa_dpadd_u_h(cpu_env, twd, tws, twt);
                break;
            case DF_WORD:
                gen_helper_msa_dpadd_u_w(cpu_env, twd, tws, twt);
                break;
            case DF_DOUBLE:
                gen_helper_msa_dpadd_u_d(cpu_env, twd, tws, twt);
                break;
            }
            break;
        case OPC_DPSUB_S_df:
            switch (df) {
            case DF_HALF:
                gen_helper_msa_dpsub_s_h(cpu_env, twd, tws, twt);
                break;
            case DF_WORD:
                gen_helper_msa_dpsub_s_w(cpu_env, twd, tws, twt);
                break;
            case DF_DOUBLE:
                gen_helper_msa_dpsub_s_d(cpu_env, twd, tws, twt);
                break;
            }
            break;
        case OPC_DPSUB_U_df:
            switch (df) {
            case DF_HALF:
                gen_helper_msa_dpsub_u_h(cpu_env, twd, tws, twt);
                break;
            case DF_WORD:
                gen_helper_msa_dpsub_u_w(cpu_env, twd, tws, twt);
                break;
            case DF_DOUBLE:
                gen_helper_msa_dpsub_u_d(cpu_env, twd, tws, twt);
                break;
            }
            break;
        }
        break;
    default:
        MIPS_INVAL("MSA instruction");
        gen_reserved_instruction(ctx);
        break;
    }
    tcg_temp_free_i32(twd);
    tcg_temp_free_i32(tws);
    tcg_temp_free_i32(twt);
    tcg_temp_free_i32(tdf);
}

static void gen_msa_elm_3e(DisasContext *ctx)
{
#define MASK_MSA_ELM_DF3E(op)   (MASK_MSA_MINOR(op) | (op & (0x3FF << 16)))
    uint8_t source = (ctx->opcode >> 11) & 0x1f;
    uint8_t dest = (ctx->opcode >> 6) & 0x1f;
    TCGv telm = tcg_temp_new();
    TCGv_i32 tsr = tcg_const_i32(source);
    TCGv_i32 tdt = tcg_const_i32(dest);

    switch (MASK_MSA_ELM_DF3E(ctx->opcode)) {
    case OPC_CTCMSA:
        gen_load_gpr(telm, source);
        gen_helper_msa_ctcmsa(cpu_env, telm, tdt);
        break;
    case OPC_CFCMSA:
        gen_helper_msa_cfcmsa(telm, cpu_env, tsr);
        gen_store_gpr(telm, dest);
        break;
    case OPC_MOVE_V:
        gen_helper_msa_move_v(cpu_env, tdt, tsr);
        break;
    default:
        MIPS_INVAL("MSA instruction");
        gen_reserved_instruction(ctx);
        break;
    }

    tcg_temp_free(telm);
    tcg_temp_free_i32(tdt);
    tcg_temp_free_i32(tsr);
}

static void gen_msa_elm_df(DisasContext *ctx, uint32_t df, uint32_t n)
{
#define MASK_MSA_ELM(op)    (MASK_MSA_MINOR(op) | (op & (0xf << 22)))
    uint8_t ws = (ctx->opcode >> 11) & 0x1f;
    uint8_t wd = (ctx->opcode >> 6) & 0x1f;

    TCGv_i32 tws = tcg_const_i32(ws);
    TCGv_i32 twd = tcg_const_i32(wd);
    TCGv_i32 tn  = tcg_const_i32(n);
    TCGv_i32 tdf = tcg_const_i32(df);

    switch (MASK_MSA_ELM(ctx->opcode)) {
    case OPC_SLDI_df:
        gen_helper_msa_sldi_df(cpu_env, tdf, twd, tws, tn);
        break;
    case OPC_SPLATI_df:
        gen_helper_msa_splati_df(cpu_env, tdf, twd, tws, tn);
        break;
    case OPC_INSVE_df:
        gen_helper_msa_insve_df(cpu_env, tdf, twd, tws, tn);
        break;
    case OPC_COPY_S_df:
    case OPC_COPY_U_df:
    case OPC_INSERT_df:
#if !defined(TARGET_MIPS64)
        /* Double format valid only for MIPS64 */
        if (df == DF_DOUBLE) {
            gen_reserved_instruction(ctx);
            break;
        }
        if ((MASK_MSA_ELM(ctx->opcode) == OPC_COPY_U_df) &&
              (df == DF_WORD)) {
            gen_reserved_instruction(ctx);
            break;
        }
#endif
        switch (MASK_MSA_ELM(ctx->opcode)) {
        case OPC_COPY_S_df:
            if (likely(wd != 0)) {
                switch (df) {
                case DF_BYTE:
                    gen_helper_msa_copy_s_b(cpu_env, twd, tws, tn);
                    break;
                case DF_HALF:
                    gen_helper_msa_copy_s_h(cpu_env, twd, tws, tn);
                    break;
                case DF_WORD:
                    gen_helper_msa_copy_s_w(cpu_env, twd, tws, tn);
                    break;
#if defined(TARGET_MIPS64)
                case DF_DOUBLE:
                    gen_helper_msa_copy_s_d(cpu_env, twd, tws, tn);
                    break;
#endif
                default:
                    assert(0);
                }
            }
            break;
        case OPC_COPY_U_df:
            if (likely(wd != 0)) {
                switch (df) {
                case DF_BYTE:
                    gen_helper_msa_copy_u_b(cpu_env, twd, tws, tn);
                    break;
                case DF_HALF:
                    gen_helper_msa_copy_u_h(cpu_env, twd, tws, tn);
                    break;
#if defined(TARGET_MIPS64)
                case DF_WORD:
                    gen_helper_msa_copy_u_w(cpu_env, twd, tws, tn);
                    break;
#endif
                default:
                    assert(0);
                }
            }
            break;
        case OPC_INSERT_df:
            switch (df) {
            case DF_BYTE:
                gen_helper_msa_insert_b(cpu_env, twd, tws, tn);
                break;
            case DF_HALF:
                gen_helper_msa_insert_h(cpu_env, twd, tws, tn);
                break;
            case DF_WORD:
                gen_helper_msa_insert_w(cpu_env, twd, tws, tn);
                break;
#if defined(TARGET_MIPS64)
            case DF_DOUBLE:
                gen_helper_msa_insert_d(cpu_env, twd, tws, tn);
                break;
#endif
            default:
                assert(0);
            }
            break;
        }
        break;
    default:
        MIPS_INVAL("MSA instruction");
        gen_reserved_instruction(ctx);
    }
    tcg_temp_free_i32(twd);
    tcg_temp_free_i32(tws);
    tcg_temp_free_i32(tn);
    tcg_temp_free_i32(tdf);
}

static void gen_msa_elm(DisasContext *ctx)
{
    uint8_t dfn = (ctx->opcode >> 16) & 0x3f;
    uint32_t df = 0, n = 0;

    if ((dfn & 0x30) == 0x00) {
        n = dfn & 0x0f;
        df = DF_BYTE;
    } else if ((dfn & 0x38) == 0x20) {
        n = dfn & 0x07;
        df = DF_HALF;
    } else if ((dfn & 0x3c) == 0x30) {
        n = dfn & 0x03;
        df = DF_WORD;
    } else if ((dfn & 0x3e) == 0x38) {
        n = dfn & 0x01;
        df = DF_DOUBLE;
    } else if (dfn == 0x3E) {
        /* CTCMSA, CFCMSA, MOVE.V */
        gen_msa_elm_3e(ctx);
        return;
    } else {
        gen_reserved_instruction(ctx);
        return;
    }

    gen_msa_elm_df(ctx, df, n);
}

static void gen_msa_3rf(DisasContext *ctx)
{
#define MASK_MSA_3RF(op)    (MASK_MSA_MINOR(op) | (op & (0xf << 22)))
    uint8_t df = (ctx->opcode >> 21) & 0x1;
    uint8_t wt = (ctx->opcode >> 16) & 0x1f;
    uint8_t ws = (ctx->opcode >> 11) & 0x1f;
    uint8_t wd = (ctx->opcode >> 6) & 0x1f;

    TCGv_i32 twd = tcg_const_i32(wd);
    TCGv_i32 tws = tcg_const_i32(ws);
    TCGv_i32 twt = tcg_const_i32(wt);
    TCGv_i32 tdf = tcg_temp_new_i32();

    /* adjust df value for floating-point instruction */
    tcg_gen_movi_i32(tdf, df + 2);

    switch (MASK_MSA_3RF(ctx->opcode)) {
    case OPC_FCAF_df:
        gen_helper_msa_fcaf_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_FADD_df:
        gen_helper_msa_fadd_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_FCUN_df:
        gen_helper_msa_fcun_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_FSUB_df:
        gen_helper_msa_fsub_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_FCOR_df:
        gen_helper_msa_fcor_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_FCEQ_df:
        gen_helper_msa_fceq_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_FMUL_df:
        gen_helper_msa_fmul_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_FCUNE_df:
        gen_helper_msa_fcune_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_FCUEQ_df:
        gen_helper_msa_fcueq_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_FDIV_df:
        gen_helper_msa_fdiv_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_FCNE_df:
        gen_helper_msa_fcne_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_FCLT_df:
        gen_helper_msa_fclt_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_FMADD_df:
        gen_helper_msa_fmadd_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_MUL_Q_df:
        tcg_gen_movi_i32(tdf, df + 1);
        gen_helper_msa_mul_q_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_FCULT_df:
        gen_helper_msa_fcult_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_FMSUB_df:
        gen_helper_msa_fmsub_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_MADD_Q_df:
        tcg_gen_movi_i32(tdf, df + 1);
        gen_helper_msa_madd_q_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_FCLE_df:
        gen_helper_msa_fcle_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_MSUB_Q_df:
        tcg_gen_movi_i32(tdf, df + 1);
        gen_helper_msa_msub_q_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_FCULE_df:
        gen_helper_msa_fcule_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_FEXP2_df:
        gen_helper_msa_fexp2_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_FSAF_df:
        gen_helper_msa_fsaf_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_FEXDO_df:
        gen_helper_msa_fexdo_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_FSUN_df:
        gen_helper_msa_fsun_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_FSOR_df:
        gen_helper_msa_fsor_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_FSEQ_df:
        gen_helper_msa_fseq_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_FTQ_df:
        gen_helper_msa_ftq_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_FSUNE_df:
        gen_helper_msa_fsune_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_FSUEQ_df:
        gen_helper_msa_fsueq_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_FSNE_df:
        gen_helper_msa_fsne_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_FSLT_df:
        gen_helper_msa_fslt_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_FMIN_df:
        gen_helper_msa_fmin_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_MULR_Q_df:
        tcg_gen_movi_i32(tdf, df + 1);
        gen_helper_msa_mulr_q_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_FSULT_df:
        gen_helper_msa_fsult_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_FMIN_A_df:
        gen_helper_msa_fmin_a_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_MADDR_Q_df:
        tcg_gen_movi_i32(tdf, df + 1);
        gen_helper_msa_maddr_q_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_FSLE_df:
        gen_helper_msa_fsle_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_FMAX_df:
        gen_helper_msa_fmax_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_MSUBR_Q_df:
        tcg_gen_movi_i32(tdf, df + 1);
        gen_helper_msa_msubr_q_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_FSULE_df:
        gen_helper_msa_fsule_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_FMAX_A_df:
        gen_helper_msa_fmax_a_df(cpu_env, tdf, twd, tws, twt);
        break;
    default:
        MIPS_INVAL("MSA instruction");
        gen_reserved_instruction(ctx);
        break;
    }

    tcg_temp_free_i32(twd);
    tcg_temp_free_i32(tws);
    tcg_temp_free_i32(twt);
    tcg_temp_free_i32(tdf);
}

static void gen_msa_2r(DisasContext *ctx)
{
#define MASK_MSA_2R(op)     (MASK_MSA_MINOR(op) | (op & (0x1f << 21)) | \
                            (op & (0x7 << 18)))
    uint8_t wt = (ctx->opcode >> 16) & 0x1f;
    uint8_t ws = (ctx->opcode >> 11) & 0x1f;
    uint8_t wd = (ctx->opcode >> 6) & 0x1f;
    uint8_t df = (ctx->opcode >> 16) & 0x3;
    TCGv_i32 twd = tcg_const_i32(wd);
    TCGv_i32 tws = tcg_const_i32(ws);
    TCGv_i32 twt = tcg_const_i32(wt);
    TCGv_i32 tdf = tcg_const_i32(df);

    switch (MASK_MSA_2R(ctx->opcode)) {
    case OPC_FILL_df:
#if !defined(TARGET_MIPS64)
        /* Double format valid only for MIPS64 */
        if (df == DF_DOUBLE) {
            gen_reserved_instruction(ctx);
            break;
        }
#endif
        gen_helper_msa_fill_df(cpu_env, tdf, twd, tws); /* trs */
        break;
    case OPC_NLOC_df:
        switch (df) {
        case DF_BYTE:
            gen_helper_msa_nloc_b(cpu_env, twd, tws);
            break;
        case DF_HALF:
            gen_helper_msa_nloc_h(cpu_env, twd, tws);
            break;
        case DF_WORD:
            gen_helper_msa_nloc_w(cpu_env, twd, tws);
            break;
        case DF_DOUBLE:
            gen_helper_msa_nloc_d(cpu_env, twd, tws);
            break;
        }
        break;
    case OPC_NLZC_df:
        switch (df) {
        case DF_BYTE:
            gen_helper_msa_nlzc_b(cpu_env, twd, tws);
            break;
        case DF_HALF:
            gen_helper_msa_nlzc_h(cpu_env, twd, tws);
            break;
        case DF_WORD:
            gen_helper_msa_nlzc_w(cpu_env, twd, tws);
            break;
        case DF_DOUBLE:
            gen_helper_msa_nlzc_d(cpu_env, twd, tws);
            break;
        }
        break;
    case OPC_PCNT_df:
        switch (df) {
        case DF_BYTE:
            gen_helper_msa_pcnt_b(cpu_env, twd, tws);
            break;
        case DF_HALF:
            gen_helper_msa_pcnt_h(cpu_env, twd, tws);
            break;
        case DF_WORD:
            gen_helper_msa_pcnt_w(cpu_env, twd, tws);
            break;
        case DF_DOUBLE:
            gen_helper_msa_pcnt_d(cpu_env, twd, tws);
            break;
        }
        break;
    default:
        MIPS_INVAL("MSA instruction");
        gen_reserved_instruction(ctx);
        break;
    }

    tcg_temp_free_i32(twd);
    tcg_temp_free_i32(tws);
    tcg_temp_free_i32(twt);
    tcg_temp_free_i32(tdf);
}

static void gen_msa_2rf(DisasContext *ctx)
{
#define MASK_MSA_2RF(op)    (MASK_MSA_MINOR(op) | (op & (0x1f << 21)) | \
                            (op & (0xf << 17)))
    uint8_t wt = (ctx->opcode >> 16) & 0x1f;
    uint8_t ws = (ctx->opcode >> 11) & 0x1f;
    uint8_t wd = (ctx->opcode >> 6) & 0x1f;
    uint8_t df = (ctx->opcode >> 16) & 0x1;
    TCGv_i32 twd = tcg_const_i32(wd);
    TCGv_i32 tws = tcg_const_i32(ws);
    TCGv_i32 twt = tcg_const_i32(wt);
    /* adjust df value for floating-point instruction */
    TCGv_i32 tdf = tcg_const_i32(df + 2);

    switch (MASK_MSA_2RF(ctx->opcode)) {
    case OPC_FCLASS_df:
        gen_helper_msa_fclass_df(cpu_env, tdf, twd, tws);
        break;
    case OPC_FTRUNC_S_df:
        gen_helper_msa_ftrunc_s_df(cpu_env, tdf, twd, tws);
        break;
    case OPC_FTRUNC_U_df:
        gen_helper_msa_ftrunc_u_df(cpu_env, tdf, twd, tws);
        break;
    case OPC_FSQRT_df:
        gen_helper_msa_fsqrt_df(cpu_env, tdf, twd, tws);
        break;
    case OPC_FRSQRT_df:
        gen_helper_msa_frsqrt_df(cpu_env, tdf, twd, tws);
        break;
    case OPC_FRCP_df:
        gen_helper_msa_frcp_df(cpu_env, tdf, twd, tws);
        break;
    case OPC_FRINT_df:
        gen_helper_msa_frint_df(cpu_env, tdf, twd, tws);
        break;
    case OPC_FLOG2_df:
        gen_helper_msa_flog2_df(cpu_env, tdf, twd, tws);
        break;
    case OPC_FEXUPL_df:
        gen_helper_msa_fexupl_df(cpu_env, tdf, twd, tws);
        break;
    case OPC_FEXUPR_df:
        gen_helper_msa_fexupr_df(cpu_env, tdf, twd, tws);
        break;
    case OPC_FFQL_df:
        gen_helper_msa_ffql_df(cpu_env, tdf, twd, tws);
        break;
    case OPC_FFQR_df:
        gen_helper_msa_ffqr_df(cpu_env, tdf, twd, tws);
        break;
    case OPC_FTINT_S_df:
        gen_helper_msa_ftint_s_df(cpu_env, tdf, twd, tws);
        break;
    case OPC_FTINT_U_df:
        gen_helper_msa_ftint_u_df(cpu_env, tdf, twd, tws);
        break;
    case OPC_FFINT_S_df:
        gen_helper_msa_ffint_s_df(cpu_env, tdf, twd, tws);
        break;
    case OPC_FFINT_U_df:
        gen_helper_msa_ffint_u_df(cpu_env, tdf, twd, tws);
        break;
    }

    tcg_temp_free_i32(twd);
    tcg_temp_free_i32(tws);
    tcg_temp_free_i32(twt);
    tcg_temp_free_i32(tdf);
}

static void gen_msa_vec_v(DisasContext *ctx)
{
#define MASK_MSA_VEC(op)    (MASK_MSA_MINOR(op) | (op & (0x1f << 21)))
    uint8_t wt = (ctx->opcode >> 16) & 0x1f;
    uint8_t ws = (ctx->opcode >> 11) & 0x1f;
    uint8_t wd = (ctx->opcode >> 6) & 0x1f;
    TCGv_i32 twd = tcg_const_i32(wd);
    TCGv_i32 tws = tcg_const_i32(ws);
    TCGv_i32 twt = tcg_const_i32(wt);

    switch (MASK_MSA_VEC(ctx->opcode)) {
    case OPC_AND_V:
        gen_helper_msa_and_v(cpu_env, twd, tws, twt);
        break;
    case OPC_OR_V:
        gen_helper_msa_or_v(cpu_env, twd, tws, twt);
        break;
    case OPC_NOR_V:
        gen_helper_msa_nor_v(cpu_env, twd, tws, twt);
        break;
    case OPC_XOR_V:
        gen_helper_msa_xor_v(cpu_env, twd, tws, twt);
        break;
    case OPC_BMNZ_V:
        gen_helper_msa_bmnz_v(cpu_env, twd, tws, twt);
        break;
    case OPC_BMZ_V:
        gen_helper_msa_bmz_v(cpu_env, twd, tws, twt);
        break;
    case OPC_BSEL_V:
        gen_helper_msa_bsel_v(cpu_env, twd, tws, twt);
        break;
    default:
        MIPS_INVAL("MSA instruction");
        gen_reserved_instruction(ctx);
        break;
    }

    tcg_temp_free_i32(twd);
    tcg_temp_free_i32(tws);
    tcg_temp_free_i32(twt);
}

static void gen_msa_vec(DisasContext *ctx)
{
    switch (MASK_MSA_VEC(ctx->opcode)) {
    case OPC_AND_V:
    case OPC_OR_V:
    case OPC_NOR_V:
    case OPC_XOR_V:
    case OPC_BMNZ_V:
    case OPC_BMZ_V:
    case OPC_BSEL_V:
        gen_msa_vec_v(ctx);
        break;
    case OPC_MSA_2R:
        gen_msa_2r(ctx);
        break;
    case OPC_MSA_2RF:
        gen_msa_2rf(ctx);
        break;
    default:
        MIPS_INVAL("MSA instruction");
        gen_reserved_instruction(ctx);
        break;
    }
}

static void gen_msa(DisasContext *ctx)
{
    uint32_t opcode = ctx->opcode;

    check_msa_access(ctx);

    switch (MASK_MSA_MINOR(opcode)) {
    case OPC_MSA_I8_00:
    case OPC_MSA_I8_01:
    case OPC_MSA_I8_02:
        gen_msa_i8(ctx);
        break;
    case OPC_MSA_I5_06:
    case OPC_MSA_I5_07:
        gen_msa_i5(ctx);
        break;
    case OPC_MSA_BIT_09:
    case OPC_MSA_BIT_0A:
        gen_msa_bit(ctx);
        break;
    case OPC_MSA_3R_0D:
    case OPC_MSA_3R_0E:
    case OPC_MSA_3R_0F:
    case OPC_MSA_3R_10:
    case OPC_MSA_3R_11:
    case OPC_MSA_3R_12:
    case OPC_MSA_3R_13:
    case OPC_MSA_3R_14:
    case OPC_MSA_3R_15:
        gen_msa_3r(ctx);
        break;
    case OPC_MSA_ELM:
        gen_msa_elm(ctx);
        break;
    case OPC_MSA_3RF_1A:
    case OPC_MSA_3RF_1B:
    case OPC_MSA_3RF_1C:
        gen_msa_3rf(ctx);
        break;
    case OPC_MSA_VEC:
        gen_msa_vec(ctx);
        break;
    case OPC_LD_B:
    case OPC_LD_H:
    case OPC_LD_W:
    case OPC_LD_D:
    case OPC_ST_B:
    case OPC_ST_H:
    case OPC_ST_W:
    case OPC_ST_D:
        {
            int32_t s10 = sextract32(ctx->opcode, 16, 10);
            uint8_t rs = (ctx->opcode >> 11) & 0x1f;
            uint8_t wd = (ctx->opcode >> 6) & 0x1f;
            uint8_t df = (ctx->opcode >> 0) & 0x3;

            TCGv_i32 twd = tcg_const_i32(wd);
            TCGv taddr = tcg_temp_new();
            gen_base_offset_addr(ctx, taddr, rs, s10 << df);

            switch (MASK_MSA_MINOR(opcode)) {
            case OPC_LD_B:
                gen_helper_msa_ld_b(cpu_env, twd, taddr);
                break;
            case OPC_LD_H:
                gen_helper_msa_ld_h(cpu_env, twd, taddr);
                break;
            case OPC_LD_W:
                gen_helper_msa_ld_w(cpu_env, twd, taddr);
                break;
            case OPC_LD_D:
                gen_helper_msa_ld_d(cpu_env, twd, taddr);
                break;
            case OPC_ST_B:
                gen_helper_msa_st_b(cpu_env, twd, taddr);
                break;
            case OPC_ST_H:
                gen_helper_msa_st_h(cpu_env, twd, taddr);
                break;
            case OPC_ST_W:
                gen_helper_msa_st_w(cpu_env, twd, taddr);
                break;
            case OPC_ST_D:
                gen_helper_msa_st_d(cpu_env, twd, taddr);
                break;
            }

            tcg_temp_free_i32(twd);
            tcg_temp_free(taddr);
        }
        break;
    default:
        MIPS_INVAL("MSA instruction");
        gen_reserved_instruction(ctx);
        break;
    }
}

static bool trans_MSA(DisasContext *ctx, arg_MSA *a)
{
    gen_msa(ctx);

    return true;
}

static bool trans_LSA(DisasContext *ctx, arg_rtype *a)
{
    return gen_lsa(ctx, a->rd, a->rt, a->rs, a->sa);
}

static bool trans_DLSA(DisasContext *ctx, arg_rtype *a)
{
    return gen_dlsa(ctx, a->rd, a->rt, a->rs, a->sa);
}

bool decode_ase_msa(DisasContext *ctx, uint32_t insn)
{
    if (TARGET_LONG_BITS == 64 && decode_msa64(ctx, insn)) {
        return true;
    }
    return decode_msa32(ctx, insn);
}

/*
 * MIPS SIMD Architecture Module Instruction emulation helpers for QEMU.
 *
 * Copyright (c) 2014 Imagination Technologies
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "cpu.h"
#include "exec/helper-proto.h"

/* Data format min and max values */
#define DF_BITS(df) (1 << ((df) + 3))

#define DF_MAX_INT(df)  (int64_t)((1LL << (DF_BITS(df) - 1)) - 1)
#define M_MAX_INT(m)    (int64_t)((1LL << ((m)         - 1)) - 1)

#define DF_MIN_INT(df)  (int64_t)(-(1LL << (DF_BITS(df) - 1)))
#define M_MIN_INT(m)    (int64_t)(-(1LL << ((m)         - 1)))

#define DF_MAX_UINT(df) (uint64_t)(-1ULL >> (64 - DF_BITS(df)))
#define M_MAX_UINT(m)   (uint64_t)(-1ULL >> (64 - (m)))

#define UNSIGNED(x, df) ((x) & DF_MAX_UINT(df))
#define SIGNED(x, df)                                                   \
    ((((int64_t)x) << (64 - DF_BITS(df))) >> (64 - DF_BITS(df)))

/* Element-by-element access macros */
#define DF_ELEMENTS(df) (MSA_WRLEN / DF_BITS(df))

static inline void msa_move_v(wr_t *pwd, wr_t *pws)
{
    uint32_t i;

    for (i = 0; i < DF_ELEMENTS(DF_DOUBLE); i++) {
        pwd->d[i] = pws->d[i];
    }
}

#define MSA_FN_IMM8(FUNC, DEST, OPERATION)                              \
void helper_msa_ ## FUNC(CPUMIPSState *env, uint32_t wd, uint32_t ws,   \
        uint32_t i8)                                                    \
{                                                                       \
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);                          \
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);                          \
    uint32_t i;                                                         \
    for (i = 0; i < DF_ELEMENTS(DF_BYTE); i++) {                        \
        DEST = OPERATION;                                               \
    }                                                                   \
}

MSA_FN_IMM8(andi_b, pwd->b[i], pws->b[i] & i8)
MSA_FN_IMM8(ori_b, pwd->b[i], pws->b[i] | i8)
MSA_FN_IMM8(nori_b, pwd->b[i], ~(pws->b[i] | i8))
MSA_FN_IMM8(xori_b, pwd->b[i], pws->b[i] ^ i8)

#define BIT_MOVE_IF_NOT_ZERO(dest, arg1, arg2, df) \
            UNSIGNED(((dest & (~arg2)) | (arg1 & arg2)), df)
MSA_FN_IMM8(bmnzi_b, pwd->b[i],
        BIT_MOVE_IF_NOT_ZERO(pwd->b[i], pws->b[i], i8, DF_BYTE))

#define BIT_MOVE_IF_ZERO(dest, arg1, arg2, df) \
            UNSIGNED((dest & arg2) | (arg1 & (~arg2)), df)
MSA_FN_IMM8(bmzi_b, pwd->b[i],
        BIT_MOVE_IF_ZERO(pwd->b[i], pws->b[i], i8, DF_BYTE))

#define BIT_SELECT(dest, arg1, arg2, df) \
            UNSIGNED((arg1 & (~dest)) | (arg2 & dest), df)
MSA_FN_IMM8(bseli_b, pwd->b[i],
        BIT_SELECT(pwd->b[i], pws->b[i], i8, DF_BYTE))

#undef MSA_FN_IMM8

#define SHF_POS(i, imm) (((i) & 0xfc) + (((imm) >> (2 * ((i) & 0x03))) & 0x03))

void helper_msa_shf_df(CPUMIPSState *env, uint32_t df, uint32_t wd,
                       uint32_t ws, uint32_t imm)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t wx, *pwx = &wx;
    uint32_t i;

    switch (df) {
    case DF_BYTE:
        for (i = 0; i < DF_ELEMENTS(DF_BYTE); i++) {
            pwx->b[i] = pws->b[SHF_POS(i, imm)];
        }
        break;
    case DF_HALF:
        for (i = 0; i < DF_ELEMENTS(DF_HALF); i++) {
            pwx->h[i] = pws->h[SHF_POS(i, imm)];
        }
        break;
    case DF_WORD:
        for (i = 0; i < DF_ELEMENTS(DF_WORD); i++) {
            pwx->w[i] = pws->w[SHF_POS(i, imm)];
        }
        break;
    default:
        assert(0);
    }
    msa_move_v(pwd, pwx);
}

static inline int64_t msa_addv_df(uint32_t df, int64_t arg1, int64_t arg2)
{
    return arg1 + arg2;
}

static inline int64_t msa_subv_df(uint32_t df, int64_t arg1, int64_t arg2)
{
    return arg1 - arg2;
}

static inline int64_t msa_ceq_df(uint32_t df, int64_t arg1, int64_t arg2)
{
    return arg1 == arg2 ? -1 : 0;
}

static inline int64_t msa_cle_s_df(uint32_t df, int64_t arg1, int64_t arg2)
{
    return arg1 <= arg2 ? -1 : 0;
}

static inline int64_t msa_cle_u_df(uint32_t df, int64_t arg1, int64_t arg2)
{
    uint64_t u_arg1 = UNSIGNED(arg1, df);
    uint64_t u_arg2 = UNSIGNED(arg2, df);
    return u_arg1 <= u_arg2 ? -1 : 0;
}

static inline int64_t msa_clt_s_df(uint32_t df, int64_t arg1, int64_t arg2)
{
    return arg1 < arg2 ? -1 : 0;
}

static inline int64_t msa_clt_u_df(uint32_t df, int64_t arg1, int64_t arg2)
{
    uint64_t u_arg1 = UNSIGNED(arg1, df);
    uint64_t u_arg2 = UNSIGNED(arg2, df);
    return u_arg1 < u_arg2 ? -1 : 0;
}

static inline int64_t msa_max_s_df(uint32_t df, int64_t arg1, int64_t arg2)
{
    return arg1 > arg2 ? arg1 : arg2;
}

static inline int64_t msa_max_u_df(uint32_t df, int64_t arg1, int64_t arg2)
{
    uint64_t u_arg1 = UNSIGNED(arg1, df);
    uint64_t u_arg2 = UNSIGNED(arg2, df);
    return u_arg1 > u_arg2 ? arg1 : arg2;
}

static inline int64_t msa_min_s_df(uint32_t df, int64_t arg1, int64_t arg2)
{
    return arg1 < arg2 ? arg1 : arg2;
}

static inline int64_t msa_min_u_df(uint32_t df, int64_t arg1, int64_t arg2)
{
    uint64_t u_arg1 = UNSIGNED(arg1, df);
    uint64_t u_arg2 = UNSIGNED(arg2, df);
    return u_arg1 < u_arg2 ? arg1 : arg2;
}

#define MSA_BINOP_IMM_DF(helper, func)                                  \
void helper_msa_ ## helper ## _df(CPUMIPSState *env, uint32_t df,       \
                        uint32_t wd, uint32_t ws, int32_t u5)           \
{                                                                       \
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);                          \
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);                          \
    uint32_t i;                                                         \
                                                                        \
    switch (df) {                                                       \
    case DF_BYTE:                                                       \
        for (i = 0; i < DF_ELEMENTS(DF_BYTE); i++) {                    \
            pwd->b[i] = msa_ ## func ## _df(df, pws->b[i], u5);         \
        }                                                               \
        break;                                                          \
    case DF_HALF:                                                       \
        for (i = 0; i < DF_ELEMENTS(DF_HALF); i++) {                    \
            pwd->h[i] = msa_ ## func ## _df(df, pws->h[i], u5);         \
        }                                                               \
        break;                                                          \
    case DF_WORD:                                                       \
        for (i = 0; i < DF_ELEMENTS(DF_WORD); i++) {                    \
            pwd->w[i] = msa_ ## func ## _df(df, pws->w[i], u5);         \
        }                                                               \
        break;                                                          \
    case DF_DOUBLE:                                                     \
        for (i = 0; i < DF_ELEMENTS(DF_DOUBLE); i++) {                  \
            pwd->d[i] = msa_ ## func ## _df(df, pws->d[i], u5);         \
        }                                                               \
        break;                                                          \
    default:                                                            \
        assert(0);                                                      \
    }                                                                   \
}

MSA_BINOP_IMM_DF(addvi, addv)
MSA_BINOP_IMM_DF(subvi, subv)
MSA_BINOP_IMM_DF(ceqi, ceq)
MSA_BINOP_IMM_DF(clei_s, cle_s)
MSA_BINOP_IMM_DF(clei_u, cle_u)
MSA_BINOP_IMM_DF(clti_s, clt_s)
MSA_BINOP_IMM_DF(clti_u, clt_u)
MSA_BINOP_IMM_DF(maxi_s, max_s)
MSA_BINOP_IMM_DF(maxi_u, max_u)
MSA_BINOP_IMM_DF(mini_s, min_s)
MSA_BINOP_IMM_DF(mini_u, min_u)
#undef MSA_BINOP_IMM_DF

void helper_msa_ldi_df(CPUMIPSState *env, uint32_t df, uint32_t wd,
                       int32_t s10)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    uint32_t i;

    switch (df) {
    case DF_BYTE:
        for (i = 0; i < DF_ELEMENTS(DF_BYTE); i++) {
            pwd->b[i] = (int8_t)s10;
        }
        break;
    case DF_HALF:
        for (i = 0; i < DF_ELEMENTS(DF_HALF); i++) {
            pwd->h[i] = (int16_t)s10;
        }
        break;
    case DF_WORD:
        for (i = 0; i < DF_ELEMENTS(DF_WORD); i++) {
            pwd->w[i] = (int32_t)s10;
        }
        break;
    case DF_DOUBLE:
        for (i = 0; i < DF_ELEMENTS(DF_DOUBLE); i++) {
            pwd->d[i] = (int64_t)s10;
        }
       break;
    default:
        assert(0);
    }
}

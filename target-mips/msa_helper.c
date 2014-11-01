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

/* Data format bit position and unsigned values */
#define BIT_POSITION(x, df) ((uint64_t)(x) % DF_BITS(df))

static inline int64_t msa_sll_df(uint32_t df, int64_t arg1, int64_t arg2)
{
    int32_t b_arg2 = BIT_POSITION(arg2, df);
    return arg1 << b_arg2;
}

static inline int64_t msa_sra_df(uint32_t df, int64_t arg1, int64_t arg2)
{
    int32_t b_arg2 = BIT_POSITION(arg2, df);
    return arg1 >> b_arg2;
}

static inline int64_t msa_srl_df(uint32_t df, int64_t arg1, int64_t arg2)
{
    uint64_t u_arg1 = UNSIGNED(arg1, df);
    int32_t b_arg2 = BIT_POSITION(arg2, df);
    return u_arg1 >> b_arg2;
}

static inline int64_t msa_bclr_df(uint32_t df, int64_t arg1, int64_t arg2)
{
    int32_t b_arg2 = BIT_POSITION(arg2, df);
    return UNSIGNED(arg1 & (~(1LL << b_arg2)), df);
}

static inline int64_t msa_bset_df(uint32_t df, int64_t arg1,
        int64_t arg2)
{
    int32_t b_arg2 = BIT_POSITION(arg2, df);
    return UNSIGNED(arg1 | (1LL << b_arg2), df);
}

static inline int64_t msa_bneg_df(uint32_t df, int64_t arg1, int64_t arg2)
{
    int32_t b_arg2 = BIT_POSITION(arg2, df);
    return UNSIGNED(arg1 ^ (1LL << b_arg2), df);
}

static inline int64_t msa_binsl_df(uint32_t df, int64_t dest, int64_t arg1,
                                   int64_t arg2)
{
    uint64_t u_arg1 = UNSIGNED(arg1, df);
    uint64_t u_dest = UNSIGNED(dest, df);
    int32_t sh_d = BIT_POSITION(arg2, df) + 1;
    int32_t sh_a = DF_BITS(df) - sh_d;
    if (sh_d == DF_BITS(df)) {
        return u_arg1;
    } else {
        return UNSIGNED(UNSIGNED(u_dest << sh_d, df) >> sh_d, df) |
               UNSIGNED(UNSIGNED(u_arg1 >> sh_a, df) << sh_a, df);
    }
}

static inline int64_t msa_binsr_df(uint32_t df, int64_t dest, int64_t arg1,
                                   int64_t arg2)
{
    uint64_t u_arg1 = UNSIGNED(arg1, df);
    uint64_t u_dest = UNSIGNED(dest, df);
    int32_t sh_d = BIT_POSITION(arg2, df) + 1;
    int32_t sh_a = DF_BITS(df) - sh_d;
    if (sh_d == DF_BITS(df)) {
        return u_arg1;
    } else {
        return UNSIGNED(UNSIGNED(u_dest >> sh_d, df) << sh_d, df) |
               UNSIGNED(UNSIGNED(u_arg1 << sh_a, df) >> sh_a, df);
    }
}

static inline int64_t msa_sat_s_df(uint32_t df, int64_t arg, uint32_t m)
{
    return arg < M_MIN_INT(m+1) ? M_MIN_INT(m+1) :
                                  arg > M_MAX_INT(m+1) ? M_MAX_INT(m+1) :
                                                         arg;
}

static inline int64_t msa_sat_u_df(uint32_t df, int64_t arg, uint32_t m)
{
    uint64_t u_arg = UNSIGNED(arg, df);
    return  u_arg < M_MAX_UINT(m+1) ? u_arg :
                                      M_MAX_UINT(m+1);
}

static inline int64_t msa_srar_df(uint32_t df, int64_t arg1, int64_t arg2)
{
    int32_t b_arg2 = BIT_POSITION(arg2, df);
    if (b_arg2 == 0) {
        return arg1;
    } else {
        int64_t r_bit = (arg1 >> (b_arg2 - 1)) & 1;
        return (arg1 >> b_arg2) + r_bit;
    }
}

static inline int64_t msa_srlr_df(uint32_t df, int64_t arg1, int64_t arg2)
{
    uint64_t u_arg1 = UNSIGNED(arg1, df);
    int32_t b_arg2 = BIT_POSITION(arg2, df);
    if (b_arg2 == 0) {
        return u_arg1;
    } else {
        uint64_t r_bit = (u_arg1 >> (b_arg2 - 1)) & 1;
        return (u_arg1 >> b_arg2) + r_bit;
    }
}

#define MSA_BINOP_IMMU_DF(helper, func)                                  \
void helper_msa_ ## helper ## _df(CPUMIPSState *env, uint32_t df, uint32_t wd, \
                       uint32_t ws, uint32_t u5)                        \
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

MSA_BINOP_IMMU_DF(slli, sll)
MSA_BINOP_IMMU_DF(srai, sra)
MSA_BINOP_IMMU_DF(srli, srl)
MSA_BINOP_IMMU_DF(bclri, bclr)
MSA_BINOP_IMMU_DF(bseti, bset)
MSA_BINOP_IMMU_DF(bnegi, bneg)
MSA_BINOP_IMMU_DF(sat_s, sat_s)
MSA_BINOP_IMMU_DF(sat_u, sat_u)
MSA_BINOP_IMMU_DF(srari, srar)
MSA_BINOP_IMMU_DF(srlri, srlr)
#undef MSA_BINOP_IMMU_DF

#define MSA_TEROP_IMMU_DF(helper, func)                                  \
void helper_msa_ ## helper ## _df(CPUMIPSState *env, uint32_t df,       \
                                  uint32_t wd, uint32_t ws, uint32_t u5) \
{                                                                       \
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);                          \
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);                          \
    uint32_t i;                                                         \
                                                                        \
    switch (df) {                                                       \
    case DF_BYTE:                                                       \
        for (i = 0; i < DF_ELEMENTS(DF_BYTE); i++) {                    \
            pwd->b[i] = msa_ ## func ## _df(df, pwd->b[i], pws->b[i],   \
                                            u5);                        \
        }                                                               \
        break;                                                          \
    case DF_HALF:                                                       \
        for (i = 0; i < DF_ELEMENTS(DF_HALF); i++) {                    \
            pwd->h[i] = msa_ ## func ## _df(df, pwd->h[i], pws->h[i],   \
                                            u5);                        \
        }                                                               \
        break;                                                          \
    case DF_WORD:                                                       \
        for (i = 0; i < DF_ELEMENTS(DF_WORD); i++) {                    \
            pwd->w[i] = msa_ ## func ## _df(df, pwd->w[i], pws->w[i],   \
                                            u5);                        \
        }                                                               \
        break;                                                          \
    case DF_DOUBLE:                                                     \
        for (i = 0; i < DF_ELEMENTS(DF_DOUBLE); i++) {                  \
            pwd->d[i] = msa_ ## func ## _df(df, pwd->d[i], pws->d[i],   \
                                            u5);                        \
        }                                                               \
        break;                                                          \
    default:                                                            \
        assert(0);                                                      \
    }                                                                   \
}

MSA_TEROP_IMMU_DF(binsli, binsl)
MSA_TEROP_IMMU_DF(binsri, binsr)
#undef MSA_TEROP_IMMU_DF

static inline int64_t msa_max_a_df(uint32_t df, int64_t arg1, int64_t arg2)
{
    uint64_t abs_arg1 = arg1 >= 0 ? arg1 : -arg1;
    uint64_t abs_arg2 = arg2 >= 0 ? arg2 : -arg2;
    return abs_arg1 > abs_arg2 ? arg1 : arg2;
}

static inline int64_t msa_min_a_df(uint32_t df, int64_t arg1, int64_t arg2)
{
    uint64_t abs_arg1 = arg1 >= 0 ? arg1 : -arg1;
    uint64_t abs_arg2 = arg2 >= 0 ? arg2 : -arg2;
    return abs_arg1 < abs_arg2 ? arg1 : arg2;
}

static inline int64_t msa_add_a_df(uint32_t df, int64_t arg1, int64_t arg2)
{
    uint64_t abs_arg1 = arg1 >= 0 ? arg1 : -arg1;
    uint64_t abs_arg2 = arg2 >= 0 ? arg2 : -arg2;
    return abs_arg1 + abs_arg2;
}

static inline int64_t msa_adds_a_df(uint32_t df, int64_t arg1, int64_t arg2)
{
    uint64_t max_int = (uint64_t)DF_MAX_INT(df);
    uint64_t abs_arg1 = arg1 >= 0 ? arg1 : -arg1;
    uint64_t abs_arg2 = arg2 >= 0 ? arg2 : -arg2;
    if (abs_arg1 > max_int || abs_arg2 > max_int) {
        return (int64_t)max_int;
    } else {
        return (abs_arg1 < max_int - abs_arg2) ? abs_arg1 + abs_arg2 : max_int;
    }
}

static inline int64_t msa_adds_s_df(uint32_t df, int64_t arg1, int64_t arg2)
{
    int64_t max_int = DF_MAX_INT(df);
    int64_t min_int = DF_MIN_INT(df);
    if (arg1 < 0) {
        return (min_int - arg1 < arg2) ? arg1 + arg2 : min_int;
    } else {
        return (arg2 < max_int - arg1) ? arg1 + arg2 : max_int;
    }
}

static inline uint64_t msa_adds_u_df(uint32_t df, uint64_t arg1, uint64_t arg2)
{
    uint64_t max_uint = DF_MAX_UINT(df);
    uint64_t u_arg1 = UNSIGNED(arg1, df);
    uint64_t u_arg2 = UNSIGNED(arg2, df);
    return (u_arg1 < max_uint - u_arg2) ? u_arg1 + u_arg2 : max_uint;
}

static inline int64_t msa_ave_s_df(uint32_t df, int64_t arg1, int64_t arg2)
{
    /* signed shift */
    return (arg1 >> 1) + (arg2 >> 1) + (arg1 & arg2 & 1);
}

static inline uint64_t msa_ave_u_df(uint32_t df, uint64_t arg1, uint64_t arg2)
{
    uint64_t u_arg1 = UNSIGNED(arg1, df);
    uint64_t u_arg2 = UNSIGNED(arg2, df);
    /* unsigned shift */
    return (u_arg1 >> 1) + (u_arg2 >> 1) + (u_arg1 & u_arg2 & 1);
}

static inline int64_t msa_aver_s_df(uint32_t df, int64_t arg1, int64_t arg2)
{
    /* signed shift */
    return (arg1 >> 1) + (arg2 >> 1) + ((arg1 | arg2) & 1);
}

static inline uint64_t msa_aver_u_df(uint32_t df, uint64_t arg1, uint64_t arg2)
{
    uint64_t u_arg1 = UNSIGNED(arg1, df);
    uint64_t u_arg2 = UNSIGNED(arg2, df);
    /* unsigned shift */
    return (u_arg1 >> 1) + (u_arg2 >> 1) + ((u_arg1 | u_arg2) & 1);
}

static inline int64_t msa_subs_s_df(uint32_t df, int64_t arg1, int64_t arg2)
{
    int64_t max_int = DF_MAX_INT(df);
    int64_t min_int = DF_MIN_INT(df);
    if (arg2 > 0) {
        return (min_int + arg2 < arg1) ? arg1 - arg2 : min_int;
    } else {
        return (arg1 < max_int + arg2) ? arg1 - arg2 : max_int;
    }
}

static inline int64_t msa_subs_u_df(uint32_t df, int64_t arg1, int64_t arg2)
{
    uint64_t u_arg1 = UNSIGNED(arg1, df);
    uint64_t u_arg2 = UNSIGNED(arg2, df);
    return (u_arg1 > u_arg2) ? u_arg1 - u_arg2 : 0;
}

static inline int64_t msa_subsus_u_df(uint32_t df, int64_t arg1, int64_t arg2)
{
    uint64_t u_arg1 = UNSIGNED(arg1, df);
    uint64_t max_uint = DF_MAX_UINT(df);
    if (arg2 >= 0) {
        uint64_t u_arg2 = (uint64_t)arg2;
        return (u_arg1 > u_arg2) ?
            (int64_t)(u_arg1 - u_arg2) :
            0;
    } else {
        uint64_t u_arg2 = (uint64_t)(-arg2);
        return (u_arg1 < max_uint - u_arg2) ?
            (int64_t)(u_arg1 + u_arg2) :
            (int64_t)max_uint;
    }
}

static inline int64_t msa_subsuu_s_df(uint32_t df, int64_t arg1, int64_t arg2)
{
    uint64_t u_arg1 = UNSIGNED(arg1, df);
    uint64_t u_arg2 = UNSIGNED(arg2, df);
    int64_t max_int = DF_MAX_INT(df);
    int64_t min_int = DF_MIN_INT(df);
    if (u_arg1 > u_arg2) {
        return u_arg1 - u_arg2 < (uint64_t)max_int ?
            (int64_t)(u_arg1 - u_arg2) :
            max_int;
    } else {
        return u_arg2 - u_arg1 < (uint64_t)(-min_int) ?
            (int64_t)(u_arg1 - u_arg2) :
            min_int;
    }
}

static inline int64_t msa_asub_s_df(uint32_t df, int64_t arg1, int64_t arg2)
{
    /* signed compare */
    return (arg1 < arg2) ?
        (uint64_t)(arg2 - arg1) : (uint64_t)(arg1 - arg2);
}

static inline uint64_t msa_asub_u_df(uint32_t df, uint64_t arg1, uint64_t arg2)
{
    uint64_t u_arg1 = UNSIGNED(arg1, df);
    uint64_t u_arg2 = UNSIGNED(arg2, df);
    /* unsigned compare */
    return (u_arg1 < u_arg2) ?
        (uint64_t)(u_arg2 - u_arg1) : (uint64_t)(u_arg1 - u_arg2);
}

static inline int64_t msa_mulv_df(uint32_t df, int64_t arg1, int64_t arg2)
{
    return arg1 * arg2;
}

static inline int64_t msa_div_s_df(uint32_t df, int64_t arg1, int64_t arg2)
{
    if (arg1 == DF_MIN_INT(df) && arg2 == -1) {
        return DF_MIN_INT(df);
    }
    return arg2 ? arg1 / arg2 : 0;
}

static inline int64_t msa_div_u_df(uint32_t df, int64_t arg1, int64_t arg2)
{
    uint64_t u_arg1 = UNSIGNED(arg1, df);
    uint64_t u_arg2 = UNSIGNED(arg2, df);
    return u_arg2 ? u_arg1 / u_arg2 : 0;
}

static inline int64_t msa_mod_s_df(uint32_t df, int64_t arg1, int64_t arg2)
{
    if (arg1 == DF_MIN_INT(df) && arg2 == -1) {
        return 0;
    }
    return arg2 ? arg1 % arg2 : 0;
}

static inline int64_t msa_mod_u_df(uint32_t df, int64_t arg1, int64_t arg2)
{
    uint64_t u_arg1 = UNSIGNED(arg1, df);
    uint64_t u_arg2 = UNSIGNED(arg2, df);
    return u_arg2 ? u_arg1 % u_arg2 : 0;
}

#define SIGNED_EVEN(a, df) \
        ((((int64_t)(a)) << (64 - DF_BITS(df)/2)) >> (64 - DF_BITS(df)/2))

#define UNSIGNED_EVEN(a, df) \
        ((((uint64_t)(a)) << (64 - DF_BITS(df)/2)) >> (64 - DF_BITS(df)/2))

#define SIGNED_ODD(a, df) \
        ((((int64_t)(a)) << (64 - DF_BITS(df))) >> (64 - DF_BITS(df)/2))

#define UNSIGNED_ODD(a, df) \
        ((((uint64_t)(a)) << (64 - DF_BITS(df))) >> (64 - DF_BITS(df)/2))

#define SIGNED_EXTRACT(e, o, a, df)     \
    do {                                \
        e = SIGNED_EVEN(a, df);         \
        o = SIGNED_ODD(a, df);          \
    } while (0);

#define UNSIGNED_EXTRACT(e, o, a, df)   \
    do {                                \
        e = UNSIGNED_EVEN(a, df);       \
        o = UNSIGNED_ODD(a, df);        \
    } while (0);

static inline int64_t msa_dotp_s_df(uint32_t df, int64_t arg1, int64_t arg2)
{
    int64_t even_arg1;
    int64_t even_arg2;
    int64_t odd_arg1;
    int64_t odd_arg2;
    SIGNED_EXTRACT(even_arg1, odd_arg1, arg1, df);
    SIGNED_EXTRACT(even_arg2, odd_arg2, arg2, df);
    return (even_arg1 * even_arg2) + (odd_arg1 * odd_arg2);
}

static inline int64_t msa_dotp_u_df(uint32_t df, int64_t arg1, int64_t arg2)
{
    int64_t even_arg1;
    int64_t even_arg2;
    int64_t odd_arg1;
    int64_t odd_arg2;
    UNSIGNED_EXTRACT(even_arg1, odd_arg1, arg1, df);
    UNSIGNED_EXTRACT(even_arg2, odd_arg2, arg2, df);
    return (even_arg1 * even_arg2) + (odd_arg1 * odd_arg2);
}

#define CONCATENATE_AND_SLIDE(s, k)             \
    do {                                        \
        for (i = 0; i < s; i++) {               \
            v[i]     = pws->b[s * k + i];       \
            v[i + s] = pwd->b[s * k + i];       \
        }                                       \
        for (i = 0; i < s; i++) {               \
            pwd->b[s * k + i] = v[i + n];       \
        }                                       \
    } while (0)

static inline void msa_sld_df(uint32_t df, wr_t *pwd,
                              wr_t *pws, target_ulong rt)
{
    uint32_t n = rt % DF_ELEMENTS(df);
    uint8_t v[64];
    uint32_t i, k;

    switch (df) {
    case DF_BYTE:
        CONCATENATE_AND_SLIDE(DF_ELEMENTS(DF_BYTE), 0);
        break;
    case DF_HALF:
        for (k = 0; k < 2; k++) {
            CONCATENATE_AND_SLIDE(DF_ELEMENTS(DF_HALF), k);
        }
        break;
    case DF_WORD:
        for (k = 0; k < 4; k++) {
            CONCATENATE_AND_SLIDE(DF_ELEMENTS(DF_WORD), k);
        }
        break;
    case DF_DOUBLE:
        for (k = 0; k < 8; k++) {
            CONCATENATE_AND_SLIDE(DF_ELEMENTS(DF_DOUBLE), k);
        }
        break;
    default:
        assert(0);
    }
}

static inline int64_t msa_hadd_s_df(uint32_t df, int64_t arg1, int64_t arg2)
{
    return SIGNED_ODD(arg1, df) + SIGNED_EVEN(arg2, df);
}

static inline int64_t msa_hadd_u_df(uint32_t df, int64_t arg1, int64_t arg2)
{
    return UNSIGNED_ODD(arg1, df) + UNSIGNED_EVEN(arg2, df);
}

static inline int64_t msa_hsub_s_df(uint32_t df, int64_t arg1, int64_t arg2)
{
    return SIGNED_ODD(arg1, df) - SIGNED_EVEN(arg2, df);
}

static inline int64_t msa_hsub_u_df(uint32_t df, int64_t arg1, int64_t arg2)
{
    return UNSIGNED_ODD(arg1, df) - UNSIGNED_EVEN(arg2, df);
}

#define MSA_BINOP_DF(func) \
void helper_msa_ ## func ## _df(CPUMIPSState *env, uint32_t df,         \
                                uint32_t wd, uint32_t ws, uint32_t wt)  \
{                                                                       \
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);                          \
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);                          \
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);                          \
    uint32_t i;                                                         \
                                                                        \
    switch (df) {                                                       \
    case DF_BYTE:                                                       \
        for (i = 0; i < DF_ELEMENTS(DF_BYTE); i++) {                    \
            pwd->b[i] = msa_ ## func ## _df(df, pws->b[i], pwt->b[i]);  \
        }                                                               \
        break;                                                          \
    case DF_HALF:                                                       \
        for (i = 0; i < DF_ELEMENTS(DF_HALF); i++) {                    \
            pwd->h[i] = msa_ ## func ## _df(df, pws->h[i], pwt->h[i]);  \
        }                                                               \
        break;                                                          \
    case DF_WORD:                                                       \
        for (i = 0; i < DF_ELEMENTS(DF_WORD); i++) {                    \
            pwd->w[i] = msa_ ## func ## _df(df, pws->w[i], pwt->w[i]);  \
        }                                                               \
        break;                                                          \
    case DF_DOUBLE:                                                     \
        for (i = 0; i < DF_ELEMENTS(DF_DOUBLE); i++) {                  \
            pwd->d[i] = msa_ ## func ## _df(df, pws->d[i], pwt->d[i]);  \
        }                                                               \
        break;                                                          \
    default:                                                            \
        assert(0);                                                      \
    }                                                                   \
}

MSA_BINOP_DF(sll)
MSA_BINOP_DF(sra)
MSA_BINOP_DF(srl)
MSA_BINOP_DF(bclr)
MSA_BINOP_DF(bset)
MSA_BINOP_DF(bneg)
MSA_BINOP_DF(addv)
MSA_BINOP_DF(subv)
MSA_BINOP_DF(max_s)
MSA_BINOP_DF(max_u)
MSA_BINOP_DF(min_s)
MSA_BINOP_DF(min_u)
MSA_BINOP_DF(max_a)
MSA_BINOP_DF(min_a)
MSA_BINOP_DF(ceq)
MSA_BINOP_DF(clt_s)
MSA_BINOP_DF(clt_u)
MSA_BINOP_DF(cle_s)
MSA_BINOP_DF(cle_u)
MSA_BINOP_DF(add_a)
MSA_BINOP_DF(adds_a)
MSA_BINOP_DF(adds_s)
MSA_BINOP_DF(adds_u)
MSA_BINOP_DF(ave_s)
MSA_BINOP_DF(ave_u)
MSA_BINOP_DF(aver_s)
MSA_BINOP_DF(aver_u)
MSA_BINOP_DF(subs_s)
MSA_BINOP_DF(subs_u)
MSA_BINOP_DF(subsus_u)
MSA_BINOP_DF(subsuu_s)
MSA_BINOP_DF(asub_s)
MSA_BINOP_DF(asub_u)
MSA_BINOP_DF(mulv)
MSA_BINOP_DF(div_s)
MSA_BINOP_DF(div_u)
MSA_BINOP_DF(mod_s)
MSA_BINOP_DF(mod_u)
MSA_BINOP_DF(dotp_s)
MSA_BINOP_DF(dotp_u)
MSA_BINOP_DF(srar)
MSA_BINOP_DF(srlr)
MSA_BINOP_DF(hadd_s)
MSA_BINOP_DF(hadd_u)
MSA_BINOP_DF(hsub_s)
MSA_BINOP_DF(hsub_u)
#undef MSA_BINOP_DF

void helper_msa_sld_df(CPUMIPSState *env, uint32_t df, uint32_t wd,
                       uint32_t ws, uint32_t rt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);

    msa_sld_df(df, pwd, pws, env->active_tc.gpr[rt]);
}

static inline int64_t msa_maddv_df(uint32_t df, int64_t dest, int64_t arg1,
                                   int64_t arg2)
{
    return dest + arg1 * arg2;
}

static inline int64_t msa_msubv_df(uint32_t df, int64_t dest, int64_t arg1,
                                   int64_t arg2)
{
    return dest - arg1 * arg2;
}

static inline int64_t msa_dpadd_s_df(uint32_t df, int64_t dest, int64_t arg1,
                                     int64_t arg2)
{
    int64_t even_arg1;
    int64_t even_arg2;
    int64_t odd_arg1;
    int64_t odd_arg2;
    SIGNED_EXTRACT(even_arg1, odd_arg1, arg1, df);
    SIGNED_EXTRACT(even_arg2, odd_arg2, arg2, df);
    return dest + (even_arg1 * even_arg2) + (odd_arg1 * odd_arg2);
}

static inline int64_t msa_dpadd_u_df(uint32_t df, int64_t dest, int64_t arg1,
                                     int64_t arg2)
{
    int64_t even_arg1;
    int64_t even_arg2;
    int64_t odd_arg1;
    int64_t odd_arg2;
    UNSIGNED_EXTRACT(even_arg1, odd_arg1, arg1, df);
    UNSIGNED_EXTRACT(even_arg2, odd_arg2, arg2, df);
    return dest + (even_arg1 * even_arg2) + (odd_arg1 * odd_arg2);
}

static inline int64_t msa_dpsub_s_df(uint32_t df, int64_t dest, int64_t arg1,
                                     int64_t arg2)
{
    int64_t even_arg1;
    int64_t even_arg2;
    int64_t odd_arg1;
    int64_t odd_arg2;
    SIGNED_EXTRACT(even_arg1, odd_arg1, arg1, df);
    SIGNED_EXTRACT(even_arg2, odd_arg2, arg2, df);
    return dest - ((even_arg1 * even_arg2) + (odd_arg1 * odd_arg2));
}

static inline int64_t msa_dpsub_u_df(uint32_t df, int64_t dest, int64_t arg1,
                                     int64_t arg2)
{
    int64_t even_arg1;
    int64_t even_arg2;
    int64_t odd_arg1;
    int64_t odd_arg2;
    UNSIGNED_EXTRACT(even_arg1, odd_arg1, arg1, df);
    UNSIGNED_EXTRACT(even_arg2, odd_arg2, arg2, df);
    return dest - ((even_arg1 * even_arg2) + (odd_arg1 * odd_arg2));
}

#define MSA_TEROP_DF(func) \
void helper_msa_ ## func ## _df(CPUMIPSState *env, uint32_t df, uint32_t wd,   \
                          uint32_t ws, uint32_t wt)                     \
{                                                                       \
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);                          \
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);                          \
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);                          \
    uint32_t i;                                                         \
                                                                        \
    switch (df) {                                                       \
    case DF_BYTE:                                                       \
        for (i = 0; i < DF_ELEMENTS(DF_BYTE); i++) {                    \
            pwd->b[i] = msa_ ## func ## _df(df, pwd->b[i], pws->b[i],   \
                                            pwt->b[i]);                 \
        }                                                               \
        break;                                                          \
    case DF_HALF:                                                       \
        for (i = 0; i < DF_ELEMENTS(DF_HALF); i++) {                    \
            pwd->h[i] = msa_ ## func ## _df(df, pwd->h[i], pws->h[i],   \
                                            pwt->h[i]);                 \
        }                                                               \
        break;                                                          \
    case DF_WORD:                                                       \
        for (i = 0; i < DF_ELEMENTS(DF_WORD); i++) {                    \
            pwd->w[i] = msa_ ## func ## _df(df, pwd->w[i], pws->w[i],   \
                                            pwt->w[i]);                 \
        }                                                               \
        break;                                                          \
    case DF_DOUBLE:                                                     \
        for (i = 0; i < DF_ELEMENTS(DF_DOUBLE); i++) {                  \
            pwd->d[i] = msa_ ## func ## _df(df, pwd->d[i], pws->d[i],   \
                                            pwt->d[i]);                 \
        }                                                               \
        break;                                                          \
    default:                                                            \
        assert(0);                                                      \
    }                                                                   \
}

MSA_TEROP_DF(maddv)
MSA_TEROP_DF(msubv)
MSA_TEROP_DF(dpadd_s)
MSA_TEROP_DF(dpadd_u)
MSA_TEROP_DF(dpsub_s)
MSA_TEROP_DF(dpsub_u)
MSA_TEROP_DF(binsl)
MSA_TEROP_DF(binsr)
#undef MSA_TEROP_DF

static inline void msa_splat_df(uint32_t df, wr_t *pwd,
                                wr_t *pws, target_ulong rt)
{
    uint32_t n = rt % DF_ELEMENTS(df);
    uint32_t i;

    switch (df) {
    case DF_BYTE:
        for (i = 0; i < DF_ELEMENTS(DF_BYTE); i++) {
            pwd->b[i] = pws->b[n];
        }
        break;
    case DF_HALF:
        for (i = 0; i < DF_ELEMENTS(DF_HALF); i++) {
            pwd->h[i] = pws->h[n];
        }
        break;
    case DF_WORD:
        for (i = 0; i < DF_ELEMENTS(DF_WORD); i++) {
            pwd->w[i] = pws->w[n];
        }
        break;
    case DF_DOUBLE:
        for (i = 0; i < DF_ELEMENTS(DF_DOUBLE); i++) {
            pwd->d[i] = pws->d[n];
        }
       break;
    default:
        assert(0);
    }
}

void helper_msa_splat_df(CPUMIPSState *env, uint32_t df, uint32_t wd,
                         uint32_t ws, uint32_t rt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);

    msa_splat_df(df, pwd, pws, env->active_tc.gpr[rt]);
}

#define MSA_DO_B MSA_DO(b)
#define MSA_DO_H MSA_DO(h)
#define MSA_DO_W MSA_DO(w)
#define MSA_DO_D MSA_DO(d)

#define MSA_LOOP_B MSA_LOOP(B)
#define MSA_LOOP_H MSA_LOOP(H)
#define MSA_LOOP_W MSA_LOOP(W)
#define MSA_LOOP_D MSA_LOOP(D)

#define MSA_LOOP_COND_B MSA_LOOP_COND(DF_BYTE)
#define MSA_LOOP_COND_H MSA_LOOP_COND(DF_HALF)
#define MSA_LOOP_COND_W MSA_LOOP_COND(DF_WORD)
#define MSA_LOOP_COND_D MSA_LOOP_COND(DF_DOUBLE)

#define MSA_LOOP(DF) \
        for (i = 0; i < (MSA_LOOP_COND_ ## DF) ; i++) { \
            MSA_DO_ ## DF \
        }

#define MSA_FN_DF(FUNC)                                             \
void helper_msa_##FUNC(CPUMIPSState *env, uint32_t df, uint32_t wd, \
        uint32_t ws, uint32_t wt)                                   \
{                                                                   \
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);                      \
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);                      \
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);                      \
    wr_t wx, *pwx = &wx;                                            \
    uint32_t i;                                                     \
    switch (df) {                                                   \
    case DF_BYTE:                                                   \
        MSA_LOOP_B                                                  \
        break;                                                      \
    case DF_HALF:                                                   \
        MSA_LOOP_H                                                  \
        break;                                                      \
    case DF_WORD:                                                   \
        MSA_LOOP_W                                                  \
        break;                                                      \
    case DF_DOUBLE:                                                 \
        MSA_LOOP_D                                                  \
       break;                                                       \
    default:                                                        \
        assert(0);                                                  \
    }                                                               \
    msa_move_v(pwd, pwx);                                           \
}

#define MSA_LOOP_COND(DF) \
            (DF_ELEMENTS(DF) / 2)

#define Rb(pwr, i) (pwr->b[i])
#define Lb(pwr, i) (pwr->b[i + DF_ELEMENTS(DF_BYTE)/2])
#define Rh(pwr, i) (pwr->h[i])
#define Lh(pwr, i) (pwr->h[i + DF_ELEMENTS(DF_HALF)/2])
#define Rw(pwr, i) (pwr->w[i])
#define Lw(pwr, i) (pwr->w[i + DF_ELEMENTS(DF_WORD)/2])
#define Rd(pwr, i) (pwr->d[i])
#define Ld(pwr, i) (pwr->d[i + DF_ELEMENTS(DF_DOUBLE)/2])

#define MSA_DO(DF)                      \
    do {                                \
        R##DF(pwx, i) = pwt->DF[2*i];   \
        L##DF(pwx, i) = pws->DF[2*i];   \
    } while (0);
MSA_FN_DF(pckev_df)
#undef MSA_DO

#define MSA_DO(DF)                      \
    do {                                \
        R##DF(pwx, i) = pwt->DF[2*i+1]; \
        L##DF(pwx, i) = pws->DF[2*i+1]; \
    } while (0);
MSA_FN_DF(pckod_df)
#undef MSA_DO

#define MSA_DO(DF)                      \
    do {                                \
        pwx->DF[2*i]   = L##DF(pwt, i); \
        pwx->DF[2*i+1] = L##DF(pws, i); \
    } while (0);
MSA_FN_DF(ilvl_df)
#undef MSA_DO

#define MSA_DO(DF)                      \
    do {                                \
        pwx->DF[2*i]   = R##DF(pwt, i); \
        pwx->DF[2*i+1] = R##DF(pws, i); \
    } while (0);
MSA_FN_DF(ilvr_df)
#undef MSA_DO

#define MSA_DO(DF)                      \
    do {                                \
        pwx->DF[2*i]   = pwt->DF[2*i];  \
        pwx->DF[2*i+1] = pws->DF[2*i];  \
    } while (0);
MSA_FN_DF(ilvev_df)
#undef MSA_DO

#define MSA_DO(DF)                          \
    do {                                    \
        pwx->DF[2*i]   = pwt->DF[2*i+1];    \
        pwx->DF[2*i+1] = pws->DF[2*i+1];    \
    } while (0);
MSA_FN_DF(ilvod_df)
#undef MSA_DO
#undef MSA_LOOP_COND

#define MSA_LOOP_COND(DF) \
            (DF_ELEMENTS(DF))

#define MSA_DO(DF)                                                          \
    do {                                                                    \
        uint32_t n = DF_ELEMENTS(df);                                       \
        uint32_t k = (pwd->DF[i] & 0x3f) % (2 * n);                         \
        pwx->DF[i] =                                                        \
            (pwd->DF[i] & 0xc0) ? 0 : k < n ? pwt->DF[k] : pws->DF[k - n];  \
    } while (0);
MSA_FN_DF(vshf_df)
#undef MSA_DO
#undef MSA_LOOP_COND
#undef MSA_FN_DF

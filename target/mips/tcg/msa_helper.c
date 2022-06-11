/*
 * MIPS SIMD Architecture Module Instruction emulation helpers for QEMU.
 *
 * Copyright (c) 2014 Imagination Technologies
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

#include "qemu/osdep.h"
#include "cpu.h"
#include "internal.h"
#include "tcg/tcg.h"
#include "exec/exec-all.h"
#include "exec/helper-proto.h"
#include "exec/memop.h"
#include "fpu/softfloat.h"
#include "fpu_helper.h"

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



/*
 * Bit Count
 * ---------
 *
 * +---------------+----------------------------------------------------------+
 * | NLOC.B        | Vector Leading Ones Count (byte)                         |
 * | NLOC.H        | Vector Leading Ones Count (halfword)                     |
 * | NLOC.W        | Vector Leading Ones Count (word)                         |
 * | NLOC.D        | Vector Leading Ones Count (doubleword)                   |
 * | NLZC.B        | Vector Leading Zeros Count (byte)                        |
 * | NLZC.H        | Vector Leading Zeros Count (halfword)                    |
 * | NLZC.W        | Vector Leading Zeros Count (word)                        |
 * | NLZC.D        | Vector Leading Zeros Count (doubleword)                  |
 * | PCNT.B        | Vector Population Count (byte)                           |
 * | PCNT.H        | Vector Population Count (halfword)                       |
 * | PCNT.W        | Vector Population Count (word)                           |
 * | PCNT.D        | Vector Population Count (doubleword)                     |
 * +---------------+----------------------------------------------------------+
 */

static inline int64_t msa_nlzc_df(uint32_t df, int64_t arg)
{
    uint64_t x, y;
    int n, c;

    x = UNSIGNED(arg, df);
    n = DF_BITS(df);
    c = DF_BITS(df) / 2;

    do {
        y = x >> c;
        if (y != 0) {
            n = n - c;
            x = y;
        }
        c = c >> 1;
    } while (c != 0);

    return n - x;
}

static inline int64_t msa_nloc_df(uint32_t df, int64_t arg)
{
    return msa_nlzc_df(df, UNSIGNED((~arg), df));
}

void helper_msa_nloc_b(CPUMIPSState *env, uint32_t wd, uint32_t ws)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);

    pwd->b[0]  = msa_nloc_df(DF_BYTE, pws->b[0]);
    pwd->b[1]  = msa_nloc_df(DF_BYTE, pws->b[1]);
    pwd->b[2]  = msa_nloc_df(DF_BYTE, pws->b[2]);
    pwd->b[3]  = msa_nloc_df(DF_BYTE, pws->b[3]);
    pwd->b[4]  = msa_nloc_df(DF_BYTE, pws->b[4]);
    pwd->b[5]  = msa_nloc_df(DF_BYTE, pws->b[5]);
    pwd->b[6]  = msa_nloc_df(DF_BYTE, pws->b[6]);
    pwd->b[7]  = msa_nloc_df(DF_BYTE, pws->b[7]);
    pwd->b[8]  = msa_nloc_df(DF_BYTE, pws->b[8]);
    pwd->b[9]  = msa_nloc_df(DF_BYTE, pws->b[9]);
    pwd->b[10] = msa_nloc_df(DF_BYTE, pws->b[10]);
    pwd->b[11] = msa_nloc_df(DF_BYTE, pws->b[11]);
    pwd->b[12] = msa_nloc_df(DF_BYTE, pws->b[12]);
    pwd->b[13] = msa_nloc_df(DF_BYTE, pws->b[13]);
    pwd->b[14] = msa_nloc_df(DF_BYTE, pws->b[14]);
    pwd->b[15] = msa_nloc_df(DF_BYTE, pws->b[15]);
}

void helper_msa_nloc_h(CPUMIPSState *env, uint32_t wd, uint32_t ws)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);

    pwd->h[0]  = msa_nloc_df(DF_HALF, pws->h[0]);
    pwd->h[1]  = msa_nloc_df(DF_HALF, pws->h[1]);
    pwd->h[2]  = msa_nloc_df(DF_HALF, pws->h[2]);
    pwd->h[3]  = msa_nloc_df(DF_HALF, pws->h[3]);
    pwd->h[4]  = msa_nloc_df(DF_HALF, pws->h[4]);
    pwd->h[5]  = msa_nloc_df(DF_HALF, pws->h[5]);
    pwd->h[6]  = msa_nloc_df(DF_HALF, pws->h[6]);
    pwd->h[7]  = msa_nloc_df(DF_HALF, pws->h[7]);
}

void helper_msa_nloc_w(CPUMIPSState *env, uint32_t wd, uint32_t ws)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);

    pwd->w[0]  = msa_nloc_df(DF_WORD, pws->w[0]);
    pwd->w[1]  = msa_nloc_df(DF_WORD, pws->w[1]);
    pwd->w[2]  = msa_nloc_df(DF_WORD, pws->w[2]);
    pwd->w[3]  = msa_nloc_df(DF_WORD, pws->w[3]);
}

void helper_msa_nloc_d(CPUMIPSState *env, uint32_t wd, uint32_t ws)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);

    pwd->d[0]  = msa_nloc_df(DF_DOUBLE, pws->d[0]);
    pwd->d[1]  = msa_nloc_df(DF_DOUBLE, pws->d[1]);
}

void helper_msa_nlzc_b(CPUMIPSState *env, uint32_t wd, uint32_t ws)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);

    pwd->b[0]  = msa_nlzc_df(DF_BYTE, pws->b[0]);
    pwd->b[1]  = msa_nlzc_df(DF_BYTE, pws->b[1]);
    pwd->b[2]  = msa_nlzc_df(DF_BYTE, pws->b[2]);
    pwd->b[3]  = msa_nlzc_df(DF_BYTE, pws->b[3]);
    pwd->b[4]  = msa_nlzc_df(DF_BYTE, pws->b[4]);
    pwd->b[5]  = msa_nlzc_df(DF_BYTE, pws->b[5]);
    pwd->b[6]  = msa_nlzc_df(DF_BYTE, pws->b[6]);
    pwd->b[7]  = msa_nlzc_df(DF_BYTE, pws->b[7]);
    pwd->b[8]  = msa_nlzc_df(DF_BYTE, pws->b[8]);
    pwd->b[9]  = msa_nlzc_df(DF_BYTE, pws->b[9]);
    pwd->b[10] = msa_nlzc_df(DF_BYTE, pws->b[10]);
    pwd->b[11] = msa_nlzc_df(DF_BYTE, pws->b[11]);
    pwd->b[12] = msa_nlzc_df(DF_BYTE, pws->b[12]);
    pwd->b[13] = msa_nlzc_df(DF_BYTE, pws->b[13]);
    pwd->b[14] = msa_nlzc_df(DF_BYTE, pws->b[14]);
    pwd->b[15] = msa_nlzc_df(DF_BYTE, pws->b[15]);
}

void helper_msa_nlzc_h(CPUMIPSState *env, uint32_t wd, uint32_t ws)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);

    pwd->h[0]  = msa_nlzc_df(DF_HALF, pws->h[0]);
    pwd->h[1]  = msa_nlzc_df(DF_HALF, pws->h[1]);
    pwd->h[2]  = msa_nlzc_df(DF_HALF, pws->h[2]);
    pwd->h[3]  = msa_nlzc_df(DF_HALF, pws->h[3]);
    pwd->h[4]  = msa_nlzc_df(DF_HALF, pws->h[4]);
    pwd->h[5]  = msa_nlzc_df(DF_HALF, pws->h[5]);
    pwd->h[6]  = msa_nlzc_df(DF_HALF, pws->h[6]);
    pwd->h[7]  = msa_nlzc_df(DF_HALF, pws->h[7]);
}

void helper_msa_nlzc_w(CPUMIPSState *env, uint32_t wd, uint32_t ws)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);

    pwd->w[0]  = msa_nlzc_df(DF_WORD, pws->w[0]);
    pwd->w[1]  = msa_nlzc_df(DF_WORD, pws->w[1]);
    pwd->w[2]  = msa_nlzc_df(DF_WORD, pws->w[2]);
    pwd->w[3]  = msa_nlzc_df(DF_WORD, pws->w[3]);
}

void helper_msa_nlzc_d(CPUMIPSState *env, uint32_t wd, uint32_t ws)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);

    pwd->d[0]  = msa_nlzc_df(DF_DOUBLE, pws->d[0]);
    pwd->d[1]  = msa_nlzc_df(DF_DOUBLE, pws->d[1]);
}

static inline int64_t msa_pcnt_df(uint32_t df, int64_t arg)
{
    uint64_t x;

    x = UNSIGNED(arg, df);

    x = (x & 0x5555555555555555ULL) + ((x >>  1) & 0x5555555555555555ULL);
    x = (x & 0x3333333333333333ULL) + ((x >>  2) & 0x3333333333333333ULL);
    x = (x & 0x0F0F0F0F0F0F0F0FULL) + ((x >>  4) & 0x0F0F0F0F0F0F0F0FULL);
    x = (x & 0x00FF00FF00FF00FFULL) + ((x >>  8) & 0x00FF00FF00FF00FFULL);
    x = (x & 0x0000FFFF0000FFFFULL) + ((x >> 16) & 0x0000FFFF0000FFFFULL);
    x = (x & 0x00000000FFFFFFFFULL) + ((x >> 32));

    return x;
}

void helper_msa_pcnt_b(CPUMIPSState *env, uint32_t wd, uint32_t ws)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);

    pwd->b[0]  = msa_pcnt_df(DF_BYTE, pws->b[0]);
    pwd->b[1]  = msa_pcnt_df(DF_BYTE, pws->b[1]);
    pwd->b[2]  = msa_pcnt_df(DF_BYTE, pws->b[2]);
    pwd->b[3]  = msa_pcnt_df(DF_BYTE, pws->b[3]);
    pwd->b[4]  = msa_pcnt_df(DF_BYTE, pws->b[4]);
    pwd->b[5]  = msa_pcnt_df(DF_BYTE, pws->b[5]);
    pwd->b[6]  = msa_pcnt_df(DF_BYTE, pws->b[6]);
    pwd->b[7]  = msa_pcnt_df(DF_BYTE, pws->b[7]);
    pwd->b[8]  = msa_pcnt_df(DF_BYTE, pws->b[8]);
    pwd->b[9]  = msa_pcnt_df(DF_BYTE, pws->b[9]);
    pwd->b[10] = msa_pcnt_df(DF_BYTE, pws->b[10]);
    pwd->b[11] = msa_pcnt_df(DF_BYTE, pws->b[11]);
    pwd->b[12] = msa_pcnt_df(DF_BYTE, pws->b[12]);
    pwd->b[13] = msa_pcnt_df(DF_BYTE, pws->b[13]);
    pwd->b[14] = msa_pcnt_df(DF_BYTE, pws->b[14]);
    pwd->b[15] = msa_pcnt_df(DF_BYTE, pws->b[15]);
}

void helper_msa_pcnt_h(CPUMIPSState *env, uint32_t wd, uint32_t ws)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);

    pwd->h[0]  = msa_pcnt_df(DF_HALF, pws->h[0]);
    pwd->h[1]  = msa_pcnt_df(DF_HALF, pws->h[1]);
    pwd->h[2]  = msa_pcnt_df(DF_HALF, pws->h[2]);
    pwd->h[3]  = msa_pcnt_df(DF_HALF, pws->h[3]);
    pwd->h[4]  = msa_pcnt_df(DF_HALF, pws->h[4]);
    pwd->h[5]  = msa_pcnt_df(DF_HALF, pws->h[5]);
    pwd->h[6]  = msa_pcnt_df(DF_HALF, pws->h[6]);
    pwd->h[7]  = msa_pcnt_df(DF_HALF, pws->h[7]);
}

void helper_msa_pcnt_w(CPUMIPSState *env, uint32_t wd, uint32_t ws)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);

    pwd->w[0]  = msa_pcnt_df(DF_WORD, pws->w[0]);
    pwd->w[1]  = msa_pcnt_df(DF_WORD, pws->w[1]);
    pwd->w[2]  = msa_pcnt_df(DF_WORD, pws->w[2]);
    pwd->w[3]  = msa_pcnt_df(DF_WORD, pws->w[3]);
}

void helper_msa_pcnt_d(CPUMIPSState *env, uint32_t wd, uint32_t ws)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);

    pwd->d[0]  = msa_pcnt_df(DF_DOUBLE, pws->d[0]);
    pwd->d[1]  = msa_pcnt_df(DF_DOUBLE, pws->d[1]);
}


/*
 * Bit Move
 * --------
 *
 * +---------------+----------------------------------------------------------+
 * | BINSL.B       | Vector Bit Insert Left (byte)                            |
 * | BINSL.H       | Vector Bit Insert Left (halfword)                        |
 * | BINSL.W       | Vector Bit Insert Left (word)                            |
 * | BINSL.D       | Vector Bit Insert Left (doubleword)                      |
 * | BINSR.B       | Vector Bit Insert Right (byte)                           |
 * | BINSR.H       | Vector Bit Insert Right (halfword)                       |
 * | BINSR.W       | Vector Bit Insert Right (word)                           |
 * | BINSR.D       | Vector Bit Insert Right (doubleword)                     |
 * | BMNZ.V        | Vector Bit Move If Not Zero                              |
 * | BMZ.V         | Vector Bit Move If Zero                                  |
 * | BSEL.V        | Vector Bit Select                                        |
 * +---------------+----------------------------------------------------------+
 */

/* Data format bit position and unsigned values */
#define BIT_POSITION(x, df) ((uint64_t)(x) % DF_BITS(df))

static inline int64_t msa_binsl_df(uint32_t df,
                                   int64_t dest, int64_t arg1, int64_t arg2)
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

void helper_msa_binsl_b(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->b[0]  = msa_binsl_df(DF_BYTE, pwd->b[0],  pws->b[0],  pwt->b[0]);
    pwd->b[1]  = msa_binsl_df(DF_BYTE, pwd->b[1],  pws->b[1],  pwt->b[1]);
    pwd->b[2]  = msa_binsl_df(DF_BYTE, pwd->b[2],  pws->b[2],  pwt->b[2]);
    pwd->b[3]  = msa_binsl_df(DF_BYTE, pwd->b[3],  pws->b[3],  pwt->b[3]);
    pwd->b[4]  = msa_binsl_df(DF_BYTE, pwd->b[4],  pws->b[4],  pwt->b[4]);
    pwd->b[5]  = msa_binsl_df(DF_BYTE, pwd->b[5],  pws->b[5],  pwt->b[5]);
    pwd->b[6]  = msa_binsl_df(DF_BYTE, pwd->b[6],  pws->b[6],  pwt->b[6]);
    pwd->b[7]  = msa_binsl_df(DF_BYTE, pwd->b[7],  pws->b[7],  pwt->b[7]);
    pwd->b[8]  = msa_binsl_df(DF_BYTE, pwd->b[8],  pws->b[8],  pwt->b[8]);
    pwd->b[9]  = msa_binsl_df(DF_BYTE, pwd->b[9],  pws->b[9],  pwt->b[9]);
    pwd->b[10] = msa_binsl_df(DF_BYTE, pwd->b[10], pws->b[10], pwt->b[10]);
    pwd->b[11] = msa_binsl_df(DF_BYTE, pwd->b[11], pws->b[11], pwt->b[11]);
    pwd->b[12] = msa_binsl_df(DF_BYTE, pwd->b[12], pws->b[12], pwt->b[12]);
    pwd->b[13] = msa_binsl_df(DF_BYTE, pwd->b[13], pws->b[13], pwt->b[13]);
    pwd->b[14] = msa_binsl_df(DF_BYTE, pwd->b[14], pws->b[14], pwt->b[14]);
    pwd->b[15] = msa_binsl_df(DF_BYTE, pwd->b[15], pws->b[15], pwt->b[15]);
}

void helper_msa_binsl_h(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->h[0]  = msa_binsl_df(DF_HALF, pwd->h[0],  pws->h[0],  pwt->h[0]);
    pwd->h[1]  = msa_binsl_df(DF_HALF, pwd->h[1],  pws->h[1],  pwt->h[1]);
    pwd->h[2]  = msa_binsl_df(DF_HALF, pwd->h[2],  pws->h[2],  pwt->h[2]);
    pwd->h[3]  = msa_binsl_df(DF_HALF, pwd->h[3],  pws->h[3],  pwt->h[3]);
    pwd->h[4]  = msa_binsl_df(DF_HALF, pwd->h[4],  pws->h[4],  pwt->h[4]);
    pwd->h[5]  = msa_binsl_df(DF_HALF, pwd->h[5],  pws->h[5],  pwt->h[5]);
    pwd->h[6]  = msa_binsl_df(DF_HALF, pwd->h[6],  pws->h[6],  pwt->h[6]);
    pwd->h[7]  = msa_binsl_df(DF_HALF, pwd->h[7],  pws->h[7],  pwt->h[7]);
}

void helper_msa_binsl_w(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->w[0]  = msa_binsl_df(DF_WORD, pwd->w[0],  pws->w[0],  pwt->w[0]);
    pwd->w[1]  = msa_binsl_df(DF_WORD, pwd->w[1],  pws->w[1],  pwt->w[1]);
    pwd->w[2]  = msa_binsl_df(DF_WORD, pwd->w[2],  pws->w[2],  pwt->w[2]);
    pwd->w[3]  = msa_binsl_df(DF_WORD, pwd->w[3],  pws->w[3],  pwt->w[3]);
}

void helper_msa_binsl_d(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->d[0]  = msa_binsl_df(DF_DOUBLE, pwd->d[0],  pws->d[0],  pwt->d[0]);
    pwd->d[1]  = msa_binsl_df(DF_DOUBLE, pwd->d[1],  pws->d[1],  pwt->d[1]);
}

static inline int64_t msa_binsr_df(uint32_t df,
                                   int64_t dest, int64_t arg1, int64_t arg2)
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

void helper_msa_binsr_b(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->b[0]  = msa_binsr_df(DF_BYTE, pwd->b[0],  pws->b[0],  pwt->b[0]);
    pwd->b[1]  = msa_binsr_df(DF_BYTE, pwd->b[1],  pws->b[1],  pwt->b[1]);
    pwd->b[2]  = msa_binsr_df(DF_BYTE, pwd->b[2],  pws->b[2],  pwt->b[2]);
    pwd->b[3]  = msa_binsr_df(DF_BYTE, pwd->b[3],  pws->b[3],  pwt->b[3]);
    pwd->b[4]  = msa_binsr_df(DF_BYTE, pwd->b[4],  pws->b[4],  pwt->b[4]);
    pwd->b[5]  = msa_binsr_df(DF_BYTE, pwd->b[5],  pws->b[5],  pwt->b[5]);
    pwd->b[6]  = msa_binsr_df(DF_BYTE, pwd->b[6],  pws->b[6],  pwt->b[6]);
    pwd->b[7]  = msa_binsr_df(DF_BYTE, pwd->b[7],  pws->b[7],  pwt->b[7]);
    pwd->b[8]  = msa_binsr_df(DF_BYTE, pwd->b[8],  pws->b[8],  pwt->b[8]);
    pwd->b[9]  = msa_binsr_df(DF_BYTE, pwd->b[9],  pws->b[9],  pwt->b[9]);
    pwd->b[10] = msa_binsr_df(DF_BYTE, pwd->b[10], pws->b[10], pwt->b[10]);
    pwd->b[11] = msa_binsr_df(DF_BYTE, pwd->b[11], pws->b[11], pwt->b[11]);
    pwd->b[12] = msa_binsr_df(DF_BYTE, pwd->b[12], pws->b[12], pwt->b[12]);
    pwd->b[13] = msa_binsr_df(DF_BYTE, pwd->b[13], pws->b[13], pwt->b[13]);
    pwd->b[14] = msa_binsr_df(DF_BYTE, pwd->b[14], pws->b[14], pwt->b[14]);
    pwd->b[15] = msa_binsr_df(DF_BYTE, pwd->b[15], pws->b[15], pwt->b[15]);
}

void helper_msa_binsr_h(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->h[0]  = msa_binsr_df(DF_HALF, pwd->h[0],  pws->h[0],  pwt->h[0]);
    pwd->h[1]  = msa_binsr_df(DF_HALF, pwd->h[1],  pws->h[1],  pwt->h[1]);
    pwd->h[2]  = msa_binsr_df(DF_HALF, pwd->h[2],  pws->h[2],  pwt->h[2]);
    pwd->h[3]  = msa_binsr_df(DF_HALF, pwd->h[3],  pws->h[3],  pwt->h[3]);
    pwd->h[4]  = msa_binsr_df(DF_HALF, pwd->h[4],  pws->h[4],  pwt->h[4]);
    pwd->h[5]  = msa_binsr_df(DF_HALF, pwd->h[5],  pws->h[5],  pwt->h[5]);
    pwd->h[6]  = msa_binsr_df(DF_HALF, pwd->h[6],  pws->h[6],  pwt->h[6]);
    pwd->h[7]  = msa_binsr_df(DF_HALF, pwd->h[7],  pws->h[7],  pwt->h[7]);
}

void helper_msa_binsr_w(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->w[0]  = msa_binsr_df(DF_WORD, pwd->w[0],  pws->w[0],  pwt->w[0]);
    pwd->w[1]  = msa_binsr_df(DF_WORD, pwd->w[1],  pws->w[1],  pwt->w[1]);
    pwd->w[2]  = msa_binsr_df(DF_WORD, pwd->w[2],  pws->w[2],  pwt->w[2]);
    pwd->w[3]  = msa_binsr_df(DF_WORD, pwd->w[3],  pws->w[3],  pwt->w[3]);
}

void helper_msa_binsr_d(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->d[0]  = msa_binsr_df(DF_DOUBLE, pwd->d[0],  pws->d[0],  pwt->d[0]);
    pwd->d[1]  = msa_binsr_df(DF_DOUBLE, pwd->d[1],  pws->d[1],  pwt->d[1]);
}

void helper_msa_bmnz_v(CPUMIPSState *env, uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->d[0] = UNSIGNED(                                                     \
        ((pwd->d[0] & (~pwt->d[0])) | (pws->d[0] & pwt->d[0])), DF_DOUBLE);
    pwd->d[1] = UNSIGNED(                                                     \
        ((pwd->d[1] & (~pwt->d[1])) | (pws->d[1] & pwt->d[1])), DF_DOUBLE);
}

void helper_msa_bmz_v(CPUMIPSState *env, uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->d[0] = UNSIGNED(                                                     \
        ((pwd->d[0] & pwt->d[0]) | (pws->d[0] & (~pwt->d[0]))), DF_DOUBLE);
    pwd->d[1] = UNSIGNED(                                                     \
        ((pwd->d[1] & pwt->d[1]) | (pws->d[1] & (~pwt->d[1]))), DF_DOUBLE);
}

void helper_msa_bsel_v(CPUMIPSState *env, uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->d[0] = UNSIGNED(                                                     \
        (pws->d[0] & (~pwd->d[0])) | (pwt->d[0] & pwd->d[0]), DF_DOUBLE);
    pwd->d[1] = UNSIGNED(                                                     \
        (pws->d[1] & (~pwd->d[1])) | (pwt->d[1] & pwd->d[1]), DF_DOUBLE);
}


/*
 * Bit Set
 * -------
 *
 * +---------------+----------------------------------------------------------+
 * | BCLR.B        | Vector Bit Clear (byte)                                  |
 * | BCLR.H        | Vector Bit Clear (halfword)                              |
 * | BCLR.W        | Vector Bit Clear (word)                                  |
 * | BCLR.D        | Vector Bit Clear (doubleword)                            |
 * | BNEG.B        | Vector Bit Negate (byte)                                 |
 * | BNEG.H        | Vector Bit Negate (halfword)                             |
 * | BNEG.W        | Vector Bit Negate (word)                                 |
 * | BNEG.D        | Vector Bit Negate (doubleword)                           |
 * | BSET.B        | Vector Bit Set (byte)                                    |
 * | BSET.H        | Vector Bit Set (halfword)                                |
 * | BSET.W        | Vector Bit Set (word)                                    |
 * | BSET.D        | Vector Bit Set (doubleword)                              |
 * +---------------+----------------------------------------------------------+
 */

static inline int64_t msa_bclr_df(uint32_t df, int64_t arg1, int64_t arg2)
{
    int32_t b_arg2 = BIT_POSITION(arg2, df);
    return UNSIGNED(arg1 & (~(1LL << b_arg2)), df);
}

void helper_msa_bclr_b(CPUMIPSState *env, uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->b[0]  = msa_bclr_df(DF_BYTE, pws->b[0],  pwt->b[0]);
    pwd->b[1]  = msa_bclr_df(DF_BYTE, pws->b[1],  pwt->b[1]);
    pwd->b[2]  = msa_bclr_df(DF_BYTE, pws->b[2],  pwt->b[2]);
    pwd->b[3]  = msa_bclr_df(DF_BYTE, pws->b[3],  pwt->b[3]);
    pwd->b[4]  = msa_bclr_df(DF_BYTE, pws->b[4],  pwt->b[4]);
    pwd->b[5]  = msa_bclr_df(DF_BYTE, pws->b[5],  pwt->b[5]);
    pwd->b[6]  = msa_bclr_df(DF_BYTE, pws->b[6],  pwt->b[6]);
    pwd->b[7]  = msa_bclr_df(DF_BYTE, pws->b[7],  pwt->b[7]);
    pwd->b[8]  = msa_bclr_df(DF_BYTE, pws->b[8],  pwt->b[8]);
    pwd->b[9]  = msa_bclr_df(DF_BYTE, pws->b[9],  pwt->b[9]);
    pwd->b[10] = msa_bclr_df(DF_BYTE, pws->b[10], pwt->b[10]);
    pwd->b[11] = msa_bclr_df(DF_BYTE, pws->b[11], pwt->b[11]);
    pwd->b[12] = msa_bclr_df(DF_BYTE, pws->b[12], pwt->b[12]);
    pwd->b[13] = msa_bclr_df(DF_BYTE, pws->b[13], pwt->b[13]);
    pwd->b[14] = msa_bclr_df(DF_BYTE, pws->b[14], pwt->b[14]);
    pwd->b[15] = msa_bclr_df(DF_BYTE, pws->b[15], pwt->b[15]);
}

void helper_msa_bclr_h(CPUMIPSState *env, uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->h[0]  = msa_bclr_df(DF_HALF, pws->h[0],  pwt->h[0]);
    pwd->h[1]  = msa_bclr_df(DF_HALF, pws->h[1],  pwt->h[1]);
    pwd->h[2]  = msa_bclr_df(DF_HALF, pws->h[2],  pwt->h[2]);
    pwd->h[3]  = msa_bclr_df(DF_HALF, pws->h[3],  pwt->h[3]);
    pwd->h[4]  = msa_bclr_df(DF_HALF, pws->h[4],  pwt->h[4]);
    pwd->h[5]  = msa_bclr_df(DF_HALF, pws->h[5],  pwt->h[5]);
    pwd->h[6]  = msa_bclr_df(DF_HALF, pws->h[6],  pwt->h[6]);
    pwd->h[7]  = msa_bclr_df(DF_HALF, pws->h[7],  pwt->h[7]);
}

void helper_msa_bclr_w(CPUMIPSState *env, uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->w[0]  = msa_bclr_df(DF_WORD, pws->w[0],  pwt->w[0]);
    pwd->w[1]  = msa_bclr_df(DF_WORD, pws->w[1],  pwt->w[1]);
    pwd->w[2]  = msa_bclr_df(DF_WORD, pws->w[2],  pwt->w[2]);
    pwd->w[3]  = msa_bclr_df(DF_WORD, pws->w[3],  pwt->w[3]);
}

void helper_msa_bclr_d(CPUMIPSState *env, uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->d[0]  = msa_bclr_df(DF_DOUBLE, pws->d[0],  pwt->d[0]);
    pwd->d[1]  = msa_bclr_df(DF_DOUBLE, pws->d[1],  pwt->d[1]);
}

static inline int64_t msa_bneg_df(uint32_t df, int64_t arg1, int64_t arg2)
{
    int32_t b_arg2 = BIT_POSITION(arg2, df);
    return UNSIGNED(arg1 ^ (1LL << b_arg2), df);
}

void helper_msa_bneg_b(CPUMIPSState *env, uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->b[0]  = msa_bneg_df(DF_BYTE, pws->b[0],  pwt->b[0]);
    pwd->b[1]  = msa_bneg_df(DF_BYTE, pws->b[1],  pwt->b[1]);
    pwd->b[2]  = msa_bneg_df(DF_BYTE, pws->b[2],  pwt->b[2]);
    pwd->b[3]  = msa_bneg_df(DF_BYTE, pws->b[3],  pwt->b[3]);
    pwd->b[4]  = msa_bneg_df(DF_BYTE, pws->b[4],  pwt->b[4]);
    pwd->b[5]  = msa_bneg_df(DF_BYTE, pws->b[5],  pwt->b[5]);
    pwd->b[6]  = msa_bneg_df(DF_BYTE, pws->b[6],  pwt->b[6]);
    pwd->b[7]  = msa_bneg_df(DF_BYTE, pws->b[7],  pwt->b[7]);
    pwd->b[8]  = msa_bneg_df(DF_BYTE, pws->b[8],  pwt->b[8]);
    pwd->b[9]  = msa_bneg_df(DF_BYTE, pws->b[9],  pwt->b[9]);
    pwd->b[10] = msa_bneg_df(DF_BYTE, pws->b[10], pwt->b[10]);
    pwd->b[11] = msa_bneg_df(DF_BYTE, pws->b[11], pwt->b[11]);
    pwd->b[12] = msa_bneg_df(DF_BYTE, pws->b[12], pwt->b[12]);
    pwd->b[13] = msa_bneg_df(DF_BYTE, pws->b[13], pwt->b[13]);
    pwd->b[14] = msa_bneg_df(DF_BYTE, pws->b[14], pwt->b[14]);
    pwd->b[15] = msa_bneg_df(DF_BYTE, pws->b[15], pwt->b[15]);
}

void helper_msa_bneg_h(CPUMIPSState *env, uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->h[0]  = msa_bneg_df(DF_HALF, pws->h[0],  pwt->h[0]);
    pwd->h[1]  = msa_bneg_df(DF_HALF, pws->h[1],  pwt->h[1]);
    pwd->h[2]  = msa_bneg_df(DF_HALF, pws->h[2],  pwt->h[2]);
    pwd->h[3]  = msa_bneg_df(DF_HALF, pws->h[3],  pwt->h[3]);
    pwd->h[4]  = msa_bneg_df(DF_HALF, pws->h[4],  pwt->h[4]);
    pwd->h[5]  = msa_bneg_df(DF_HALF, pws->h[5],  pwt->h[5]);
    pwd->h[6]  = msa_bneg_df(DF_HALF, pws->h[6],  pwt->h[6]);
    pwd->h[7]  = msa_bneg_df(DF_HALF, pws->h[7],  pwt->h[7]);
}

void helper_msa_bneg_w(CPUMIPSState *env, uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->w[0]  = msa_bneg_df(DF_WORD, pws->w[0],  pwt->w[0]);
    pwd->w[1]  = msa_bneg_df(DF_WORD, pws->w[1],  pwt->w[1]);
    pwd->w[2]  = msa_bneg_df(DF_WORD, pws->w[2],  pwt->w[2]);
    pwd->w[3]  = msa_bneg_df(DF_WORD, pws->w[3],  pwt->w[3]);
}

void helper_msa_bneg_d(CPUMIPSState *env, uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->d[0]  = msa_bneg_df(DF_DOUBLE, pws->d[0],  pwt->d[0]);
    pwd->d[1]  = msa_bneg_df(DF_DOUBLE, pws->d[1],  pwt->d[1]);
}

static inline int64_t msa_bset_df(uint32_t df, int64_t arg1,
        int64_t arg2)
{
    int32_t b_arg2 = BIT_POSITION(arg2, df);
    return UNSIGNED(arg1 | (1LL << b_arg2), df);
}

void helper_msa_bset_b(CPUMIPSState *env, uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->b[0]  = msa_bset_df(DF_BYTE, pws->b[0],  pwt->b[0]);
    pwd->b[1]  = msa_bset_df(DF_BYTE, pws->b[1],  pwt->b[1]);
    pwd->b[2]  = msa_bset_df(DF_BYTE, pws->b[2],  pwt->b[2]);
    pwd->b[3]  = msa_bset_df(DF_BYTE, pws->b[3],  pwt->b[3]);
    pwd->b[4]  = msa_bset_df(DF_BYTE, pws->b[4],  pwt->b[4]);
    pwd->b[5]  = msa_bset_df(DF_BYTE, pws->b[5],  pwt->b[5]);
    pwd->b[6]  = msa_bset_df(DF_BYTE, pws->b[6],  pwt->b[6]);
    pwd->b[7]  = msa_bset_df(DF_BYTE, pws->b[7],  pwt->b[7]);
    pwd->b[8]  = msa_bset_df(DF_BYTE, pws->b[8],  pwt->b[8]);
    pwd->b[9]  = msa_bset_df(DF_BYTE, pws->b[9],  pwt->b[9]);
    pwd->b[10] = msa_bset_df(DF_BYTE, pws->b[10], pwt->b[10]);
    pwd->b[11] = msa_bset_df(DF_BYTE, pws->b[11], pwt->b[11]);
    pwd->b[12] = msa_bset_df(DF_BYTE, pws->b[12], pwt->b[12]);
    pwd->b[13] = msa_bset_df(DF_BYTE, pws->b[13], pwt->b[13]);
    pwd->b[14] = msa_bset_df(DF_BYTE, pws->b[14], pwt->b[14]);
    pwd->b[15] = msa_bset_df(DF_BYTE, pws->b[15], pwt->b[15]);
}

void helper_msa_bset_h(CPUMIPSState *env, uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->h[0]  = msa_bset_df(DF_HALF, pws->h[0],  pwt->h[0]);
    pwd->h[1]  = msa_bset_df(DF_HALF, pws->h[1],  pwt->h[1]);
    pwd->h[2]  = msa_bset_df(DF_HALF, pws->h[2],  pwt->h[2]);
    pwd->h[3]  = msa_bset_df(DF_HALF, pws->h[3],  pwt->h[3]);
    pwd->h[4]  = msa_bset_df(DF_HALF, pws->h[4],  pwt->h[4]);
    pwd->h[5]  = msa_bset_df(DF_HALF, pws->h[5],  pwt->h[5]);
    pwd->h[6]  = msa_bset_df(DF_HALF, pws->h[6],  pwt->h[6]);
    pwd->h[7]  = msa_bset_df(DF_HALF, pws->h[7],  pwt->h[7]);
}

void helper_msa_bset_w(CPUMIPSState *env, uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->w[0]  = msa_bset_df(DF_WORD, pws->w[0],  pwt->w[0]);
    pwd->w[1]  = msa_bset_df(DF_WORD, pws->w[1],  pwt->w[1]);
    pwd->w[2]  = msa_bset_df(DF_WORD, pws->w[2],  pwt->w[2]);
    pwd->w[3]  = msa_bset_df(DF_WORD, pws->w[3],  pwt->w[3]);
}

void helper_msa_bset_d(CPUMIPSState *env, uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->d[0]  = msa_bset_df(DF_DOUBLE, pws->d[0],  pwt->d[0]);
    pwd->d[1]  = msa_bset_df(DF_DOUBLE, pws->d[1],  pwt->d[1]);
}


/*
 * Fixed Multiply
 * --------------
 *
 * +---------------+----------------------------------------------------------+
 * | MADD_Q.H      | Vector Fixed-Point Multiply and Add (halfword)           |
 * | MADD_Q.W      | Vector Fixed-Point Multiply and Add (word)               |
 * | MADDR_Q.H     | Vector Fixed-Point Multiply and Add Rounded (halfword)   |
 * | MADDR_Q.W     | Vector Fixed-Point Multiply and Add Rounded (word)       |
 * | MSUB_Q.H      | Vector Fixed-Point Multiply and Subtr. (halfword)        |
 * | MSUB_Q.W      | Vector Fixed-Point Multiply and Subtr. (word)            |
 * | MSUBR_Q.H     | Vector Fixed-Point Multiply and Subtr. Rounded (halfword)|
 * | MSUBR_Q.W     | Vector Fixed-Point Multiply and Subtr. Rounded (word)    |
 * | MUL_Q.H       | Vector Fixed-Point Multiply (halfword)                   |
 * | MUL_Q.W       | Vector Fixed-Point Multiply (word)                       |
 * | MULR_Q.H      | Vector Fixed-Point Multiply Rounded (halfword)           |
 * | MULR_Q.W      | Vector Fixed-Point Multiply Rounded (word)               |
 * +---------------+----------------------------------------------------------+
 */

/* TODO: insert Fixed Multiply group helpers here */


/*
 * Float Max Min
 * -------------
 *
 * +---------------+----------------------------------------------------------+
 * | FMAX_A.W      | Vector Floating-Point Maximum (Absolute) (word)          |
 * | FMAX_A.D      | Vector Floating-Point Maximum (Absolute) (doubleword)    |
 * | FMAX.W        | Vector Floating-Point Maximum (word)                     |
 * | FMAX.D        | Vector Floating-Point Maximum (doubleword)               |
 * | FMIN_A.W      | Vector Floating-Point Minimum (Absolute) (word)          |
 * | FMIN_A.D      | Vector Floating-Point Minimum (Absolute) (doubleword)    |
 * | FMIN.W        | Vector Floating-Point Minimum (word)                     |
 * | FMIN.D        | Vector Floating-Point Minimum (doubleword)               |
 * +---------------+----------------------------------------------------------+
 */

/* TODO: insert Float Max Min group helpers here */


/*
 * Int Add
 * -------
 *
 * +---------------+----------------------------------------------------------+
 * | ADD_A.B       | Vector Add Absolute Values (byte)                        |
 * | ADD_A.H       | Vector Add Absolute Values (halfword)                    |
 * | ADD_A.W       | Vector Add Absolute Values (word)                        |
 * | ADD_A.D       | Vector Add Absolute Values (doubleword)                  |
 * | ADDS_A.B      | Vector Signed Saturated Add (of Absolute) (byte)         |
 * | ADDS_A.H      | Vector Signed Saturated Add (of Absolute) (halfword)     |
 * | ADDS_A.W      | Vector Signed Saturated Add (of Absolute) (word)         |
 * | ADDS_A.D      | Vector Signed Saturated Add (of Absolute) (doubleword)   |
 * | ADDS_S.B      | Vector Signed Saturated Add (of Signed) (byte)           |
 * | ADDS_S.H      | Vector Signed Saturated Add (of Signed) (halfword)       |
 * | ADDS_S.W      | Vector Signed Saturated Add (of Signed) (word)           |
 * | ADDS_S.D      | Vector Signed Saturated Add (of Signed) (doubleword)     |
 * | ADDS_U.B      | Vector Unsigned Saturated Add (of Unsigned) (byte)       |
 * | ADDS_U.H      | Vector Unsigned Saturated Add (of Unsigned) (halfword)   |
 * | ADDS_U.W      | Vector Unsigned Saturated Add (of Unsigned) (word)       |
 * | ADDS_U.D      | Vector Unsigned Saturated Add (of Unsigned) (doubleword) |
 * | ADDV.B        | Vector Add (byte)                                        |
 * | ADDV.H        | Vector Add (halfword)                                    |
 * | ADDV.W        | Vector Add (word)                                        |
 * | ADDV.D        | Vector Add (doubleword)                                  |
 * | HADD_S.H      | Vector Signed Horizontal Add (halfword)                  |
 * | HADD_S.W      | Vector Signed Horizontal Add (word)                      |
 * | HADD_S.D      | Vector Signed Horizontal Add (doubleword)                |
 * | HADD_U.H      | Vector Unigned Horizontal Add (halfword)                 |
 * | HADD_U.W      | Vector Unigned Horizontal Add (word)                     |
 * | HADD_U.D      | Vector Unigned Horizontal Add (doubleword)               |
 * +---------------+----------------------------------------------------------+
 */


static inline int64_t msa_add_a_df(uint32_t df, int64_t arg1, int64_t arg2)
{
    uint64_t abs_arg1 = arg1 >= 0 ? arg1 : -arg1;
    uint64_t abs_arg2 = arg2 >= 0 ? arg2 : -arg2;
    return abs_arg1 + abs_arg2;
}

void helper_msa_add_a_b(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->b[0]  = msa_add_a_df(DF_BYTE, pws->b[0],  pwt->b[0]);
    pwd->b[1]  = msa_add_a_df(DF_BYTE, pws->b[1],  pwt->b[1]);
    pwd->b[2]  = msa_add_a_df(DF_BYTE, pws->b[2],  pwt->b[2]);
    pwd->b[3]  = msa_add_a_df(DF_BYTE, pws->b[3],  pwt->b[3]);
    pwd->b[4]  = msa_add_a_df(DF_BYTE, pws->b[4],  pwt->b[4]);
    pwd->b[5]  = msa_add_a_df(DF_BYTE, pws->b[5],  pwt->b[5]);
    pwd->b[6]  = msa_add_a_df(DF_BYTE, pws->b[6],  pwt->b[6]);
    pwd->b[7]  = msa_add_a_df(DF_BYTE, pws->b[7],  pwt->b[7]);
    pwd->b[8]  = msa_add_a_df(DF_BYTE, pws->b[8],  pwt->b[8]);
    pwd->b[9]  = msa_add_a_df(DF_BYTE, pws->b[9],  pwt->b[9]);
    pwd->b[10] = msa_add_a_df(DF_BYTE, pws->b[10], pwt->b[10]);
    pwd->b[11] = msa_add_a_df(DF_BYTE, pws->b[11], pwt->b[11]);
    pwd->b[12] = msa_add_a_df(DF_BYTE, pws->b[12], pwt->b[12]);
    pwd->b[13] = msa_add_a_df(DF_BYTE, pws->b[13], pwt->b[13]);
    pwd->b[14] = msa_add_a_df(DF_BYTE, pws->b[14], pwt->b[14]);
    pwd->b[15] = msa_add_a_df(DF_BYTE, pws->b[15], pwt->b[15]);
}

void helper_msa_add_a_h(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->h[0]  = msa_add_a_df(DF_HALF, pws->h[0],  pwt->h[0]);
    pwd->h[1]  = msa_add_a_df(DF_HALF, pws->h[1],  pwt->h[1]);
    pwd->h[2]  = msa_add_a_df(DF_HALF, pws->h[2],  pwt->h[2]);
    pwd->h[3]  = msa_add_a_df(DF_HALF, pws->h[3],  pwt->h[3]);
    pwd->h[4]  = msa_add_a_df(DF_HALF, pws->h[4],  pwt->h[4]);
    pwd->h[5]  = msa_add_a_df(DF_HALF, pws->h[5],  pwt->h[5]);
    pwd->h[6]  = msa_add_a_df(DF_HALF, pws->h[6],  pwt->h[6]);
    pwd->h[7]  = msa_add_a_df(DF_HALF, pws->h[7],  pwt->h[7]);
}

void helper_msa_add_a_w(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->w[0]  = msa_add_a_df(DF_WORD, pws->w[0],  pwt->w[0]);
    pwd->w[1]  = msa_add_a_df(DF_WORD, pws->w[1],  pwt->w[1]);
    pwd->w[2]  = msa_add_a_df(DF_WORD, pws->w[2],  pwt->w[2]);
    pwd->w[3]  = msa_add_a_df(DF_WORD, pws->w[3],  pwt->w[3]);
}

void helper_msa_add_a_d(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->d[0]  = msa_add_a_df(DF_DOUBLE, pws->d[0],  pwt->d[0]);
    pwd->d[1]  = msa_add_a_df(DF_DOUBLE, pws->d[1],  pwt->d[1]);
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

void helper_msa_adds_a_b(CPUMIPSState *env,
                         uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->b[0]  = msa_adds_a_df(DF_BYTE, pws->b[0],  pwt->b[0]);
    pwd->b[1]  = msa_adds_a_df(DF_BYTE, pws->b[1],  pwt->b[1]);
    pwd->b[2]  = msa_adds_a_df(DF_BYTE, pws->b[2],  pwt->b[2]);
    pwd->b[3]  = msa_adds_a_df(DF_BYTE, pws->b[3],  pwt->b[3]);
    pwd->b[4]  = msa_adds_a_df(DF_BYTE, pws->b[4],  pwt->b[4]);
    pwd->b[5]  = msa_adds_a_df(DF_BYTE, pws->b[5],  pwt->b[5]);
    pwd->b[6]  = msa_adds_a_df(DF_BYTE, pws->b[6],  pwt->b[6]);
    pwd->b[7]  = msa_adds_a_df(DF_BYTE, pws->b[7],  pwt->b[7]);
    pwd->b[8]  = msa_adds_a_df(DF_BYTE, pws->b[8],  pwt->b[8]);
    pwd->b[9]  = msa_adds_a_df(DF_BYTE, pws->b[9],  pwt->b[9]);
    pwd->b[10] = msa_adds_a_df(DF_BYTE, pws->b[10], pwt->b[10]);
    pwd->b[11] = msa_adds_a_df(DF_BYTE, pws->b[11], pwt->b[11]);
    pwd->b[12] = msa_adds_a_df(DF_BYTE, pws->b[12], pwt->b[12]);
    pwd->b[13] = msa_adds_a_df(DF_BYTE, pws->b[13], pwt->b[13]);
    pwd->b[14] = msa_adds_a_df(DF_BYTE, pws->b[14], pwt->b[14]);
    pwd->b[15] = msa_adds_a_df(DF_BYTE, pws->b[15], pwt->b[15]);
}

void helper_msa_adds_a_h(CPUMIPSState *env,
                         uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->h[0]  = msa_adds_a_df(DF_HALF, pws->h[0],  pwt->h[0]);
    pwd->h[1]  = msa_adds_a_df(DF_HALF, pws->h[1],  pwt->h[1]);
    pwd->h[2]  = msa_adds_a_df(DF_HALF, pws->h[2],  pwt->h[2]);
    pwd->h[3]  = msa_adds_a_df(DF_HALF, pws->h[3],  pwt->h[3]);
    pwd->h[4]  = msa_adds_a_df(DF_HALF, pws->h[4],  pwt->h[4]);
    pwd->h[5]  = msa_adds_a_df(DF_HALF, pws->h[5],  pwt->h[5]);
    pwd->h[6]  = msa_adds_a_df(DF_HALF, pws->h[6],  pwt->h[6]);
    pwd->h[7]  = msa_adds_a_df(DF_HALF, pws->h[7],  pwt->h[7]);
}

void helper_msa_adds_a_w(CPUMIPSState *env,
                         uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->w[0]  = msa_adds_a_df(DF_WORD, pws->w[0],  pwt->w[0]);
    pwd->w[1]  = msa_adds_a_df(DF_WORD, pws->w[1],  pwt->w[1]);
    pwd->w[2]  = msa_adds_a_df(DF_WORD, pws->w[2],  pwt->w[2]);
    pwd->w[3]  = msa_adds_a_df(DF_WORD, pws->w[3],  pwt->w[3]);
}

void helper_msa_adds_a_d(CPUMIPSState *env,
                         uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->d[0]  = msa_adds_a_df(DF_DOUBLE, pws->d[0],  pwt->d[0]);
    pwd->d[1]  = msa_adds_a_df(DF_DOUBLE, pws->d[1],  pwt->d[1]);
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

void helper_msa_adds_s_b(CPUMIPSState *env,
                         uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->b[0]  = msa_adds_s_df(DF_BYTE, pws->b[0],  pwt->b[0]);
    pwd->b[1]  = msa_adds_s_df(DF_BYTE, pws->b[1],  pwt->b[1]);
    pwd->b[2]  = msa_adds_s_df(DF_BYTE, pws->b[2],  pwt->b[2]);
    pwd->b[3]  = msa_adds_s_df(DF_BYTE, pws->b[3],  pwt->b[3]);
    pwd->b[4]  = msa_adds_s_df(DF_BYTE, pws->b[4],  pwt->b[4]);
    pwd->b[5]  = msa_adds_s_df(DF_BYTE, pws->b[5],  pwt->b[5]);
    pwd->b[6]  = msa_adds_s_df(DF_BYTE, pws->b[6],  pwt->b[6]);
    pwd->b[7]  = msa_adds_s_df(DF_BYTE, pws->b[7],  pwt->b[7]);
    pwd->b[8]  = msa_adds_s_df(DF_BYTE, pws->b[8],  pwt->b[8]);
    pwd->b[9]  = msa_adds_s_df(DF_BYTE, pws->b[9],  pwt->b[9]);
    pwd->b[10] = msa_adds_s_df(DF_BYTE, pws->b[10], pwt->b[10]);
    pwd->b[11] = msa_adds_s_df(DF_BYTE, pws->b[11], pwt->b[11]);
    pwd->b[12] = msa_adds_s_df(DF_BYTE, pws->b[12], pwt->b[12]);
    pwd->b[13] = msa_adds_s_df(DF_BYTE, pws->b[13], pwt->b[13]);
    pwd->b[14] = msa_adds_s_df(DF_BYTE, pws->b[14], pwt->b[14]);
    pwd->b[15] = msa_adds_s_df(DF_BYTE, pws->b[15], pwt->b[15]);
}

void helper_msa_adds_s_h(CPUMIPSState *env,
                         uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->h[0]  = msa_adds_s_df(DF_HALF, pws->h[0],  pwt->h[0]);
    pwd->h[1]  = msa_adds_s_df(DF_HALF, pws->h[1],  pwt->h[1]);
    pwd->h[2]  = msa_adds_s_df(DF_HALF, pws->h[2],  pwt->h[2]);
    pwd->h[3]  = msa_adds_s_df(DF_HALF, pws->h[3],  pwt->h[3]);
    pwd->h[4]  = msa_adds_s_df(DF_HALF, pws->h[4],  pwt->h[4]);
    pwd->h[5]  = msa_adds_s_df(DF_HALF, pws->h[5],  pwt->h[5]);
    pwd->h[6]  = msa_adds_s_df(DF_HALF, pws->h[6],  pwt->h[6]);
    pwd->h[7]  = msa_adds_s_df(DF_HALF, pws->h[7],  pwt->h[7]);
}

void helper_msa_adds_s_w(CPUMIPSState *env,
                         uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->w[0]  = msa_adds_s_df(DF_WORD, pws->w[0],  pwt->w[0]);
    pwd->w[1]  = msa_adds_s_df(DF_WORD, pws->w[1],  pwt->w[1]);
    pwd->w[2]  = msa_adds_s_df(DF_WORD, pws->w[2],  pwt->w[2]);
    pwd->w[3]  = msa_adds_s_df(DF_WORD, pws->w[3],  pwt->w[3]);
}

void helper_msa_adds_s_d(CPUMIPSState *env,
                         uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->d[0]  = msa_adds_s_df(DF_DOUBLE, pws->d[0],  pwt->d[0]);
    pwd->d[1]  = msa_adds_s_df(DF_DOUBLE, pws->d[1],  pwt->d[1]);
}


static inline uint64_t msa_adds_u_df(uint32_t df, uint64_t arg1, uint64_t arg2)
{
    uint64_t max_uint = DF_MAX_UINT(df);
    uint64_t u_arg1 = UNSIGNED(arg1, df);
    uint64_t u_arg2 = UNSIGNED(arg2, df);
    return (u_arg1 < max_uint - u_arg2) ? u_arg1 + u_arg2 : max_uint;
}

void helper_msa_adds_u_b(CPUMIPSState *env,
                         uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->b[0]  = msa_adds_u_df(DF_BYTE, pws->b[0],  pwt->b[0]);
    pwd->b[1]  = msa_adds_u_df(DF_BYTE, pws->b[1],  pwt->b[1]);
    pwd->b[2]  = msa_adds_u_df(DF_BYTE, pws->b[2],  pwt->b[2]);
    pwd->b[3]  = msa_adds_u_df(DF_BYTE, pws->b[3],  pwt->b[3]);
    pwd->b[4]  = msa_adds_u_df(DF_BYTE, pws->b[4],  pwt->b[4]);
    pwd->b[5]  = msa_adds_u_df(DF_BYTE, pws->b[5],  pwt->b[5]);
    pwd->b[6]  = msa_adds_u_df(DF_BYTE, pws->b[6],  pwt->b[6]);
    pwd->b[7]  = msa_adds_u_df(DF_BYTE, pws->b[7],  pwt->b[7]);
    pwd->b[8]  = msa_adds_u_df(DF_BYTE, pws->b[8],  pwt->b[8]);
    pwd->b[9]  = msa_adds_u_df(DF_BYTE, pws->b[9],  pwt->b[9]);
    pwd->b[10] = msa_adds_u_df(DF_BYTE, pws->b[10], pwt->b[10]);
    pwd->b[11] = msa_adds_u_df(DF_BYTE, pws->b[11], pwt->b[11]);
    pwd->b[12] = msa_adds_u_df(DF_BYTE, pws->b[12], pwt->b[12]);
    pwd->b[13] = msa_adds_u_df(DF_BYTE, pws->b[13], pwt->b[13]);
    pwd->b[14] = msa_adds_u_df(DF_BYTE, pws->b[14], pwt->b[14]);
    pwd->b[15] = msa_adds_u_df(DF_BYTE, pws->b[15], pwt->b[15]);
}

void helper_msa_adds_u_h(CPUMIPSState *env,
                         uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->h[0]  = msa_adds_u_df(DF_HALF, pws->h[0],  pwt->h[0]);
    pwd->h[1]  = msa_adds_u_df(DF_HALF, pws->h[1],  pwt->h[1]);
    pwd->h[2]  = msa_adds_u_df(DF_HALF, pws->h[2],  pwt->h[2]);
    pwd->h[3]  = msa_adds_u_df(DF_HALF, pws->h[3],  pwt->h[3]);
    pwd->h[4]  = msa_adds_u_df(DF_HALF, pws->h[4],  pwt->h[4]);
    pwd->h[5]  = msa_adds_u_df(DF_HALF, pws->h[5],  pwt->h[5]);
    pwd->h[6]  = msa_adds_u_df(DF_HALF, pws->h[6],  pwt->h[6]);
    pwd->h[7]  = msa_adds_u_df(DF_HALF, pws->h[7],  pwt->h[7]);
}

void helper_msa_adds_u_w(CPUMIPSState *env,
                         uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->w[0]  = msa_adds_u_df(DF_WORD, pws->w[0],  pwt->w[0]);
    pwd->w[1]  = msa_adds_u_df(DF_WORD, pws->w[1],  pwt->w[1]);
    pwd->w[2]  = msa_adds_u_df(DF_WORD, pws->w[2],  pwt->w[2]);
    pwd->w[3]  = msa_adds_u_df(DF_WORD, pws->w[3],  pwt->w[3]);
}

void helper_msa_adds_u_d(CPUMIPSState *env,
                         uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->d[0]  = msa_adds_u_df(DF_DOUBLE, pws->d[0],  pwt->d[0]);
    pwd->d[1]  = msa_adds_u_df(DF_DOUBLE, pws->d[1],  pwt->d[1]);
}


static inline int64_t msa_addv_df(uint32_t df, int64_t arg1, int64_t arg2)
{
    return arg1 + arg2;
}

void helper_msa_addv_b(CPUMIPSState *env,
                       uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->b[0]  = msa_addv_df(DF_BYTE, pws->b[0],  pwt->b[0]);
    pwd->b[1]  = msa_addv_df(DF_BYTE, pws->b[1],  pwt->b[1]);
    pwd->b[2]  = msa_addv_df(DF_BYTE, pws->b[2],  pwt->b[2]);
    pwd->b[3]  = msa_addv_df(DF_BYTE, pws->b[3],  pwt->b[3]);
    pwd->b[4]  = msa_addv_df(DF_BYTE, pws->b[4],  pwt->b[4]);
    pwd->b[5]  = msa_addv_df(DF_BYTE, pws->b[5],  pwt->b[5]);
    pwd->b[6]  = msa_addv_df(DF_BYTE, pws->b[6],  pwt->b[6]);
    pwd->b[7]  = msa_addv_df(DF_BYTE, pws->b[7],  pwt->b[7]);
    pwd->b[8]  = msa_addv_df(DF_BYTE, pws->b[8],  pwt->b[8]);
    pwd->b[9]  = msa_addv_df(DF_BYTE, pws->b[9],  pwt->b[9]);
    pwd->b[10] = msa_addv_df(DF_BYTE, pws->b[10], pwt->b[10]);
    pwd->b[11] = msa_addv_df(DF_BYTE, pws->b[11], pwt->b[11]);
    pwd->b[12] = msa_addv_df(DF_BYTE, pws->b[12], pwt->b[12]);
    pwd->b[13] = msa_addv_df(DF_BYTE, pws->b[13], pwt->b[13]);
    pwd->b[14] = msa_addv_df(DF_BYTE, pws->b[14], pwt->b[14]);
    pwd->b[15] = msa_addv_df(DF_BYTE, pws->b[15], pwt->b[15]);
}

void helper_msa_addv_h(CPUMIPSState *env,
                       uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->h[0]  = msa_addv_df(DF_HALF, pws->h[0],  pwt->h[0]);
    pwd->h[1]  = msa_addv_df(DF_HALF, pws->h[1],  pwt->h[1]);
    pwd->h[2]  = msa_addv_df(DF_HALF, pws->h[2],  pwt->h[2]);
    pwd->h[3]  = msa_addv_df(DF_HALF, pws->h[3],  pwt->h[3]);
    pwd->h[4]  = msa_addv_df(DF_HALF, pws->h[4],  pwt->h[4]);
    pwd->h[5]  = msa_addv_df(DF_HALF, pws->h[5],  pwt->h[5]);
    pwd->h[6]  = msa_addv_df(DF_HALF, pws->h[6],  pwt->h[6]);
    pwd->h[7]  = msa_addv_df(DF_HALF, pws->h[7],  pwt->h[7]);
}

void helper_msa_addv_w(CPUMIPSState *env,
                       uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->w[0]  = msa_addv_df(DF_WORD, pws->w[0],  pwt->w[0]);
    pwd->w[1]  = msa_addv_df(DF_WORD, pws->w[1],  pwt->w[1]);
    pwd->w[2]  = msa_addv_df(DF_WORD, pws->w[2],  pwt->w[2]);
    pwd->w[3]  = msa_addv_df(DF_WORD, pws->w[3],  pwt->w[3]);
}

void helper_msa_addv_d(CPUMIPSState *env,
                       uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->d[0]  = msa_addv_df(DF_DOUBLE, pws->d[0],  pwt->d[0]);
    pwd->d[1]  = msa_addv_df(DF_DOUBLE, pws->d[1],  pwt->d[1]);
}


#define SIGNED_EVEN(a, df) \
        ((((int64_t)(a)) << (64 - DF_BITS(df) / 2)) >> (64 - DF_BITS(df) / 2))

#define UNSIGNED_EVEN(a, df) \
        ((((uint64_t)(a)) << (64 - DF_BITS(df) / 2)) >> (64 - DF_BITS(df) / 2))

#define SIGNED_ODD(a, df) \
        ((((int64_t)(a)) << (64 - DF_BITS(df))) >> (64 - DF_BITS(df) / 2))

#define UNSIGNED_ODD(a, df) \
        ((((uint64_t)(a)) << (64 - DF_BITS(df))) >> (64 - DF_BITS(df) / 2))


static inline int64_t msa_hadd_s_df(uint32_t df, int64_t arg1, int64_t arg2)
{
    return SIGNED_ODD(arg1, df) + SIGNED_EVEN(arg2, df);
}

void helper_msa_hadd_s_h(CPUMIPSState *env,
                         uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->h[0]  = msa_hadd_s_df(DF_HALF, pws->h[0],  pwt->h[0]);
    pwd->h[1]  = msa_hadd_s_df(DF_HALF, pws->h[1],  pwt->h[1]);
    pwd->h[2]  = msa_hadd_s_df(DF_HALF, pws->h[2],  pwt->h[2]);
    pwd->h[3]  = msa_hadd_s_df(DF_HALF, pws->h[3],  pwt->h[3]);
    pwd->h[4]  = msa_hadd_s_df(DF_HALF, pws->h[4],  pwt->h[4]);
    pwd->h[5]  = msa_hadd_s_df(DF_HALF, pws->h[5],  pwt->h[5]);
    pwd->h[6]  = msa_hadd_s_df(DF_HALF, pws->h[6],  pwt->h[6]);
    pwd->h[7]  = msa_hadd_s_df(DF_HALF, pws->h[7],  pwt->h[7]);
}

void helper_msa_hadd_s_w(CPUMIPSState *env,
                         uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->w[0]  = msa_hadd_s_df(DF_WORD, pws->w[0],  pwt->w[0]);
    pwd->w[1]  = msa_hadd_s_df(DF_WORD, pws->w[1],  pwt->w[1]);
    pwd->w[2]  = msa_hadd_s_df(DF_WORD, pws->w[2],  pwt->w[2]);
    pwd->w[3]  = msa_hadd_s_df(DF_WORD, pws->w[3],  pwt->w[3]);
}

void helper_msa_hadd_s_d(CPUMIPSState *env,
                         uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->d[0]  = msa_hadd_s_df(DF_DOUBLE, pws->d[0],  pwt->d[0]);
    pwd->d[1]  = msa_hadd_s_df(DF_DOUBLE, pws->d[1],  pwt->d[1]);
}


static inline int64_t msa_hadd_u_df(uint32_t df, int64_t arg1, int64_t arg2)
{
    return UNSIGNED_ODD(arg1, df) + UNSIGNED_EVEN(arg2, df);
}

void helper_msa_hadd_u_h(CPUMIPSState *env,
                         uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->h[0]  = msa_hadd_u_df(DF_HALF, pws->h[0],  pwt->h[0]);
    pwd->h[1]  = msa_hadd_u_df(DF_HALF, pws->h[1],  pwt->h[1]);
    pwd->h[2]  = msa_hadd_u_df(DF_HALF, pws->h[2],  pwt->h[2]);
    pwd->h[3]  = msa_hadd_u_df(DF_HALF, pws->h[3],  pwt->h[3]);
    pwd->h[4]  = msa_hadd_u_df(DF_HALF, pws->h[4],  pwt->h[4]);
    pwd->h[5]  = msa_hadd_u_df(DF_HALF, pws->h[5],  pwt->h[5]);
    pwd->h[6]  = msa_hadd_u_df(DF_HALF, pws->h[6],  pwt->h[6]);
    pwd->h[7]  = msa_hadd_u_df(DF_HALF, pws->h[7],  pwt->h[7]);
}

void helper_msa_hadd_u_w(CPUMIPSState *env,
                         uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->w[0]  = msa_hadd_u_df(DF_WORD, pws->w[0],  pwt->w[0]);
    pwd->w[1]  = msa_hadd_u_df(DF_WORD, pws->w[1],  pwt->w[1]);
    pwd->w[2]  = msa_hadd_u_df(DF_WORD, pws->w[2],  pwt->w[2]);
    pwd->w[3]  = msa_hadd_u_df(DF_WORD, pws->w[3],  pwt->w[3]);
}

void helper_msa_hadd_u_d(CPUMIPSState *env,
                         uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->d[0]  = msa_hadd_u_df(DF_DOUBLE, pws->d[0],  pwt->d[0]);
    pwd->d[1]  = msa_hadd_u_df(DF_DOUBLE, pws->d[1],  pwt->d[1]);
}


/*
 * Int Average
 * -----------
 *
 * +---------------+----------------------------------------------------------+
 * | AVE_S.B       | Vector Signed Average (byte)                             |
 * | AVE_S.H       | Vector Signed Average (halfword)                         |
 * | AVE_S.W       | Vector Signed Average (word)                             |
 * | AVE_S.D       | Vector Signed Average (doubleword)                       |
 * | AVE_U.B       | Vector Unsigned Average (byte)                           |
 * | AVE_U.H       | Vector Unsigned Average (halfword)                       |
 * | AVE_U.W       | Vector Unsigned Average (word)                           |
 * | AVE_U.D       | Vector Unsigned Average (doubleword)                     |
 * | AVER_S.B      | Vector Signed Average Rounded (byte)                     |
 * | AVER_S.H      | Vector Signed Average Rounded (halfword)                 |
 * | AVER_S.W      | Vector Signed Average Rounded (word)                     |
 * | AVER_S.D      | Vector Signed Average Rounded (doubleword)               |
 * | AVER_U.B      | Vector Unsigned Average Rounded (byte)                   |
 * | AVER_U.H      | Vector Unsigned Average Rounded (halfword)               |
 * | AVER_U.W      | Vector Unsigned Average Rounded (word)                   |
 * | AVER_U.D      | Vector Unsigned Average Rounded (doubleword)             |
 * +---------------+----------------------------------------------------------+
 */

static inline int64_t msa_ave_s_df(uint32_t df, int64_t arg1, int64_t arg2)
{
    /* signed shift */
    return (arg1 >> 1) + (arg2 >> 1) + (arg1 & arg2 & 1);
}

void helper_msa_ave_s_b(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->b[0]  = msa_ave_s_df(DF_BYTE, pws->b[0],  pwt->b[0]);
    pwd->b[1]  = msa_ave_s_df(DF_BYTE, pws->b[1],  pwt->b[1]);
    pwd->b[2]  = msa_ave_s_df(DF_BYTE, pws->b[2],  pwt->b[2]);
    pwd->b[3]  = msa_ave_s_df(DF_BYTE, pws->b[3],  pwt->b[3]);
    pwd->b[4]  = msa_ave_s_df(DF_BYTE, pws->b[4],  pwt->b[4]);
    pwd->b[5]  = msa_ave_s_df(DF_BYTE, pws->b[5],  pwt->b[5]);
    pwd->b[6]  = msa_ave_s_df(DF_BYTE, pws->b[6],  pwt->b[6]);
    pwd->b[7]  = msa_ave_s_df(DF_BYTE, pws->b[7],  pwt->b[7]);
    pwd->b[8]  = msa_ave_s_df(DF_BYTE, pws->b[8],  pwt->b[8]);
    pwd->b[9]  = msa_ave_s_df(DF_BYTE, pws->b[9],  pwt->b[9]);
    pwd->b[10] = msa_ave_s_df(DF_BYTE, pws->b[10], pwt->b[10]);
    pwd->b[11] = msa_ave_s_df(DF_BYTE, pws->b[11], pwt->b[11]);
    pwd->b[12] = msa_ave_s_df(DF_BYTE, pws->b[12], pwt->b[12]);
    pwd->b[13] = msa_ave_s_df(DF_BYTE, pws->b[13], pwt->b[13]);
    pwd->b[14] = msa_ave_s_df(DF_BYTE, pws->b[14], pwt->b[14]);
    pwd->b[15] = msa_ave_s_df(DF_BYTE, pws->b[15], pwt->b[15]);
}

void helper_msa_ave_s_h(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->h[0]  = msa_ave_s_df(DF_HALF, pws->h[0],  pwt->h[0]);
    pwd->h[1]  = msa_ave_s_df(DF_HALF, pws->h[1],  pwt->h[1]);
    pwd->h[2]  = msa_ave_s_df(DF_HALF, pws->h[2],  pwt->h[2]);
    pwd->h[3]  = msa_ave_s_df(DF_HALF, pws->h[3],  pwt->h[3]);
    pwd->h[4]  = msa_ave_s_df(DF_HALF, pws->h[4],  pwt->h[4]);
    pwd->h[5]  = msa_ave_s_df(DF_HALF, pws->h[5],  pwt->h[5]);
    pwd->h[6]  = msa_ave_s_df(DF_HALF, pws->h[6],  pwt->h[6]);
    pwd->h[7]  = msa_ave_s_df(DF_HALF, pws->h[7],  pwt->h[7]);
}

void helper_msa_ave_s_w(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->w[0]  = msa_ave_s_df(DF_WORD, pws->w[0],  pwt->w[0]);
    pwd->w[1]  = msa_ave_s_df(DF_WORD, pws->w[1],  pwt->w[1]);
    pwd->w[2]  = msa_ave_s_df(DF_WORD, pws->w[2],  pwt->w[2]);
    pwd->w[3]  = msa_ave_s_df(DF_WORD, pws->w[3],  pwt->w[3]);
}

void helper_msa_ave_s_d(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->d[0]  = msa_ave_s_df(DF_DOUBLE, pws->d[0],  pwt->d[0]);
    pwd->d[1]  = msa_ave_s_df(DF_DOUBLE, pws->d[1],  pwt->d[1]);
}

static inline uint64_t msa_ave_u_df(uint32_t df, uint64_t arg1, uint64_t arg2)
{
    uint64_t u_arg1 = UNSIGNED(arg1, df);
    uint64_t u_arg2 = UNSIGNED(arg2, df);
    /* unsigned shift */
    return (u_arg1 >> 1) + (u_arg2 >> 1) + (u_arg1 & u_arg2 & 1);
}

void helper_msa_ave_u_b(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->b[0]  = msa_ave_u_df(DF_BYTE, pws->b[0],  pwt->b[0]);
    pwd->b[1]  = msa_ave_u_df(DF_BYTE, pws->b[1],  pwt->b[1]);
    pwd->b[2]  = msa_ave_u_df(DF_BYTE, pws->b[2],  pwt->b[2]);
    pwd->b[3]  = msa_ave_u_df(DF_BYTE, pws->b[3],  pwt->b[3]);
    pwd->b[4]  = msa_ave_u_df(DF_BYTE, pws->b[4],  pwt->b[4]);
    pwd->b[5]  = msa_ave_u_df(DF_BYTE, pws->b[5],  pwt->b[5]);
    pwd->b[6]  = msa_ave_u_df(DF_BYTE, pws->b[6],  pwt->b[6]);
    pwd->b[7]  = msa_ave_u_df(DF_BYTE, pws->b[7],  pwt->b[7]);
    pwd->b[8]  = msa_ave_u_df(DF_BYTE, pws->b[8],  pwt->b[8]);
    pwd->b[9]  = msa_ave_u_df(DF_BYTE, pws->b[9],  pwt->b[9]);
    pwd->b[10] = msa_ave_u_df(DF_BYTE, pws->b[10], pwt->b[10]);
    pwd->b[11] = msa_ave_u_df(DF_BYTE, pws->b[11], pwt->b[11]);
    pwd->b[12] = msa_ave_u_df(DF_BYTE, pws->b[12], pwt->b[12]);
    pwd->b[13] = msa_ave_u_df(DF_BYTE, pws->b[13], pwt->b[13]);
    pwd->b[14] = msa_ave_u_df(DF_BYTE, pws->b[14], pwt->b[14]);
    pwd->b[15] = msa_ave_u_df(DF_BYTE, pws->b[15], pwt->b[15]);
}

void helper_msa_ave_u_h(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->h[0]  = msa_ave_u_df(DF_HALF, pws->h[0],  pwt->h[0]);
    pwd->h[1]  = msa_ave_u_df(DF_HALF, pws->h[1],  pwt->h[1]);
    pwd->h[2]  = msa_ave_u_df(DF_HALF, pws->h[2],  pwt->h[2]);
    pwd->h[3]  = msa_ave_u_df(DF_HALF, pws->h[3],  pwt->h[3]);
    pwd->h[4]  = msa_ave_u_df(DF_HALF, pws->h[4],  pwt->h[4]);
    pwd->h[5]  = msa_ave_u_df(DF_HALF, pws->h[5],  pwt->h[5]);
    pwd->h[6]  = msa_ave_u_df(DF_HALF, pws->h[6],  pwt->h[6]);
    pwd->h[7]  = msa_ave_u_df(DF_HALF, pws->h[7],  pwt->h[7]);
}

void helper_msa_ave_u_w(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->w[0]  = msa_ave_u_df(DF_WORD, pws->w[0],  pwt->w[0]);
    pwd->w[1]  = msa_ave_u_df(DF_WORD, pws->w[1],  pwt->w[1]);
    pwd->w[2]  = msa_ave_u_df(DF_WORD, pws->w[2],  pwt->w[2]);
    pwd->w[3]  = msa_ave_u_df(DF_WORD, pws->w[3],  pwt->w[3]);
}

void helper_msa_ave_u_d(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->d[0]  = msa_ave_u_df(DF_DOUBLE, pws->d[0],  pwt->d[0]);
    pwd->d[1]  = msa_ave_u_df(DF_DOUBLE, pws->d[1],  pwt->d[1]);
}

static inline int64_t msa_aver_s_df(uint32_t df, int64_t arg1, int64_t arg2)
{
    /* signed shift */
    return (arg1 >> 1) + (arg2 >> 1) + ((arg1 | arg2) & 1);
}

void helper_msa_aver_s_b(CPUMIPSState *env,
                         uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->b[0]  = msa_aver_s_df(DF_BYTE, pws->b[0],  pwt->b[0]);
    pwd->b[1]  = msa_aver_s_df(DF_BYTE, pws->b[1],  pwt->b[1]);
    pwd->b[2]  = msa_aver_s_df(DF_BYTE, pws->b[2],  pwt->b[2]);
    pwd->b[3]  = msa_aver_s_df(DF_BYTE, pws->b[3],  pwt->b[3]);
    pwd->b[4]  = msa_aver_s_df(DF_BYTE, pws->b[4],  pwt->b[4]);
    pwd->b[5]  = msa_aver_s_df(DF_BYTE, pws->b[5],  pwt->b[5]);
    pwd->b[6]  = msa_aver_s_df(DF_BYTE, pws->b[6],  pwt->b[6]);
    pwd->b[7]  = msa_aver_s_df(DF_BYTE, pws->b[7],  pwt->b[7]);
    pwd->b[8]  = msa_aver_s_df(DF_BYTE, pws->b[8],  pwt->b[8]);
    pwd->b[9]  = msa_aver_s_df(DF_BYTE, pws->b[9],  pwt->b[9]);
    pwd->b[10] = msa_aver_s_df(DF_BYTE, pws->b[10], pwt->b[10]);
    pwd->b[11] = msa_aver_s_df(DF_BYTE, pws->b[11], pwt->b[11]);
    pwd->b[12] = msa_aver_s_df(DF_BYTE, pws->b[12], pwt->b[12]);
    pwd->b[13] = msa_aver_s_df(DF_BYTE, pws->b[13], pwt->b[13]);
    pwd->b[14] = msa_aver_s_df(DF_BYTE, pws->b[14], pwt->b[14]);
    pwd->b[15] = msa_aver_s_df(DF_BYTE, pws->b[15], pwt->b[15]);
}

void helper_msa_aver_s_h(CPUMIPSState *env,
                         uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->h[0]  = msa_aver_s_df(DF_HALF, pws->h[0],  pwt->h[0]);
    pwd->h[1]  = msa_aver_s_df(DF_HALF, pws->h[1],  pwt->h[1]);
    pwd->h[2]  = msa_aver_s_df(DF_HALF, pws->h[2],  pwt->h[2]);
    pwd->h[3]  = msa_aver_s_df(DF_HALF, pws->h[3],  pwt->h[3]);
    pwd->h[4]  = msa_aver_s_df(DF_HALF, pws->h[4],  pwt->h[4]);
    pwd->h[5]  = msa_aver_s_df(DF_HALF, pws->h[5],  pwt->h[5]);
    pwd->h[6]  = msa_aver_s_df(DF_HALF, pws->h[6],  pwt->h[6]);
    pwd->h[7]  = msa_aver_s_df(DF_HALF, pws->h[7],  pwt->h[7]);
}

void helper_msa_aver_s_w(CPUMIPSState *env,
                         uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->w[0]  = msa_aver_s_df(DF_WORD, pws->w[0],  pwt->w[0]);
    pwd->w[1]  = msa_aver_s_df(DF_WORD, pws->w[1],  pwt->w[1]);
    pwd->w[2]  = msa_aver_s_df(DF_WORD, pws->w[2],  pwt->w[2]);
    pwd->w[3]  = msa_aver_s_df(DF_WORD, pws->w[3],  pwt->w[3]);
}

void helper_msa_aver_s_d(CPUMIPSState *env,
                         uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->d[0]  = msa_aver_s_df(DF_DOUBLE, pws->d[0],  pwt->d[0]);
    pwd->d[1]  = msa_aver_s_df(DF_DOUBLE, pws->d[1],  pwt->d[1]);
}

static inline uint64_t msa_aver_u_df(uint32_t df, uint64_t arg1, uint64_t arg2)
{
    uint64_t u_arg1 = UNSIGNED(arg1, df);
    uint64_t u_arg2 = UNSIGNED(arg2, df);
    /* unsigned shift */
    return (u_arg1 >> 1) + (u_arg2 >> 1) + ((u_arg1 | u_arg2) & 1);
}

void helper_msa_aver_u_b(CPUMIPSState *env,
                         uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->b[0]  = msa_aver_u_df(DF_BYTE, pws->b[0],  pwt->b[0]);
    pwd->b[1]  = msa_aver_u_df(DF_BYTE, pws->b[1],  pwt->b[1]);
    pwd->b[2]  = msa_aver_u_df(DF_BYTE, pws->b[2],  pwt->b[2]);
    pwd->b[3]  = msa_aver_u_df(DF_BYTE, pws->b[3],  pwt->b[3]);
    pwd->b[4]  = msa_aver_u_df(DF_BYTE, pws->b[4],  pwt->b[4]);
    pwd->b[5]  = msa_aver_u_df(DF_BYTE, pws->b[5],  pwt->b[5]);
    pwd->b[6]  = msa_aver_u_df(DF_BYTE, pws->b[6],  pwt->b[6]);
    pwd->b[7]  = msa_aver_u_df(DF_BYTE, pws->b[7],  pwt->b[7]);
    pwd->b[8]  = msa_aver_u_df(DF_BYTE, pws->b[8],  pwt->b[8]);
    pwd->b[9]  = msa_aver_u_df(DF_BYTE, pws->b[9],  pwt->b[9]);
    pwd->b[10] = msa_aver_u_df(DF_BYTE, pws->b[10], pwt->b[10]);
    pwd->b[11] = msa_aver_u_df(DF_BYTE, pws->b[11], pwt->b[11]);
    pwd->b[12] = msa_aver_u_df(DF_BYTE, pws->b[12], pwt->b[12]);
    pwd->b[13] = msa_aver_u_df(DF_BYTE, pws->b[13], pwt->b[13]);
    pwd->b[14] = msa_aver_u_df(DF_BYTE, pws->b[14], pwt->b[14]);
    pwd->b[15] = msa_aver_u_df(DF_BYTE, pws->b[15], pwt->b[15]);
}

void helper_msa_aver_u_h(CPUMIPSState *env,
                         uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->h[0]  = msa_aver_u_df(DF_HALF, pws->h[0],  pwt->h[0]);
    pwd->h[1]  = msa_aver_u_df(DF_HALF, pws->h[1],  pwt->h[1]);
    pwd->h[2]  = msa_aver_u_df(DF_HALF, pws->h[2],  pwt->h[2]);
    pwd->h[3]  = msa_aver_u_df(DF_HALF, pws->h[3],  pwt->h[3]);
    pwd->h[4]  = msa_aver_u_df(DF_HALF, pws->h[4],  pwt->h[4]);
    pwd->h[5]  = msa_aver_u_df(DF_HALF, pws->h[5],  pwt->h[5]);
    pwd->h[6]  = msa_aver_u_df(DF_HALF, pws->h[6],  pwt->h[6]);
    pwd->h[7]  = msa_aver_u_df(DF_HALF, pws->h[7],  pwt->h[7]);
}

void helper_msa_aver_u_w(CPUMIPSState *env,
                         uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->w[0]  = msa_aver_u_df(DF_WORD, pws->w[0],  pwt->w[0]);
    pwd->w[1]  = msa_aver_u_df(DF_WORD, pws->w[1],  pwt->w[1]);
    pwd->w[2]  = msa_aver_u_df(DF_WORD, pws->w[2],  pwt->w[2]);
    pwd->w[3]  = msa_aver_u_df(DF_WORD, pws->w[3],  pwt->w[3]);
}

void helper_msa_aver_u_d(CPUMIPSState *env,
                         uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->d[0]  = msa_aver_u_df(DF_DOUBLE, pws->d[0],  pwt->d[0]);
    pwd->d[1]  = msa_aver_u_df(DF_DOUBLE, pws->d[1],  pwt->d[1]);
}


/*
 * Int Compare
 * -----------
 *
 * +---------------+----------------------------------------------------------+
 * | CEQ.B         | Vector Compare Equal (byte)                              |
 * | CEQ.H         | Vector Compare Equal (halfword)                          |
 * | CEQ.W         | Vector Compare Equal (word)                              |
 * | CEQ.D         | Vector Compare Equal (doubleword)                        |
 * | CLE_S.B       | Vector Compare Signed Less Than or Equal (byte)          |
 * | CLE_S.H       | Vector Compare Signed Less Than or Equal (halfword)      |
 * | CLE_S.W       | Vector Compare Signed Less Than or Equal (word)          |
 * | CLE_S.D       | Vector Compare Signed Less Than or Equal (doubleword)    |
 * | CLE_U.B       | Vector Compare Unsigned Less Than or Equal (byte)        |
 * | CLE_U.H       | Vector Compare Unsigned Less Than or Equal (halfword)    |
 * | CLE_U.W       | Vector Compare Unsigned Less Than or Equal (word)        |
 * | CLE_U.D       | Vector Compare Unsigned Less Than or Equal (doubleword)  |
 * | CLT_S.B       | Vector Compare Signed Less Than (byte)                   |
 * | CLT_S.H       | Vector Compare Signed Less Than (halfword)               |
 * | CLT_S.W       | Vector Compare Signed Less Than (word)                   |
 * | CLT_S.D       | Vector Compare Signed Less Than (doubleword)             |
 * | CLT_U.B       | Vector Compare Unsigned Less Than (byte)                 |
 * | CLT_U.H       | Vector Compare Unsigned Less Than (halfword)             |
 * | CLT_U.W       | Vector Compare Unsigned Less Than (word)                 |
 * | CLT_U.D       | Vector Compare Unsigned Less Than (doubleword)           |
 * +---------------+----------------------------------------------------------+
 */

static inline int64_t msa_ceq_df(uint32_t df, int64_t arg1, int64_t arg2)
{
    return arg1 == arg2 ? -1 : 0;
}

static inline int8_t msa_ceq_b(int8_t arg1, int8_t arg2)
{
    return arg1 == arg2 ? -1 : 0;
}

void helper_msa_ceq_b(CPUMIPSState *env, uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->b[0]  = msa_ceq_b(pws->b[0],  pwt->b[0]);
    pwd->b[1]  = msa_ceq_b(pws->b[1],  pwt->b[1]);
    pwd->b[2]  = msa_ceq_b(pws->b[2],  pwt->b[2]);
    pwd->b[3]  = msa_ceq_b(pws->b[3],  pwt->b[3]);
    pwd->b[4]  = msa_ceq_b(pws->b[4],  pwt->b[4]);
    pwd->b[5]  = msa_ceq_b(pws->b[5],  pwt->b[5]);
    pwd->b[6]  = msa_ceq_b(pws->b[6],  pwt->b[6]);
    pwd->b[7]  = msa_ceq_b(pws->b[7],  pwt->b[7]);
    pwd->b[8]  = msa_ceq_b(pws->b[8],  pwt->b[8]);
    pwd->b[9]  = msa_ceq_b(pws->b[9],  pwt->b[9]);
    pwd->b[10] = msa_ceq_b(pws->b[10], pwt->b[10]);
    pwd->b[11] = msa_ceq_b(pws->b[11], pwt->b[11]);
    pwd->b[12] = msa_ceq_b(pws->b[12], pwt->b[12]);
    pwd->b[13] = msa_ceq_b(pws->b[13], pwt->b[13]);
    pwd->b[14] = msa_ceq_b(pws->b[14], pwt->b[14]);
    pwd->b[15] = msa_ceq_b(pws->b[15], pwt->b[15]);
}

static inline int16_t msa_ceq_h(int16_t arg1, int16_t arg2)
{
    return arg1 == arg2 ? -1 : 0;
}

void helper_msa_ceq_h(CPUMIPSState *env, uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->h[0]  = msa_ceq_h(pws->h[0],  pwt->h[0]);
    pwd->h[1]  = msa_ceq_h(pws->h[1],  pwt->h[1]);
    pwd->h[2]  = msa_ceq_h(pws->h[2],  pwt->h[2]);
    pwd->h[3]  = msa_ceq_h(pws->h[3],  pwt->h[3]);
    pwd->h[4]  = msa_ceq_h(pws->h[4],  pwt->h[4]);
    pwd->h[5]  = msa_ceq_h(pws->h[5],  pwt->h[5]);
    pwd->h[6]  = msa_ceq_h(pws->h[6],  pwt->h[6]);
    pwd->h[7]  = msa_ceq_h(pws->h[7],  pwt->h[7]);
}

static inline int32_t msa_ceq_w(int32_t arg1, int32_t arg2)
{
    return arg1 == arg2 ? -1 : 0;
}

void helper_msa_ceq_w(CPUMIPSState *env, uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->w[0]  = msa_ceq_w(pws->w[0],  pwt->w[0]);
    pwd->w[1]  = msa_ceq_w(pws->w[1],  pwt->w[1]);
    pwd->w[2]  = msa_ceq_w(pws->w[2],  pwt->w[2]);
    pwd->w[3]  = msa_ceq_w(pws->w[3],  pwt->w[3]);
}

static inline int64_t msa_ceq_d(int64_t arg1, int64_t arg2)
{
    return arg1 == arg2 ? -1 : 0;
}

void helper_msa_ceq_d(CPUMIPSState *env, uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->d[0]  = msa_ceq_d(pws->d[0],  pwt->d[0]);
    pwd->d[1]  = msa_ceq_d(pws->d[1],  pwt->d[1]);
}

static inline int64_t msa_cle_s_df(uint32_t df, int64_t arg1, int64_t arg2)
{
    return arg1 <= arg2 ? -1 : 0;
}

void helper_msa_cle_s_b(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->b[0]  = msa_cle_s_df(DF_BYTE, pws->b[0],  pwt->b[0]);
    pwd->b[1]  = msa_cle_s_df(DF_BYTE, pws->b[1],  pwt->b[1]);
    pwd->b[2]  = msa_cle_s_df(DF_BYTE, pws->b[2],  pwt->b[2]);
    pwd->b[3]  = msa_cle_s_df(DF_BYTE, pws->b[3],  pwt->b[3]);
    pwd->b[4]  = msa_cle_s_df(DF_BYTE, pws->b[4],  pwt->b[4]);
    pwd->b[5]  = msa_cle_s_df(DF_BYTE, pws->b[5],  pwt->b[5]);
    pwd->b[6]  = msa_cle_s_df(DF_BYTE, pws->b[6],  pwt->b[6]);
    pwd->b[7]  = msa_cle_s_df(DF_BYTE, pws->b[7],  pwt->b[7]);
    pwd->b[8]  = msa_cle_s_df(DF_BYTE, pws->b[8],  pwt->b[8]);
    pwd->b[9]  = msa_cle_s_df(DF_BYTE, pws->b[9],  pwt->b[9]);
    pwd->b[10] = msa_cle_s_df(DF_BYTE, pws->b[10], pwt->b[10]);
    pwd->b[11] = msa_cle_s_df(DF_BYTE, pws->b[11], pwt->b[11]);
    pwd->b[12] = msa_cle_s_df(DF_BYTE, pws->b[12], pwt->b[12]);
    pwd->b[13] = msa_cle_s_df(DF_BYTE, pws->b[13], pwt->b[13]);
    pwd->b[14] = msa_cle_s_df(DF_BYTE, pws->b[14], pwt->b[14]);
    pwd->b[15] = msa_cle_s_df(DF_BYTE, pws->b[15], pwt->b[15]);
}

void helper_msa_cle_s_h(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->h[0]  = msa_cle_s_df(DF_HALF, pws->h[0],  pwt->h[0]);
    pwd->h[1]  = msa_cle_s_df(DF_HALF, pws->h[1],  pwt->h[1]);
    pwd->h[2]  = msa_cle_s_df(DF_HALF, pws->h[2],  pwt->h[2]);
    pwd->h[3]  = msa_cle_s_df(DF_HALF, pws->h[3],  pwt->h[3]);
    pwd->h[4]  = msa_cle_s_df(DF_HALF, pws->h[4],  pwt->h[4]);
    pwd->h[5]  = msa_cle_s_df(DF_HALF, pws->h[5],  pwt->h[5]);
    pwd->h[6]  = msa_cle_s_df(DF_HALF, pws->h[6],  pwt->h[6]);
    pwd->h[7]  = msa_cle_s_df(DF_HALF, pws->h[7],  pwt->h[7]);
}

void helper_msa_cle_s_w(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->w[0]  = msa_cle_s_df(DF_WORD, pws->w[0],  pwt->w[0]);
    pwd->w[1]  = msa_cle_s_df(DF_WORD, pws->w[1],  pwt->w[1]);
    pwd->w[2]  = msa_cle_s_df(DF_WORD, pws->w[2],  pwt->w[2]);
    pwd->w[3]  = msa_cle_s_df(DF_WORD, pws->w[3],  pwt->w[3]);
}

void helper_msa_cle_s_d(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->d[0]  = msa_cle_s_df(DF_DOUBLE, pws->d[0],  pwt->d[0]);
    pwd->d[1]  = msa_cle_s_df(DF_DOUBLE, pws->d[1],  pwt->d[1]);
}

static inline int64_t msa_cle_u_df(uint32_t df, int64_t arg1, int64_t arg2)
{
    uint64_t u_arg1 = UNSIGNED(arg1, df);
    uint64_t u_arg2 = UNSIGNED(arg2, df);
    return u_arg1 <= u_arg2 ? -1 : 0;
}

void helper_msa_cle_u_b(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->b[0]  = msa_cle_u_df(DF_BYTE, pws->b[0],  pwt->b[0]);
    pwd->b[1]  = msa_cle_u_df(DF_BYTE, pws->b[1],  pwt->b[1]);
    pwd->b[2]  = msa_cle_u_df(DF_BYTE, pws->b[2],  pwt->b[2]);
    pwd->b[3]  = msa_cle_u_df(DF_BYTE, pws->b[3],  pwt->b[3]);
    pwd->b[4]  = msa_cle_u_df(DF_BYTE, pws->b[4],  pwt->b[4]);
    pwd->b[5]  = msa_cle_u_df(DF_BYTE, pws->b[5],  pwt->b[5]);
    pwd->b[6]  = msa_cle_u_df(DF_BYTE, pws->b[6],  pwt->b[6]);
    pwd->b[7]  = msa_cle_u_df(DF_BYTE, pws->b[7],  pwt->b[7]);
    pwd->b[8]  = msa_cle_u_df(DF_BYTE, pws->b[8],  pwt->b[8]);
    pwd->b[9]  = msa_cle_u_df(DF_BYTE, pws->b[9],  pwt->b[9]);
    pwd->b[10] = msa_cle_u_df(DF_BYTE, pws->b[10], pwt->b[10]);
    pwd->b[11] = msa_cle_u_df(DF_BYTE, pws->b[11], pwt->b[11]);
    pwd->b[12] = msa_cle_u_df(DF_BYTE, pws->b[12], pwt->b[12]);
    pwd->b[13] = msa_cle_u_df(DF_BYTE, pws->b[13], pwt->b[13]);
    pwd->b[14] = msa_cle_u_df(DF_BYTE, pws->b[14], pwt->b[14]);
    pwd->b[15] = msa_cle_u_df(DF_BYTE, pws->b[15], pwt->b[15]);
}

void helper_msa_cle_u_h(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->h[0]  = msa_cle_u_df(DF_HALF, pws->h[0],  pwt->h[0]);
    pwd->h[1]  = msa_cle_u_df(DF_HALF, pws->h[1],  pwt->h[1]);
    pwd->h[2]  = msa_cle_u_df(DF_HALF, pws->h[2],  pwt->h[2]);
    pwd->h[3]  = msa_cle_u_df(DF_HALF, pws->h[3],  pwt->h[3]);
    pwd->h[4]  = msa_cle_u_df(DF_HALF, pws->h[4],  pwt->h[4]);
    pwd->h[5]  = msa_cle_u_df(DF_HALF, pws->h[5],  pwt->h[5]);
    pwd->h[6]  = msa_cle_u_df(DF_HALF, pws->h[6],  pwt->h[6]);
    pwd->h[7]  = msa_cle_u_df(DF_HALF, pws->h[7],  pwt->h[7]);
}

void helper_msa_cle_u_w(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->w[0]  = msa_cle_u_df(DF_WORD, pws->w[0],  pwt->w[0]);
    pwd->w[1]  = msa_cle_u_df(DF_WORD, pws->w[1],  pwt->w[1]);
    pwd->w[2]  = msa_cle_u_df(DF_WORD, pws->w[2],  pwt->w[2]);
    pwd->w[3]  = msa_cle_u_df(DF_WORD, pws->w[3],  pwt->w[3]);
}

void helper_msa_cle_u_d(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->d[0]  = msa_cle_u_df(DF_DOUBLE, pws->d[0],  pwt->d[0]);
    pwd->d[1]  = msa_cle_u_df(DF_DOUBLE, pws->d[1],  pwt->d[1]);
}

static inline int64_t msa_clt_s_df(uint32_t df, int64_t arg1, int64_t arg2)
{
    return arg1 < arg2 ? -1 : 0;
}

static inline int8_t msa_clt_s_b(int8_t arg1, int8_t arg2)
{
    return arg1 < arg2 ? -1 : 0;
}

void helper_msa_clt_s_b(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->b[0]  = msa_clt_s_b(pws->b[0],  pwt->b[0]);
    pwd->b[1]  = msa_clt_s_b(pws->b[1],  pwt->b[1]);
    pwd->b[2]  = msa_clt_s_b(pws->b[2],  pwt->b[2]);
    pwd->b[3]  = msa_clt_s_b(pws->b[3],  pwt->b[3]);
    pwd->b[4]  = msa_clt_s_b(pws->b[4],  pwt->b[4]);
    pwd->b[5]  = msa_clt_s_b(pws->b[5],  pwt->b[5]);
    pwd->b[6]  = msa_clt_s_b(pws->b[6],  pwt->b[6]);
    pwd->b[7]  = msa_clt_s_b(pws->b[7],  pwt->b[7]);
    pwd->b[8]  = msa_clt_s_b(pws->b[8],  pwt->b[8]);
    pwd->b[9]  = msa_clt_s_b(pws->b[9],  pwt->b[9]);
    pwd->b[10] = msa_clt_s_b(pws->b[10], pwt->b[10]);
    pwd->b[11] = msa_clt_s_b(pws->b[11], pwt->b[11]);
    pwd->b[12] = msa_clt_s_b(pws->b[12], pwt->b[12]);
    pwd->b[13] = msa_clt_s_b(pws->b[13], pwt->b[13]);
    pwd->b[14] = msa_clt_s_b(pws->b[14], pwt->b[14]);
    pwd->b[15] = msa_clt_s_b(pws->b[15], pwt->b[15]);
}

static inline int16_t msa_clt_s_h(int16_t arg1, int16_t arg2)
{
    return arg1 < arg2 ? -1 : 0;
}

void helper_msa_clt_s_h(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->h[0]  = msa_clt_s_h(pws->h[0],  pwt->h[0]);
    pwd->h[1]  = msa_clt_s_h(pws->h[1],  pwt->h[1]);
    pwd->h[2]  = msa_clt_s_h(pws->h[2],  pwt->h[2]);
    pwd->h[3]  = msa_clt_s_h(pws->h[3],  pwt->h[3]);
    pwd->h[4]  = msa_clt_s_h(pws->h[4],  pwt->h[4]);
    pwd->h[5]  = msa_clt_s_h(pws->h[5],  pwt->h[5]);
    pwd->h[6]  = msa_clt_s_h(pws->h[6],  pwt->h[6]);
    pwd->h[7]  = msa_clt_s_h(pws->h[7],  pwt->h[7]);
}

static inline int32_t msa_clt_s_w(int32_t arg1, int32_t arg2)
{
    return arg1 < arg2 ? -1 : 0;
}

void helper_msa_clt_s_w(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->w[0]  = msa_clt_s_w(pws->w[0],  pwt->w[0]);
    pwd->w[1]  = msa_clt_s_w(pws->w[1],  pwt->w[1]);
    pwd->w[2]  = msa_clt_s_w(pws->w[2],  pwt->w[2]);
    pwd->w[3]  = msa_clt_s_w(pws->w[3],  pwt->w[3]);
}

static inline int64_t msa_clt_s_d(int64_t arg1, int64_t arg2)
{
    return arg1 < arg2 ? -1 : 0;
}

void helper_msa_clt_s_d(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->d[0]  = msa_clt_s_d(pws->d[0],  pwt->d[0]);
    pwd->d[1]  = msa_clt_s_d(pws->d[1],  pwt->d[1]);
}

static inline int64_t msa_clt_u_df(uint32_t df, int64_t arg1, int64_t arg2)
{
    uint64_t u_arg1 = UNSIGNED(arg1, df);
    uint64_t u_arg2 = UNSIGNED(arg2, df);
    return u_arg1 < u_arg2 ? -1 : 0;
}

void helper_msa_clt_u_b(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->b[0]  = msa_clt_u_df(DF_BYTE, pws->b[0],  pwt->b[0]);
    pwd->b[1]  = msa_clt_u_df(DF_BYTE, pws->b[1],  pwt->b[1]);
    pwd->b[2]  = msa_clt_u_df(DF_BYTE, pws->b[2],  pwt->b[2]);
    pwd->b[3]  = msa_clt_u_df(DF_BYTE, pws->b[3],  pwt->b[3]);
    pwd->b[4]  = msa_clt_u_df(DF_BYTE, pws->b[4],  pwt->b[4]);
    pwd->b[5]  = msa_clt_u_df(DF_BYTE, pws->b[5],  pwt->b[5]);
    pwd->b[6]  = msa_clt_u_df(DF_BYTE, pws->b[6],  pwt->b[6]);
    pwd->b[7]  = msa_clt_u_df(DF_BYTE, pws->b[7],  pwt->b[7]);
    pwd->b[8]  = msa_clt_u_df(DF_BYTE, pws->b[8],  pwt->b[8]);
    pwd->b[9]  = msa_clt_u_df(DF_BYTE, pws->b[9],  pwt->b[9]);
    pwd->b[10] = msa_clt_u_df(DF_BYTE, pws->b[10], pwt->b[10]);
    pwd->b[11] = msa_clt_u_df(DF_BYTE, pws->b[11], pwt->b[11]);
    pwd->b[12] = msa_clt_u_df(DF_BYTE, pws->b[12], pwt->b[12]);
    pwd->b[13] = msa_clt_u_df(DF_BYTE, pws->b[13], pwt->b[13]);
    pwd->b[14] = msa_clt_u_df(DF_BYTE, pws->b[14], pwt->b[14]);
    pwd->b[15] = msa_clt_u_df(DF_BYTE, pws->b[15], pwt->b[15]);
}

void helper_msa_clt_u_h(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->h[0]  = msa_clt_u_df(DF_HALF, pws->h[0],  pwt->h[0]);
    pwd->h[1]  = msa_clt_u_df(DF_HALF, pws->h[1],  pwt->h[1]);
    pwd->h[2]  = msa_clt_u_df(DF_HALF, pws->h[2],  pwt->h[2]);
    pwd->h[3]  = msa_clt_u_df(DF_HALF, pws->h[3],  pwt->h[3]);
    pwd->h[4]  = msa_clt_u_df(DF_HALF, pws->h[4],  pwt->h[4]);
    pwd->h[5]  = msa_clt_u_df(DF_HALF, pws->h[5],  pwt->h[5]);
    pwd->h[6]  = msa_clt_u_df(DF_HALF, pws->h[6],  pwt->h[6]);
    pwd->h[7]  = msa_clt_u_df(DF_HALF, pws->h[7],  pwt->h[7]);
}

void helper_msa_clt_u_w(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->w[0]  = msa_clt_u_df(DF_WORD, pws->w[0],  pwt->w[0]);
    pwd->w[1]  = msa_clt_u_df(DF_WORD, pws->w[1],  pwt->w[1]);
    pwd->w[2]  = msa_clt_u_df(DF_WORD, pws->w[2],  pwt->w[2]);
    pwd->w[3]  = msa_clt_u_df(DF_WORD, pws->w[3],  pwt->w[3]);
}

void helper_msa_clt_u_d(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->d[0]  = msa_clt_u_df(DF_DOUBLE, pws->d[0],  pwt->d[0]);
    pwd->d[1]  = msa_clt_u_df(DF_DOUBLE, pws->d[1],  pwt->d[1]);
}


/*
 * Int Divide
 * ----------
 *
 * +---------------+----------------------------------------------------------+
 * | DIV_S.B       | Vector Signed Divide (byte)                              |
 * | DIV_S.H       | Vector Signed Divide (halfword)                          |
 * | DIV_S.W       | Vector Signed Divide (word)                              |
 * | DIV_S.D       | Vector Signed Divide (doubleword)                        |
 * | DIV_U.B       | Vector Unsigned Divide (byte)                            |
 * | DIV_U.H       | Vector Unsigned Divide (halfword)                        |
 * | DIV_U.W       | Vector Unsigned Divide (word)                            |
 * | DIV_U.D       | Vector Unsigned Divide (doubleword)                      |
 * +---------------+----------------------------------------------------------+
 */


static inline int64_t msa_div_s_df(uint32_t df, int64_t arg1, int64_t arg2)
{
    if (arg1 == DF_MIN_INT(df) && arg2 == -1) {
        return DF_MIN_INT(df);
    }
    return arg2 ? arg1 / arg2
                : arg1 >= 0 ? -1 : 1;
}

void helper_msa_div_s_b(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->b[0]  = msa_div_s_df(DF_BYTE, pws->b[0],  pwt->b[0]);
    pwd->b[1]  = msa_div_s_df(DF_BYTE, pws->b[1],  pwt->b[1]);
    pwd->b[2]  = msa_div_s_df(DF_BYTE, pws->b[2],  pwt->b[2]);
    pwd->b[3]  = msa_div_s_df(DF_BYTE, pws->b[3],  pwt->b[3]);
    pwd->b[4]  = msa_div_s_df(DF_BYTE, pws->b[4],  pwt->b[4]);
    pwd->b[5]  = msa_div_s_df(DF_BYTE, pws->b[5],  pwt->b[5]);
    pwd->b[6]  = msa_div_s_df(DF_BYTE, pws->b[6],  pwt->b[6]);
    pwd->b[7]  = msa_div_s_df(DF_BYTE, pws->b[7],  pwt->b[7]);
    pwd->b[8]  = msa_div_s_df(DF_BYTE, pws->b[8],  pwt->b[8]);
    pwd->b[9]  = msa_div_s_df(DF_BYTE, pws->b[9],  pwt->b[9]);
    pwd->b[10] = msa_div_s_df(DF_BYTE, pws->b[10], pwt->b[10]);
    pwd->b[11] = msa_div_s_df(DF_BYTE, pws->b[11], pwt->b[11]);
    pwd->b[12] = msa_div_s_df(DF_BYTE, pws->b[12], pwt->b[12]);
    pwd->b[13] = msa_div_s_df(DF_BYTE, pws->b[13], pwt->b[13]);
    pwd->b[14] = msa_div_s_df(DF_BYTE, pws->b[14], pwt->b[14]);
    pwd->b[15] = msa_div_s_df(DF_BYTE, pws->b[15], pwt->b[15]);
}

void helper_msa_div_s_h(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->h[0]  = msa_div_s_df(DF_HALF, pws->h[0],  pwt->h[0]);
    pwd->h[1]  = msa_div_s_df(DF_HALF, pws->h[1],  pwt->h[1]);
    pwd->h[2]  = msa_div_s_df(DF_HALF, pws->h[2],  pwt->h[2]);
    pwd->h[3]  = msa_div_s_df(DF_HALF, pws->h[3],  pwt->h[3]);
    pwd->h[4]  = msa_div_s_df(DF_HALF, pws->h[4],  pwt->h[4]);
    pwd->h[5]  = msa_div_s_df(DF_HALF, pws->h[5],  pwt->h[5]);
    pwd->h[6]  = msa_div_s_df(DF_HALF, pws->h[6],  pwt->h[6]);
    pwd->h[7]  = msa_div_s_df(DF_HALF, pws->h[7],  pwt->h[7]);
}

void helper_msa_div_s_w(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->w[0]  = msa_div_s_df(DF_WORD, pws->w[0],  pwt->w[0]);
    pwd->w[1]  = msa_div_s_df(DF_WORD, pws->w[1],  pwt->w[1]);
    pwd->w[2]  = msa_div_s_df(DF_WORD, pws->w[2],  pwt->w[2]);
    pwd->w[3]  = msa_div_s_df(DF_WORD, pws->w[3],  pwt->w[3]);
}

void helper_msa_div_s_d(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->d[0]  = msa_div_s_df(DF_DOUBLE, pws->d[0],  pwt->d[0]);
    pwd->d[1]  = msa_div_s_df(DF_DOUBLE, pws->d[1],  pwt->d[1]);
}

static inline int64_t msa_div_u_df(uint32_t df, int64_t arg1, int64_t arg2)
{
    uint64_t u_arg1 = UNSIGNED(arg1, df);
    uint64_t u_arg2 = UNSIGNED(arg2, df);
    return arg2 ? u_arg1 / u_arg2 : -1;
}

void helper_msa_div_u_b(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->b[0]  = msa_div_u_df(DF_BYTE, pws->b[0],  pwt->b[0]);
    pwd->b[1]  = msa_div_u_df(DF_BYTE, pws->b[1],  pwt->b[1]);
    pwd->b[2]  = msa_div_u_df(DF_BYTE, pws->b[2],  pwt->b[2]);
    pwd->b[3]  = msa_div_u_df(DF_BYTE, pws->b[3],  pwt->b[3]);
    pwd->b[4]  = msa_div_u_df(DF_BYTE, pws->b[4],  pwt->b[4]);
    pwd->b[5]  = msa_div_u_df(DF_BYTE, pws->b[5],  pwt->b[5]);
    pwd->b[6]  = msa_div_u_df(DF_BYTE, pws->b[6],  pwt->b[6]);
    pwd->b[7]  = msa_div_u_df(DF_BYTE, pws->b[7],  pwt->b[7]);
    pwd->b[8]  = msa_div_u_df(DF_BYTE, pws->b[8],  pwt->b[8]);
    pwd->b[9]  = msa_div_u_df(DF_BYTE, pws->b[9],  pwt->b[9]);
    pwd->b[10] = msa_div_u_df(DF_BYTE, pws->b[10], pwt->b[10]);
    pwd->b[11] = msa_div_u_df(DF_BYTE, pws->b[11], pwt->b[11]);
    pwd->b[12] = msa_div_u_df(DF_BYTE, pws->b[12], pwt->b[12]);
    pwd->b[13] = msa_div_u_df(DF_BYTE, pws->b[13], pwt->b[13]);
    pwd->b[14] = msa_div_u_df(DF_BYTE, pws->b[14], pwt->b[14]);
    pwd->b[15] = msa_div_u_df(DF_BYTE, pws->b[15], pwt->b[15]);
}

void helper_msa_div_u_h(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->h[0]  = msa_div_u_df(DF_HALF, pws->h[0],  pwt->h[0]);
    pwd->h[1]  = msa_div_u_df(DF_HALF, pws->h[1],  pwt->h[1]);
    pwd->h[2]  = msa_div_u_df(DF_HALF, pws->h[2],  pwt->h[2]);
    pwd->h[3]  = msa_div_u_df(DF_HALF, pws->h[3],  pwt->h[3]);
    pwd->h[4]  = msa_div_u_df(DF_HALF, pws->h[4],  pwt->h[4]);
    pwd->h[5]  = msa_div_u_df(DF_HALF, pws->h[5],  pwt->h[5]);
    pwd->h[6]  = msa_div_u_df(DF_HALF, pws->h[6],  pwt->h[6]);
    pwd->h[7]  = msa_div_u_df(DF_HALF, pws->h[7],  pwt->h[7]);
}

void helper_msa_div_u_w(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->w[0]  = msa_div_u_df(DF_WORD, pws->w[0],  pwt->w[0]);
    pwd->w[1]  = msa_div_u_df(DF_WORD, pws->w[1],  pwt->w[1]);
    pwd->w[2]  = msa_div_u_df(DF_WORD, pws->w[2],  pwt->w[2]);
    pwd->w[3]  = msa_div_u_df(DF_WORD, pws->w[3],  pwt->w[3]);
}

void helper_msa_div_u_d(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->d[0]  = msa_div_u_df(DF_DOUBLE, pws->d[0],  pwt->d[0]);
    pwd->d[1]  = msa_div_u_df(DF_DOUBLE, pws->d[1],  pwt->d[1]);
}


/*
 * Int Dot Product
 * ---------------
 *
 * +---------------+----------------------------------------------------------+
 * | DOTP_S.H      | Vector Signed Dot Product (halfword)                     |
 * | DOTP_S.W      | Vector Signed Dot Product (word)                         |
 * | DOTP_S.D      | Vector Signed Dot Product (doubleword)                   |
 * | DOTP_U.H      | Vector Unsigned Dot Product (halfword)                   |
 * | DOTP_U.W      | Vector Unsigned Dot Product (word)                       |
 * | DOTP_U.D      | Vector Unsigned Dot Product (doubleword)                 |
 * | DPADD_S.H     | Vector Signed Dot Product (halfword)                     |
 * | DPADD_S.W     | Vector Signed Dot Product (word)                         |
 * | DPADD_S.D     | Vector Signed Dot Product (doubleword)                   |
 * | DPADD_U.H     | Vector Unsigned Dot Product (halfword)                   |
 * | DPADD_U.W     | Vector Unsigned Dot Product (word)                       |
 * | DPADD_U.D     | Vector Unsigned Dot Product (doubleword)                 |
 * | DPSUB_S.H     | Vector Signed Dot Product (halfword)                     |
 * | DPSUB_S.W     | Vector Signed Dot Product (word)                         |
 * | DPSUB_S.D     | Vector Signed Dot Product (doubleword)                   |
 * | DPSUB_U.H     | Vector Unsigned Dot Product (halfword)                   |
 * | DPSUB_U.W     | Vector Unsigned Dot Product (word)                       |
 * | DPSUB_U.D     | Vector Unsigned Dot Product (doubleword)                 |
 * +---------------+----------------------------------------------------------+
 */

#define SIGNED_EXTRACT(e, o, a, df)     \
    do {                                \
        e = SIGNED_EVEN(a, df);         \
        o = SIGNED_ODD(a, df);          \
    } while (0)

#define UNSIGNED_EXTRACT(e, o, a, df)   \
    do {                                \
        e = UNSIGNED_EVEN(a, df);       \
        o = UNSIGNED_ODD(a, df);        \
    } while (0)


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

void helper_msa_dotp_s_h(CPUMIPSState *env,
                         uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->h[0]  = msa_dotp_s_df(DF_HALF, pws->h[0],  pwt->h[0]);
    pwd->h[1]  = msa_dotp_s_df(DF_HALF, pws->h[1],  pwt->h[1]);
    pwd->h[2]  = msa_dotp_s_df(DF_HALF, pws->h[2],  pwt->h[2]);
    pwd->h[3]  = msa_dotp_s_df(DF_HALF, pws->h[3],  pwt->h[3]);
    pwd->h[4]  = msa_dotp_s_df(DF_HALF, pws->h[4],  pwt->h[4]);
    pwd->h[5]  = msa_dotp_s_df(DF_HALF, pws->h[5],  pwt->h[5]);
    pwd->h[6]  = msa_dotp_s_df(DF_HALF, pws->h[6],  pwt->h[6]);
    pwd->h[7]  = msa_dotp_s_df(DF_HALF, pws->h[7],  pwt->h[7]);
}

void helper_msa_dotp_s_w(CPUMIPSState *env,
                         uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->w[0]  = msa_dotp_s_df(DF_WORD, pws->w[0],  pwt->w[0]);
    pwd->w[1]  = msa_dotp_s_df(DF_WORD, pws->w[1],  pwt->w[1]);
    pwd->w[2]  = msa_dotp_s_df(DF_WORD, pws->w[2],  pwt->w[2]);
    pwd->w[3]  = msa_dotp_s_df(DF_WORD, pws->w[3],  pwt->w[3]);
}

void helper_msa_dotp_s_d(CPUMIPSState *env,
                         uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->d[0]  = msa_dotp_s_df(DF_DOUBLE, pws->d[0],  pwt->d[0]);
    pwd->d[1]  = msa_dotp_s_df(DF_DOUBLE, pws->d[1],  pwt->d[1]);
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

void helper_msa_dotp_u_h(CPUMIPSState *env,
                         uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->h[0]  = msa_dotp_u_df(DF_HALF, pws->h[0],  pwt->h[0]);
    pwd->h[1]  = msa_dotp_u_df(DF_HALF, pws->h[1],  pwt->h[1]);
    pwd->h[2]  = msa_dotp_u_df(DF_HALF, pws->h[2],  pwt->h[2]);
    pwd->h[3]  = msa_dotp_u_df(DF_HALF, pws->h[3],  pwt->h[3]);
    pwd->h[4]  = msa_dotp_u_df(DF_HALF, pws->h[4],  pwt->h[4]);
    pwd->h[5]  = msa_dotp_u_df(DF_HALF, pws->h[5],  pwt->h[5]);
    pwd->h[6]  = msa_dotp_u_df(DF_HALF, pws->h[6],  pwt->h[6]);
    pwd->h[7]  = msa_dotp_u_df(DF_HALF, pws->h[7],  pwt->h[7]);
}

void helper_msa_dotp_u_w(CPUMIPSState *env,
                         uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->w[0]  = msa_dotp_u_df(DF_WORD, pws->w[0],  pwt->w[0]);
    pwd->w[1]  = msa_dotp_u_df(DF_WORD, pws->w[1],  pwt->w[1]);
    pwd->w[2]  = msa_dotp_u_df(DF_WORD, pws->w[2],  pwt->w[2]);
    pwd->w[3]  = msa_dotp_u_df(DF_WORD, pws->w[3],  pwt->w[3]);
}

void helper_msa_dotp_u_d(CPUMIPSState *env,
                         uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->d[0]  = msa_dotp_u_df(DF_DOUBLE, pws->d[0],  pwt->d[0]);
    pwd->d[1]  = msa_dotp_u_df(DF_DOUBLE, pws->d[1],  pwt->d[1]);
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

void helper_msa_dpadd_s_h(CPUMIPSState *env,
                          uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->h[0]  = msa_dpadd_s_df(DF_HALF, pwd->h[0],  pws->h[0],  pwt->h[0]);
    pwd->h[1]  = msa_dpadd_s_df(DF_HALF, pwd->h[1],  pws->h[1],  pwt->h[1]);
    pwd->h[2]  = msa_dpadd_s_df(DF_HALF, pwd->h[2],  pws->h[2],  pwt->h[2]);
    pwd->h[3]  = msa_dpadd_s_df(DF_HALF, pwd->h[3],  pws->h[3],  pwt->h[3]);
    pwd->h[4]  = msa_dpadd_s_df(DF_HALF, pwd->h[4],  pws->h[4],  pwt->h[4]);
    pwd->h[5]  = msa_dpadd_s_df(DF_HALF, pwd->h[5],  pws->h[5],  pwt->h[5]);
    pwd->h[6]  = msa_dpadd_s_df(DF_HALF, pwd->h[6],  pws->h[6],  pwt->h[6]);
    pwd->h[7]  = msa_dpadd_s_df(DF_HALF, pwd->h[7],  pws->h[7],  pwt->h[7]);
}

void helper_msa_dpadd_s_w(CPUMIPSState *env,
                          uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->w[0]  = msa_dpadd_s_df(DF_WORD, pwd->w[0],  pws->w[0],  pwt->w[0]);
    pwd->w[1]  = msa_dpadd_s_df(DF_WORD, pwd->w[1],  pws->w[1],  pwt->w[1]);
    pwd->w[2]  = msa_dpadd_s_df(DF_WORD, pwd->w[2],  pws->w[2],  pwt->w[2]);
    pwd->w[3]  = msa_dpadd_s_df(DF_WORD, pwd->w[3],  pws->w[3],  pwt->w[3]);
}

void helper_msa_dpadd_s_d(CPUMIPSState *env,
                          uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->d[0]  = msa_dpadd_s_df(DF_DOUBLE, pwd->d[0],  pws->d[0],  pwt->d[0]);
    pwd->d[1]  = msa_dpadd_s_df(DF_DOUBLE, pwd->d[1],  pws->d[1],  pwt->d[1]);
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

void helper_msa_dpadd_u_h(CPUMIPSState *env,
                          uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->h[0]  = msa_dpadd_u_df(DF_HALF, pwd->h[0],  pws->h[0],  pwt->h[0]);
    pwd->h[1]  = msa_dpadd_u_df(DF_HALF, pwd->h[1],  pws->h[1],  pwt->h[1]);
    pwd->h[2]  = msa_dpadd_u_df(DF_HALF, pwd->h[2],  pws->h[2],  pwt->h[2]);
    pwd->h[3]  = msa_dpadd_u_df(DF_HALF, pwd->h[3],  pws->h[3],  pwt->h[3]);
    pwd->h[4]  = msa_dpadd_u_df(DF_HALF, pwd->h[4],  pws->h[4],  pwt->h[4]);
    pwd->h[5]  = msa_dpadd_u_df(DF_HALF, pwd->h[5],  pws->h[5],  pwt->h[5]);
    pwd->h[6]  = msa_dpadd_u_df(DF_HALF, pwd->h[6],  pws->h[6],  pwt->h[6]);
    pwd->h[7]  = msa_dpadd_u_df(DF_HALF, pwd->h[7],  pws->h[7],  pwt->h[7]);
}

void helper_msa_dpadd_u_w(CPUMIPSState *env,
                          uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->w[0]  = msa_dpadd_u_df(DF_WORD, pwd->w[0],  pws->w[0],  pwt->w[0]);
    pwd->w[1]  = msa_dpadd_u_df(DF_WORD, pwd->w[1],  pws->w[1],  pwt->w[1]);
    pwd->w[2]  = msa_dpadd_u_df(DF_WORD, pwd->w[2],  pws->w[2],  pwt->w[2]);
    pwd->w[3]  = msa_dpadd_u_df(DF_WORD, pwd->w[3],  pws->w[3],  pwt->w[3]);
}

void helper_msa_dpadd_u_d(CPUMIPSState *env,
                          uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->d[0]  = msa_dpadd_u_df(DF_DOUBLE, pwd->d[0],  pws->d[0],  pwt->d[0]);
    pwd->d[1]  = msa_dpadd_u_df(DF_DOUBLE, pwd->d[1],  pws->d[1],  pwt->d[1]);
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

void helper_msa_dpsub_s_h(CPUMIPSState *env,
                          uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->h[0]  = msa_dpsub_s_df(DF_HALF, pwd->h[0],  pws->h[0],  pwt->h[0]);
    pwd->h[1]  = msa_dpsub_s_df(DF_HALF, pwd->h[1],  pws->h[1],  pwt->h[1]);
    pwd->h[2]  = msa_dpsub_s_df(DF_HALF, pwd->h[2],  pws->h[2],  pwt->h[2]);
    pwd->h[3]  = msa_dpsub_s_df(DF_HALF, pwd->h[3],  pws->h[3],  pwt->h[3]);
    pwd->h[4]  = msa_dpsub_s_df(DF_HALF, pwd->h[4],  pws->h[4],  pwt->h[4]);
    pwd->h[5]  = msa_dpsub_s_df(DF_HALF, pwd->h[5],  pws->h[5],  pwt->h[5]);
    pwd->h[6]  = msa_dpsub_s_df(DF_HALF, pwd->h[6],  pws->h[6],  pwt->h[6]);
    pwd->h[7]  = msa_dpsub_s_df(DF_HALF, pwd->h[7],  pws->h[7],  pwt->h[7]);
}

void helper_msa_dpsub_s_w(CPUMIPSState *env,
                          uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->w[0]  = msa_dpsub_s_df(DF_WORD, pwd->w[0],  pws->w[0],  pwt->w[0]);
    pwd->w[1]  = msa_dpsub_s_df(DF_WORD, pwd->w[1],  pws->w[1],  pwt->w[1]);
    pwd->w[2]  = msa_dpsub_s_df(DF_WORD, pwd->w[2],  pws->w[2],  pwt->w[2]);
    pwd->w[3]  = msa_dpsub_s_df(DF_WORD, pwd->w[3],  pws->w[3],  pwt->w[3]);
}

void helper_msa_dpsub_s_d(CPUMIPSState *env,
                          uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->d[0]  = msa_dpsub_s_df(DF_DOUBLE, pwd->d[0],  pws->d[0],  pwt->d[0]);
    pwd->d[1]  = msa_dpsub_s_df(DF_DOUBLE, pwd->d[1],  pws->d[1],  pwt->d[1]);
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

void helper_msa_dpsub_u_h(CPUMIPSState *env,
                          uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->h[0]  = msa_dpsub_u_df(DF_HALF, pwd->h[0],  pws->h[0],  pwt->h[0]);
    pwd->h[1]  = msa_dpsub_u_df(DF_HALF, pwd->h[1],  pws->h[1],  pwt->h[1]);
    pwd->h[2]  = msa_dpsub_u_df(DF_HALF, pwd->h[2],  pws->h[2],  pwt->h[2]);
    pwd->h[3]  = msa_dpsub_u_df(DF_HALF, pwd->h[3],  pws->h[3],  pwt->h[3]);
    pwd->h[4]  = msa_dpsub_u_df(DF_HALF, pwd->h[4],  pws->h[4],  pwt->h[4]);
    pwd->h[5]  = msa_dpsub_u_df(DF_HALF, pwd->h[5],  pws->h[5],  pwt->h[5]);
    pwd->h[6]  = msa_dpsub_u_df(DF_HALF, pwd->h[6],  pws->h[6],  pwt->h[6]);
    pwd->h[7]  = msa_dpsub_u_df(DF_HALF, pwd->h[7],  pws->h[7],  pwt->h[7]);
}

void helper_msa_dpsub_u_w(CPUMIPSState *env,
                          uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->w[0]  = msa_dpsub_u_df(DF_WORD, pwd->w[0],  pws->w[0],  pwt->w[0]);
    pwd->w[1]  = msa_dpsub_u_df(DF_WORD, pwd->w[1],  pws->w[1],  pwt->w[1]);
    pwd->w[2]  = msa_dpsub_u_df(DF_WORD, pwd->w[2],  pws->w[2],  pwt->w[2]);
    pwd->w[3]  = msa_dpsub_u_df(DF_WORD, pwd->w[3],  pws->w[3],  pwt->w[3]);
}

void helper_msa_dpsub_u_d(CPUMIPSState *env,
                          uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->d[0]  = msa_dpsub_u_df(DF_DOUBLE, pwd->d[0],  pws->d[0],  pwt->d[0]);
    pwd->d[1]  = msa_dpsub_u_df(DF_DOUBLE, pwd->d[1],  pws->d[1],  pwt->d[1]);
}


/*
 * Int Max Min
 * -----------
 *
 * +---------------+----------------------------------------------------------+
 * | MAX_A.B       | Vector Maximum Based on Absolute Value (byte)            |
 * | MAX_A.H       | Vector Maximum Based on Absolute Value (halfword)        |
 * | MAX_A.W       | Vector Maximum Based on Absolute Value (word)            |
 * | MAX_A.D       | Vector Maximum Based on Absolute Value (doubleword)      |
 * | MAX_S.B       | Vector Signed Maximum (byte)                             |
 * | MAX_S.H       | Vector Signed Maximum (halfword)                         |
 * | MAX_S.W       | Vector Signed Maximum (word)                             |
 * | MAX_S.D       | Vector Signed Maximum (doubleword)                       |
 * | MAX_U.B       | Vector Unsigned Maximum (byte)                           |
 * | MAX_U.H       | Vector Unsigned Maximum (halfword)                       |
 * | MAX_U.W       | Vector Unsigned Maximum (word)                           |
 * | MAX_U.D       | Vector Unsigned Maximum (doubleword)                     |
 * | MIN_A.B       | Vector Minimum Based on Absolute Value (byte)            |
 * | MIN_A.H       | Vector Minimum Based on Absolute Value (halfword)        |
 * | MIN_A.W       | Vector Minimum Based on Absolute Value (word)            |
 * | MIN_A.D       | Vector Minimum Based on Absolute Value (doubleword)      |
 * | MIN_S.B       | Vector Signed Minimum (byte)                             |
 * | MIN_S.H       | Vector Signed Minimum (halfword)                         |
 * | MIN_S.W       | Vector Signed Minimum (word)                             |
 * | MIN_S.D       | Vector Signed Minimum (doubleword)                       |
 * | MIN_U.B       | Vector Unsigned Minimum (byte)                           |
 * | MIN_U.H       | Vector Unsigned Minimum (halfword)                       |
 * | MIN_U.W       | Vector Unsigned Minimum (word)                           |
 * | MIN_U.D       | Vector Unsigned Minimum (doubleword)                     |
 * +---------------+----------------------------------------------------------+
 */

static inline int64_t msa_max_a_df(uint32_t df, int64_t arg1, int64_t arg2)
{
    uint64_t abs_arg1 = arg1 >= 0 ? arg1 : -arg1;
    uint64_t abs_arg2 = arg2 >= 0 ? arg2 : -arg2;
    return abs_arg1 > abs_arg2 ? arg1 : arg2;
}

void helper_msa_max_a_b(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->b[0]  = msa_max_a_df(DF_BYTE, pws->b[0],  pwt->b[0]);
    pwd->b[1]  = msa_max_a_df(DF_BYTE, pws->b[1],  pwt->b[1]);
    pwd->b[2]  = msa_max_a_df(DF_BYTE, pws->b[2],  pwt->b[2]);
    pwd->b[3]  = msa_max_a_df(DF_BYTE, pws->b[3],  pwt->b[3]);
    pwd->b[4]  = msa_max_a_df(DF_BYTE, pws->b[4],  pwt->b[4]);
    pwd->b[5]  = msa_max_a_df(DF_BYTE, pws->b[5],  pwt->b[5]);
    pwd->b[6]  = msa_max_a_df(DF_BYTE, pws->b[6],  pwt->b[6]);
    pwd->b[7]  = msa_max_a_df(DF_BYTE, pws->b[7],  pwt->b[7]);
    pwd->b[8]  = msa_max_a_df(DF_BYTE, pws->b[8],  pwt->b[8]);
    pwd->b[9]  = msa_max_a_df(DF_BYTE, pws->b[9],  pwt->b[9]);
    pwd->b[10] = msa_max_a_df(DF_BYTE, pws->b[10], pwt->b[10]);
    pwd->b[11] = msa_max_a_df(DF_BYTE, pws->b[11], pwt->b[11]);
    pwd->b[12] = msa_max_a_df(DF_BYTE, pws->b[12], pwt->b[12]);
    pwd->b[13] = msa_max_a_df(DF_BYTE, pws->b[13], pwt->b[13]);
    pwd->b[14] = msa_max_a_df(DF_BYTE, pws->b[14], pwt->b[14]);
    pwd->b[15] = msa_max_a_df(DF_BYTE, pws->b[15], pwt->b[15]);
}

void helper_msa_max_a_h(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->h[0]  = msa_max_a_df(DF_HALF, pws->h[0],  pwt->h[0]);
    pwd->h[1]  = msa_max_a_df(DF_HALF, pws->h[1],  pwt->h[1]);
    pwd->h[2]  = msa_max_a_df(DF_HALF, pws->h[2],  pwt->h[2]);
    pwd->h[3]  = msa_max_a_df(DF_HALF, pws->h[3],  pwt->h[3]);
    pwd->h[4]  = msa_max_a_df(DF_HALF, pws->h[4],  pwt->h[4]);
    pwd->h[5]  = msa_max_a_df(DF_HALF, pws->h[5],  pwt->h[5]);
    pwd->h[6]  = msa_max_a_df(DF_HALF, pws->h[6],  pwt->h[6]);
    pwd->h[7]  = msa_max_a_df(DF_HALF, pws->h[7],  pwt->h[7]);
}

void helper_msa_max_a_w(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->w[0]  = msa_max_a_df(DF_WORD, pws->w[0],  pwt->w[0]);
    pwd->w[1]  = msa_max_a_df(DF_WORD, pws->w[1],  pwt->w[1]);
    pwd->w[2]  = msa_max_a_df(DF_WORD, pws->w[2],  pwt->w[2]);
    pwd->w[3]  = msa_max_a_df(DF_WORD, pws->w[3],  pwt->w[3]);
}

void helper_msa_max_a_d(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->d[0]  = msa_max_a_df(DF_DOUBLE, pws->d[0],  pwt->d[0]);
    pwd->d[1]  = msa_max_a_df(DF_DOUBLE, pws->d[1],  pwt->d[1]);
}


static inline int64_t msa_max_s_df(uint32_t df, int64_t arg1, int64_t arg2)
{
    return arg1 > arg2 ? arg1 : arg2;
}

void helper_msa_max_s_b(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->b[0]  = msa_max_s_df(DF_BYTE, pws->b[0],  pwt->b[0]);
    pwd->b[1]  = msa_max_s_df(DF_BYTE, pws->b[1],  pwt->b[1]);
    pwd->b[2]  = msa_max_s_df(DF_BYTE, pws->b[2],  pwt->b[2]);
    pwd->b[3]  = msa_max_s_df(DF_BYTE, pws->b[3],  pwt->b[3]);
    pwd->b[4]  = msa_max_s_df(DF_BYTE, pws->b[4],  pwt->b[4]);
    pwd->b[5]  = msa_max_s_df(DF_BYTE, pws->b[5],  pwt->b[5]);
    pwd->b[6]  = msa_max_s_df(DF_BYTE, pws->b[6],  pwt->b[6]);
    pwd->b[7]  = msa_max_s_df(DF_BYTE, pws->b[7],  pwt->b[7]);
    pwd->b[8]  = msa_max_s_df(DF_BYTE, pws->b[8],  pwt->b[8]);
    pwd->b[9]  = msa_max_s_df(DF_BYTE, pws->b[9],  pwt->b[9]);
    pwd->b[10] = msa_max_s_df(DF_BYTE, pws->b[10], pwt->b[10]);
    pwd->b[11] = msa_max_s_df(DF_BYTE, pws->b[11], pwt->b[11]);
    pwd->b[12] = msa_max_s_df(DF_BYTE, pws->b[12], pwt->b[12]);
    pwd->b[13] = msa_max_s_df(DF_BYTE, pws->b[13], pwt->b[13]);
    pwd->b[14] = msa_max_s_df(DF_BYTE, pws->b[14], pwt->b[14]);
    pwd->b[15] = msa_max_s_df(DF_BYTE, pws->b[15], pwt->b[15]);
}

void helper_msa_max_s_h(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->h[0]  = msa_max_s_df(DF_HALF, pws->h[0],  pwt->h[0]);
    pwd->h[1]  = msa_max_s_df(DF_HALF, pws->h[1],  pwt->h[1]);
    pwd->h[2]  = msa_max_s_df(DF_HALF, pws->h[2],  pwt->h[2]);
    pwd->h[3]  = msa_max_s_df(DF_HALF, pws->h[3],  pwt->h[3]);
    pwd->h[4]  = msa_max_s_df(DF_HALF, pws->h[4],  pwt->h[4]);
    pwd->h[5]  = msa_max_s_df(DF_HALF, pws->h[5],  pwt->h[5]);
    pwd->h[6]  = msa_max_s_df(DF_HALF, pws->h[6],  pwt->h[6]);
    pwd->h[7]  = msa_max_s_df(DF_HALF, pws->h[7],  pwt->h[7]);
}

void helper_msa_max_s_w(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->w[0]  = msa_max_s_df(DF_WORD, pws->w[0],  pwt->w[0]);
    pwd->w[1]  = msa_max_s_df(DF_WORD, pws->w[1],  pwt->w[1]);
    pwd->w[2]  = msa_max_s_df(DF_WORD, pws->w[2],  pwt->w[2]);
    pwd->w[3]  = msa_max_s_df(DF_WORD, pws->w[3],  pwt->w[3]);
}

void helper_msa_max_s_d(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->d[0]  = msa_max_s_df(DF_DOUBLE, pws->d[0],  pwt->d[0]);
    pwd->d[1]  = msa_max_s_df(DF_DOUBLE, pws->d[1],  pwt->d[1]);
}


static inline int64_t msa_max_u_df(uint32_t df, int64_t arg1, int64_t arg2)
{
    uint64_t u_arg1 = UNSIGNED(arg1, df);
    uint64_t u_arg2 = UNSIGNED(arg2, df);
    return u_arg1 > u_arg2 ? arg1 : arg2;
}

void helper_msa_max_u_b(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->b[0]  = msa_max_u_df(DF_BYTE, pws->b[0],  pwt->b[0]);
    pwd->b[1]  = msa_max_u_df(DF_BYTE, pws->b[1],  pwt->b[1]);
    pwd->b[2]  = msa_max_u_df(DF_BYTE, pws->b[2],  pwt->b[2]);
    pwd->b[3]  = msa_max_u_df(DF_BYTE, pws->b[3],  pwt->b[3]);
    pwd->b[4]  = msa_max_u_df(DF_BYTE, pws->b[4],  pwt->b[4]);
    pwd->b[5]  = msa_max_u_df(DF_BYTE, pws->b[5],  pwt->b[5]);
    pwd->b[6]  = msa_max_u_df(DF_BYTE, pws->b[6],  pwt->b[6]);
    pwd->b[7]  = msa_max_u_df(DF_BYTE, pws->b[7],  pwt->b[7]);
    pwd->b[8]  = msa_max_u_df(DF_BYTE, pws->b[8],  pwt->b[8]);
    pwd->b[9]  = msa_max_u_df(DF_BYTE, pws->b[9],  pwt->b[9]);
    pwd->b[10] = msa_max_u_df(DF_BYTE, pws->b[10], pwt->b[10]);
    pwd->b[11] = msa_max_u_df(DF_BYTE, pws->b[11], pwt->b[11]);
    pwd->b[12] = msa_max_u_df(DF_BYTE, pws->b[12], pwt->b[12]);
    pwd->b[13] = msa_max_u_df(DF_BYTE, pws->b[13], pwt->b[13]);
    pwd->b[14] = msa_max_u_df(DF_BYTE, pws->b[14], pwt->b[14]);
    pwd->b[15] = msa_max_u_df(DF_BYTE, pws->b[15], pwt->b[15]);
}

void helper_msa_max_u_h(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->h[0]  = msa_max_u_df(DF_HALF, pws->h[0],  pwt->h[0]);
    pwd->h[1]  = msa_max_u_df(DF_HALF, pws->h[1],  pwt->h[1]);
    pwd->h[2]  = msa_max_u_df(DF_HALF, pws->h[2],  pwt->h[2]);
    pwd->h[3]  = msa_max_u_df(DF_HALF, pws->h[3],  pwt->h[3]);
    pwd->h[4]  = msa_max_u_df(DF_HALF, pws->h[4],  pwt->h[4]);
    pwd->h[5]  = msa_max_u_df(DF_HALF, pws->h[5],  pwt->h[5]);
    pwd->h[6]  = msa_max_u_df(DF_HALF, pws->h[6],  pwt->h[6]);
    pwd->h[7]  = msa_max_u_df(DF_HALF, pws->h[7],  pwt->h[7]);
}

void helper_msa_max_u_w(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->w[0]  = msa_max_u_df(DF_WORD, pws->w[0],  pwt->w[0]);
    pwd->w[1]  = msa_max_u_df(DF_WORD, pws->w[1],  pwt->w[1]);
    pwd->w[2]  = msa_max_u_df(DF_WORD, pws->w[2],  pwt->w[2]);
    pwd->w[3]  = msa_max_u_df(DF_WORD, pws->w[3],  pwt->w[3]);
}

void helper_msa_max_u_d(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->d[0]  = msa_max_u_df(DF_DOUBLE, pws->d[0],  pwt->d[0]);
    pwd->d[1]  = msa_max_u_df(DF_DOUBLE, pws->d[1],  pwt->d[1]);
}


static inline int64_t msa_min_a_df(uint32_t df, int64_t arg1, int64_t arg2)
{
    uint64_t abs_arg1 = arg1 >= 0 ? arg1 : -arg1;
    uint64_t abs_arg2 = arg2 >= 0 ? arg2 : -arg2;
    return abs_arg1 < abs_arg2 ? arg1 : arg2;
}

void helper_msa_min_a_b(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->b[0]  = msa_min_a_df(DF_BYTE, pws->b[0],  pwt->b[0]);
    pwd->b[1]  = msa_min_a_df(DF_BYTE, pws->b[1],  pwt->b[1]);
    pwd->b[2]  = msa_min_a_df(DF_BYTE, pws->b[2],  pwt->b[2]);
    pwd->b[3]  = msa_min_a_df(DF_BYTE, pws->b[3],  pwt->b[3]);
    pwd->b[4]  = msa_min_a_df(DF_BYTE, pws->b[4],  pwt->b[4]);
    pwd->b[5]  = msa_min_a_df(DF_BYTE, pws->b[5],  pwt->b[5]);
    pwd->b[6]  = msa_min_a_df(DF_BYTE, pws->b[6],  pwt->b[6]);
    pwd->b[7]  = msa_min_a_df(DF_BYTE, pws->b[7],  pwt->b[7]);
    pwd->b[8]  = msa_min_a_df(DF_BYTE, pws->b[8],  pwt->b[8]);
    pwd->b[9]  = msa_min_a_df(DF_BYTE, pws->b[9],  pwt->b[9]);
    pwd->b[10] = msa_min_a_df(DF_BYTE, pws->b[10], pwt->b[10]);
    pwd->b[11] = msa_min_a_df(DF_BYTE, pws->b[11], pwt->b[11]);
    pwd->b[12] = msa_min_a_df(DF_BYTE, pws->b[12], pwt->b[12]);
    pwd->b[13] = msa_min_a_df(DF_BYTE, pws->b[13], pwt->b[13]);
    pwd->b[14] = msa_min_a_df(DF_BYTE, pws->b[14], pwt->b[14]);
    pwd->b[15] = msa_min_a_df(DF_BYTE, pws->b[15], pwt->b[15]);
}

void helper_msa_min_a_h(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->h[0]  = msa_min_a_df(DF_HALF, pws->h[0],  pwt->h[0]);
    pwd->h[1]  = msa_min_a_df(DF_HALF, pws->h[1],  pwt->h[1]);
    pwd->h[2]  = msa_min_a_df(DF_HALF, pws->h[2],  pwt->h[2]);
    pwd->h[3]  = msa_min_a_df(DF_HALF, pws->h[3],  pwt->h[3]);
    pwd->h[4]  = msa_min_a_df(DF_HALF, pws->h[4],  pwt->h[4]);
    pwd->h[5]  = msa_min_a_df(DF_HALF, pws->h[5],  pwt->h[5]);
    pwd->h[6]  = msa_min_a_df(DF_HALF, pws->h[6],  pwt->h[6]);
    pwd->h[7]  = msa_min_a_df(DF_HALF, pws->h[7],  pwt->h[7]);
}

void helper_msa_min_a_w(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->w[0]  = msa_min_a_df(DF_WORD, pws->w[0],  pwt->w[0]);
    pwd->w[1]  = msa_min_a_df(DF_WORD, pws->w[1],  pwt->w[1]);
    pwd->w[2]  = msa_min_a_df(DF_WORD, pws->w[2],  pwt->w[2]);
    pwd->w[3]  = msa_min_a_df(DF_WORD, pws->w[3],  pwt->w[3]);
}

void helper_msa_min_a_d(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->d[0]  = msa_min_a_df(DF_DOUBLE, pws->d[0],  pwt->d[0]);
    pwd->d[1]  = msa_min_a_df(DF_DOUBLE, pws->d[1],  pwt->d[1]);
}


static inline int64_t msa_min_s_df(uint32_t df, int64_t arg1, int64_t arg2)
{
    return arg1 < arg2 ? arg1 : arg2;
}

void helper_msa_min_s_b(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->b[0]  = msa_min_s_df(DF_BYTE, pws->b[0],  pwt->b[0]);
    pwd->b[1]  = msa_min_s_df(DF_BYTE, pws->b[1],  pwt->b[1]);
    pwd->b[2]  = msa_min_s_df(DF_BYTE, pws->b[2],  pwt->b[2]);
    pwd->b[3]  = msa_min_s_df(DF_BYTE, pws->b[3],  pwt->b[3]);
    pwd->b[4]  = msa_min_s_df(DF_BYTE, pws->b[4],  pwt->b[4]);
    pwd->b[5]  = msa_min_s_df(DF_BYTE, pws->b[5],  pwt->b[5]);
    pwd->b[6]  = msa_min_s_df(DF_BYTE, pws->b[6],  pwt->b[6]);
    pwd->b[7]  = msa_min_s_df(DF_BYTE, pws->b[7],  pwt->b[7]);
    pwd->b[8]  = msa_min_s_df(DF_BYTE, pws->b[8],  pwt->b[8]);
    pwd->b[9]  = msa_min_s_df(DF_BYTE, pws->b[9],  pwt->b[9]);
    pwd->b[10] = msa_min_s_df(DF_BYTE, pws->b[10], pwt->b[10]);
    pwd->b[11] = msa_min_s_df(DF_BYTE, pws->b[11], pwt->b[11]);
    pwd->b[12] = msa_min_s_df(DF_BYTE, pws->b[12], pwt->b[12]);
    pwd->b[13] = msa_min_s_df(DF_BYTE, pws->b[13], pwt->b[13]);
    pwd->b[14] = msa_min_s_df(DF_BYTE, pws->b[14], pwt->b[14]);
    pwd->b[15] = msa_min_s_df(DF_BYTE, pws->b[15], pwt->b[15]);
}

void helper_msa_min_s_h(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->h[0]  = msa_min_s_df(DF_HALF, pws->h[0],  pwt->h[0]);
    pwd->h[1]  = msa_min_s_df(DF_HALF, pws->h[1],  pwt->h[1]);
    pwd->h[2]  = msa_min_s_df(DF_HALF, pws->h[2],  pwt->h[2]);
    pwd->h[3]  = msa_min_s_df(DF_HALF, pws->h[3],  pwt->h[3]);
    pwd->h[4]  = msa_min_s_df(DF_HALF, pws->h[4],  pwt->h[4]);
    pwd->h[5]  = msa_min_s_df(DF_HALF, pws->h[5],  pwt->h[5]);
    pwd->h[6]  = msa_min_s_df(DF_HALF, pws->h[6],  pwt->h[6]);
    pwd->h[7]  = msa_min_s_df(DF_HALF, pws->h[7],  pwt->h[7]);
}

void helper_msa_min_s_w(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->w[0]  = msa_min_s_df(DF_WORD, pws->w[0],  pwt->w[0]);
    pwd->w[1]  = msa_min_s_df(DF_WORD, pws->w[1],  pwt->w[1]);
    pwd->w[2]  = msa_min_s_df(DF_WORD, pws->w[2],  pwt->w[2]);
    pwd->w[3]  = msa_min_s_df(DF_WORD, pws->w[3],  pwt->w[3]);
}

void helper_msa_min_s_d(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->d[0]  = msa_min_s_df(DF_DOUBLE, pws->d[0],  pwt->d[0]);
    pwd->d[1]  = msa_min_s_df(DF_DOUBLE, pws->d[1],  pwt->d[1]);
}


static inline int64_t msa_min_u_df(uint32_t df, int64_t arg1, int64_t arg2)
{
    uint64_t u_arg1 = UNSIGNED(arg1, df);
    uint64_t u_arg2 = UNSIGNED(arg2, df);
    return u_arg1 < u_arg2 ? arg1 : arg2;
}

void helper_msa_min_u_b(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->b[0]  = msa_min_u_df(DF_BYTE, pws->b[0],  pwt->b[0]);
    pwd->b[1]  = msa_min_u_df(DF_BYTE, pws->b[1],  pwt->b[1]);
    pwd->b[2]  = msa_min_u_df(DF_BYTE, pws->b[2],  pwt->b[2]);
    pwd->b[3]  = msa_min_u_df(DF_BYTE, pws->b[3],  pwt->b[3]);
    pwd->b[4]  = msa_min_u_df(DF_BYTE, pws->b[4],  pwt->b[4]);
    pwd->b[5]  = msa_min_u_df(DF_BYTE, pws->b[5],  pwt->b[5]);
    pwd->b[6]  = msa_min_u_df(DF_BYTE, pws->b[6],  pwt->b[6]);
    pwd->b[7]  = msa_min_u_df(DF_BYTE, pws->b[7],  pwt->b[7]);
    pwd->b[8]  = msa_min_u_df(DF_BYTE, pws->b[8],  pwt->b[8]);
    pwd->b[9]  = msa_min_u_df(DF_BYTE, pws->b[9],  pwt->b[9]);
    pwd->b[10] = msa_min_u_df(DF_BYTE, pws->b[10], pwt->b[10]);
    pwd->b[11] = msa_min_u_df(DF_BYTE, pws->b[11], pwt->b[11]);
    pwd->b[12] = msa_min_u_df(DF_BYTE, pws->b[12], pwt->b[12]);
    pwd->b[13] = msa_min_u_df(DF_BYTE, pws->b[13], pwt->b[13]);
    pwd->b[14] = msa_min_u_df(DF_BYTE, pws->b[14], pwt->b[14]);
    pwd->b[15] = msa_min_u_df(DF_BYTE, pws->b[15], pwt->b[15]);
}

void helper_msa_min_u_h(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->h[0]  = msa_min_u_df(DF_HALF, pws->h[0],  pwt->h[0]);
    pwd->h[1]  = msa_min_u_df(DF_HALF, pws->h[1],  pwt->h[1]);
    pwd->h[2]  = msa_min_u_df(DF_HALF, pws->h[2],  pwt->h[2]);
    pwd->h[3]  = msa_min_u_df(DF_HALF, pws->h[3],  pwt->h[3]);
    pwd->h[4]  = msa_min_u_df(DF_HALF, pws->h[4],  pwt->h[4]);
    pwd->h[5]  = msa_min_u_df(DF_HALF, pws->h[5],  pwt->h[5]);
    pwd->h[6]  = msa_min_u_df(DF_HALF, pws->h[6],  pwt->h[6]);
    pwd->h[7]  = msa_min_u_df(DF_HALF, pws->h[7],  pwt->h[7]);
}

void helper_msa_min_u_w(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->w[0]  = msa_min_u_df(DF_WORD, pws->w[0],  pwt->w[0]);
    pwd->w[1]  = msa_min_u_df(DF_WORD, pws->w[1],  pwt->w[1]);
    pwd->w[2]  = msa_min_u_df(DF_WORD, pws->w[2],  pwt->w[2]);
    pwd->w[3]  = msa_min_u_df(DF_WORD, pws->w[3],  pwt->w[3]);
}

void helper_msa_min_u_d(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->d[0]  = msa_min_u_df(DF_DOUBLE, pws->d[0],  pwt->d[0]);
    pwd->d[1]  = msa_min_u_df(DF_DOUBLE, pws->d[1],  pwt->d[1]);
}


/*
 * Int Modulo
 * ----------
 *
 * +---------------+----------------------------------------------------------+
 * | MOD_S.B       | Vector Signed Modulo (byte)                              |
 * | MOD_S.H       | Vector Signed Modulo (halfword)                          |
 * | MOD_S.W       | Vector Signed Modulo (word)                              |
 * | MOD_S.D       | Vector Signed Modulo (doubleword)                        |
 * | MOD_U.B       | Vector Unsigned Modulo (byte)                            |
 * | MOD_U.H       | Vector Unsigned Modulo (halfword)                        |
 * | MOD_U.W       | Vector Unsigned Modulo (word)                            |
 * | MOD_U.D       | Vector Unsigned Modulo (doubleword)                      |
 * +---------------+----------------------------------------------------------+
 */

static inline int64_t msa_mod_s_df(uint32_t df, int64_t arg1, int64_t arg2)
{
    if (arg1 == DF_MIN_INT(df) && arg2 == -1) {
        return 0;
    }
    return arg2 ? arg1 % arg2 : arg1;
}

void helper_msa_mod_s_b(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->b[0]  = msa_mod_s_df(DF_BYTE, pws->b[0],  pwt->b[0]);
    pwd->b[1]  = msa_mod_s_df(DF_BYTE, pws->b[1],  pwt->b[1]);
    pwd->b[2]  = msa_mod_s_df(DF_BYTE, pws->b[2],  pwt->b[2]);
    pwd->b[3]  = msa_mod_s_df(DF_BYTE, pws->b[3],  pwt->b[3]);
    pwd->b[4]  = msa_mod_s_df(DF_BYTE, pws->b[4],  pwt->b[4]);
    pwd->b[5]  = msa_mod_s_df(DF_BYTE, pws->b[5],  pwt->b[5]);
    pwd->b[6]  = msa_mod_s_df(DF_BYTE, pws->b[6],  pwt->b[6]);
    pwd->b[7]  = msa_mod_s_df(DF_BYTE, pws->b[7],  pwt->b[7]);
    pwd->b[8]  = msa_mod_s_df(DF_BYTE, pws->b[8],  pwt->b[8]);
    pwd->b[9]  = msa_mod_s_df(DF_BYTE, pws->b[9],  pwt->b[9]);
    pwd->b[10] = msa_mod_s_df(DF_BYTE, pws->b[10], pwt->b[10]);
    pwd->b[11] = msa_mod_s_df(DF_BYTE, pws->b[11], pwt->b[11]);
    pwd->b[12] = msa_mod_s_df(DF_BYTE, pws->b[12], pwt->b[12]);
    pwd->b[13] = msa_mod_s_df(DF_BYTE, pws->b[13], pwt->b[13]);
    pwd->b[14] = msa_mod_s_df(DF_BYTE, pws->b[14], pwt->b[14]);
    pwd->b[15] = msa_mod_s_df(DF_BYTE, pws->b[15], pwt->b[15]);
}

void helper_msa_mod_s_h(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->h[0]  = msa_mod_s_df(DF_HALF, pws->h[0],  pwt->h[0]);
    pwd->h[1]  = msa_mod_s_df(DF_HALF, pws->h[1],  pwt->h[1]);
    pwd->h[2]  = msa_mod_s_df(DF_HALF, pws->h[2],  pwt->h[2]);
    pwd->h[3]  = msa_mod_s_df(DF_HALF, pws->h[3],  pwt->h[3]);
    pwd->h[4]  = msa_mod_s_df(DF_HALF, pws->h[4],  pwt->h[4]);
    pwd->h[5]  = msa_mod_s_df(DF_HALF, pws->h[5],  pwt->h[5]);
    pwd->h[6]  = msa_mod_s_df(DF_HALF, pws->h[6],  pwt->h[6]);
    pwd->h[7]  = msa_mod_s_df(DF_HALF, pws->h[7],  pwt->h[7]);
}

void helper_msa_mod_s_w(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->w[0]  = msa_mod_s_df(DF_WORD, pws->w[0],  pwt->w[0]);
    pwd->w[1]  = msa_mod_s_df(DF_WORD, pws->w[1],  pwt->w[1]);
    pwd->w[2]  = msa_mod_s_df(DF_WORD, pws->w[2],  pwt->w[2]);
    pwd->w[3]  = msa_mod_s_df(DF_WORD, pws->w[3],  pwt->w[3]);
}

void helper_msa_mod_s_d(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->d[0]  = msa_mod_s_df(DF_DOUBLE, pws->d[0],  pwt->d[0]);
    pwd->d[1]  = msa_mod_s_df(DF_DOUBLE, pws->d[1],  pwt->d[1]);
}

static inline int64_t msa_mod_u_df(uint32_t df, int64_t arg1, int64_t arg2)
{
    uint64_t u_arg1 = UNSIGNED(arg1, df);
    uint64_t u_arg2 = UNSIGNED(arg2, df);
    return u_arg2 ? u_arg1 % u_arg2 : u_arg1;
}

void helper_msa_mod_u_b(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->b[0]  = msa_mod_u_df(DF_BYTE, pws->b[0],  pwt->b[0]);
    pwd->b[1]  = msa_mod_u_df(DF_BYTE, pws->b[1],  pwt->b[1]);
    pwd->b[2]  = msa_mod_u_df(DF_BYTE, pws->b[2],  pwt->b[2]);
    pwd->b[3]  = msa_mod_u_df(DF_BYTE, pws->b[3],  pwt->b[3]);
    pwd->b[4]  = msa_mod_u_df(DF_BYTE, pws->b[4],  pwt->b[4]);
    pwd->b[5]  = msa_mod_u_df(DF_BYTE, pws->b[5],  pwt->b[5]);
    pwd->b[6]  = msa_mod_u_df(DF_BYTE, pws->b[6],  pwt->b[6]);
    pwd->b[7]  = msa_mod_u_df(DF_BYTE, pws->b[7],  pwt->b[7]);
    pwd->b[8]  = msa_mod_u_df(DF_BYTE, pws->b[8],  pwt->b[8]);
    pwd->b[9]  = msa_mod_u_df(DF_BYTE, pws->b[9],  pwt->b[9]);
    pwd->b[10] = msa_mod_u_df(DF_BYTE, pws->b[10], pwt->b[10]);
    pwd->b[11] = msa_mod_u_df(DF_BYTE, pws->b[11], pwt->b[11]);
    pwd->b[12] = msa_mod_u_df(DF_BYTE, pws->b[12], pwt->b[12]);
    pwd->b[13] = msa_mod_u_df(DF_BYTE, pws->b[13], pwt->b[13]);
    pwd->b[14] = msa_mod_u_df(DF_BYTE, pws->b[14], pwt->b[14]);
    pwd->b[15] = msa_mod_u_df(DF_BYTE, pws->b[15], pwt->b[15]);
}

void helper_msa_mod_u_h(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->h[0]  = msa_mod_u_df(DF_HALF, pws->h[0],  pwt->h[0]);
    pwd->h[1]  = msa_mod_u_df(DF_HALF, pws->h[1],  pwt->h[1]);
    pwd->h[2]  = msa_mod_u_df(DF_HALF, pws->h[2],  pwt->h[2]);
    pwd->h[3]  = msa_mod_u_df(DF_HALF, pws->h[3],  pwt->h[3]);
    pwd->h[4]  = msa_mod_u_df(DF_HALF, pws->h[4],  pwt->h[4]);
    pwd->h[5]  = msa_mod_u_df(DF_HALF, pws->h[5],  pwt->h[5]);
    pwd->h[6]  = msa_mod_u_df(DF_HALF, pws->h[6],  pwt->h[6]);
    pwd->h[7]  = msa_mod_u_df(DF_HALF, pws->h[7],  pwt->h[7]);
}

void helper_msa_mod_u_w(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->w[0]  = msa_mod_u_df(DF_WORD, pws->w[0],  pwt->w[0]);
    pwd->w[1]  = msa_mod_u_df(DF_WORD, pws->w[1],  pwt->w[1]);
    pwd->w[2]  = msa_mod_u_df(DF_WORD, pws->w[2],  pwt->w[2]);
    pwd->w[3]  = msa_mod_u_df(DF_WORD, pws->w[3],  pwt->w[3]);
}

void helper_msa_mod_u_d(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->d[0]  = msa_mod_u_df(DF_DOUBLE, pws->d[0],  pwt->d[0]);
    pwd->d[1]  = msa_mod_u_df(DF_DOUBLE, pws->d[1],  pwt->d[1]);
}


/*
 * Int Multiply
 * ------------
 *
 * +---------------+----------------------------------------------------------+
 * | MADDV.B       | Vector Multiply and Add (byte)                           |
 * | MADDV.H       | Vector Multiply and Add (halfword)                       |
 * | MADDV.W       | Vector Multiply and Add (word)                           |
 * | MADDV.D       | Vector Multiply and Add (doubleword)                     |
 * | MSUBV.B       | Vector Multiply and Subtract (byte)                      |
 * | MSUBV.H       | Vector Multiply and Subtract (halfword)                  |
 * | MSUBV.W       | Vector Multiply and Subtract (word)                      |
 * | MSUBV.D       | Vector Multiply and Subtract (doubleword)                |
 * | MULV.B        | Vector Multiply (byte)                                   |
 * | MULV.H        | Vector Multiply (halfword)                               |
 * | MULV.W        | Vector Multiply (word)                                   |
 * | MULV.D        | Vector Multiply (doubleword)                             |
 * +---------------+----------------------------------------------------------+
 */

static inline int64_t msa_maddv_df(uint32_t df, int64_t dest, int64_t arg1,
                                   int64_t arg2)
{
    return dest + arg1 * arg2;
}

void helper_msa_maddv_b(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->b[0]  = msa_maddv_df(DF_BYTE, pwd->b[0],  pws->b[0],  pwt->b[0]);
    pwd->b[1]  = msa_maddv_df(DF_BYTE, pwd->b[1],  pws->b[1],  pwt->b[1]);
    pwd->b[2]  = msa_maddv_df(DF_BYTE, pwd->b[2],  pws->b[2],  pwt->b[2]);
    pwd->b[3]  = msa_maddv_df(DF_BYTE, pwd->b[3],  pws->b[3],  pwt->b[3]);
    pwd->b[4]  = msa_maddv_df(DF_BYTE, pwd->b[4],  pws->b[4],  pwt->b[4]);
    pwd->b[5]  = msa_maddv_df(DF_BYTE, pwd->b[5],  pws->b[5],  pwt->b[5]);
    pwd->b[6]  = msa_maddv_df(DF_BYTE, pwd->b[6],  pws->b[6],  pwt->b[6]);
    pwd->b[7]  = msa_maddv_df(DF_BYTE, pwd->b[7],  pws->b[7],  pwt->b[7]);
    pwd->b[8]  = msa_maddv_df(DF_BYTE, pwd->b[8],  pws->b[8],  pwt->b[8]);
    pwd->b[9]  = msa_maddv_df(DF_BYTE, pwd->b[9],  pws->b[9],  pwt->b[9]);
    pwd->b[10] = msa_maddv_df(DF_BYTE, pwd->b[10], pws->b[10], pwt->b[10]);
    pwd->b[11] = msa_maddv_df(DF_BYTE, pwd->b[11], pws->b[11], pwt->b[11]);
    pwd->b[12] = msa_maddv_df(DF_BYTE, pwd->b[12], pws->b[12], pwt->b[12]);
    pwd->b[13] = msa_maddv_df(DF_BYTE, pwd->b[13], pws->b[13], pwt->b[13]);
    pwd->b[14] = msa_maddv_df(DF_BYTE, pwd->b[14], pws->b[14], pwt->b[14]);
    pwd->b[15] = msa_maddv_df(DF_BYTE, pwd->b[15], pws->b[15], pwt->b[15]);
}

void helper_msa_maddv_h(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->h[0]  = msa_maddv_df(DF_HALF, pwd->h[0],  pws->h[0],  pwt->h[0]);
    pwd->h[1]  = msa_maddv_df(DF_HALF, pwd->h[1],  pws->h[1],  pwt->h[1]);
    pwd->h[2]  = msa_maddv_df(DF_HALF, pwd->h[2],  pws->h[2],  pwt->h[2]);
    pwd->h[3]  = msa_maddv_df(DF_HALF, pwd->h[3],  pws->h[3],  pwt->h[3]);
    pwd->h[4]  = msa_maddv_df(DF_HALF, pwd->h[4],  pws->h[4],  pwt->h[4]);
    pwd->h[5]  = msa_maddv_df(DF_HALF, pwd->h[5],  pws->h[5],  pwt->h[5]);
    pwd->h[6]  = msa_maddv_df(DF_HALF, pwd->h[6],  pws->h[6],  pwt->h[6]);
    pwd->h[7]  = msa_maddv_df(DF_HALF, pwd->h[7],  pws->h[7],  pwt->h[7]);
}

void helper_msa_maddv_w(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->w[0]  = msa_maddv_df(DF_WORD, pwd->w[0],  pws->w[0],  pwt->w[0]);
    pwd->w[1]  = msa_maddv_df(DF_WORD, pwd->w[1],  pws->w[1],  pwt->w[1]);
    pwd->w[2]  = msa_maddv_df(DF_WORD, pwd->w[2],  pws->w[2],  pwt->w[2]);
    pwd->w[3]  = msa_maddv_df(DF_WORD, pwd->w[3],  pws->w[3],  pwt->w[3]);
}

void helper_msa_maddv_d(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->d[0]  = msa_maddv_df(DF_DOUBLE, pwd->d[0],  pws->d[0],  pwt->d[0]);
    pwd->d[1]  = msa_maddv_df(DF_DOUBLE, pwd->d[1],  pws->d[1],  pwt->d[1]);
}

static inline int64_t msa_msubv_df(uint32_t df, int64_t dest, int64_t arg1,
                                   int64_t arg2)
{
    return dest - arg1 * arg2;
}

void helper_msa_msubv_b(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->b[0]  = msa_msubv_df(DF_BYTE, pwd->b[0],  pws->b[0],  pwt->b[0]);
    pwd->b[1]  = msa_msubv_df(DF_BYTE, pwd->b[1],  pws->b[1],  pwt->b[1]);
    pwd->b[2]  = msa_msubv_df(DF_BYTE, pwd->b[2],  pws->b[2],  pwt->b[2]);
    pwd->b[3]  = msa_msubv_df(DF_BYTE, pwd->b[3],  pws->b[3],  pwt->b[3]);
    pwd->b[4]  = msa_msubv_df(DF_BYTE, pwd->b[4],  pws->b[4],  pwt->b[4]);
    pwd->b[5]  = msa_msubv_df(DF_BYTE, pwd->b[5],  pws->b[5],  pwt->b[5]);
    pwd->b[6]  = msa_msubv_df(DF_BYTE, pwd->b[6],  pws->b[6],  pwt->b[6]);
    pwd->b[7]  = msa_msubv_df(DF_BYTE, pwd->b[7],  pws->b[7],  pwt->b[7]);
    pwd->b[8]  = msa_msubv_df(DF_BYTE, pwd->b[8],  pws->b[8],  pwt->b[8]);
    pwd->b[9]  = msa_msubv_df(DF_BYTE, pwd->b[9],  pws->b[9],  pwt->b[9]);
    pwd->b[10] = msa_msubv_df(DF_BYTE, pwd->b[10], pws->b[10], pwt->b[10]);
    pwd->b[11] = msa_msubv_df(DF_BYTE, pwd->b[11], pws->b[11], pwt->b[11]);
    pwd->b[12] = msa_msubv_df(DF_BYTE, pwd->b[12], pws->b[12], pwt->b[12]);
    pwd->b[13] = msa_msubv_df(DF_BYTE, pwd->b[13], pws->b[13], pwt->b[13]);
    pwd->b[14] = msa_msubv_df(DF_BYTE, pwd->b[14], pws->b[14], pwt->b[14]);
    pwd->b[15] = msa_msubv_df(DF_BYTE, pwd->b[15], pws->b[15], pwt->b[15]);
}

void helper_msa_msubv_h(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->h[0]  = msa_msubv_df(DF_HALF, pwd->h[0],  pws->h[0],  pwt->h[0]);
    pwd->h[1]  = msa_msubv_df(DF_HALF, pwd->h[1],  pws->h[1],  pwt->h[1]);
    pwd->h[2]  = msa_msubv_df(DF_HALF, pwd->h[2],  pws->h[2],  pwt->h[2]);
    pwd->h[3]  = msa_msubv_df(DF_HALF, pwd->h[3],  pws->h[3],  pwt->h[3]);
    pwd->h[4]  = msa_msubv_df(DF_HALF, pwd->h[4],  pws->h[4],  pwt->h[4]);
    pwd->h[5]  = msa_msubv_df(DF_HALF, pwd->h[5],  pws->h[5],  pwt->h[5]);
    pwd->h[6]  = msa_msubv_df(DF_HALF, pwd->h[6],  pws->h[6],  pwt->h[6]);
    pwd->h[7]  = msa_msubv_df(DF_HALF, pwd->h[7],  pws->h[7],  pwt->h[7]);
}

void helper_msa_msubv_w(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->w[0]  = msa_msubv_df(DF_WORD, pwd->w[0],  pws->w[0],  pwt->w[0]);
    pwd->w[1]  = msa_msubv_df(DF_WORD, pwd->w[1],  pws->w[1],  pwt->w[1]);
    pwd->w[2]  = msa_msubv_df(DF_WORD, pwd->w[2],  pws->w[2],  pwt->w[2]);
    pwd->w[3]  = msa_msubv_df(DF_WORD, pwd->w[3],  pws->w[3],  pwt->w[3]);
}

void helper_msa_msubv_d(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->d[0]  = msa_msubv_df(DF_DOUBLE, pwd->d[0],  pws->d[0],  pwt->d[0]);
    pwd->d[1]  = msa_msubv_df(DF_DOUBLE, pwd->d[1],  pws->d[1],  pwt->d[1]);
}


static inline int64_t msa_mulv_df(uint32_t df, int64_t arg1, int64_t arg2)
{
    return arg1 * arg2;
}

void helper_msa_mulv_b(CPUMIPSState *env,
                       uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->b[0]  = msa_mulv_df(DF_BYTE, pws->b[0],  pwt->b[0]);
    pwd->b[1]  = msa_mulv_df(DF_BYTE, pws->b[1],  pwt->b[1]);
    pwd->b[2]  = msa_mulv_df(DF_BYTE, pws->b[2],  pwt->b[2]);
    pwd->b[3]  = msa_mulv_df(DF_BYTE, pws->b[3],  pwt->b[3]);
    pwd->b[4]  = msa_mulv_df(DF_BYTE, pws->b[4],  pwt->b[4]);
    pwd->b[5]  = msa_mulv_df(DF_BYTE, pws->b[5],  pwt->b[5]);
    pwd->b[6]  = msa_mulv_df(DF_BYTE, pws->b[6],  pwt->b[6]);
    pwd->b[7]  = msa_mulv_df(DF_BYTE, pws->b[7],  pwt->b[7]);
    pwd->b[8]  = msa_mulv_df(DF_BYTE, pws->b[8],  pwt->b[8]);
    pwd->b[9]  = msa_mulv_df(DF_BYTE, pws->b[9],  pwt->b[9]);
    pwd->b[10] = msa_mulv_df(DF_BYTE, pws->b[10], pwt->b[10]);
    pwd->b[11] = msa_mulv_df(DF_BYTE, pws->b[11], pwt->b[11]);
    pwd->b[12] = msa_mulv_df(DF_BYTE, pws->b[12], pwt->b[12]);
    pwd->b[13] = msa_mulv_df(DF_BYTE, pws->b[13], pwt->b[13]);
    pwd->b[14] = msa_mulv_df(DF_BYTE, pws->b[14], pwt->b[14]);
    pwd->b[15] = msa_mulv_df(DF_BYTE, pws->b[15], pwt->b[15]);
}

void helper_msa_mulv_h(CPUMIPSState *env,
                       uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->h[0]  = msa_mulv_df(DF_HALF, pws->h[0],  pwt->h[0]);
    pwd->h[1]  = msa_mulv_df(DF_HALF, pws->h[1],  pwt->h[1]);
    pwd->h[2]  = msa_mulv_df(DF_HALF, pws->h[2],  pwt->h[2]);
    pwd->h[3]  = msa_mulv_df(DF_HALF, pws->h[3],  pwt->h[3]);
    pwd->h[4]  = msa_mulv_df(DF_HALF, pws->h[4],  pwt->h[4]);
    pwd->h[5]  = msa_mulv_df(DF_HALF, pws->h[5],  pwt->h[5]);
    pwd->h[6]  = msa_mulv_df(DF_HALF, pws->h[6],  pwt->h[6]);
    pwd->h[7]  = msa_mulv_df(DF_HALF, pws->h[7],  pwt->h[7]);
}

void helper_msa_mulv_w(CPUMIPSState *env,
                       uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->w[0]  = msa_mulv_df(DF_WORD, pws->w[0],  pwt->w[0]);
    pwd->w[1]  = msa_mulv_df(DF_WORD, pws->w[1],  pwt->w[1]);
    pwd->w[2]  = msa_mulv_df(DF_WORD, pws->w[2],  pwt->w[2]);
    pwd->w[3]  = msa_mulv_df(DF_WORD, pws->w[3],  pwt->w[3]);
}

void helper_msa_mulv_d(CPUMIPSState *env,
                       uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->d[0]  = msa_mulv_df(DF_DOUBLE, pws->d[0],  pwt->d[0]);
    pwd->d[1]  = msa_mulv_df(DF_DOUBLE, pws->d[1],  pwt->d[1]);
}


/*
 * Int Subtract
 * ------------
 *
 * +---------------+----------------------------------------------------------+
 * | ASUB_S.B      | Vector Absolute Values of Signed Subtract (byte)         |
 * | ASUB_S.H      | Vector Absolute Values of Signed Subtract (halfword)     |
 * | ASUB_S.W      | Vector Absolute Values of Signed Subtract (word)         |
 * | ASUB_S.D      | Vector Absolute Values of Signed Subtract (doubleword)   |
 * | ASUB_U.B      | Vector Absolute Values of Unsigned Subtract (byte)       |
 * | ASUB_U.H      | Vector Absolute Values of Unsigned Subtract (halfword)   |
 * | ASUB_U.W      | Vector Absolute Values of Unsigned Subtract (word)       |
 * | ASUB_U.D      | Vector Absolute Values of Unsigned Subtract (doubleword) |
 * | HSUB_S.H      | Vector Signed Horizontal Subtract (halfword)             |
 * | HSUB_S.W      | Vector Signed Horizontal Subtract (word)                 |
 * | HSUB_S.D      | Vector Signed Horizontal Subtract (doubleword)           |
 * | HSUB_U.H      | Vector Unigned Horizontal Subtract (halfword)            |
 * | HSUB_U.W      | Vector Unigned Horizontal Subtract (word)                |
 * | HSUB_U.D      | Vector Unigned Horizontal Subtract (doubleword)          |
 * | SUBS_S.B      | Vector Signed Saturated Subtract (of Signed) (byte)      |
 * | SUBS_S.H      | Vector Signed Saturated Subtract (of Signed) (halfword)  |
 * | SUBS_S.W      | Vector Signed Saturated Subtract (of Signed) (word)      |
 * | SUBS_S.D      | Vector Signed Saturated Subtract (of Signed) (doubleword)|
 * | SUBS_U.B      | Vector Unsigned Saturated Subtract (of Uns.) (byte)      |
 * | SUBS_U.H      | Vector Unsigned Saturated Subtract (of Uns.) (halfword)  |
 * | SUBS_U.W      | Vector Unsigned Saturated Subtract (of Uns.) (word)      |
 * | SUBS_U.D      | Vector Unsigned Saturated Subtract (of Uns.) (doubleword)|
 * | SUBSUS_U.B    | Vector Uns. Sat. Subtract (of S. from Uns.) (byte)       |
 * | SUBSUS_U.H    | Vector Uns. Sat. Subtract (of S. from Uns.) (halfword)   |
 * | SUBSUS_U.W    | Vector Uns. Sat. Subtract (of S. from Uns.) (word)       |
 * | SUBSUS_U.D    | Vector Uns. Sat. Subtract (of S. from Uns.) (doubleword) |
 * | SUBSUU_S.B    | Vector Signed Saturated Subtract (of Uns.) (byte)        |
 * | SUBSUU_S.H    | Vector Signed Saturated Subtract (of Uns.) (halfword)    |
 * | SUBSUU_S.W    | Vector Signed Saturated Subtract (of Uns.) (word)        |
 * | SUBSUU_S.D    | Vector Signed Saturated Subtract (of Uns.) (doubleword)  |
 * | SUBV.B        | Vector Subtract (byte)                                   |
 * | SUBV.H        | Vector Subtract (halfword)                               |
 * | SUBV.W        | Vector Subtract (word)                                   |
 * | SUBV.D        | Vector Subtract (doubleword)                             |
 * +---------------+----------------------------------------------------------+
 */


static inline int64_t msa_asub_s_df(uint32_t df, int64_t arg1, int64_t arg2)
{
    /* signed compare */
    return (arg1 < arg2) ?
        (uint64_t)(arg2 - arg1) : (uint64_t)(arg1 - arg2);
}

void helper_msa_asub_s_b(CPUMIPSState *env,
                         uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->b[0]  = msa_asub_s_df(DF_BYTE, pws->b[0],  pwt->b[0]);
    pwd->b[1]  = msa_asub_s_df(DF_BYTE, pws->b[1],  pwt->b[1]);
    pwd->b[2]  = msa_asub_s_df(DF_BYTE, pws->b[2],  pwt->b[2]);
    pwd->b[3]  = msa_asub_s_df(DF_BYTE, pws->b[3],  pwt->b[3]);
    pwd->b[4]  = msa_asub_s_df(DF_BYTE, pws->b[4],  pwt->b[4]);
    pwd->b[5]  = msa_asub_s_df(DF_BYTE, pws->b[5],  pwt->b[5]);
    pwd->b[6]  = msa_asub_s_df(DF_BYTE, pws->b[6],  pwt->b[6]);
    pwd->b[7]  = msa_asub_s_df(DF_BYTE, pws->b[7],  pwt->b[7]);
    pwd->b[8]  = msa_asub_s_df(DF_BYTE, pws->b[8],  pwt->b[8]);
    pwd->b[9]  = msa_asub_s_df(DF_BYTE, pws->b[9],  pwt->b[9]);
    pwd->b[10] = msa_asub_s_df(DF_BYTE, pws->b[10], pwt->b[10]);
    pwd->b[11] = msa_asub_s_df(DF_BYTE, pws->b[11], pwt->b[11]);
    pwd->b[12] = msa_asub_s_df(DF_BYTE, pws->b[12], pwt->b[12]);
    pwd->b[13] = msa_asub_s_df(DF_BYTE, pws->b[13], pwt->b[13]);
    pwd->b[14] = msa_asub_s_df(DF_BYTE, pws->b[14], pwt->b[14]);
    pwd->b[15] = msa_asub_s_df(DF_BYTE, pws->b[15], pwt->b[15]);
}

void helper_msa_asub_s_h(CPUMIPSState *env,
                         uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->h[0]  = msa_asub_s_df(DF_HALF, pws->h[0],  pwt->h[0]);
    pwd->h[1]  = msa_asub_s_df(DF_HALF, pws->h[1],  pwt->h[1]);
    pwd->h[2]  = msa_asub_s_df(DF_HALF, pws->h[2],  pwt->h[2]);
    pwd->h[3]  = msa_asub_s_df(DF_HALF, pws->h[3],  pwt->h[3]);
    pwd->h[4]  = msa_asub_s_df(DF_HALF, pws->h[4],  pwt->h[4]);
    pwd->h[5]  = msa_asub_s_df(DF_HALF, pws->h[5],  pwt->h[5]);
    pwd->h[6]  = msa_asub_s_df(DF_HALF, pws->h[6],  pwt->h[6]);
    pwd->h[7]  = msa_asub_s_df(DF_HALF, pws->h[7],  pwt->h[7]);
}

void helper_msa_asub_s_w(CPUMIPSState *env,
                         uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->w[0]  = msa_asub_s_df(DF_WORD, pws->w[0],  pwt->w[0]);
    pwd->w[1]  = msa_asub_s_df(DF_WORD, pws->w[1],  pwt->w[1]);
    pwd->w[2]  = msa_asub_s_df(DF_WORD, pws->w[2],  pwt->w[2]);
    pwd->w[3]  = msa_asub_s_df(DF_WORD, pws->w[3],  pwt->w[3]);
}

void helper_msa_asub_s_d(CPUMIPSState *env,
                         uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->d[0]  = msa_asub_s_df(DF_DOUBLE, pws->d[0],  pwt->d[0]);
    pwd->d[1]  = msa_asub_s_df(DF_DOUBLE, pws->d[1],  pwt->d[1]);
}


static inline uint64_t msa_asub_u_df(uint32_t df, uint64_t arg1, uint64_t arg2)
{
    uint64_t u_arg1 = UNSIGNED(arg1, df);
    uint64_t u_arg2 = UNSIGNED(arg2, df);
    /* unsigned compare */
    return (u_arg1 < u_arg2) ?
        (uint64_t)(u_arg2 - u_arg1) : (uint64_t)(u_arg1 - u_arg2);
}

void helper_msa_asub_u_b(CPUMIPSState *env,
                         uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->b[0]  = msa_asub_u_df(DF_BYTE, pws->b[0],  pwt->b[0]);
    pwd->b[1]  = msa_asub_u_df(DF_BYTE, pws->b[1],  pwt->b[1]);
    pwd->b[2]  = msa_asub_u_df(DF_BYTE, pws->b[2],  pwt->b[2]);
    pwd->b[3]  = msa_asub_u_df(DF_BYTE, pws->b[3],  pwt->b[3]);
    pwd->b[4]  = msa_asub_u_df(DF_BYTE, pws->b[4],  pwt->b[4]);
    pwd->b[5]  = msa_asub_u_df(DF_BYTE, pws->b[5],  pwt->b[5]);
    pwd->b[6]  = msa_asub_u_df(DF_BYTE, pws->b[6],  pwt->b[6]);
    pwd->b[7]  = msa_asub_u_df(DF_BYTE, pws->b[7],  pwt->b[7]);
    pwd->b[8]  = msa_asub_u_df(DF_BYTE, pws->b[8],  pwt->b[8]);
    pwd->b[9]  = msa_asub_u_df(DF_BYTE, pws->b[9],  pwt->b[9]);
    pwd->b[10] = msa_asub_u_df(DF_BYTE, pws->b[10], pwt->b[10]);
    pwd->b[11] = msa_asub_u_df(DF_BYTE, pws->b[11], pwt->b[11]);
    pwd->b[12] = msa_asub_u_df(DF_BYTE, pws->b[12], pwt->b[12]);
    pwd->b[13] = msa_asub_u_df(DF_BYTE, pws->b[13], pwt->b[13]);
    pwd->b[14] = msa_asub_u_df(DF_BYTE, pws->b[14], pwt->b[14]);
    pwd->b[15] = msa_asub_u_df(DF_BYTE, pws->b[15], pwt->b[15]);
}

void helper_msa_asub_u_h(CPUMIPSState *env,
                         uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->h[0]  = msa_asub_u_df(DF_HALF, pws->h[0],  pwt->h[0]);
    pwd->h[1]  = msa_asub_u_df(DF_HALF, pws->h[1],  pwt->h[1]);
    pwd->h[2]  = msa_asub_u_df(DF_HALF, pws->h[2],  pwt->h[2]);
    pwd->h[3]  = msa_asub_u_df(DF_HALF, pws->h[3],  pwt->h[3]);
    pwd->h[4]  = msa_asub_u_df(DF_HALF, pws->h[4],  pwt->h[4]);
    pwd->h[5]  = msa_asub_u_df(DF_HALF, pws->h[5],  pwt->h[5]);
    pwd->h[6]  = msa_asub_u_df(DF_HALF, pws->h[6],  pwt->h[6]);
    pwd->h[7]  = msa_asub_u_df(DF_HALF, pws->h[7],  pwt->h[7]);
}

void helper_msa_asub_u_w(CPUMIPSState *env,
                         uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->w[0]  = msa_asub_u_df(DF_WORD, pws->w[0],  pwt->w[0]);
    pwd->w[1]  = msa_asub_u_df(DF_WORD, pws->w[1],  pwt->w[1]);
    pwd->w[2]  = msa_asub_u_df(DF_WORD, pws->w[2],  pwt->w[2]);
    pwd->w[3]  = msa_asub_u_df(DF_WORD, pws->w[3],  pwt->w[3]);
}

void helper_msa_asub_u_d(CPUMIPSState *env,
                         uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->d[0]  = msa_asub_u_df(DF_DOUBLE, pws->d[0],  pwt->d[0]);
    pwd->d[1]  = msa_asub_u_df(DF_DOUBLE, pws->d[1],  pwt->d[1]);
}


static inline int64_t msa_hsub_s_df(uint32_t df, int64_t arg1, int64_t arg2)
{
    return SIGNED_ODD(arg1, df) - SIGNED_EVEN(arg2, df);
}

void helper_msa_hsub_s_h(CPUMIPSState *env,
                         uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->h[0]  = msa_hsub_s_df(DF_HALF, pws->h[0],  pwt->h[0]);
    pwd->h[1]  = msa_hsub_s_df(DF_HALF, pws->h[1],  pwt->h[1]);
    pwd->h[2]  = msa_hsub_s_df(DF_HALF, pws->h[2],  pwt->h[2]);
    pwd->h[3]  = msa_hsub_s_df(DF_HALF, pws->h[3],  pwt->h[3]);
    pwd->h[4]  = msa_hsub_s_df(DF_HALF, pws->h[4],  pwt->h[4]);
    pwd->h[5]  = msa_hsub_s_df(DF_HALF, pws->h[5],  pwt->h[5]);
    pwd->h[6]  = msa_hsub_s_df(DF_HALF, pws->h[6],  pwt->h[6]);
    pwd->h[7]  = msa_hsub_s_df(DF_HALF, pws->h[7],  pwt->h[7]);
}

void helper_msa_hsub_s_w(CPUMIPSState *env,
                         uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->w[0]  = msa_hsub_s_df(DF_WORD, pws->w[0],  pwt->w[0]);
    pwd->w[1]  = msa_hsub_s_df(DF_WORD, pws->w[1],  pwt->w[1]);
    pwd->w[2]  = msa_hsub_s_df(DF_WORD, pws->w[2],  pwt->w[2]);
    pwd->w[3]  = msa_hsub_s_df(DF_WORD, pws->w[3],  pwt->w[3]);
}

void helper_msa_hsub_s_d(CPUMIPSState *env,
                         uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->d[0]  = msa_hsub_s_df(DF_DOUBLE, pws->d[0],  pwt->d[0]);
    pwd->d[1]  = msa_hsub_s_df(DF_DOUBLE, pws->d[1],  pwt->d[1]);
}


static inline int64_t msa_hsub_u_df(uint32_t df, int64_t arg1, int64_t arg2)
{
    return UNSIGNED_ODD(arg1, df) - UNSIGNED_EVEN(arg2, df);
}

void helper_msa_hsub_u_h(CPUMIPSState *env,
                         uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->h[0]  = msa_hsub_u_df(DF_HALF, pws->h[0],  pwt->h[0]);
    pwd->h[1]  = msa_hsub_u_df(DF_HALF, pws->h[1],  pwt->h[1]);
    pwd->h[2]  = msa_hsub_u_df(DF_HALF, pws->h[2],  pwt->h[2]);
    pwd->h[3]  = msa_hsub_u_df(DF_HALF, pws->h[3],  pwt->h[3]);
    pwd->h[4]  = msa_hsub_u_df(DF_HALF, pws->h[4],  pwt->h[4]);
    pwd->h[5]  = msa_hsub_u_df(DF_HALF, pws->h[5],  pwt->h[5]);
    pwd->h[6]  = msa_hsub_u_df(DF_HALF, pws->h[6],  pwt->h[6]);
    pwd->h[7]  = msa_hsub_u_df(DF_HALF, pws->h[7],  pwt->h[7]);
}

void helper_msa_hsub_u_w(CPUMIPSState *env,
                         uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->w[0]  = msa_hsub_u_df(DF_WORD, pws->w[0],  pwt->w[0]);
    pwd->w[1]  = msa_hsub_u_df(DF_WORD, pws->w[1],  pwt->w[1]);
    pwd->w[2]  = msa_hsub_u_df(DF_WORD, pws->w[2],  pwt->w[2]);
    pwd->w[3]  = msa_hsub_u_df(DF_WORD, pws->w[3],  pwt->w[3]);
}

void helper_msa_hsub_u_d(CPUMIPSState *env,
                         uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->d[0]  = msa_hsub_u_df(DF_DOUBLE, pws->d[0],  pwt->d[0]);
    pwd->d[1]  = msa_hsub_u_df(DF_DOUBLE, pws->d[1],  pwt->d[1]);
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

void helper_msa_subs_s_b(CPUMIPSState *env,
                         uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->b[0]  = msa_subs_s_df(DF_BYTE, pws->b[0],  pwt->b[0]);
    pwd->b[1]  = msa_subs_s_df(DF_BYTE, pws->b[1],  pwt->b[1]);
    pwd->b[2]  = msa_subs_s_df(DF_BYTE, pws->b[2],  pwt->b[2]);
    pwd->b[3]  = msa_subs_s_df(DF_BYTE, pws->b[3],  pwt->b[3]);
    pwd->b[4]  = msa_subs_s_df(DF_BYTE, pws->b[4],  pwt->b[4]);
    pwd->b[5]  = msa_subs_s_df(DF_BYTE, pws->b[5],  pwt->b[5]);
    pwd->b[6]  = msa_subs_s_df(DF_BYTE, pws->b[6],  pwt->b[6]);
    pwd->b[7]  = msa_subs_s_df(DF_BYTE, pws->b[7],  pwt->b[7]);
    pwd->b[8]  = msa_subs_s_df(DF_BYTE, pws->b[8],  pwt->b[8]);
    pwd->b[9]  = msa_subs_s_df(DF_BYTE, pws->b[9],  pwt->b[9]);
    pwd->b[10] = msa_subs_s_df(DF_BYTE, pws->b[10], pwt->b[10]);
    pwd->b[11] = msa_subs_s_df(DF_BYTE, pws->b[11], pwt->b[11]);
    pwd->b[12] = msa_subs_s_df(DF_BYTE, pws->b[12], pwt->b[12]);
    pwd->b[13] = msa_subs_s_df(DF_BYTE, pws->b[13], pwt->b[13]);
    pwd->b[14] = msa_subs_s_df(DF_BYTE, pws->b[14], pwt->b[14]);
    pwd->b[15] = msa_subs_s_df(DF_BYTE, pws->b[15], pwt->b[15]);
}

void helper_msa_subs_s_h(CPUMIPSState *env,
                         uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->h[0]  = msa_subs_s_df(DF_HALF, pws->h[0],  pwt->h[0]);
    pwd->h[1]  = msa_subs_s_df(DF_HALF, pws->h[1],  pwt->h[1]);
    pwd->h[2]  = msa_subs_s_df(DF_HALF, pws->h[2],  pwt->h[2]);
    pwd->h[3]  = msa_subs_s_df(DF_HALF, pws->h[3],  pwt->h[3]);
    pwd->h[4]  = msa_subs_s_df(DF_HALF, pws->h[4],  pwt->h[4]);
    pwd->h[5]  = msa_subs_s_df(DF_HALF, pws->h[5],  pwt->h[5]);
    pwd->h[6]  = msa_subs_s_df(DF_HALF, pws->h[6],  pwt->h[6]);
    pwd->h[7]  = msa_subs_s_df(DF_HALF, pws->h[7],  pwt->h[7]);
}

void helper_msa_subs_s_w(CPUMIPSState *env,
                         uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->w[0]  = msa_subs_s_df(DF_WORD, pws->w[0],  pwt->w[0]);
    pwd->w[1]  = msa_subs_s_df(DF_WORD, pws->w[1],  pwt->w[1]);
    pwd->w[2]  = msa_subs_s_df(DF_WORD, pws->w[2],  pwt->w[2]);
    pwd->w[3]  = msa_subs_s_df(DF_WORD, pws->w[3],  pwt->w[3]);
}

void helper_msa_subs_s_d(CPUMIPSState *env,
                         uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->d[0]  = msa_subs_s_df(DF_DOUBLE, pws->d[0],  pwt->d[0]);
    pwd->d[1]  = msa_subs_s_df(DF_DOUBLE, pws->d[1],  pwt->d[1]);
}


static inline int64_t msa_subs_u_df(uint32_t df, int64_t arg1, int64_t arg2)
{
    uint64_t u_arg1 = UNSIGNED(arg1, df);
    uint64_t u_arg2 = UNSIGNED(arg2, df);
    return (u_arg1 > u_arg2) ? u_arg1 - u_arg2 : 0;
}

void helper_msa_subs_u_b(CPUMIPSState *env,
                         uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->b[0]  = msa_subs_u_df(DF_BYTE, pws->b[0],  pwt->b[0]);
    pwd->b[1]  = msa_subs_u_df(DF_BYTE, pws->b[1],  pwt->b[1]);
    pwd->b[2]  = msa_subs_u_df(DF_BYTE, pws->b[2],  pwt->b[2]);
    pwd->b[3]  = msa_subs_u_df(DF_BYTE, pws->b[3],  pwt->b[3]);
    pwd->b[4]  = msa_subs_u_df(DF_BYTE, pws->b[4],  pwt->b[4]);
    pwd->b[5]  = msa_subs_u_df(DF_BYTE, pws->b[5],  pwt->b[5]);
    pwd->b[6]  = msa_subs_u_df(DF_BYTE, pws->b[6],  pwt->b[6]);
    pwd->b[7]  = msa_subs_u_df(DF_BYTE, pws->b[7],  pwt->b[7]);
    pwd->b[8]  = msa_subs_u_df(DF_BYTE, pws->b[8],  pwt->b[8]);
    pwd->b[9]  = msa_subs_u_df(DF_BYTE, pws->b[9],  pwt->b[9]);
    pwd->b[10] = msa_subs_u_df(DF_BYTE, pws->b[10], pwt->b[10]);
    pwd->b[11] = msa_subs_u_df(DF_BYTE, pws->b[11], pwt->b[11]);
    pwd->b[12] = msa_subs_u_df(DF_BYTE, pws->b[12], pwt->b[12]);
    pwd->b[13] = msa_subs_u_df(DF_BYTE, pws->b[13], pwt->b[13]);
    pwd->b[14] = msa_subs_u_df(DF_BYTE, pws->b[14], pwt->b[14]);
    pwd->b[15] = msa_subs_u_df(DF_BYTE, pws->b[15], pwt->b[15]);
}

void helper_msa_subs_u_h(CPUMIPSState *env,
                         uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->h[0]  = msa_subs_u_df(DF_HALF, pws->h[0],  pwt->h[0]);
    pwd->h[1]  = msa_subs_u_df(DF_HALF, pws->h[1],  pwt->h[1]);
    pwd->h[2]  = msa_subs_u_df(DF_HALF, pws->h[2],  pwt->h[2]);
    pwd->h[3]  = msa_subs_u_df(DF_HALF, pws->h[3],  pwt->h[3]);
    pwd->h[4]  = msa_subs_u_df(DF_HALF, pws->h[4],  pwt->h[4]);
    pwd->h[5]  = msa_subs_u_df(DF_HALF, pws->h[5],  pwt->h[5]);
    pwd->h[6]  = msa_subs_u_df(DF_HALF, pws->h[6],  pwt->h[6]);
    pwd->h[7]  = msa_subs_u_df(DF_HALF, pws->h[7],  pwt->h[7]);
}

void helper_msa_subs_u_w(CPUMIPSState *env,
                         uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->w[0]  = msa_subs_u_df(DF_WORD, pws->w[0],  pwt->w[0]);
    pwd->w[1]  = msa_subs_u_df(DF_WORD, pws->w[1],  pwt->w[1]);
    pwd->w[2]  = msa_subs_u_df(DF_WORD, pws->w[2],  pwt->w[2]);
    pwd->w[3]  = msa_subs_u_df(DF_WORD, pws->w[3],  pwt->w[3]);
}

void helper_msa_subs_u_d(CPUMIPSState *env,
                         uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->d[0]  = msa_subs_u_df(DF_DOUBLE, pws->d[0],  pwt->d[0]);
    pwd->d[1]  = msa_subs_u_df(DF_DOUBLE, pws->d[1],  pwt->d[1]);
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

void helper_msa_subsus_u_b(CPUMIPSState *env,
                           uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->b[0]  = msa_subsus_u_df(DF_BYTE, pws->b[0],  pwt->b[0]);
    pwd->b[1]  = msa_subsus_u_df(DF_BYTE, pws->b[1],  pwt->b[1]);
    pwd->b[2]  = msa_subsus_u_df(DF_BYTE, pws->b[2],  pwt->b[2]);
    pwd->b[3]  = msa_subsus_u_df(DF_BYTE, pws->b[3],  pwt->b[3]);
    pwd->b[4]  = msa_subsus_u_df(DF_BYTE, pws->b[4],  pwt->b[4]);
    pwd->b[5]  = msa_subsus_u_df(DF_BYTE, pws->b[5],  pwt->b[5]);
    pwd->b[6]  = msa_subsus_u_df(DF_BYTE, pws->b[6],  pwt->b[6]);
    pwd->b[7]  = msa_subsus_u_df(DF_BYTE, pws->b[7],  pwt->b[7]);
    pwd->b[8]  = msa_subsus_u_df(DF_BYTE, pws->b[8],  pwt->b[8]);
    pwd->b[9]  = msa_subsus_u_df(DF_BYTE, pws->b[9],  pwt->b[9]);
    pwd->b[10] = msa_subsus_u_df(DF_BYTE, pws->b[10], pwt->b[10]);
    pwd->b[11] = msa_subsus_u_df(DF_BYTE, pws->b[11], pwt->b[11]);
    pwd->b[12] = msa_subsus_u_df(DF_BYTE, pws->b[12], pwt->b[12]);
    pwd->b[13] = msa_subsus_u_df(DF_BYTE, pws->b[13], pwt->b[13]);
    pwd->b[14] = msa_subsus_u_df(DF_BYTE, pws->b[14], pwt->b[14]);
    pwd->b[15] = msa_subsus_u_df(DF_BYTE, pws->b[15], pwt->b[15]);
}

void helper_msa_subsus_u_h(CPUMIPSState *env,
                           uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->h[0]  = msa_subsus_u_df(DF_HALF, pws->h[0],  pwt->h[0]);
    pwd->h[1]  = msa_subsus_u_df(DF_HALF, pws->h[1],  pwt->h[1]);
    pwd->h[2]  = msa_subsus_u_df(DF_HALF, pws->h[2],  pwt->h[2]);
    pwd->h[3]  = msa_subsus_u_df(DF_HALF, pws->h[3],  pwt->h[3]);
    pwd->h[4]  = msa_subsus_u_df(DF_HALF, pws->h[4],  pwt->h[4]);
    pwd->h[5]  = msa_subsus_u_df(DF_HALF, pws->h[5],  pwt->h[5]);
    pwd->h[6]  = msa_subsus_u_df(DF_HALF, pws->h[6],  pwt->h[6]);
    pwd->h[7]  = msa_subsus_u_df(DF_HALF, pws->h[7],  pwt->h[7]);
}

void helper_msa_subsus_u_w(CPUMIPSState *env,
                           uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->w[0]  = msa_subsus_u_df(DF_WORD, pws->w[0],  pwt->w[0]);
    pwd->w[1]  = msa_subsus_u_df(DF_WORD, pws->w[1],  pwt->w[1]);
    pwd->w[2]  = msa_subsus_u_df(DF_WORD, pws->w[2],  pwt->w[2]);
    pwd->w[3]  = msa_subsus_u_df(DF_WORD, pws->w[3],  pwt->w[3]);
}

void helper_msa_subsus_u_d(CPUMIPSState *env,
                           uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->d[0]  = msa_subsus_u_df(DF_DOUBLE, pws->d[0],  pwt->d[0]);
    pwd->d[1]  = msa_subsus_u_df(DF_DOUBLE, pws->d[1],  pwt->d[1]);
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

void helper_msa_subsuu_s_b(CPUMIPSState *env,
                           uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->b[0]  = msa_subsuu_s_df(DF_BYTE, pws->b[0],  pwt->b[0]);
    pwd->b[1]  = msa_subsuu_s_df(DF_BYTE, pws->b[1],  pwt->b[1]);
    pwd->b[2]  = msa_subsuu_s_df(DF_BYTE, pws->b[2],  pwt->b[2]);
    pwd->b[3]  = msa_subsuu_s_df(DF_BYTE, pws->b[3],  pwt->b[3]);
    pwd->b[4]  = msa_subsuu_s_df(DF_BYTE, pws->b[4],  pwt->b[4]);
    pwd->b[5]  = msa_subsuu_s_df(DF_BYTE, pws->b[5],  pwt->b[5]);
    pwd->b[6]  = msa_subsuu_s_df(DF_BYTE, pws->b[6],  pwt->b[6]);
    pwd->b[7]  = msa_subsuu_s_df(DF_BYTE, pws->b[7],  pwt->b[7]);
    pwd->b[8]  = msa_subsuu_s_df(DF_BYTE, pws->b[8],  pwt->b[8]);
    pwd->b[9]  = msa_subsuu_s_df(DF_BYTE, pws->b[9],  pwt->b[9]);
    pwd->b[10] = msa_subsuu_s_df(DF_BYTE, pws->b[10], pwt->b[10]);
    pwd->b[11] = msa_subsuu_s_df(DF_BYTE, pws->b[11], pwt->b[11]);
    pwd->b[12] = msa_subsuu_s_df(DF_BYTE, pws->b[12], pwt->b[12]);
    pwd->b[13] = msa_subsuu_s_df(DF_BYTE, pws->b[13], pwt->b[13]);
    pwd->b[14] = msa_subsuu_s_df(DF_BYTE, pws->b[14], pwt->b[14]);
    pwd->b[15] = msa_subsuu_s_df(DF_BYTE, pws->b[15], pwt->b[15]);
}

void helper_msa_subsuu_s_h(CPUMIPSState *env,
                           uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->h[0]  = msa_subsuu_s_df(DF_HALF, pws->h[0],  pwt->h[0]);
    pwd->h[1]  = msa_subsuu_s_df(DF_HALF, pws->h[1],  pwt->h[1]);
    pwd->h[2]  = msa_subsuu_s_df(DF_HALF, pws->h[2],  pwt->h[2]);
    pwd->h[3]  = msa_subsuu_s_df(DF_HALF, pws->h[3],  pwt->h[3]);
    pwd->h[4]  = msa_subsuu_s_df(DF_HALF, pws->h[4],  pwt->h[4]);
    pwd->h[5]  = msa_subsuu_s_df(DF_HALF, pws->h[5],  pwt->h[5]);
    pwd->h[6]  = msa_subsuu_s_df(DF_HALF, pws->h[6],  pwt->h[6]);
    pwd->h[7]  = msa_subsuu_s_df(DF_HALF, pws->h[7],  pwt->h[7]);
}

void helper_msa_subsuu_s_w(CPUMIPSState *env,
                           uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->w[0]  = msa_subsuu_s_df(DF_WORD, pws->w[0],  pwt->w[0]);
    pwd->w[1]  = msa_subsuu_s_df(DF_WORD, pws->w[1],  pwt->w[1]);
    pwd->w[2]  = msa_subsuu_s_df(DF_WORD, pws->w[2],  pwt->w[2]);
    pwd->w[3]  = msa_subsuu_s_df(DF_WORD, pws->w[3],  pwt->w[3]);
}

void helper_msa_subsuu_s_d(CPUMIPSState *env,
                           uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->d[0]  = msa_subsuu_s_df(DF_DOUBLE, pws->d[0],  pwt->d[0]);
    pwd->d[1]  = msa_subsuu_s_df(DF_DOUBLE, pws->d[1],  pwt->d[1]);
}


static inline int64_t msa_subv_df(uint32_t df, int64_t arg1, int64_t arg2)
{
    return arg1 - arg2;
}

void helper_msa_subv_b(CPUMIPSState *env,
                       uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->b[0]  = msa_subv_df(DF_BYTE, pws->b[0],  pwt->b[0]);
    pwd->b[1]  = msa_subv_df(DF_BYTE, pws->b[1],  pwt->b[1]);
    pwd->b[2]  = msa_subv_df(DF_BYTE, pws->b[2],  pwt->b[2]);
    pwd->b[3]  = msa_subv_df(DF_BYTE, pws->b[3],  pwt->b[3]);
    pwd->b[4]  = msa_subv_df(DF_BYTE, pws->b[4],  pwt->b[4]);
    pwd->b[5]  = msa_subv_df(DF_BYTE, pws->b[5],  pwt->b[5]);
    pwd->b[6]  = msa_subv_df(DF_BYTE, pws->b[6],  pwt->b[6]);
    pwd->b[7]  = msa_subv_df(DF_BYTE, pws->b[7],  pwt->b[7]);
    pwd->b[8]  = msa_subv_df(DF_BYTE, pws->b[8],  pwt->b[8]);
    pwd->b[9]  = msa_subv_df(DF_BYTE, pws->b[9],  pwt->b[9]);
    pwd->b[10] = msa_subv_df(DF_BYTE, pws->b[10], pwt->b[10]);
    pwd->b[11] = msa_subv_df(DF_BYTE, pws->b[11], pwt->b[11]);
    pwd->b[12] = msa_subv_df(DF_BYTE, pws->b[12], pwt->b[12]);
    pwd->b[13] = msa_subv_df(DF_BYTE, pws->b[13], pwt->b[13]);
    pwd->b[14] = msa_subv_df(DF_BYTE, pws->b[14], pwt->b[14]);
    pwd->b[15] = msa_subv_df(DF_BYTE, pws->b[15], pwt->b[15]);
}

void helper_msa_subv_h(CPUMIPSState *env,
                       uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->h[0]  = msa_subv_df(DF_HALF, pws->h[0],  pwt->h[0]);
    pwd->h[1]  = msa_subv_df(DF_HALF, pws->h[1],  pwt->h[1]);
    pwd->h[2]  = msa_subv_df(DF_HALF, pws->h[2],  pwt->h[2]);
    pwd->h[3]  = msa_subv_df(DF_HALF, pws->h[3],  pwt->h[3]);
    pwd->h[4]  = msa_subv_df(DF_HALF, pws->h[4],  pwt->h[4]);
    pwd->h[5]  = msa_subv_df(DF_HALF, pws->h[5],  pwt->h[5]);
    pwd->h[6]  = msa_subv_df(DF_HALF, pws->h[6],  pwt->h[6]);
    pwd->h[7]  = msa_subv_df(DF_HALF, pws->h[7],  pwt->h[7]);
}

void helper_msa_subv_w(CPUMIPSState *env,
                       uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->w[0]  = msa_subv_df(DF_WORD, pws->w[0],  pwt->w[0]);
    pwd->w[1]  = msa_subv_df(DF_WORD, pws->w[1],  pwt->w[1]);
    pwd->w[2]  = msa_subv_df(DF_WORD, pws->w[2],  pwt->w[2]);
    pwd->w[3]  = msa_subv_df(DF_WORD, pws->w[3],  pwt->w[3]);
}

void helper_msa_subv_d(CPUMIPSState *env,
                       uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->d[0]  = msa_subv_df(DF_DOUBLE, pws->d[0],  pwt->d[0]);
    pwd->d[1]  = msa_subv_df(DF_DOUBLE, pws->d[1],  pwt->d[1]);
}


/*
 * Interleave
 * ----------
 *
 * +---------------+----------------------------------------------------------+
 * | ILVEV.B       | Vector Interleave Even (byte)                            |
 * | ILVEV.H       | Vector Interleave Even (halfword)                        |
 * | ILVEV.W       | Vector Interleave Even (word)                            |
 * | ILVEV.D       | Vector Interleave Even (doubleword)                      |
 * | ILVOD.B       | Vector Interleave Odd (byte)                             |
 * | ILVOD.H       | Vector Interleave Odd (halfword)                         |
 * | ILVOD.W       | Vector Interleave Odd (word)                             |
 * | ILVOD.D       | Vector Interleave Odd (doubleword)                       |
 * | ILVL.B        | Vector Interleave Left (byte)                            |
 * | ILVL.H        | Vector Interleave Left (halfword)                        |
 * | ILVL.W        | Vector Interleave Left (word)                            |
 * | ILVL.D        | Vector Interleave Left (doubleword)                      |
 * | ILVR.B        | Vector Interleave Right (byte)                           |
 * | ILVR.H        | Vector Interleave Right (halfword)                       |
 * | ILVR.W        | Vector Interleave Right (word)                           |
 * | ILVR.D        | Vector Interleave Right (doubleword)                     |
 * +---------------+----------------------------------------------------------+
 */


void helper_msa_ilvev_b(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

#if HOST_BIG_ENDIAN
    pwd->b[8]  = pws->b[9];
    pwd->b[9]  = pwt->b[9];
    pwd->b[10] = pws->b[11];
    pwd->b[11] = pwt->b[11];
    pwd->b[12] = pws->b[13];
    pwd->b[13] = pwt->b[13];
    pwd->b[14] = pws->b[15];
    pwd->b[15] = pwt->b[15];
    pwd->b[0]  = pws->b[1];
    pwd->b[1]  = pwt->b[1];
    pwd->b[2]  = pws->b[3];
    pwd->b[3]  = pwt->b[3];
    pwd->b[4]  = pws->b[5];
    pwd->b[5]  = pwt->b[5];
    pwd->b[6]  = pws->b[7];
    pwd->b[7]  = pwt->b[7];
#else
    pwd->b[15] = pws->b[14];
    pwd->b[14] = pwt->b[14];
    pwd->b[13] = pws->b[12];
    pwd->b[12] = pwt->b[12];
    pwd->b[11] = pws->b[10];
    pwd->b[10] = pwt->b[10];
    pwd->b[9]  = pws->b[8];
    pwd->b[8]  = pwt->b[8];
    pwd->b[7]  = pws->b[6];
    pwd->b[6]  = pwt->b[6];
    pwd->b[5]  = pws->b[4];
    pwd->b[4]  = pwt->b[4];
    pwd->b[3]  = pws->b[2];
    pwd->b[2]  = pwt->b[2];
    pwd->b[1]  = pws->b[0];
    pwd->b[0]  = pwt->b[0];
#endif
}

void helper_msa_ilvev_h(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

#if HOST_BIG_ENDIAN
    pwd->h[4] = pws->h[5];
    pwd->h[5] = pwt->h[5];
    pwd->h[6] = pws->h[7];
    pwd->h[7] = pwt->h[7];
    pwd->h[0] = pws->h[1];
    pwd->h[1] = pwt->h[1];
    pwd->h[2] = pws->h[3];
    pwd->h[3] = pwt->h[3];
#else
    pwd->h[7] = pws->h[6];
    pwd->h[6] = pwt->h[6];
    pwd->h[5] = pws->h[4];
    pwd->h[4] = pwt->h[4];
    pwd->h[3] = pws->h[2];
    pwd->h[2] = pwt->h[2];
    pwd->h[1] = pws->h[0];
    pwd->h[0] = pwt->h[0];
#endif
}

void helper_msa_ilvev_w(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

#if HOST_BIG_ENDIAN
    pwd->w[2] = pws->w[3];
    pwd->w[3] = pwt->w[3];
    pwd->w[0] = pws->w[1];
    pwd->w[1] = pwt->w[1];
#else
    pwd->w[3] = pws->w[2];
    pwd->w[2] = pwt->w[2];
    pwd->w[1] = pws->w[0];
    pwd->w[0] = pwt->w[0];
#endif
}

void helper_msa_ilvev_d(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->d[1] = pws->d[0];
    pwd->d[0] = pwt->d[0];
}


void helper_msa_ilvod_b(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

#if HOST_BIG_ENDIAN
    pwd->b[7]  = pwt->b[6];
    pwd->b[6]  = pws->b[6];
    pwd->b[5]  = pwt->b[4];
    pwd->b[4]  = pws->b[4];
    pwd->b[3]  = pwt->b[2];
    pwd->b[2]  = pws->b[2];
    pwd->b[1]  = pwt->b[0];
    pwd->b[0]  = pws->b[0];
    pwd->b[15] = pwt->b[14];
    pwd->b[14] = pws->b[14];
    pwd->b[13] = pwt->b[12];
    pwd->b[12] = pws->b[12];
    pwd->b[11] = pwt->b[10];
    pwd->b[10] = pws->b[10];
    pwd->b[9]  = pwt->b[8];
    pwd->b[8]  = pws->b[8];
#else
    pwd->b[0]  = pwt->b[1];
    pwd->b[1]  = pws->b[1];
    pwd->b[2]  = pwt->b[3];
    pwd->b[3]  = pws->b[3];
    pwd->b[4]  = pwt->b[5];
    pwd->b[5]  = pws->b[5];
    pwd->b[6]  = pwt->b[7];
    pwd->b[7]  = pws->b[7];
    pwd->b[8]  = pwt->b[9];
    pwd->b[9]  = pws->b[9];
    pwd->b[10] = pwt->b[11];
    pwd->b[11] = pws->b[11];
    pwd->b[12] = pwt->b[13];
    pwd->b[13] = pws->b[13];
    pwd->b[14] = pwt->b[15];
    pwd->b[15] = pws->b[15];
#endif
}

void helper_msa_ilvod_h(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

#if HOST_BIG_ENDIAN
    pwd->h[3] = pwt->h[2];
    pwd->h[2] = pws->h[2];
    pwd->h[1] = pwt->h[0];
    pwd->h[0] = pws->h[0];
    pwd->h[7] = pwt->h[6];
    pwd->h[6] = pws->h[6];
    pwd->h[5] = pwt->h[4];
    pwd->h[4] = pws->h[4];
#else
    pwd->h[0] = pwt->h[1];
    pwd->h[1] = pws->h[1];
    pwd->h[2] = pwt->h[3];
    pwd->h[3] = pws->h[3];
    pwd->h[4] = pwt->h[5];
    pwd->h[5] = pws->h[5];
    pwd->h[6] = pwt->h[7];
    pwd->h[7] = pws->h[7];
#endif
}

void helper_msa_ilvod_w(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

#if HOST_BIG_ENDIAN
    pwd->w[1] = pwt->w[0];
    pwd->w[0] = pws->w[0];
    pwd->w[3] = pwt->w[2];
    pwd->w[2] = pws->w[2];
#else
    pwd->w[0] = pwt->w[1];
    pwd->w[1] = pws->w[1];
    pwd->w[2] = pwt->w[3];
    pwd->w[3] = pws->w[3];
#endif
}

void helper_msa_ilvod_d(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->d[0] = pwt->d[1];
    pwd->d[1] = pws->d[1];
}


void helper_msa_ilvl_b(CPUMIPSState *env,
                       uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

#if HOST_BIG_ENDIAN
    pwd->b[7]  = pwt->b[15];
    pwd->b[6]  = pws->b[15];
    pwd->b[5]  = pwt->b[14];
    pwd->b[4]  = pws->b[14];
    pwd->b[3]  = pwt->b[13];
    pwd->b[2]  = pws->b[13];
    pwd->b[1]  = pwt->b[12];
    pwd->b[0]  = pws->b[12];
    pwd->b[15] = pwt->b[11];
    pwd->b[14] = pws->b[11];
    pwd->b[13] = pwt->b[10];
    pwd->b[12] = pws->b[10];
    pwd->b[11] = pwt->b[9];
    pwd->b[10] = pws->b[9];
    pwd->b[9]  = pwt->b[8];
    pwd->b[8]  = pws->b[8];
#else
    pwd->b[0]  = pwt->b[8];
    pwd->b[1]  = pws->b[8];
    pwd->b[2]  = pwt->b[9];
    pwd->b[3]  = pws->b[9];
    pwd->b[4]  = pwt->b[10];
    pwd->b[5]  = pws->b[10];
    pwd->b[6]  = pwt->b[11];
    pwd->b[7]  = pws->b[11];
    pwd->b[8]  = pwt->b[12];
    pwd->b[9]  = pws->b[12];
    pwd->b[10] = pwt->b[13];
    pwd->b[11] = pws->b[13];
    pwd->b[12] = pwt->b[14];
    pwd->b[13] = pws->b[14];
    pwd->b[14] = pwt->b[15];
    pwd->b[15] = pws->b[15];
#endif
}

void helper_msa_ilvl_h(CPUMIPSState *env,
                       uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

#if HOST_BIG_ENDIAN
    pwd->h[3] = pwt->h[7];
    pwd->h[2] = pws->h[7];
    pwd->h[1] = pwt->h[6];
    pwd->h[0] = pws->h[6];
    pwd->h[7] = pwt->h[5];
    pwd->h[6] = pws->h[5];
    pwd->h[5] = pwt->h[4];
    pwd->h[4] = pws->h[4];
#else
    pwd->h[0] = pwt->h[4];
    pwd->h[1] = pws->h[4];
    pwd->h[2] = pwt->h[5];
    pwd->h[3] = pws->h[5];
    pwd->h[4] = pwt->h[6];
    pwd->h[5] = pws->h[6];
    pwd->h[6] = pwt->h[7];
    pwd->h[7] = pws->h[7];
#endif
}

void helper_msa_ilvl_w(CPUMIPSState *env,
                       uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

#if HOST_BIG_ENDIAN
    pwd->w[1] = pwt->w[3];
    pwd->w[0] = pws->w[3];
    pwd->w[3] = pwt->w[2];
    pwd->w[2] = pws->w[2];
#else
    pwd->w[0] = pwt->w[2];
    pwd->w[1] = pws->w[2];
    pwd->w[2] = pwt->w[3];
    pwd->w[3] = pws->w[3];
#endif
}

void helper_msa_ilvl_d(CPUMIPSState *env,
                       uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->d[0] = pwt->d[1];
    pwd->d[1] = pws->d[1];
}


void helper_msa_ilvr_b(CPUMIPSState *env,
                       uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

#if HOST_BIG_ENDIAN
    pwd->b[8]  = pws->b[0];
    pwd->b[9]  = pwt->b[0];
    pwd->b[10] = pws->b[1];
    pwd->b[11] = pwt->b[1];
    pwd->b[12] = pws->b[2];
    pwd->b[13] = pwt->b[2];
    pwd->b[14] = pws->b[3];
    pwd->b[15] = pwt->b[3];
    pwd->b[0]  = pws->b[4];
    pwd->b[1]  = pwt->b[4];
    pwd->b[2]  = pws->b[5];
    pwd->b[3]  = pwt->b[5];
    pwd->b[4]  = pws->b[6];
    pwd->b[5]  = pwt->b[6];
    pwd->b[6]  = pws->b[7];
    pwd->b[7]  = pwt->b[7];
#else
    pwd->b[15] = pws->b[7];
    pwd->b[14] = pwt->b[7];
    pwd->b[13] = pws->b[6];
    pwd->b[12] = pwt->b[6];
    pwd->b[11] = pws->b[5];
    pwd->b[10] = pwt->b[5];
    pwd->b[9]  = pws->b[4];
    pwd->b[8]  = pwt->b[4];
    pwd->b[7]  = pws->b[3];
    pwd->b[6]  = pwt->b[3];
    pwd->b[5]  = pws->b[2];
    pwd->b[4]  = pwt->b[2];
    pwd->b[3]  = pws->b[1];
    pwd->b[2]  = pwt->b[1];
    pwd->b[1]  = pws->b[0];
    pwd->b[0]  = pwt->b[0];
#endif
}

void helper_msa_ilvr_h(CPUMIPSState *env,
                       uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

#if HOST_BIG_ENDIAN
    pwd->h[4] = pws->h[0];
    pwd->h[5] = pwt->h[0];
    pwd->h[6] = pws->h[1];
    pwd->h[7] = pwt->h[1];
    pwd->h[0] = pws->h[2];
    pwd->h[1] = pwt->h[2];
    pwd->h[2] = pws->h[3];
    pwd->h[3] = pwt->h[3];
#else
    pwd->h[7] = pws->h[3];
    pwd->h[6] = pwt->h[3];
    pwd->h[5] = pws->h[2];
    pwd->h[4] = pwt->h[2];
    pwd->h[3] = pws->h[1];
    pwd->h[2] = pwt->h[1];
    pwd->h[1] = pws->h[0];
    pwd->h[0] = pwt->h[0];
#endif
}

void helper_msa_ilvr_w(CPUMIPSState *env,
                       uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

#if HOST_BIG_ENDIAN
    pwd->w[2] = pws->w[0];
    pwd->w[3] = pwt->w[0];
    pwd->w[0] = pws->w[1];
    pwd->w[1] = pwt->w[1];
#else
    pwd->w[3] = pws->w[1];
    pwd->w[2] = pwt->w[1];
    pwd->w[1] = pws->w[0];
    pwd->w[0] = pwt->w[0];
#endif
}

void helper_msa_ilvr_d(CPUMIPSState *env,
                       uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->d[1] = pws->d[0];
    pwd->d[0] = pwt->d[0];
}


/*
 * Logic
 * -----
 *
 * +---------------+----------------------------------------------------------+
 * | AND.V         | Vector Logical And                                       |
 * | NOR.V         | Vector Logical Negated Or                                |
 * | OR.V          | Vector Logical Or                                        |
 * | XOR.V         | Vector Logical Exclusive Or                              |
 * +---------------+----------------------------------------------------------+
 */


void helper_msa_and_v(CPUMIPSState *env, uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->d[0] = pws->d[0] & pwt->d[0];
    pwd->d[1] = pws->d[1] & pwt->d[1];
}

void helper_msa_nor_v(CPUMIPSState *env, uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->d[0] = ~(pws->d[0] | pwt->d[0]);
    pwd->d[1] = ~(pws->d[1] | pwt->d[1]);
}

void helper_msa_or_v(CPUMIPSState *env, uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->d[0] = pws->d[0] | pwt->d[0];
    pwd->d[1] = pws->d[1] | pwt->d[1];
}

void helper_msa_xor_v(CPUMIPSState *env, uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->d[0] = pws->d[0] ^ pwt->d[0];
    pwd->d[1] = pws->d[1] ^ pwt->d[1];
}


/*
 * Move
 * ----
 *
 * +---------------+----------------------------------------------------------+
 * | MOVE.V        | Vector Move                                              |
 * +---------------+----------------------------------------------------------+
 */

static inline void msa_move_v(wr_t *pwd, wr_t *pws)
{
    pwd->d[0] = pws->d[0];
    pwd->d[1] = pws->d[1];
}

void helper_msa_move_v(CPUMIPSState *env, uint32_t wd, uint32_t ws)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);

    msa_move_v(pwd, pws);
}


/*
 * Pack
 * ----
 *
 * +---------------+----------------------------------------------------------+
 * | PCKEV.B       | Vector Pack Even (byte)                                  |
 * | PCKEV.H       | Vector Pack Even (halfword)                              |
 * | PCKEV.W       | Vector Pack Even (word)                                  |
 * | PCKEV.D       | Vector Pack Even (doubleword)                            |
 * | PCKOD.B       | Vector Pack Odd (byte)                                   |
 * | PCKOD.H       | Vector Pack Odd (halfword)                               |
 * | PCKOD.W       | Vector Pack Odd (word)                                   |
 * | PCKOD.D       | Vector Pack Odd (doubleword)                             |
 * | VSHF.B        | Vector Data Preserving Shuffle (byte)                    |
 * | VSHF.H        | Vector Data Preserving Shuffle (halfword)                |
 * | VSHF.W        | Vector Data Preserving Shuffle (word)                    |
 * | VSHF.D        | Vector Data Preserving Shuffle (doubleword)              |
 * +---------------+----------------------------------------------------------+
 */


void helper_msa_pckev_b(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

#if HOST_BIG_ENDIAN
    pwd->b[8]  = pws->b[9];
    pwd->b[10] = pws->b[13];
    pwd->b[12] = pws->b[1];
    pwd->b[14] = pws->b[5];
    pwd->b[0]  = pwt->b[9];
    pwd->b[2]  = pwt->b[13];
    pwd->b[4]  = pwt->b[1];
    pwd->b[6]  = pwt->b[5];
    pwd->b[9]  = pws->b[11];
    pwd->b[13] = pws->b[3];
    pwd->b[1]  = pwt->b[11];
    pwd->b[5]  = pwt->b[3];
    pwd->b[11] = pws->b[15];
    pwd->b[3]  = pwt->b[15];
    pwd->b[15] = pws->b[7];
    pwd->b[7]  = pwt->b[7];
#else
    pwd->b[15] = pws->b[14];
    pwd->b[13] = pws->b[10];
    pwd->b[11] = pws->b[6];
    pwd->b[9]  = pws->b[2];
    pwd->b[7]  = pwt->b[14];
    pwd->b[5]  = pwt->b[10];
    pwd->b[3]  = pwt->b[6];
    pwd->b[1]  = pwt->b[2];
    pwd->b[14] = pws->b[12];
    pwd->b[10] = pws->b[4];
    pwd->b[6]  = pwt->b[12];
    pwd->b[2]  = pwt->b[4];
    pwd->b[12] = pws->b[8];
    pwd->b[4]  = pwt->b[8];
    pwd->b[8]  = pws->b[0];
    pwd->b[0]  = pwt->b[0];
#endif
}

void helper_msa_pckev_h(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

#if HOST_BIG_ENDIAN
    pwd->h[4] = pws->h[5];
    pwd->h[6] = pws->h[1];
    pwd->h[0] = pwt->h[5];
    pwd->h[2] = pwt->h[1];
    pwd->h[5] = pws->h[7];
    pwd->h[1] = pwt->h[7];
    pwd->h[7] = pws->h[3];
    pwd->h[3] = pwt->h[3];
#else
    pwd->h[7] = pws->h[6];
    pwd->h[5] = pws->h[2];
    pwd->h[3] = pwt->h[6];
    pwd->h[1] = pwt->h[2];
    pwd->h[6] = pws->h[4];
    pwd->h[2] = pwt->h[4];
    pwd->h[4] = pws->h[0];
    pwd->h[0] = pwt->h[0];
#endif
}

void helper_msa_pckev_w(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

#if HOST_BIG_ENDIAN
    pwd->w[2] = pws->w[3];
    pwd->w[0] = pwt->w[3];
    pwd->w[3] = pws->w[1];
    pwd->w[1] = pwt->w[1];
#else
    pwd->w[3] = pws->w[2];
    pwd->w[1] = pwt->w[2];
    pwd->w[2] = pws->w[0];
    pwd->w[0] = pwt->w[0];
#endif
}

void helper_msa_pckev_d(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->d[1] = pws->d[0];
    pwd->d[0] = pwt->d[0];
}


void helper_msa_pckod_b(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

#if HOST_BIG_ENDIAN
    pwd->b[7]  = pwt->b[6];
    pwd->b[5]  = pwt->b[2];
    pwd->b[3]  = pwt->b[14];
    pwd->b[1]  = pwt->b[10];
    pwd->b[15] = pws->b[6];
    pwd->b[13] = pws->b[2];
    pwd->b[11] = pws->b[14];
    pwd->b[9]  = pws->b[10];
    pwd->b[6]  = pwt->b[4];
    pwd->b[2]  = pwt->b[12];
    pwd->b[14] = pws->b[4];
    pwd->b[10] = pws->b[12];
    pwd->b[4]  = pwt->b[0];
    pwd->b[12] = pws->b[0];
    pwd->b[0]  = pwt->b[8];
    pwd->b[8]  = pws->b[8];
#else
    pwd->b[0]  = pwt->b[1];
    pwd->b[2]  = pwt->b[5];
    pwd->b[4]  = pwt->b[9];
    pwd->b[6]  = pwt->b[13];
    pwd->b[8]  = pws->b[1];
    pwd->b[10] = pws->b[5];
    pwd->b[12] = pws->b[9];
    pwd->b[14] = pws->b[13];
    pwd->b[1]  = pwt->b[3];
    pwd->b[5]  = pwt->b[11];
    pwd->b[9]  = pws->b[3];
    pwd->b[13] = pws->b[11];
    pwd->b[3]  = pwt->b[7];
    pwd->b[11] = pws->b[7];
    pwd->b[7]  = pwt->b[15];
    pwd->b[15] = pws->b[15];
#endif

}

void helper_msa_pckod_h(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

#if HOST_BIG_ENDIAN
    pwd->h[3] = pwt->h[2];
    pwd->h[1] = pwt->h[6];
    pwd->h[7] = pws->h[2];
    pwd->h[5] = pws->h[6];
    pwd->h[2] = pwt->h[0];
    pwd->h[6] = pws->h[0];
    pwd->h[0] = pwt->h[4];
    pwd->h[4] = pws->h[4];
#else
    pwd->h[0] = pwt->h[1];
    pwd->h[2] = pwt->h[5];
    pwd->h[4] = pws->h[1];
    pwd->h[6] = pws->h[5];
    pwd->h[1] = pwt->h[3];
    pwd->h[5] = pws->h[3];
    pwd->h[3] = pwt->h[7];
    pwd->h[7] = pws->h[7];
#endif
}

void helper_msa_pckod_w(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

#if HOST_BIG_ENDIAN
    pwd->w[1] = pwt->w[0];
    pwd->w[3] = pws->w[0];
    pwd->w[0] = pwt->w[2];
    pwd->w[2] = pws->w[2];
#else
    pwd->w[0] = pwt->w[1];
    pwd->w[2] = pws->w[1];
    pwd->w[1] = pwt->w[3];
    pwd->w[3] = pws->w[3];
#endif
}

void helper_msa_pckod_d(CPUMIPSState *env,
                        uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->d[0] = pwt->d[1];
    pwd->d[1] = pws->d[1];
}


/*
 * Shift
 * -----
 *
 * +---------------+----------------------------------------------------------+
 * | SLL.B         | Vector Shift Left (byte)                                 |
 * | SLL.H         | Vector Shift Left (halfword)                             |
 * | SLL.W         | Vector Shift Left (word)                                 |
 * | SLL.D         | Vector Shift Left (doubleword)                           |
 * | SRA.B         | Vector Shift Right Arithmetic (byte)                     |
 * | SRA.H         | Vector Shift Right Arithmetic (halfword)                 |
 * | SRA.W         | Vector Shift Right Arithmetic (word)                     |
 * | SRA.D         | Vector Shift Right Arithmetic (doubleword)               |
 * | SRAR.B        | Vector Shift Right Arithmetic Rounded (byte)             |
 * | SRAR.H        | Vector Shift Right Arithmetic Rounded (halfword)         |
 * | SRAR.W        | Vector Shift Right Arithmetic Rounded (word)             |
 * | SRAR.D        | Vector Shift Right Arithmetic Rounded (doubleword)       |
 * | SRL.B         | Vector Shift Right Logical (byte)                        |
 * | SRL.H         | Vector Shift Right Logical (halfword)                    |
 * | SRL.W         | Vector Shift Right Logical (word)                        |
 * | SRL.D         | Vector Shift Right Logical (doubleword)                  |
 * | SRLR.B        | Vector Shift Right Logical Rounded (byte)                |
 * | SRLR.H        | Vector Shift Right Logical Rounded (halfword)            |
 * | SRLR.W        | Vector Shift Right Logical Rounded (word)                |
 * | SRLR.D        | Vector Shift Right Logical Rounded (doubleword)          |
 * +---------------+----------------------------------------------------------+
 */


static inline int64_t msa_sll_df(uint32_t df, int64_t arg1, int64_t arg2)
{
    int32_t b_arg2 = BIT_POSITION(arg2, df);
    return arg1 << b_arg2;
}

void helper_msa_sll_b(CPUMIPSState *env,
                      uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->b[0]  = msa_sll_df(DF_BYTE, pws->b[0],  pwt->b[0]);
    pwd->b[1]  = msa_sll_df(DF_BYTE, pws->b[1],  pwt->b[1]);
    pwd->b[2]  = msa_sll_df(DF_BYTE, pws->b[2],  pwt->b[2]);
    pwd->b[3]  = msa_sll_df(DF_BYTE, pws->b[3],  pwt->b[3]);
    pwd->b[4]  = msa_sll_df(DF_BYTE, pws->b[4],  pwt->b[4]);
    pwd->b[5]  = msa_sll_df(DF_BYTE, pws->b[5],  pwt->b[5]);
    pwd->b[6]  = msa_sll_df(DF_BYTE, pws->b[6],  pwt->b[6]);
    pwd->b[7]  = msa_sll_df(DF_BYTE, pws->b[7],  pwt->b[7]);
    pwd->b[8]  = msa_sll_df(DF_BYTE, pws->b[8],  pwt->b[8]);
    pwd->b[9]  = msa_sll_df(DF_BYTE, pws->b[9],  pwt->b[9]);
    pwd->b[10] = msa_sll_df(DF_BYTE, pws->b[10], pwt->b[10]);
    pwd->b[11] = msa_sll_df(DF_BYTE, pws->b[11], pwt->b[11]);
    pwd->b[12] = msa_sll_df(DF_BYTE, pws->b[12], pwt->b[12]);
    pwd->b[13] = msa_sll_df(DF_BYTE, pws->b[13], pwt->b[13]);
    pwd->b[14] = msa_sll_df(DF_BYTE, pws->b[14], pwt->b[14]);
    pwd->b[15] = msa_sll_df(DF_BYTE, pws->b[15], pwt->b[15]);
}

void helper_msa_sll_h(CPUMIPSState *env,
                      uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->h[0]  = msa_sll_df(DF_HALF, pws->h[0],  pwt->h[0]);
    pwd->h[1]  = msa_sll_df(DF_HALF, pws->h[1],  pwt->h[1]);
    pwd->h[2]  = msa_sll_df(DF_HALF, pws->h[2],  pwt->h[2]);
    pwd->h[3]  = msa_sll_df(DF_HALF, pws->h[3],  pwt->h[3]);
    pwd->h[4]  = msa_sll_df(DF_HALF, pws->h[4],  pwt->h[4]);
    pwd->h[5]  = msa_sll_df(DF_HALF, pws->h[5],  pwt->h[5]);
    pwd->h[6]  = msa_sll_df(DF_HALF, pws->h[6],  pwt->h[6]);
    pwd->h[7]  = msa_sll_df(DF_HALF, pws->h[7],  pwt->h[7]);
}

void helper_msa_sll_w(CPUMIPSState *env,
                      uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->w[0]  = msa_sll_df(DF_WORD, pws->w[0],  pwt->w[0]);
    pwd->w[1]  = msa_sll_df(DF_WORD, pws->w[1],  pwt->w[1]);
    pwd->w[2]  = msa_sll_df(DF_WORD, pws->w[2],  pwt->w[2]);
    pwd->w[3]  = msa_sll_df(DF_WORD, pws->w[3],  pwt->w[3]);
}

void helper_msa_sll_d(CPUMIPSState *env,
                      uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->d[0]  = msa_sll_df(DF_DOUBLE, pws->d[0],  pwt->d[0]);
    pwd->d[1]  = msa_sll_df(DF_DOUBLE, pws->d[1],  pwt->d[1]);
}


static inline int64_t msa_sra_df(uint32_t df, int64_t arg1, int64_t arg2)
{
    int32_t b_arg2 = BIT_POSITION(arg2, df);
    return arg1 >> b_arg2;
}

void helper_msa_sra_b(CPUMIPSState *env,
                      uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->b[0]  = msa_sra_df(DF_BYTE, pws->b[0],  pwt->b[0]);
    pwd->b[1]  = msa_sra_df(DF_BYTE, pws->b[1],  pwt->b[1]);
    pwd->b[2]  = msa_sra_df(DF_BYTE, pws->b[2],  pwt->b[2]);
    pwd->b[3]  = msa_sra_df(DF_BYTE, pws->b[3],  pwt->b[3]);
    pwd->b[4]  = msa_sra_df(DF_BYTE, pws->b[4],  pwt->b[4]);
    pwd->b[5]  = msa_sra_df(DF_BYTE, pws->b[5],  pwt->b[5]);
    pwd->b[6]  = msa_sra_df(DF_BYTE, pws->b[6],  pwt->b[6]);
    pwd->b[7]  = msa_sra_df(DF_BYTE, pws->b[7],  pwt->b[7]);
    pwd->b[8]  = msa_sra_df(DF_BYTE, pws->b[8],  pwt->b[8]);
    pwd->b[9]  = msa_sra_df(DF_BYTE, pws->b[9],  pwt->b[9]);
    pwd->b[10] = msa_sra_df(DF_BYTE, pws->b[10], pwt->b[10]);
    pwd->b[11] = msa_sra_df(DF_BYTE, pws->b[11], pwt->b[11]);
    pwd->b[12] = msa_sra_df(DF_BYTE, pws->b[12], pwt->b[12]);
    pwd->b[13] = msa_sra_df(DF_BYTE, pws->b[13], pwt->b[13]);
    pwd->b[14] = msa_sra_df(DF_BYTE, pws->b[14], pwt->b[14]);
    pwd->b[15] = msa_sra_df(DF_BYTE, pws->b[15], pwt->b[15]);
}

void helper_msa_sra_h(CPUMIPSState *env,
                      uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->h[0]  = msa_sra_df(DF_HALF, pws->h[0],  pwt->h[0]);
    pwd->h[1]  = msa_sra_df(DF_HALF, pws->h[1],  pwt->h[1]);
    pwd->h[2]  = msa_sra_df(DF_HALF, pws->h[2],  pwt->h[2]);
    pwd->h[3]  = msa_sra_df(DF_HALF, pws->h[3],  pwt->h[3]);
    pwd->h[4]  = msa_sra_df(DF_HALF, pws->h[4],  pwt->h[4]);
    pwd->h[5]  = msa_sra_df(DF_HALF, pws->h[5],  pwt->h[5]);
    pwd->h[6]  = msa_sra_df(DF_HALF, pws->h[6],  pwt->h[6]);
    pwd->h[7]  = msa_sra_df(DF_HALF, pws->h[7],  pwt->h[7]);
}

void helper_msa_sra_w(CPUMIPSState *env,
                      uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->w[0]  = msa_sra_df(DF_WORD, pws->w[0],  pwt->w[0]);
    pwd->w[1]  = msa_sra_df(DF_WORD, pws->w[1],  pwt->w[1]);
    pwd->w[2]  = msa_sra_df(DF_WORD, pws->w[2],  pwt->w[2]);
    pwd->w[3]  = msa_sra_df(DF_WORD, pws->w[3],  pwt->w[3]);
}

void helper_msa_sra_d(CPUMIPSState *env,
                      uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->d[0]  = msa_sra_df(DF_DOUBLE, pws->d[0],  pwt->d[0]);
    pwd->d[1]  = msa_sra_df(DF_DOUBLE, pws->d[1],  pwt->d[1]);
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

void helper_msa_srar_b(CPUMIPSState *env,
                       uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->b[0]  = msa_srar_df(DF_BYTE, pws->b[0],  pwt->b[0]);
    pwd->b[1]  = msa_srar_df(DF_BYTE, pws->b[1],  pwt->b[1]);
    pwd->b[2]  = msa_srar_df(DF_BYTE, pws->b[2],  pwt->b[2]);
    pwd->b[3]  = msa_srar_df(DF_BYTE, pws->b[3],  pwt->b[3]);
    pwd->b[4]  = msa_srar_df(DF_BYTE, pws->b[4],  pwt->b[4]);
    pwd->b[5]  = msa_srar_df(DF_BYTE, pws->b[5],  pwt->b[5]);
    pwd->b[6]  = msa_srar_df(DF_BYTE, pws->b[6],  pwt->b[6]);
    pwd->b[7]  = msa_srar_df(DF_BYTE, pws->b[7],  pwt->b[7]);
    pwd->b[8]  = msa_srar_df(DF_BYTE, pws->b[8],  pwt->b[8]);
    pwd->b[9]  = msa_srar_df(DF_BYTE, pws->b[9],  pwt->b[9]);
    pwd->b[10] = msa_srar_df(DF_BYTE, pws->b[10], pwt->b[10]);
    pwd->b[11] = msa_srar_df(DF_BYTE, pws->b[11], pwt->b[11]);
    pwd->b[12] = msa_srar_df(DF_BYTE, pws->b[12], pwt->b[12]);
    pwd->b[13] = msa_srar_df(DF_BYTE, pws->b[13], pwt->b[13]);
    pwd->b[14] = msa_srar_df(DF_BYTE, pws->b[14], pwt->b[14]);
    pwd->b[15] = msa_srar_df(DF_BYTE, pws->b[15], pwt->b[15]);
}

void helper_msa_srar_h(CPUMIPSState *env,
                       uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->h[0]  = msa_srar_df(DF_HALF, pws->h[0],  pwt->h[0]);
    pwd->h[1]  = msa_srar_df(DF_HALF, pws->h[1],  pwt->h[1]);
    pwd->h[2]  = msa_srar_df(DF_HALF, pws->h[2],  pwt->h[2]);
    pwd->h[3]  = msa_srar_df(DF_HALF, pws->h[3],  pwt->h[3]);
    pwd->h[4]  = msa_srar_df(DF_HALF, pws->h[4],  pwt->h[4]);
    pwd->h[5]  = msa_srar_df(DF_HALF, pws->h[5],  pwt->h[5]);
    pwd->h[6]  = msa_srar_df(DF_HALF, pws->h[6],  pwt->h[6]);
    pwd->h[7]  = msa_srar_df(DF_HALF, pws->h[7],  pwt->h[7]);
}

void helper_msa_srar_w(CPUMIPSState *env,
                       uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->w[0]  = msa_srar_df(DF_WORD, pws->w[0],  pwt->w[0]);
    pwd->w[1]  = msa_srar_df(DF_WORD, pws->w[1],  pwt->w[1]);
    pwd->w[2]  = msa_srar_df(DF_WORD, pws->w[2],  pwt->w[2]);
    pwd->w[3]  = msa_srar_df(DF_WORD, pws->w[3],  pwt->w[3]);
}

void helper_msa_srar_d(CPUMIPSState *env,
                       uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->d[0]  = msa_srar_df(DF_DOUBLE, pws->d[0],  pwt->d[0]);
    pwd->d[1]  = msa_srar_df(DF_DOUBLE, pws->d[1],  pwt->d[1]);
}


static inline int64_t msa_srl_df(uint32_t df, int64_t arg1, int64_t arg2)
{
    uint64_t u_arg1 = UNSIGNED(arg1, df);
    int32_t b_arg2 = BIT_POSITION(arg2, df);
    return u_arg1 >> b_arg2;
}

void helper_msa_srl_b(CPUMIPSState *env,
                      uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->b[0]  = msa_srl_df(DF_BYTE, pws->b[0],  pwt->b[0]);
    pwd->b[1]  = msa_srl_df(DF_BYTE, pws->b[1],  pwt->b[1]);
    pwd->b[2]  = msa_srl_df(DF_BYTE, pws->b[2],  pwt->b[2]);
    pwd->b[3]  = msa_srl_df(DF_BYTE, pws->b[3],  pwt->b[3]);
    pwd->b[4]  = msa_srl_df(DF_BYTE, pws->b[4],  pwt->b[4]);
    pwd->b[5]  = msa_srl_df(DF_BYTE, pws->b[5],  pwt->b[5]);
    pwd->b[6]  = msa_srl_df(DF_BYTE, pws->b[6],  pwt->b[6]);
    pwd->b[7]  = msa_srl_df(DF_BYTE, pws->b[7],  pwt->b[7]);
    pwd->b[8]  = msa_srl_df(DF_BYTE, pws->b[8],  pwt->b[8]);
    pwd->b[9]  = msa_srl_df(DF_BYTE, pws->b[9],  pwt->b[9]);
    pwd->b[10] = msa_srl_df(DF_BYTE, pws->b[10], pwt->b[10]);
    pwd->b[11] = msa_srl_df(DF_BYTE, pws->b[11], pwt->b[11]);
    pwd->b[12] = msa_srl_df(DF_BYTE, pws->b[12], pwt->b[12]);
    pwd->b[13] = msa_srl_df(DF_BYTE, pws->b[13], pwt->b[13]);
    pwd->b[14] = msa_srl_df(DF_BYTE, pws->b[14], pwt->b[14]);
    pwd->b[15] = msa_srl_df(DF_BYTE, pws->b[15], pwt->b[15]);
}

void helper_msa_srl_h(CPUMIPSState *env,
                      uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->h[0]  = msa_srl_df(DF_HALF, pws->h[0],  pwt->h[0]);
    pwd->h[1]  = msa_srl_df(DF_HALF, pws->h[1],  pwt->h[1]);
    pwd->h[2]  = msa_srl_df(DF_HALF, pws->h[2],  pwt->h[2]);
    pwd->h[3]  = msa_srl_df(DF_HALF, pws->h[3],  pwt->h[3]);
    pwd->h[4]  = msa_srl_df(DF_HALF, pws->h[4],  pwt->h[4]);
    pwd->h[5]  = msa_srl_df(DF_HALF, pws->h[5],  pwt->h[5]);
    pwd->h[6]  = msa_srl_df(DF_HALF, pws->h[6],  pwt->h[6]);
    pwd->h[7]  = msa_srl_df(DF_HALF, pws->h[7],  pwt->h[7]);
}

void helper_msa_srl_w(CPUMIPSState *env,
                      uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->w[0]  = msa_srl_df(DF_WORD, pws->w[0],  pwt->w[0]);
    pwd->w[1]  = msa_srl_df(DF_WORD, pws->w[1],  pwt->w[1]);
    pwd->w[2]  = msa_srl_df(DF_WORD, pws->w[2],  pwt->w[2]);
    pwd->w[3]  = msa_srl_df(DF_WORD, pws->w[3],  pwt->w[3]);
}

void helper_msa_srl_d(CPUMIPSState *env,
                      uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->d[0]  = msa_srl_df(DF_DOUBLE, pws->d[0],  pwt->d[0]);
    pwd->d[1]  = msa_srl_df(DF_DOUBLE, pws->d[1],  pwt->d[1]);
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

void helper_msa_srlr_b(CPUMIPSState *env,
                       uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->b[0]  = msa_srlr_df(DF_BYTE, pws->b[0],  pwt->b[0]);
    pwd->b[1]  = msa_srlr_df(DF_BYTE, pws->b[1],  pwt->b[1]);
    pwd->b[2]  = msa_srlr_df(DF_BYTE, pws->b[2],  pwt->b[2]);
    pwd->b[3]  = msa_srlr_df(DF_BYTE, pws->b[3],  pwt->b[3]);
    pwd->b[4]  = msa_srlr_df(DF_BYTE, pws->b[4],  pwt->b[4]);
    pwd->b[5]  = msa_srlr_df(DF_BYTE, pws->b[5],  pwt->b[5]);
    pwd->b[6]  = msa_srlr_df(DF_BYTE, pws->b[6],  pwt->b[6]);
    pwd->b[7]  = msa_srlr_df(DF_BYTE, pws->b[7],  pwt->b[7]);
    pwd->b[8]  = msa_srlr_df(DF_BYTE, pws->b[8],  pwt->b[8]);
    pwd->b[9]  = msa_srlr_df(DF_BYTE, pws->b[9],  pwt->b[9]);
    pwd->b[10] = msa_srlr_df(DF_BYTE, pws->b[10], pwt->b[10]);
    pwd->b[11] = msa_srlr_df(DF_BYTE, pws->b[11], pwt->b[11]);
    pwd->b[12] = msa_srlr_df(DF_BYTE, pws->b[12], pwt->b[12]);
    pwd->b[13] = msa_srlr_df(DF_BYTE, pws->b[13], pwt->b[13]);
    pwd->b[14] = msa_srlr_df(DF_BYTE, pws->b[14], pwt->b[14]);
    pwd->b[15] = msa_srlr_df(DF_BYTE, pws->b[15], pwt->b[15]);
}

void helper_msa_srlr_h(CPUMIPSState *env,
                       uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->h[0]  = msa_srlr_df(DF_HALF, pws->h[0],  pwt->h[0]);
    pwd->h[1]  = msa_srlr_df(DF_HALF, pws->h[1],  pwt->h[1]);
    pwd->h[2]  = msa_srlr_df(DF_HALF, pws->h[2],  pwt->h[2]);
    pwd->h[3]  = msa_srlr_df(DF_HALF, pws->h[3],  pwt->h[3]);
    pwd->h[4]  = msa_srlr_df(DF_HALF, pws->h[4],  pwt->h[4]);
    pwd->h[5]  = msa_srlr_df(DF_HALF, pws->h[5],  pwt->h[5]);
    pwd->h[6]  = msa_srlr_df(DF_HALF, pws->h[6],  pwt->h[6]);
    pwd->h[7]  = msa_srlr_df(DF_HALF, pws->h[7],  pwt->h[7]);
}

void helper_msa_srlr_w(CPUMIPSState *env,
                       uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->w[0]  = msa_srlr_df(DF_WORD, pws->w[0],  pwt->w[0]);
    pwd->w[1]  = msa_srlr_df(DF_WORD, pws->w[1],  pwt->w[1]);
    pwd->w[2]  = msa_srlr_df(DF_WORD, pws->w[2],  pwt->w[2]);
    pwd->w[3]  = msa_srlr_df(DF_WORD, pws->w[3],  pwt->w[3]);
}

void helper_msa_srlr_d(CPUMIPSState *env,
                       uint32_t wd, uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    pwd->d[0]  = msa_srlr_df(DF_DOUBLE, pws->d[0],  pwt->d[0]);
    pwd->d[1]  = msa_srlr_df(DF_DOUBLE, pws->d[1],  pwt->d[1]);
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

#undef BIT_SELECT
#undef BIT_MOVE_IF_ZERO
#undef BIT_MOVE_IF_NOT_ZERO
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

static inline int64_t msa_sat_s_df(uint32_t df, int64_t arg, uint32_t m)
{
    return arg < M_MIN_INT(m + 1) ? M_MIN_INT(m + 1) :
                                    arg > M_MAX_INT(m + 1) ? M_MAX_INT(m + 1) :
                                                             arg;
}

static inline int64_t msa_sat_u_df(uint32_t df, int64_t arg, uint32_t m)
{
    uint64_t u_arg = UNSIGNED(arg, df);
    return  u_arg < M_MAX_UINT(m + 1) ? u_arg :
                                        M_MAX_UINT(m + 1);
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

static inline int64_t msa_mul_q_df(uint32_t df, int64_t arg1, int64_t arg2)
{
    int64_t q_min = DF_MIN_INT(df);
    int64_t q_max = DF_MAX_INT(df);

    if (arg1 == q_min && arg2 == q_min) {
        return q_max;
    }
    return (arg1 * arg2) >> (DF_BITS(df) - 1);
}

static inline int64_t msa_mulr_q_df(uint32_t df, int64_t arg1, int64_t arg2)
{
    int64_t q_min = DF_MIN_INT(df);
    int64_t q_max = DF_MAX_INT(df);
    int64_t r_bit = 1 << (DF_BITS(df) - 2);

    if (arg1 == q_min && arg2 == q_min) {
        return q_max;
    }
    return (arg1 * arg2 + r_bit) >> (DF_BITS(df) - 1);
}

#define MSA_BINOP_DF(func) \
void helper_msa_ ## func ## _df(CPUMIPSState *env, uint32_t df,         \
                                uint32_t wd, uint32_t ws, uint32_t wt)  \
{                                                                       \
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);                          \
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);                          \
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);                          \
                                                                        \
    switch (df) {                                                       \
    case DF_BYTE:                                                       \
        pwd->b[0]  = msa_ ## func ## _df(df, pws->b[0],  pwt->b[0]);    \
        pwd->b[1]  = msa_ ## func ## _df(df, pws->b[1],  pwt->b[1]);    \
        pwd->b[2]  = msa_ ## func ## _df(df, pws->b[2],  pwt->b[2]);    \
        pwd->b[3]  = msa_ ## func ## _df(df, pws->b[3],  pwt->b[3]);    \
        pwd->b[4]  = msa_ ## func ## _df(df, pws->b[4],  pwt->b[4]);    \
        pwd->b[5]  = msa_ ## func ## _df(df, pws->b[5],  pwt->b[5]);    \
        pwd->b[6]  = msa_ ## func ## _df(df, pws->b[6],  pwt->b[6]);    \
        pwd->b[7]  = msa_ ## func ## _df(df, pws->b[7],  pwt->b[7]);    \
        pwd->b[8]  = msa_ ## func ## _df(df, pws->b[8],  pwt->b[8]);    \
        pwd->b[9]  = msa_ ## func ## _df(df, pws->b[9],  pwt->b[9]);    \
        pwd->b[10] = msa_ ## func ## _df(df, pws->b[10], pwt->b[10]);   \
        pwd->b[11] = msa_ ## func ## _df(df, pws->b[11], pwt->b[11]);   \
        pwd->b[12] = msa_ ## func ## _df(df, pws->b[12], pwt->b[12]);   \
        pwd->b[13] = msa_ ## func ## _df(df, pws->b[13], pwt->b[13]);   \
        pwd->b[14] = msa_ ## func ## _df(df, pws->b[14], pwt->b[14]);   \
        pwd->b[15] = msa_ ## func ## _df(df, pws->b[15], pwt->b[15]);   \
        break;                                                          \
    case DF_HALF:                                                       \
        pwd->h[0] = msa_ ## func ## _df(df, pws->h[0], pwt->h[0]);      \
        pwd->h[1] = msa_ ## func ## _df(df, pws->h[1], pwt->h[1]);      \
        pwd->h[2] = msa_ ## func ## _df(df, pws->h[2], pwt->h[2]);      \
        pwd->h[3] = msa_ ## func ## _df(df, pws->h[3], pwt->h[3]);      \
        pwd->h[4] = msa_ ## func ## _df(df, pws->h[4], pwt->h[4]);      \
        pwd->h[5] = msa_ ## func ## _df(df, pws->h[5], pwt->h[5]);      \
        pwd->h[6] = msa_ ## func ## _df(df, pws->h[6], pwt->h[6]);      \
        pwd->h[7] = msa_ ## func ## _df(df, pws->h[7], pwt->h[7]);      \
        break;                                                          \
    case DF_WORD:                                                       \
        pwd->w[0] = msa_ ## func ## _df(df, pws->w[0], pwt->w[0]);      \
        pwd->w[1] = msa_ ## func ## _df(df, pws->w[1], pwt->w[1]);      \
        pwd->w[2] = msa_ ## func ## _df(df, pws->w[2], pwt->w[2]);      \
        pwd->w[3] = msa_ ## func ## _df(df, pws->w[3], pwt->w[3]);      \
        break;                                                          \
    case DF_DOUBLE:                                                     \
        pwd->d[0] = msa_ ## func ## _df(df, pws->d[0], pwt->d[0]);      \
        pwd->d[1] = msa_ ## func ## _df(df, pws->d[1], pwt->d[1]);      \
        break;                                                          \
    default:                                                            \
        assert(0);                                                      \
    }                                                                   \
}

MSA_BINOP_DF(mul_q)
MSA_BINOP_DF(mulr_q)
#undef MSA_BINOP_DF

void helper_msa_sld_df(CPUMIPSState *env, uint32_t df, uint32_t wd,
                       uint32_t ws, uint32_t rt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);

    msa_sld_df(df, pwd, pws, env->active_tc.gpr[rt]);
}

static inline int64_t msa_madd_q_df(uint32_t df, int64_t dest, int64_t arg1,
                                    int64_t arg2)
{
    int64_t q_prod, q_ret;

    int64_t q_max = DF_MAX_INT(df);
    int64_t q_min = DF_MIN_INT(df);

    q_prod = arg1 * arg2;
    q_ret = ((dest << (DF_BITS(df) - 1)) + q_prod) >> (DF_BITS(df) - 1);

    return (q_ret < q_min) ? q_min : (q_max < q_ret) ? q_max : q_ret;
}

static inline int64_t msa_msub_q_df(uint32_t df, int64_t dest, int64_t arg1,
                                    int64_t arg2)
{
    int64_t q_prod, q_ret;

    int64_t q_max = DF_MAX_INT(df);
    int64_t q_min = DF_MIN_INT(df);

    q_prod = arg1 * arg2;
    q_ret = ((dest << (DF_BITS(df) - 1)) - q_prod) >> (DF_BITS(df) - 1);

    return (q_ret < q_min) ? q_min : (q_max < q_ret) ? q_max : q_ret;
}

static inline int64_t msa_maddr_q_df(uint32_t df, int64_t dest, int64_t arg1,
                                     int64_t arg2)
{
    int64_t q_prod, q_ret;

    int64_t q_max = DF_MAX_INT(df);
    int64_t q_min = DF_MIN_INT(df);
    int64_t r_bit = 1 << (DF_BITS(df) - 2);

    q_prod = arg1 * arg2;
    q_ret = ((dest << (DF_BITS(df) - 1)) + q_prod + r_bit) >> (DF_BITS(df) - 1);

    return (q_ret < q_min) ? q_min : (q_max < q_ret) ? q_max : q_ret;
}

static inline int64_t msa_msubr_q_df(uint32_t df, int64_t dest, int64_t arg1,
                                     int64_t arg2)
{
    int64_t q_prod, q_ret;

    int64_t q_max = DF_MAX_INT(df);
    int64_t q_min = DF_MIN_INT(df);
    int64_t r_bit = 1 << (DF_BITS(df) - 2);

    q_prod = arg1 * arg2;
    q_ret = ((dest << (DF_BITS(df) - 1)) - q_prod + r_bit) >> (DF_BITS(df) - 1);

    return (q_ret < q_min) ? q_min : (q_max < q_ret) ? q_max : q_ret;
}

#define MSA_TEROP_DF(func) \
void helper_msa_ ## func ## _df(CPUMIPSState *env, uint32_t df, uint32_t wd,  \
                                uint32_t ws, uint32_t wt)                     \
{                                                                             \
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);                                \
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);                                \
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);                                \
                                                                              \
    switch (df) {                                                             \
    case DF_BYTE:                                                             \
        pwd->b[0]  = msa_ ## func ## _df(df, pwd->b[0],  pws->b[0],           \
                                             pwt->b[0]);                      \
        pwd->b[1]  = msa_ ## func ## _df(df, pwd->b[1],  pws->b[1],           \
                                             pwt->b[1]);                      \
        pwd->b[2]  = msa_ ## func ## _df(df, pwd->b[2],  pws->b[2],           \
                                             pwt->b[2]);                      \
        pwd->b[3]  = msa_ ## func ## _df(df, pwd->b[3],  pws->b[3],           \
                                             pwt->b[3]);                      \
        pwd->b[4]  = msa_ ## func ## _df(df, pwd->b[4],  pws->b[4],           \
                                             pwt->b[4]);                      \
        pwd->b[5]  = msa_ ## func ## _df(df, pwd->b[5],  pws->b[5],           \
                                             pwt->b[5]);                      \
        pwd->b[6]  = msa_ ## func ## _df(df, pwd->b[6],  pws->b[6],           \
                                             pwt->b[6]);                      \
        pwd->b[7]  = msa_ ## func ## _df(df, pwd->b[7],  pws->b[7],           \
                                             pwt->b[7]);                      \
        pwd->b[8]  = msa_ ## func ## _df(df, pwd->b[8],  pws->b[8],           \
                                             pwt->b[8]);                      \
        pwd->b[9]  = msa_ ## func ## _df(df, pwd->b[9],  pws->b[9],           \
                                             pwt->b[9]);                      \
        pwd->b[10] = msa_ ## func ## _df(df, pwd->b[10], pws->b[10],          \
                                             pwt->b[10]);                     \
        pwd->b[11] = msa_ ## func ## _df(df, pwd->b[11], pws->b[11],          \
                                             pwt->b[11]);                     \
        pwd->b[12] = msa_ ## func ## _df(df, pwd->b[12], pws->b[12],          \
                                             pwt->b[12]);                     \
        pwd->b[13] = msa_ ## func ## _df(df, pwd->b[13], pws->b[13],          \
                                             pwt->b[13]);                     \
        pwd->b[14] = msa_ ## func ## _df(df, pwd->b[14], pws->b[14],          \
                                             pwt->b[14]);                     \
        pwd->b[15] = msa_ ## func ## _df(df, pwd->b[15], pws->b[15],          \
                                             pwt->b[15]);                     \
        break;                                                                \
    case DF_HALF:                                                             \
        pwd->h[0] = msa_ ## func ## _df(df, pwd->h[0], pws->h[0], pwt->h[0]); \
        pwd->h[1] = msa_ ## func ## _df(df, pwd->h[1], pws->h[1], pwt->h[1]); \
        pwd->h[2] = msa_ ## func ## _df(df, pwd->h[2], pws->h[2], pwt->h[2]); \
        pwd->h[3] = msa_ ## func ## _df(df, pwd->h[3], pws->h[3], pwt->h[3]); \
        pwd->h[4] = msa_ ## func ## _df(df, pwd->h[4], pws->h[4], pwt->h[4]); \
        pwd->h[5] = msa_ ## func ## _df(df, pwd->h[5], pws->h[5], pwt->h[5]); \
        pwd->h[6] = msa_ ## func ## _df(df, pwd->h[6], pws->h[6], pwt->h[6]); \
        pwd->h[7] = msa_ ## func ## _df(df, pwd->h[7], pws->h[7], pwt->h[7]); \
        break;                                                                \
    case DF_WORD:                                                             \
        pwd->w[0] = msa_ ## func ## _df(df, pwd->w[0], pws->w[0], pwt->w[0]); \
        pwd->w[1] = msa_ ## func ## _df(df, pwd->w[1], pws->w[1], pwt->w[1]); \
        pwd->w[2] = msa_ ## func ## _df(df, pwd->w[2], pws->w[2], pwt->w[2]); \
        pwd->w[3] = msa_ ## func ## _df(df, pwd->w[3], pws->w[3], pwt->w[3]); \
        break;                                                                \
    case DF_DOUBLE:                                                           \
        pwd->d[0] = msa_ ## func ## _df(df, pwd->d[0], pws->d[0], pwt->d[0]); \
        pwd->d[1] = msa_ ## func ## _df(df, pwd->d[1], pws->d[1], pwt->d[1]); \
        break;                                                                \
    default:                                                                  \
        assert(0);                                                            \
    }                                                                         \
}

MSA_TEROP_DF(binsl)
MSA_TEROP_DF(binsr)
MSA_TEROP_DF(madd_q)
MSA_TEROP_DF(msub_q)
MSA_TEROP_DF(maddr_q)
MSA_TEROP_DF(msubr_q)
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
    do { \
        for (i = 0; i < (MSA_LOOP_COND_ ## DF) ; i++) { \
            MSA_DO_ ## DF; \
        } \
    } while (0)

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
        MSA_LOOP_B;                                                 \
        break;                                                      \
    case DF_HALF:                                                   \
        MSA_LOOP_H;                                                 \
        break;                                                      \
    case DF_WORD:                                                   \
        MSA_LOOP_W;                                                 \
        break;                                                      \
    case DF_DOUBLE:                                                 \
        MSA_LOOP_D;                                                 \
        break;                                                      \
    default:                                                        \
        assert(0);                                                  \
    }                                                               \
    msa_move_v(pwd, pwx);                                           \
}

#define MSA_LOOP_COND(DF) \
            (DF_ELEMENTS(DF) / 2)

#define Rb(pwr, i) (pwr->b[i])
#define Lb(pwr, i) (pwr->b[i + DF_ELEMENTS(DF_BYTE) / 2])
#define Rh(pwr, i) (pwr->h[i])
#define Lh(pwr, i) (pwr->h[i + DF_ELEMENTS(DF_HALF) / 2])
#define Rw(pwr, i) (pwr->w[i])
#define Lw(pwr, i) (pwr->w[i + DF_ELEMENTS(DF_WORD) / 2])
#define Rd(pwr, i) (pwr->d[i])
#define Ld(pwr, i) (pwr->d[i + DF_ELEMENTS(DF_DOUBLE) / 2])

#undef MSA_LOOP_COND

#define MSA_LOOP_COND(DF) \
            (DF_ELEMENTS(DF))

#define MSA_DO(DF)                                                          \
    do {                                                                    \
        uint32_t n = DF_ELEMENTS(df);                                       \
        uint32_t k = (pwd->DF[i] & 0x3f) % (2 * n);                         \
        pwx->DF[i] =                                                        \
            (pwd->DF[i] & 0xc0) ? 0 : k < n ? pwt->DF[k] : pws->DF[k - n];  \
    } while (0)
MSA_FN_DF(vshf_df)
#undef MSA_DO
#undef MSA_LOOP_COND
#undef MSA_FN_DF


void helper_msa_sldi_df(CPUMIPSState *env, uint32_t df, uint32_t wd,
                        uint32_t ws, uint32_t n)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);

    msa_sld_df(df, pwd, pws, n);
}

void helper_msa_splati_df(CPUMIPSState *env, uint32_t df, uint32_t wd,
                          uint32_t ws, uint32_t n)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);

    msa_splat_df(df, pwd, pws, n);
}

void helper_msa_copy_s_b(CPUMIPSState *env, uint32_t rd,
                         uint32_t ws, uint32_t n)
{
    n %= 16;
#if HOST_BIG_ENDIAN
    if (n < 8) {
        n = 8 - n - 1;
    } else {
        n = 24 - n - 1;
    }
#endif
    env->active_tc.gpr[rd] = (int8_t)env->active_fpu.fpr[ws].wr.b[n];
}

void helper_msa_copy_s_h(CPUMIPSState *env, uint32_t rd,
                         uint32_t ws, uint32_t n)
{
    n %= 8;
#if HOST_BIG_ENDIAN
    if (n < 4) {
        n = 4 - n - 1;
    } else {
        n = 12 - n - 1;
    }
#endif
    env->active_tc.gpr[rd] = (int16_t)env->active_fpu.fpr[ws].wr.h[n];
}

void helper_msa_copy_s_w(CPUMIPSState *env, uint32_t rd,
                         uint32_t ws, uint32_t n)
{
    n %= 4;
#if HOST_BIG_ENDIAN
    if (n < 2) {
        n = 2 - n - 1;
    } else {
        n = 6 - n - 1;
    }
#endif
    env->active_tc.gpr[rd] = (int32_t)env->active_fpu.fpr[ws].wr.w[n];
}

void helper_msa_copy_s_d(CPUMIPSState *env, uint32_t rd,
                         uint32_t ws, uint32_t n)
{
    n %= 2;
    env->active_tc.gpr[rd] = (int64_t)env->active_fpu.fpr[ws].wr.d[n];
}

void helper_msa_copy_u_b(CPUMIPSState *env, uint32_t rd,
                         uint32_t ws, uint32_t n)
{
    n %= 16;
#if HOST_BIG_ENDIAN
    if (n < 8) {
        n = 8 - n - 1;
    } else {
        n = 24 - n - 1;
    }
#endif
    env->active_tc.gpr[rd] = (uint8_t)env->active_fpu.fpr[ws].wr.b[n];
}

void helper_msa_copy_u_h(CPUMIPSState *env, uint32_t rd,
                         uint32_t ws, uint32_t n)
{
    n %= 8;
#if HOST_BIG_ENDIAN
    if (n < 4) {
        n = 4 - n - 1;
    } else {
        n = 12 - n - 1;
    }
#endif
    env->active_tc.gpr[rd] = (uint16_t)env->active_fpu.fpr[ws].wr.h[n];
}

void helper_msa_copy_u_w(CPUMIPSState *env, uint32_t rd,
                         uint32_t ws, uint32_t n)
{
    n %= 4;
#if HOST_BIG_ENDIAN
    if (n < 2) {
        n = 2 - n - 1;
    } else {
        n = 6 - n - 1;
    }
#endif
    env->active_tc.gpr[rd] = (uint32_t)env->active_fpu.fpr[ws].wr.w[n];
}

void helper_msa_insert_b(CPUMIPSState *env, uint32_t wd,
                          uint32_t rs_num, uint32_t n)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    target_ulong rs = env->active_tc.gpr[rs_num];
    n %= 16;
#if HOST_BIG_ENDIAN
    if (n < 8) {
        n = 8 - n - 1;
    } else {
        n = 24 - n - 1;
    }
#endif
    pwd->b[n] = (int8_t)rs;
}

void helper_msa_insert_h(CPUMIPSState *env, uint32_t wd,
                          uint32_t rs_num, uint32_t n)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    target_ulong rs = env->active_tc.gpr[rs_num];
    n %= 8;
#if HOST_BIG_ENDIAN
    if (n < 4) {
        n = 4 - n - 1;
    } else {
        n = 12 - n - 1;
    }
#endif
    pwd->h[n] = (int16_t)rs;
}

void helper_msa_insert_w(CPUMIPSState *env, uint32_t wd,
                          uint32_t rs_num, uint32_t n)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    target_ulong rs = env->active_tc.gpr[rs_num];
    n %= 4;
#if HOST_BIG_ENDIAN
    if (n < 2) {
        n = 2 - n - 1;
    } else {
        n = 6 - n - 1;
    }
#endif
    pwd->w[n] = (int32_t)rs;
}

void helper_msa_insert_d(CPUMIPSState *env, uint32_t wd,
                          uint32_t rs_num, uint32_t n)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    target_ulong rs = env->active_tc.gpr[rs_num];
    n %= 2;
    pwd->d[n] = (int64_t)rs;
}

void helper_msa_insve_df(CPUMIPSState *env, uint32_t df, uint32_t wd,
                         uint32_t ws, uint32_t n)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);

    switch (df) {
    case DF_BYTE:
        pwd->b[n] = (int8_t)pws->b[0];
        break;
    case DF_HALF:
        pwd->h[n] = (int16_t)pws->h[0];
        break;
    case DF_WORD:
        pwd->w[n] = (int32_t)pws->w[0];
        break;
    case DF_DOUBLE:
        pwd->d[n] = (int64_t)pws->d[0];
        break;
    default:
        assert(0);
    }
}

void helper_msa_ctcmsa(CPUMIPSState *env, target_ulong elm, uint32_t cd)
{
    switch (cd) {
    case 0:
        break;
    case 1:
        env->active_tc.msacsr = (int32_t)elm & MSACSR_MASK;
        restore_msa_fp_status(env);
        /* check exception */
        if ((GET_FP_ENABLE(env->active_tc.msacsr) | FP_UNIMPLEMENTED)
            & GET_FP_CAUSE(env->active_tc.msacsr)) {
            do_raise_exception(env, EXCP_MSAFPE, GETPC());
        }
        break;
    }
}

target_ulong helper_msa_cfcmsa(CPUMIPSState *env, uint32_t cs)
{
    switch (cs) {
    case 0:
        return env->msair;
    case 1:
        return env->active_tc.msacsr & MSACSR_MASK;
    }
    return 0;
}

void helper_msa_fill_df(CPUMIPSState *env, uint32_t df, uint32_t wd,
                        uint32_t rs)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    uint32_t i;

    switch (df) {
    case DF_BYTE:
        for (i = 0; i < DF_ELEMENTS(DF_BYTE); i++) {
            pwd->b[i] = (int8_t)env->active_tc.gpr[rs];
        }
        break;
    case DF_HALF:
        for (i = 0; i < DF_ELEMENTS(DF_HALF); i++) {
            pwd->h[i] = (int16_t)env->active_tc.gpr[rs];
        }
        break;
    case DF_WORD:
        for (i = 0; i < DF_ELEMENTS(DF_WORD); i++) {
            pwd->w[i] = (int32_t)env->active_tc.gpr[rs];
        }
        break;
    case DF_DOUBLE:
        for (i = 0; i < DF_ELEMENTS(DF_DOUBLE); i++) {
            pwd->d[i] = (int64_t)env->active_tc.gpr[rs];
        }
       break;
    default:
        assert(0);
    }
}


#define FLOAT_ONE32 make_float32(0x3f8 << 20)
#define FLOAT_ONE64 make_float64(0x3ffULL << 52)

#define FLOAT_SNAN16(s) (float16_default_nan(s) ^ 0x0220)
        /* 0x7c20 */
#define FLOAT_SNAN32(s) (float32_default_nan(s) ^ 0x00400020)
        /* 0x7f800020 */
#define FLOAT_SNAN64(s) (float64_default_nan(s) ^ 0x0008000000000020ULL)
        /* 0x7ff0000000000020 */

static inline void clear_msacsr_cause(CPUMIPSState *env)
{
    SET_FP_CAUSE(env->active_tc.msacsr, 0);
}

static inline void check_msacsr_cause(CPUMIPSState *env, uintptr_t retaddr)
{
    if ((GET_FP_CAUSE(env->active_tc.msacsr) &
            (GET_FP_ENABLE(env->active_tc.msacsr) | FP_UNIMPLEMENTED)) == 0) {
        UPDATE_FP_FLAGS(env->active_tc.msacsr,
                GET_FP_CAUSE(env->active_tc.msacsr));
    } else {
        do_raise_exception(env, EXCP_MSAFPE, retaddr);
    }
}

/* Flush-to-zero use cases for update_msacsr() */
#define CLEAR_FS_UNDERFLOW 1
#define CLEAR_IS_INEXACT   2
#define RECIPROCAL_INEXACT 4


static inline int ieee_to_mips_xcpt_msa(int ieee_xcpt)
{
    int mips_xcpt = 0;

    if (ieee_xcpt & float_flag_invalid) {
        mips_xcpt |= FP_INVALID;
    }
    if (ieee_xcpt & float_flag_overflow) {
        mips_xcpt |= FP_OVERFLOW;
    }
    if (ieee_xcpt & float_flag_underflow) {
        mips_xcpt |= FP_UNDERFLOW;
    }
    if (ieee_xcpt & float_flag_divbyzero) {
        mips_xcpt |= FP_DIV0;
    }
    if (ieee_xcpt & float_flag_inexact) {
        mips_xcpt |= FP_INEXACT;
    }

    return mips_xcpt;
}

static inline int update_msacsr(CPUMIPSState *env, int action, int denormal)
{
    int ieee_exception_flags;
    int mips_exception_flags = 0;
    int cause;
    int enable;

    ieee_exception_flags = get_float_exception_flags(
                               &env->active_tc.msa_fp_status);

    /* QEMU softfloat does not signal all underflow cases */
    if (denormal) {
        ieee_exception_flags |= float_flag_underflow;
    }
    if (ieee_exception_flags) {
        mips_exception_flags = ieee_to_mips_xcpt_msa(ieee_exception_flags);
    }
    enable = GET_FP_ENABLE(env->active_tc.msacsr) | FP_UNIMPLEMENTED;

    /* Set Inexact (I) when flushing inputs to zero */
    if ((ieee_exception_flags & float_flag_input_denormal) &&
            (env->active_tc.msacsr & MSACSR_FS_MASK) != 0) {
        if (action & CLEAR_IS_INEXACT) {
            mips_exception_flags &= ~FP_INEXACT;
        } else {
            mips_exception_flags |= FP_INEXACT;
        }
    }

    /* Set Inexact (I) and Underflow (U) when flushing outputs to zero */
    if ((ieee_exception_flags & float_flag_output_denormal) &&
            (env->active_tc.msacsr & MSACSR_FS_MASK) != 0) {
        mips_exception_flags |= FP_INEXACT;
        if (action & CLEAR_FS_UNDERFLOW) {
            mips_exception_flags &= ~FP_UNDERFLOW;
        } else {
            mips_exception_flags |= FP_UNDERFLOW;
        }
    }

    /* Set Inexact (I) when Overflow (O) is not enabled */
    if ((mips_exception_flags & FP_OVERFLOW) != 0 &&
           (enable & FP_OVERFLOW) == 0) {
        mips_exception_flags |= FP_INEXACT;
    }

    /* Clear Exact Underflow when Underflow (U) is not enabled */
    if ((mips_exception_flags & FP_UNDERFLOW) != 0 &&
           (enable & FP_UNDERFLOW) == 0 &&
           (mips_exception_flags & FP_INEXACT) == 0) {
        mips_exception_flags &= ~FP_UNDERFLOW;
    }

    /*
     * Reciprocal operations set only Inexact when valid and not
     * divide by zero
     */
    if ((action & RECIPROCAL_INEXACT) &&
            (mips_exception_flags & (FP_INVALID | FP_DIV0)) == 0) {
        mips_exception_flags = FP_INEXACT;
    }

    cause = mips_exception_flags & enable; /* all current enabled exceptions */

    if (cause == 0) {
        /*
         * No enabled exception, update the MSACSR Cause
         * with all current exceptions
         */
        SET_FP_CAUSE(env->active_tc.msacsr,
            (GET_FP_CAUSE(env->active_tc.msacsr) | mips_exception_flags));
    } else {
        /* Current exceptions are enabled */
        if ((env->active_tc.msacsr & MSACSR_NX_MASK) == 0) {
            /*
             * Exception(s) will trap, update MSACSR Cause
             * with all enabled exceptions
             */
            SET_FP_CAUSE(env->active_tc.msacsr,
                (GET_FP_CAUSE(env->active_tc.msacsr) | mips_exception_flags));
        }
    }

    return mips_exception_flags;
}

static inline int get_enabled_exceptions(const CPUMIPSState *env, int c)
{
    int enable = GET_FP_ENABLE(env->active_tc.msacsr) | FP_UNIMPLEMENTED;
    return c & enable;
}

static inline float16 float16_from_float32(int32_t a, bool ieee,
                                           float_status *status)
{
      float16 f_val;

      f_val = float32_to_float16((float32)a, ieee, status);

      return a < 0 ? (f_val | (1 << 15)) : f_val;
}

static inline float32 float32_from_float64(int64_t a, float_status *status)
{
      float32 f_val;

      f_val = float64_to_float32((float64)a, status);

      return a < 0 ? (f_val | (1 << 31)) : f_val;
}

static inline float32 float32_from_float16(int16_t a, bool ieee,
                                           float_status *status)
{
      float32 f_val;

      f_val = float16_to_float32((float16)a, ieee, status);

      return a < 0 ? (f_val | (1 << 31)) : f_val;
}

static inline float64 float64_from_float32(int32_t a, float_status *status)
{
      float64 f_val;

      f_val = float32_to_float64((float64)a, status);

      return a < 0 ? (f_val | (1ULL << 63)) : f_val;
}

static inline float32 float32_from_q16(int16_t a, float_status *status)
{
    float32 f_val;

    /* conversion as integer and scaling */
    f_val = int32_to_float32(a, status);
    f_val = float32_scalbn(f_val, -15, status);

    return f_val;
}

static inline float64 float64_from_q32(int32_t a, float_status *status)
{
    float64 f_val;

    /* conversion as integer and scaling */
    f_val = int32_to_float64(a, status);
    f_val = float64_scalbn(f_val, -31, status);

    return f_val;
}

static inline int16_t float32_to_q16(float32 a, float_status *status)
{
    int32_t q_val;
    int32_t q_min = 0xffff8000;
    int32_t q_max = 0x00007fff;

    int ieee_ex;

    if (float32_is_any_nan(a)) {
        float_raise(float_flag_invalid, status);
        return 0;
    }

    /* scaling */
    a = float32_scalbn(a, 15, status);

    ieee_ex = get_float_exception_flags(status);
    set_float_exception_flags(ieee_ex & (~float_flag_underflow)
                             , status);

    if (ieee_ex & float_flag_overflow) {
        float_raise(float_flag_inexact, status);
        return (int32_t)a < 0 ? q_min : q_max;
    }

    /* conversion to int */
    q_val = float32_to_int32(a, status);

    ieee_ex = get_float_exception_flags(status);
    set_float_exception_flags(ieee_ex & (~float_flag_underflow)
                             , status);

    if (ieee_ex & float_flag_invalid) {
        set_float_exception_flags(ieee_ex & (~float_flag_invalid)
                               , status);
        float_raise(float_flag_overflow | float_flag_inexact, status);
        return (int32_t)a < 0 ? q_min : q_max;
    }

    if (q_val < q_min) {
        float_raise(float_flag_overflow | float_flag_inexact, status);
        return (int16_t)q_min;
    }

    if (q_max < q_val) {
        float_raise(float_flag_overflow | float_flag_inexact, status);
        return (int16_t)q_max;
    }

    return (int16_t)q_val;
}

static inline int32_t float64_to_q32(float64 a, float_status *status)
{
    int64_t q_val;
    int64_t q_min = 0xffffffff80000000LL;
    int64_t q_max = 0x000000007fffffffLL;

    int ieee_ex;

    if (float64_is_any_nan(a)) {
        float_raise(float_flag_invalid, status);
        return 0;
    }

    /* scaling */
    a = float64_scalbn(a, 31, status);

    ieee_ex = get_float_exception_flags(status);
    set_float_exception_flags(ieee_ex & (~float_flag_underflow)
           , status);

    if (ieee_ex & float_flag_overflow) {
        float_raise(float_flag_inexact, status);
        return (int64_t)a < 0 ? q_min : q_max;
    }

    /* conversion to integer */
    q_val = float64_to_int64(a, status);

    ieee_ex = get_float_exception_flags(status);
    set_float_exception_flags(ieee_ex & (~float_flag_underflow)
           , status);

    if (ieee_ex & float_flag_invalid) {
        set_float_exception_flags(ieee_ex & (~float_flag_invalid)
               , status);
        float_raise(float_flag_overflow | float_flag_inexact, status);
        return (int64_t)a < 0 ? q_min : q_max;
    }

    if (q_val < q_min) {
        float_raise(float_flag_overflow | float_flag_inexact, status);
        return (int32_t)q_min;
    }

    if (q_max < q_val) {
        float_raise(float_flag_overflow | float_flag_inexact, status);
        return (int32_t)q_max;
    }

    return (int32_t)q_val;
}

#define MSA_FLOAT_COND(DEST, OP, ARG1, ARG2, BITS, QUIET)                   \
    do {                                                                    \
        float_status *status = &env->active_tc.msa_fp_status;               \
        int c;                                                              \
        int64_t cond;                                                       \
        set_float_exception_flags(0, status);                               \
        if (!QUIET) {                                                       \
            cond = float ## BITS ## _ ## OP(ARG1, ARG2, status);            \
        } else {                                                            \
            cond = float ## BITS ## _ ## OP ## _quiet(ARG1, ARG2, status);  \
        }                                                                   \
        DEST = cond ? M_MAX_UINT(BITS) : 0;                                 \
        c = update_msacsr(env, CLEAR_IS_INEXACT, 0);                        \
                                                                            \
        if (get_enabled_exceptions(env, c)) {                               \
            DEST = ((FLOAT_SNAN ## BITS(status) >> 6) << 6) | c;            \
        }                                                                   \
    } while (0)

#define MSA_FLOAT_AF(DEST, ARG1, ARG2, BITS, QUIET)                 \
    do {                                                            \
        MSA_FLOAT_COND(DEST, eq, ARG1, ARG2, BITS, QUIET);          \
        if ((DEST & M_MAX_UINT(BITS)) == M_MAX_UINT(BITS)) {        \
            DEST = 0;                                               \
        }                                                           \
    } while (0)

#define MSA_FLOAT_UEQ(DEST, ARG1, ARG2, BITS, QUIET)                \
    do {                                                            \
        MSA_FLOAT_COND(DEST, unordered, ARG1, ARG2, BITS, QUIET);   \
        if (DEST == 0) {                                            \
            MSA_FLOAT_COND(DEST, eq, ARG1, ARG2, BITS, QUIET);      \
        }                                                           \
    } while (0)

#define MSA_FLOAT_NE(DEST, ARG1, ARG2, BITS, QUIET)                 \
    do {                                                            \
        MSA_FLOAT_COND(DEST, lt, ARG1, ARG2, BITS, QUIET);          \
        if (DEST == 0) {                                            \
            MSA_FLOAT_COND(DEST, lt, ARG2, ARG1, BITS, QUIET);      \
        }                                                           \
    } while (0)

#define MSA_FLOAT_UNE(DEST, ARG1, ARG2, BITS, QUIET)                \
    do {                                                            \
        MSA_FLOAT_COND(DEST, unordered, ARG1, ARG2, BITS, QUIET);   \
        if (DEST == 0) {                                            \
            MSA_FLOAT_COND(DEST, lt, ARG1, ARG2, BITS, QUIET);      \
            if (DEST == 0) {                                        \
                MSA_FLOAT_COND(DEST, lt, ARG2, ARG1, BITS, QUIET);  \
            }                                                       \
        }                                                           \
    } while (0)

#define MSA_FLOAT_ULE(DEST, ARG1, ARG2, BITS, QUIET)                \
    do {                                                            \
        MSA_FLOAT_COND(DEST, unordered, ARG1, ARG2, BITS, QUIET);   \
        if (DEST == 0) {                                            \
            MSA_FLOAT_COND(DEST, le, ARG1, ARG2, BITS, QUIET);      \
        }                                                           \
    } while (0)

#define MSA_FLOAT_ULT(DEST, ARG1, ARG2, BITS, QUIET)                \
    do {                                                            \
        MSA_FLOAT_COND(DEST, unordered, ARG1, ARG2, BITS, QUIET);   \
        if (DEST == 0) {                                            \
            MSA_FLOAT_COND(DEST, lt, ARG1, ARG2, BITS, QUIET);      \
        }                                                           \
    } while (0)

#define MSA_FLOAT_OR(DEST, ARG1, ARG2, BITS, QUIET)                 \
    do {                                                            \
        MSA_FLOAT_COND(DEST, le, ARG1, ARG2, BITS, QUIET);          \
        if (DEST == 0) {                                            \
            MSA_FLOAT_COND(DEST, le, ARG2, ARG1, BITS, QUIET);      \
        }                                                           \
    } while (0)

static inline void compare_af(CPUMIPSState *env, wr_t *pwd, wr_t *pws,
                              wr_t *pwt, uint32_t df, int quiet,
                              uintptr_t retaddr)
{
    wr_t wx, *pwx = &wx;
    uint32_t i;

    clear_msacsr_cause(env);

    switch (df) {
    case DF_WORD:
        for (i = 0; i < DF_ELEMENTS(DF_WORD); i++) {
            MSA_FLOAT_AF(pwx->w[i], pws->w[i], pwt->w[i], 32, quiet);
        }
        break;
    case DF_DOUBLE:
        for (i = 0; i < DF_ELEMENTS(DF_DOUBLE); i++) {
            MSA_FLOAT_AF(pwx->d[i], pws->d[i], pwt->d[i], 64, quiet);
        }
        break;
    default:
        assert(0);
    }

    check_msacsr_cause(env, retaddr);

    msa_move_v(pwd, pwx);
}

static inline void compare_un(CPUMIPSState *env, wr_t *pwd, wr_t *pws,
                              wr_t *pwt, uint32_t df, int quiet,
                              uintptr_t retaddr)
{
    wr_t wx, *pwx = &wx;
    uint32_t i;

    clear_msacsr_cause(env);

    switch (df) {
    case DF_WORD:
        for (i = 0; i < DF_ELEMENTS(DF_WORD); i++) {
            MSA_FLOAT_COND(pwx->w[i], unordered, pws->w[i], pwt->w[i], 32,
                    quiet);
        }
        break;
    case DF_DOUBLE:
        for (i = 0; i < DF_ELEMENTS(DF_DOUBLE); i++) {
            MSA_FLOAT_COND(pwx->d[i], unordered, pws->d[i], pwt->d[i], 64,
                    quiet);
        }
        break;
    default:
        assert(0);
    }

    check_msacsr_cause(env, retaddr);

    msa_move_v(pwd, pwx);
}

static inline void compare_eq(CPUMIPSState *env, wr_t *pwd, wr_t *pws,
                              wr_t *pwt, uint32_t df, int quiet,
                              uintptr_t retaddr)
{
    wr_t wx, *pwx = &wx;
    uint32_t i;

    clear_msacsr_cause(env);

    switch (df) {
    case DF_WORD:
        for (i = 0; i < DF_ELEMENTS(DF_WORD); i++) {
            MSA_FLOAT_COND(pwx->w[i], eq, pws->w[i], pwt->w[i], 32, quiet);
        }
        break;
    case DF_DOUBLE:
        for (i = 0; i < DF_ELEMENTS(DF_DOUBLE); i++) {
            MSA_FLOAT_COND(pwx->d[i], eq, pws->d[i], pwt->d[i], 64, quiet);
        }
        break;
    default:
        assert(0);
    }

    check_msacsr_cause(env, retaddr);

    msa_move_v(pwd, pwx);
}

static inline void compare_ueq(CPUMIPSState *env, wr_t *pwd, wr_t *pws,
                               wr_t *pwt, uint32_t df, int quiet,
                               uintptr_t retaddr)
{
    wr_t wx, *pwx = &wx;
    uint32_t i;

    clear_msacsr_cause(env);

    switch (df) {
    case DF_WORD:
        for (i = 0; i < DF_ELEMENTS(DF_WORD); i++) {
            MSA_FLOAT_UEQ(pwx->w[i], pws->w[i], pwt->w[i], 32, quiet);
        }
        break;
    case DF_DOUBLE:
        for (i = 0; i < DF_ELEMENTS(DF_DOUBLE); i++) {
            MSA_FLOAT_UEQ(pwx->d[i], pws->d[i], pwt->d[i], 64, quiet);
        }
        break;
    default:
        assert(0);
    }

    check_msacsr_cause(env, retaddr);

    msa_move_v(pwd, pwx);
}

static inline void compare_lt(CPUMIPSState *env, wr_t *pwd, wr_t *pws,
                              wr_t *pwt, uint32_t df, int quiet,
                              uintptr_t retaddr)
{
    wr_t wx, *pwx = &wx;
    uint32_t i;

    clear_msacsr_cause(env);

    switch (df) {
    case DF_WORD:
        for (i = 0; i < DF_ELEMENTS(DF_WORD); i++) {
            MSA_FLOAT_COND(pwx->w[i], lt, pws->w[i], pwt->w[i], 32, quiet);
        }
        break;
    case DF_DOUBLE:
        for (i = 0; i < DF_ELEMENTS(DF_DOUBLE); i++) {
            MSA_FLOAT_COND(pwx->d[i], lt, pws->d[i], pwt->d[i], 64, quiet);
        }
        break;
    default:
        assert(0);
    }

    check_msacsr_cause(env, retaddr);

    msa_move_v(pwd, pwx);
}

static inline void compare_ult(CPUMIPSState *env, wr_t *pwd, wr_t *pws,
                               wr_t *pwt, uint32_t df, int quiet,
                               uintptr_t retaddr)
{
    wr_t wx, *pwx = &wx;
    uint32_t i;

    clear_msacsr_cause(env);

    switch (df) {
    case DF_WORD:
        for (i = 0; i < DF_ELEMENTS(DF_WORD); i++) {
            MSA_FLOAT_ULT(pwx->w[i], pws->w[i], pwt->w[i], 32, quiet);
        }
        break;
    case DF_DOUBLE:
        for (i = 0; i < DF_ELEMENTS(DF_DOUBLE); i++) {
            MSA_FLOAT_ULT(pwx->d[i], pws->d[i], pwt->d[i], 64, quiet);
        }
        break;
    default:
        assert(0);
    }

    check_msacsr_cause(env, retaddr);

    msa_move_v(pwd, pwx);
}

static inline void compare_le(CPUMIPSState *env, wr_t *pwd, wr_t *pws,
                              wr_t *pwt, uint32_t df, int quiet,
                              uintptr_t retaddr)
{
    wr_t wx, *pwx = &wx;
    uint32_t i;

    clear_msacsr_cause(env);

    switch (df) {
    case DF_WORD:
        for (i = 0; i < DF_ELEMENTS(DF_WORD); i++) {
            MSA_FLOAT_COND(pwx->w[i], le, pws->w[i], pwt->w[i], 32, quiet);
        }
        break;
    case DF_DOUBLE:
        for (i = 0; i < DF_ELEMENTS(DF_DOUBLE); i++) {
            MSA_FLOAT_COND(pwx->d[i], le, pws->d[i], pwt->d[i], 64, quiet);
        }
        break;
    default:
        assert(0);
    }

    check_msacsr_cause(env, retaddr);

    msa_move_v(pwd, pwx);
}

static inline void compare_ule(CPUMIPSState *env, wr_t *pwd, wr_t *pws,
                               wr_t *pwt, uint32_t df, int quiet,
                               uintptr_t retaddr)
{
    wr_t wx, *pwx = &wx;
    uint32_t i;

    clear_msacsr_cause(env);

    switch (df) {
    case DF_WORD:
        for (i = 0; i < DF_ELEMENTS(DF_WORD); i++) {
            MSA_FLOAT_ULE(pwx->w[i], pws->w[i], pwt->w[i], 32, quiet);
        }
        break;
    case DF_DOUBLE:
        for (i = 0; i < DF_ELEMENTS(DF_DOUBLE); i++) {
            MSA_FLOAT_ULE(pwx->d[i], pws->d[i], pwt->d[i], 64, quiet);
        }
        break;
    default:
        assert(0);
    }

    check_msacsr_cause(env, retaddr);

    msa_move_v(pwd, pwx);
}

static inline void compare_or(CPUMIPSState *env, wr_t *pwd, wr_t *pws,
                              wr_t *pwt, uint32_t df, int quiet,
                              uintptr_t retaddr)
{
    wr_t wx, *pwx = &wx;
    uint32_t i;

    clear_msacsr_cause(env);

    switch (df) {
    case DF_WORD:
        for (i = 0; i < DF_ELEMENTS(DF_WORD); i++) {
            MSA_FLOAT_OR(pwx->w[i], pws->w[i], pwt->w[i], 32, quiet);
        }
        break;
    case DF_DOUBLE:
        for (i = 0; i < DF_ELEMENTS(DF_DOUBLE); i++) {
            MSA_FLOAT_OR(pwx->d[i], pws->d[i], pwt->d[i], 64, quiet);
        }
        break;
    default:
        assert(0);
    }

    check_msacsr_cause(env, retaddr);

    msa_move_v(pwd, pwx);
}

static inline void compare_une(CPUMIPSState *env, wr_t *pwd, wr_t *pws,
                               wr_t *pwt, uint32_t df, int quiet,
                               uintptr_t retaddr)
{
    wr_t wx, *pwx = &wx;
    uint32_t i;

    clear_msacsr_cause(env);

    switch (df) {
    case DF_WORD:
        for (i = 0; i < DF_ELEMENTS(DF_WORD); i++) {
            MSA_FLOAT_UNE(pwx->w[i], pws->w[i], pwt->w[i], 32, quiet);
        }
        break;
    case DF_DOUBLE:
        for (i = 0; i < DF_ELEMENTS(DF_DOUBLE); i++) {
            MSA_FLOAT_UNE(pwx->d[i], pws->d[i], pwt->d[i], 64, quiet);
        }
        break;
    default:
        assert(0);
    }

    check_msacsr_cause(env, retaddr);

    msa_move_v(pwd, pwx);
}

static inline void compare_ne(CPUMIPSState *env, wr_t *pwd, wr_t *pws,
                              wr_t *pwt, uint32_t df, int quiet,
                              uintptr_t retaddr)
{
    wr_t wx, *pwx = &wx;
    uint32_t i;

    clear_msacsr_cause(env);

    switch (df) {
    case DF_WORD:
        for (i = 0; i < DF_ELEMENTS(DF_WORD); i++) {
            MSA_FLOAT_NE(pwx->w[i], pws->w[i], pwt->w[i], 32, quiet);
        }
        break;
    case DF_DOUBLE:
        for (i = 0; i < DF_ELEMENTS(DF_DOUBLE); i++) {
            MSA_FLOAT_NE(pwx->d[i], pws->d[i], pwt->d[i], 64, quiet);
        }
        break;
    default:
        assert(0);
    }

    check_msacsr_cause(env, retaddr);

    msa_move_v(pwd, pwx);
}

void helper_msa_fcaf_df(CPUMIPSState *env, uint32_t df, uint32_t wd,
                        uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);
    compare_af(env, pwd, pws, pwt, df, 1, GETPC());
}

void helper_msa_fcun_df(CPUMIPSState *env, uint32_t df, uint32_t wd,
                        uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);
    compare_un(env, pwd, pws, pwt, df, 1, GETPC());
}

void helper_msa_fceq_df(CPUMIPSState *env, uint32_t df, uint32_t wd,
                        uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);
    compare_eq(env, pwd, pws, pwt, df, 1, GETPC());
}

void helper_msa_fcueq_df(CPUMIPSState *env, uint32_t df, uint32_t wd,
                         uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);
    compare_ueq(env, pwd, pws, pwt, df, 1, GETPC());
}

void helper_msa_fclt_df(CPUMIPSState *env, uint32_t df, uint32_t wd,
                        uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);
    compare_lt(env, pwd, pws, pwt, df, 1, GETPC());
}

void helper_msa_fcult_df(CPUMIPSState *env, uint32_t df, uint32_t wd,
                         uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);
    compare_ult(env, pwd, pws, pwt, df, 1, GETPC());
}

void helper_msa_fcle_df(CPUMIPSState *env, uint32_t df, uint32_t wd,
                        uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);
    compare_le(env, pwd, pws, pwt, df, 1, GETPC());
}

void helper_msa_fcule_df(CPUMIPSState *env, uint32_t df, uint32_t wd,
                         uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);
    compare_ule(env, pwd, pws, pwt, df, 1, GETPC());
}

void helper_msa_fsaf_df(CPUMIPSState *env, uint32_t df, uint32_t wd,
                        uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);
    compare_af(env, pwd, pws, pwt, df, 0, GETPC());
}

void helper_msa_fsun_df(CPUMIPSState *env, uint32_t df, uint32_t wd,
                        uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);
    compare_un(env, pwd, pws, pwt, df, 0, GETPC());
}

void helper_msa_fseq_df(CPUMIPSState *env, uint32_t df, uint32_t wd,
                        uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);
    compare_eq(env, pwd, pws, pwt, df, 0, GETPC());
}

void helper_msa_fsueq_df(CPUMIPSState *env, uint32_t df, uint32_t wd,
                         uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);
    compare_ueq(env, pwd, pws, pwt, df, 0, GETPC());
}

void helper_msa_fslt_df(CPUMIPSState *env, uint32_t df, uint32_t wd,
                        uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);
    compare_lt(env, pwd, pws, pwt, df, 0, GETPC());
}

void helper_msa_fsult_df(CPUMIPSState *env, uint32_t df, uint32_t wd,
                         uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);
    compare_ult(env, pwd, pws, pwt, df, 0, GETPC());
}

void helper_msa_fsle_df(CPUMIPSState *env, uint32_t df, uint32_t wd,
                        uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);
    compare_le(env, pwd, pws, pwt, df, 0, GETPC());
}

void helper_msa_fsule_df(CPUMIPSState *env, uint32_t df, uint32_t wd,
                         uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);
    compare_ule(env, pwd, pws, pwt, df, 0, GETPC());
}

void helper_msa_fcor_df(CPUMIPSState *env, uint32_t df, uint32_t wd,
                        uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);
    compare_or(env, pwd, pws, pwt, df, 1, GETPC());
}

void helper_msa_fcune_df(CPUMIPSState *env, uint32_t df, uint32_t wd,
                         uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);
    compare_une(env, pwd, pws, pwt, df, 1, GETPC());
}

void helper_msa_fcne_df(CPUMIPSState *env, uint32_t df, uint32_t wd,
                        uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);
    compare_ne(env, pwd, pws, pwt, df, 1, GETPC());
}

void helper_msa_fsor_df(CPUMIPSState *env, uint32_t df, uint32_t wd,
                        uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);
    compare_or(env, pwd, pws, pwt, df, 0, GETPC());
}

void helper_msa_fsune_df(CPUMIPSState *env, uint32_t df, uint32_t wd,
                         uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);
    compare_une(env, pwd, pws, pwt, df, 0, GETPC());
}

void helper_msa_fsne_df(CPUMIPSState *env, uint32_t df, uint32_t wd,
                        uint32_t ws, uint32_t wt)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);
    compare_ne(env, pwd, pws, pwt, df, 0, GETPC());
}

#define float16_is_zero(ARG) 0
#define float16_is_zero_or_denormal(ARG) 0

#define IS_DENORMAL(ARG, BITS)                      \
    (!float ## BITS ## _is_zero(ARG)                \
    && float ## BITS ## _is_zero_or_denormal(ARG))

#define MSA_FLOAT_BINOP(DEST, OP, ARG1, ARG2, BITS)                         \
    do {                                                                    \
        float_status *status = &env->active_tc.msa_fp_status;               \
        int c;                                                              \
                                                                            \
        set_float_exception_flags(0, status);                               \
        DEST = float ## BITS ## _ ## OP(ARG1, ARG2, status);                \
        c = update_msacsr(env, 0, IS_DENORMAL(DEST, BITS));                 \
                                                                            \
        if (get_enabled_exceptions(env, c)) {                               \
            DEST = ((FLOAT_SNAN ## BITS(status) >> 6) << 6) | c;            \
        }                                                                   \
    } while (0)

void helper_msa_fadd_df(CPUMIPSState *env, uint32_t df, uint32_t wd,
        uint32_t ws, uint32_t wt)
{
    wr_t wx, *pwx = &wx;
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);
    uint32_t i;

    clear_msacsr_cause(env);

    switch (df) {
    case DF_WORD:
        for (i = 0; i < DF_ELEMENTS(DF_WORD); i++) {
            MSA_FLOAT_BINOP(pwx->w[i], add, pws->w[i], pwt->w[i], 32);
        }
        break;
    case DF_DOUBLE:
        for (i = 0; i < DF_ELEMENTS(DF_DOUBLE); i++) {
            MSA_FLOAT_BINOP(pwx->d[i], add, pws->d[i], pwt->d[i], 64);
        }
        break;
    default:
        assert(0);
    }

    check_msacsr_cause(env, GETPC());
    msa_move_v(pwd, pwx);
}

void helper_msa_fsub_df(CPUMIPSState *env, uint32_t df, uint32_t wd,
        uint32_t ws, uint32_t wt)
{
    wr_t wx, *pwx = &wx;
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);
    uint32_t i;

    clear_msacsr_cause(env);

    switch (df) {
    case DF_WORD:
        for (i = 0; i < DF_ELEMENTS(DF_WORD); i++) {
            MSA_FLOAT_BINOP(pwx->w[i], sub, pws->w[i], pwt->w[i], 32);
        }
        break;
    case DF_DOUBLE:
        for (i = 0; i < DF_ELEMENTS(DF_DOUBLE); i++) {
            MSA_FLOAT_BINOP(pwx->d[i], sub, pws->d[i], pwt->d[i], 64);
        }
        break;
    default:
        assert(0);
    }

    check_msacsr_cause(env, GETPC());
    msa_move_v(pwd, pwx);
}

void helper_msa_fmul_df(CPUMIPSState *env, uint32_t df, uint32_t wd,
        uint32_t ws, uint32_t wt)
{
    wr_t wx, *pwx = &wx;
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);
    uint32_t i;

    clear_msacsr_cause(env);

    switch (df) {
    case DF_WORD:
        for (i = 0; i < DF_ELEMENTS(DF_WORD); i++) {
            MSA_FLOAT_BINOP(pwx->w[i], mul, pws->w[i], pwt->w[i], 32);
        }
        break;
    case DF_DOUBLE:
        for (i = 0; i < DF_ELEMENTS(DF_DOUBLE); i++) {
            MSA_FLOAT_BINOP(pwx->d[i], mul, pws->d[i], pwt->d[i], 64);
        }
        break;
    default:
        assert(0);
    }

    check_msacsr_cause(env, GETPC());

    msa_move_v(pwd, pwx);
}

void helper_msa_fdiv_df(CPUMIPSState *env, uint32_t df, uint32_t wd,
        uint32_t ws, uint32_t wt)
{
    wr_t wx, *pwx = &wx;
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);
    uint32_t i;

    clear_msacsr_cause(env);

    switch (df) {
    case DF_WORD:
        for (i = 0; i < DF_ELEMENTS(DF_WORD); i++) {
            MSA_FLOAT_BINOP(pwx->w[i], div, pws->w[i], pwt->w[i], 32);
        }
        break;
    case DF_DOUBLE:
        for (i = 0; i < DF_ELEMENTS(DF_DOUBLE); i++) {
            MSA_FLOAT_BINOP(pwx->d[i], div, pws->d[i], pwt->d[i], 64);
        }
        break;
    default:
        assert(0);
    }

    check_msacsr_cause(env, GETPC());

    msa_move_v(pwd, pwx);
}

#define MSA_FLOAT_MULADD(DEST, ARG1, ARG2, ARG3, NEGATE, BITS)              \
    do {                                                                    \
        float_status *status = &env->active_tc.msa_fp_status;               \
        int c;                                                              \
                                                                            \
        set_float_exception_flags(0, status);                               \
        DEST = float ## BITS ## _muladd(ARG2, ARG3, ARG1, NEGATE, status);  \
        c = update_msacsr(env, 0, IS_DENORMAL(DEST, BITS));                 \
                                                                            \
        if (get_enabled_exceptions(env, c)) {                               \
            DEST = ((FLOAT_SNAN ## BITS(status) >> 6) << 6) | c;            \
        }                                                                   \
    } while (0)

void helper_msa_fmadd_df(CPUMIPSState *env, uint32_t df, uint32_t wd,
        uint32_t ws, uint32_t wt)
{
    wr_t wx, *pwx = &wx;
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);
    uint32_t i;

    clear_msacsr_cause(env);

    switch (df) {
    case DF_WORD:
        for (i = 0; i < DF_ELEMENTS(DF_WORD); i++) {
            MSA_FLOAT_MULADD(pwx->w[i], pwd->w[i],
                           pws->w[i], pwt->w[i], 0, 32);
        }
        break;
    case DF_DOUBLE:
        for (i = 0; i < DF_ELEMENTS(DF_DOUBLE); i++) {
            MSA_FLOAT_MULADD(pwx->d[i], pwd->d[i],
                           pws->d[i], pwt->d[i], 0, 64);
        }
        break;
    default:
        assert(0);
    }

    check_msacsr_cause(env, GETPC());

    msa_move_v(pwd, pwx);
}

void helper_msa_fmsub_df(CPUMIPSState *env, uint32_t df, uint32_t wd,
        uint32_t ws, uint32_t wt)
{
    wr_t wx, *pwx = &wx;
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);
    uint32_t i;

    clear_msacsr_cause(env);

    switch (df) {
    case DF_WORD:
        for (i = 0; i < DF_ELEMENTS(DF_WORD); i++) {
            MSA_FLOAT_MULADD(pwx->w[i], pwd->w[i],
                           pws->w[i], pwt->w[i],
                           float_muladd_negate_product, 32);
      }
      break;
    case DF_DOUBLE:
        for (i = 0; i < DF_ELEMENTS(DF_DOUBLE); i++) {
            MSA_FLOAT_MULADD(pwx->d[i], pwd->d[i],
                           pws->d[i], pwt->d[i],
                           float_muladd_negate_product, 64);
        }
        break;
    default:
        assert(0);
    }

    check_msacsr_cause(env, GETPC());

    msa_move_v(pwd, pwx);
}

void helper_msa_fexp2_df(CPUMIPSState *env, uint32_t df, uint32_t wd,
        uint32_t ws, uint32_t wt)
{
    wr_t wx, *pwx = &wx;
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);
    uint32_t i;

    clear_msacsr_cause(env);

    switch (df) {
    case DF_WORD:
        for (i = 0; i < DF_ELEMENTS(DF_WORD); i++) {
            MSA_FLOAT_BINOP(pwx->w[i], scalbn, pws->w[i],
                            pwt->w[i] >  0x200 ?  0x200 :
                            pwt->w[i] < -0x200 ? -0x200 : pwt->w[i],
                            32);
        }
        break;
    case DF_DOUBLE:
        for (i = 0; i < DF_ELEMENTS(DF_DOUBLE); i++) {
            MSA_FLOAT_BINOP(pwx->d[i], scalbn, pws->d[i],
                            pwt->d[i] >  0x1000 ?  0x1000 :
                            pwt->d[i] < -0x1000 ? -0x1000 : pwt->d[i],
                            64);
        }
        break;
    default:
        assert(0);
    }

    check_msacsr_cause(env, GETPC());

    msa_move_v(pwd, pwx);
}

#define MSA_FLOAT_UNOP(DEST, OP, ARG, BITS)                                 \
    do {                                                                    \
        float_status *status = &env->active_tc.msa_fp_status;               \
        int c;                                                              \
                                                                            \
        set_float_exception_flags(0, status);                               \
        DEST = float ## BITS ## _ ## OP(ARG, status);                       \
        c = update_msacsr(env, 0, IS_DENORMAL(DEST, BITS));                 \
                                                                            \
        if (get_enabled_exceptions(env, c)) {                               \
            DEST = ((FLOAT_SNAN ## BITS(status) >> 6) << 6) | c;            \
        }                                                                   \
    } while (0)

void helper_msa_fexdo_df(CPUMIPSState *env, uint32_t df, uint32_t wd,
                         uint32_t ws, uint32_t wt)
{
    wr_t wx, *pwx = &wx;
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);
    uint32_t i;

    clear_msacsr_cause(env);

    switch (df) {
    case DF_WORD:
        for (i = 0; i < DF_ELEMENTS(DF_WORD); i++) {
            /*
             * Half precision floats come in two formats: standard
             * IEEE and "ARM" format.  The latter gains extra exponent
             * range by omitting the NaN/Inf encodings.
             */
            bool ieee = true;

            MSA_FLOAT_BINOP(Lh(pwx, i), from_float32, pws->w[i], ieee, 16);
            MSA_FLOAT_BINOP(Rh(pwx, i), from_float32, pwt->w[i], ieee, 16);
        }
        break;
    case DF_DOUBLE:
        for (i = 0; i < DF_ELEMENTS(DF_DOUBLE); i++) {
            MSA_FLOAT_UNOP(Lw(pwx, i), from_float64, pws->d[i], 32);
            MSA_FLOAT_UNOP(Rw(pwx, i), from_float64, pwt->d[i], 32);
        }
        break;
    default:
        assert(0);
    }

    check_msacsr_cause(env, GETPC());
    msa_move_v(pwd, pwx);
}

#define MSA_FLOAT_UNOP_XD(DEST, OP, ARG, BITS, XBITS)                       \
    do {                                                                    \
        float_status *status = &env->active_tc.msa_fp_status;               \
        int c;                                                              \
                                                                            \
        set_float_exception_flags(0, status);                               \
        DEST = float ## BITS ## _ ## OP(ARG, status);                       \
        c = update_msacsr(env, CLEAR_FS_UNDERFLOW, 0);                      \
                                                                            \
        if (get_enabled_exceptions(env, c)) {                               \
            DEST = ((FLOAT_SNAN ## XBITS(status) >> 6) << 6) | c;           \
        }                                                                   \
    } while (0)

void helper_msa_ftq_df(CPUMIPSState *env, uint32_t df, uint32_t wd,
                       uint32_t ws, uint32_t wt)
{
    wr_t wx, *pwx = &wx;
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);
    uint32_t i;

    clear_msacsr_cause(env);

    switch (df) {
    case DF_WORD:
        for (i = 0; i < DF_ELEMENTS(DF_WORD); i++) {
            MSA_FLOAT_UNOP_XD(Lh(pwx, i), to_q16, pws->w[i], 32, 16);
            MSA_FLOAT_UNOP_XD(Rh(pwx, i), to_q16, pwt->w[i], 32, 16);
        }
        break;
    case DF_DOUBLE:
        for (i = 0; i < DF_ELEMENTS(DF_DOUBLE); i++) {
            MSA_FLOAT_UNOP_XD(Lw(pwx, i), to_q32, pws->d[i], 64, 32);
            MSA_FLOAT_UNOP_XD(Rw(pwx, i), to_q32, pwt->d[i], 64, 32);
        }
        break;
    default:
        assert(0);
    }

    check_msacsr_cause(env, GETPC());

    msa_move_v(pwd, pwx);
}

#define NUMBER_QNAN_PAIR(ARG1, ARG2, BITS, STATUS)      \
    !float ## BITS ## _is_any_nan(ARG1)                 \
    && float ## BITS ## _is_quiet_nan(ARG2, STATUS)

#define MSA_FLOAT_MAXOP(DEST, OP, ARG1, ARG2, BITS)                         \
    do {                                                                    \
        float_status *status = &env->active_tc.msa_fp_status;               \
        int c;                                                              \
                                                                            \
        set_float_exception_flags(0, status);                               \
        DEST = float ## BITS ## _ ## OP(ARG1, ARG2, status);                \
        c = update_msacsr(env, 0, 0);                                       \
                                                                            \
        if (get_enabled_exceptions(env, c)) {                               \
            DEST = ((FLOAT_SNAN ## BITS(status) >> 6) << 6) | c;            \
        }                                                                   \
    } while (0)

#define FMAXMIN_A(F, G, X, _S, _T, BITS, STATUS)                    \
    do {                                                            \
        uint## BITS ##_t S = _S, T = _T;                            \
        uint## BITS ##_t as, at, xs, xt, xd;                        \
        if (NUMBER_QNAN_PAIR(S, T, BITS, STATUS)) {                 \
            T = S;                                                  \
        }                                                           \
        else if (NUMBER_QNAN_PAIR(T, S, BITS, STATUS)) {            \
            S = T;                                                  \
        }                                                           \
        as = float## BITS ##_abs(S);                                \
        at = float## BITS ##_abs(T);                                \
        MSA_FLOAT_MAXOP(xs, F,  S,  T, BITS);                       \
        MSA_FLOAT_MAXOP(xt, G,  S,  T, BITS);                       \
        MSA_FLOAT_MAXOP(xd, F, as, at, BITS);                       \
        X = (as == at || xd == float## BITS ##_abs(xs)) ? xs : xt;  \
    } while (0)

void helper_msa_fmin_df(CPUMIPSState *env, uint32_t df, uint32_t wd,
        uint32_t ws, uint32_t wt)
{
    float_status *status = &env->active_tc.msa_fp_status;
    wr_t wx, *pwx = &wx;
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    clear_msacsr_cause(env);

    if (df == DF_WORD) {

        if (NUMBER_QNAN_PAIR(pws->w[0], pwt->w[0], 32, status)) {
            MSA_FLOAT_MAXOP(pwx->w[0], min, pws->w[0], pws->w[0], 32);
        } else if (NUMBER_QNAN_PAIR(pwt->w[0], pws->w[0], 32, status)) {
            MSA_FLOAT_MAXOP(pwx->w[0], min, pwt->w[0], pwt->w[0], 32);
        } else {
            MSA_FLOAT_MAXOP(pwx->w[0], min, pws->w[0], pwt->w[0], 32);
        }

        if (NUMBER_QNAN_PAIR(pws->w[1], pwt->w[1], 32, status)) {
            MSA_FLOAT_MAXOP(pwx->w[1], min, pws->w[1], pws->w[1], 32);
        } else if (NUMBER_QNAN_PAIR(pwt->w[1], pws->w[1], 32, status)) {
            MSA_FLOAT_MAXOP(pwx->w[1], min, pwt->w[1], pwt->w[1], 32);
        } else {
            MSA_FLOAT_MAXOP(pwx->w[1], min, pws->w[1], pwt->w[1], 32);
        }

        if (NUMBER_QNAN_PAIR(pws->w[2], pwt->w[2], 32, status)) {
            MSA_FLOAT_MAXOP(pwx->w[2], min, pws->w[2], pws->w[2], 32);
        } else if (NUMBER_QNAN_PAIR(pwt->w[2], pws->w[2], 32, status)) {
            MSA_FLOAT_MAXOP(pwx->w[2], min, pwt->w[2], pwt->w[2], 32);
        } else {
            MSA_FLOAT_MAXOP(pwx->w[2], min, pws->w[2], pwt->w[2], 32);
        }

        if (NUMBER_QNAN_PAIR(pws->w[3], pwt->w[3], 32, status)) {
            MSA_FLOAT_MAXOP(pwx->w[3], min, pws->w[3], pws->w[3], 32);
        } else if (NUMBER_QNAN_PAIR(pwt->w[3], pws->w[3], 32, status)) {
            MSA_FLOAT_MAXOP(pwx->w[3], min, pwt->w[3], pwt->w[3], 32);
        } else {
            MSA_FLOAT_MAXOP(pwx->w[3], min, pws->w[3], pwt->w[3], 32);
        }

    } else if (df == DF_DOUBLE) {

        if (NUMBER_QNAN_PAIR(pws->d[0], pwt->d[0], 64, status)) {
            MSA_FLOAT_MAXOP(pwx->d[0], min, pws->d[0], pws->d[0], 64);
        } else if (NUMBER_QNAN_PAIR(pwt->d[0], pws->d[0], 64, status)) {
            MSA_FLOAT_MAXOP(pwx->d[0], min, pwt->d[0], pwt->d[0], 64);
        } else {
            MSA_FLOAT_MAXOP(pwx->d[0], min, pws->d[0], pwt->d[0], 64);
        }

        if (NUMBER_QNAN_PAIR(pws->d[1], pwt->d[1], 64, status)) {
            MSA_FLOAT_MAXOP(pwx->d[1], min, pws->d[1], pws->d[1], 64);
        } else if (NUMBER_QNAN_PAIR(pwt->d[1], pws->d[1], 64, status)) {
            MSA_FLOAT_MAXOP(pwx->d[1], min, pwt->d[1], pwt->d[1], 64);
        } else {
            MSA_FLOAT_MAXOP(pwx->d[1], min, pws->d[1], pwt->d[1], 64);
        }

    } else {

        assert(0);

    }

    check_msacsr_cause(env, GETPC());

    msa_move_v(pwd, pwx);
}

void helper_msa_fmin_a_df(CPUMIPSState *env, uint32_t df, uint32_t wd,
        uint32_t ws, uint32_t wt)
{
    float_status *status = &env->active_tc.msa_fp_status;
    wr_t wx, *pwx = &wx;
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    clear_msacsr_cause(env);

    if (df == DF_WORD) {
        FMAXMIN_A(min, max, pwx->w[0], pws->w[0], pwt->w[0], 32, status);
        FMAXMIN_A(min, max, pwx->w[1], pws->w[1], pwt->w[1], 32, status);
        FMAXMIN_A(min, max, pwx->w[2], pws->w[2], pwt->w[2], 32, status);
        FMAXMIN_A(min, max, pwx->w[3], pws->w[3], pwt->w[3], 32, status);
    } else if (df == DF_DOUBLE) {
        FMAXMIN_A(min, max, pwx->d[0], pws->d[0], pwt->d[0], 64, status);
        FMAXMIN_A(min, max, pwx->d[1], pws->d[1], pwt->d[1], 64, status);
    } else {
        assert(0);
    }

    check_msacsr_cause(env, GETPC());

    msa_move_v(pwd, pwx);
}

void helper_msa_fmax_df(CPUMIPSState *env, uint32_t df, uint32_t wd,
        uint32_t ws, uint32_t wt)
{
     float_status *status = &env->active_tc.msa_fp_status;
    wr_t wx, *pwx = &wx;
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    clear_msacsr_cause(env);

    if (df == DF_WORD) {

        if (NUMBER_QNAN_PAIR(pws->w[0], pwt->w[0], 32, status)) {
            MSA_FLOAT_MAXOP(pwx->w[0], max, pws->w[0], pws->w[0], 32);
        } else if (NUMBER_QNAN_PAIR(pwt->w[0], pws->w[0], 32, status)) {
            MSA_FLOAT_MAXOP(pwx->w[0], max, pwt->w[0], pwt->w[0], 32);
        } else {
            MSA_FLOAT_MAXOP(pwx->w[0], max, pws->w[0], pwt->w[0], 32);
        }

        if (NUMBER_QNAN_PAIR(pws->w[1], pwt->w[1], 32, status)) {
            MSA_FLOAT_MAXOP(pwx->w[1], max, pws->w[1], pws->w[1], 32);
        } else if (NUMBER_QNAN_PAIR(pwt->w[1], pws->w[1], 32, status)) {
            MSA_FLOAT_MAXOP(pwx->w[1], max, pwt->w[1], pwt->w[1], 32);
        } else {
            MSA_FLOAT_MAXOP(pwx->w[1], max, pws->w[1], pwt->w[1], 32);
        }

        if (NUMBER_QNAN_PAIR(pws->w[2], pwt->w[2], 32, status)) {
            MSA_FLOAT_MAXOP(pwx->w[2], max, pws->w[2], pws->w[2], 32);
        } else if (NUMBER_QNAN_PAIR(pwt->w[2], pws->w[2], 32, status)) {
            MSA_FLOAT_MAXOP(pwx->w[2], max, pwt->w[2], pwt->w[2], 32);
        } else {
            MSA_FLOAT_MAXOP(pwx->w[2], max, pws->w[2], pwt->w[2], 32);
        }

        if (NUMBER_QNAN_PAIR(pws->w[3], pwt->w[3], 32, status)) {
            MSA_FLOAT_MAXOP(pwx->w[3], max, pws->w[3], pws->w[3], 32);
        } else if (NUMBER_QNAN_PAIR(pwt->w[3], pws->w[3], 32, status)) {
            MSA_FLOAT_MAXOP(pwx->w[3], max, pwt->w[3], pwt->w[3], 32);
        } else {
            MSA_FLOAT_MAXOP(pwx->w[3], max, pws->w[3], pwt->w[3], 32);
        }

    } else if (df == DF_DOUBLE) {

        if (NUMBER_QNAN_PAIR(pws->d[0], pwt->d[0], 64, status)) {
            MSA_FLOAT_MAXOP(pwx->d[0], max, pws->d[0], pws->d[0], 64);
        } else if (NUMBER_QNAN_PAIR(pwt->d[0], pws->d[0], 64, status)) {
            MSA_FLOAT_MAXOP(pwx->d[0], max, pwt->d[0], pwt->d[0], 64);
        } else {
            MSA_FLOAT_MAXOP(pwx->d[0], max, pws->d[0], pwt->d[0], 64);
        }

        if (NUMBER_QNAN_PAIR(pws->d[1], pwt->d[1], 64, status)) {
            MSA_FLOAT_MAXOP(pwx->d[1], max, pws->d[1], pws->d[1], 64);
        } else if (NUMBER_QNAN_PAIR(pwt->d[1], pws->d[1], 64, status)) {
            MSA_FLOAT_MAXOP(pwx->d[1], max, pwt->d[1], pwt->d[1], 64);
        } else {
            MSA_FLOAT_MAXOP(pwx->d[1], max, pws->d[1], pwt->d[1], 64);
        }

    } else {

        assert(0);

    }

    check_msacsr_cause(env, GETPC());

    msa_move_v(pwd, pwx);
}

void helper_msa_fmax_a_df(CPUMIPSState *env, uint32_t df, uint32_t wd,
        uint32_t ws, uint32_t wt)
{
    float_status *status = &env->active_tc.msa_fp_status;
    wr_t wx, *pwx = &wx;
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    wr_t *pwt = &(env->active_fpu.fpr[wt].wr);

    clear_msacsr_cause(env);

    if (df == DF_WORD) {
        FMAXMIN_A(max, min, pwx->w[0], pws->w[0], pwt->w[0], 32, status);
        FMAXMIN_A(max, min, pwx->w[1], pws->w[1], pwt->w[1], 32, status);
        FMAXMIN_A(max, min, pwx->w[2], pws->w[2], pwt->w[2], 32, status);
        FMAXMIN_A(max, min, pwx->w[3], pws->w[3], pwt->w[3], 32, status);
    } else if (df == DF_DOUBLE) {
        FMAXMIN_A(max, min, pwx->d[0], pws->d[0], pwt->d[0], 64, status);
        FMAXMIN_A(max, min, pwx->d[1], pws->d[1], pwt->d[1], 64, status);
    } else {
        assert(0);
    }

    check_msacsr_cause(env, GETPC());

    msa_move_v(pwd, pwx);
}

void helper_msa_fclass_df(CPUMIPSState *env, uint32_t df,
        uint32_t wd, uint32_t ws)
{
    float_status *status = &env->active_tc.msa_fp_status;

    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    if (df == DF_WORD) {
        pwd->w[0] = float_class_s(pws->w[0], status);
        pwd->w[1] = float_class_s(pws->w[1], status);
        pwd->w[2] = float_class_s(pws->w[2], status);
        pwd->w[3] = float_class_s(pws->w[3], status);
    } else if (df == DF_DOUBLE) {
        pwd->d[0] = float_class_d(pws->d[0], status);
        pwd->d[1] = float_class_d(pws->d[1], status);
    } else {
        assert(0);
    }
}

#define MSA_FLOAT_UNOP0(DEST, OP, ARG, BITS)                                \
    do {                                                                    \
        float_status *status = &env->active_tc.msa_fp_status;               \
        int c;                                                              \
                                                                            \
        set_float_exception_flags(0, status);                               \
        DEST = float ## BITS ## _ ## OP(ARG, status);                       \
        c = update_msacsr(env, CLEAR_FS_UNDERFLOW, 0);                      \
                                                                            \
        if (get_enabled_exceptions(env, c)) {                               \
            DEST = ((FLOAT_SNAN ## BITS(status) >> 6) << 6) | c;            \
        } else if (float ## BITS ## _is_any_nan(ARG)) {                     \
            DEST = 0;                                                       \
        }                                                                   \
    } while (0)

void helper_msa_ftrunc_s_df(CPUMIPSState *env, uint32_t df, uint32_t wd,
                            uint32_t ws)
{
    wr_t wx, *pwx = &wx;
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    uint32_t i;

    clear_msacsr_cause(env);

    switch (df) {
    case DF_WORD:
        for (i = 0; i < DF_ELEMENTS(DF_WORD); i++) {
            MSA_FLOAT_UNOP0(pwx->w[i], to_int32_round_to_zero, pws->w[i], 32);
        }
        break;
    case DF_DOUBLE:
        for (i = 0; i < DF_ELEMENTS(DF_DOUBLE); i++) {
            MSA_FLOAT_UNOP0(pwx->d[i], to_int64_round_to_zero, pws->d[i], 64);
        }
        break;
    default:
        assert(0);
    }

    check_msacsr_cause(env, GETPC());

    msa_move_v(pwd, pwx);
}

void helper_msa_ftrunc_u_df(CPUMIPSState *env, uint32_t df, uint32_t wd,
                            uint32_t ws)
{
    wr_t wx, *pwx = &wx;
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    uint32_t i;

    clear_msacsr_cause(env);

    switch (df) {
    case DF_WORD:
        for (i = 0; i < DF_ELEMENTS(DF_WORD); i++) {
            MSA_FLOAT_UNOP0(pwx->w[i], to_uint32_round_to_zero, pws->w[i], 32);
        }
        break;
    case DF_DOUBLE:
        for (i = 0; i < DF_ELEMENTS(DF_DOUBLE); i++) {
            MSA_FLOAT_UNOP0(pwx->d[i], to_uint64_round_to_zero, pws->d[i], 64);
        }
        break;
    default:
        assert(0);
    }

    check_msacsr_cause(env, GETPC());

    msa_move_v(pwd, pwx);
}

void helper_msa_fsqrt_df(CPUMIPSState *env, uint32_t df, uint32_t wd,
                         uint32_t ws)
{
    wr_t wx, *pwx = &wx;
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    uint32_t i;

    clear_msacsr_cause(env);

    switch (df) {
    case DF_WORD:
        for (i = 0; i < DF_ELEMENTS(DF_WORD); i++) {
            MSA_FLOAT_UNOP(pwx->w[i], sqrt, pws->w[i], 32);
        }
        break;
    case DF_DOUBLE:
        for (i = 0; i < DF_ELEMENTS(DF_DOUBLE); i++) {
            MSA_FLOAT_UNOP(pwx->d[i], sqrt, pws->d[i], 64);
        }
        break;
    default:
        assert(0);
    }

    check_msacsr_cause(env, GETPC());

    msa_move_v(pwd, pwx);
}

#define MSA_FLOAT_RECIPROCAL(DEST, ARG, BITS)                               \
    do {                                                                    \
        float_status *status = &env->active_tc.msa_fp_status;               \
        int c;                                                              \
                                                                            \
        set_float_exception_flags(0, status);                               \
        DEST = float ## BITS ## _ ## div(FLOAT_ONE ## BITS, ARG, status);   \
        c = update_msacsr(env, float ## BITS ## _is_infinity(ARG) ||        \
                          float ## BITS ## _is_quiet_nan(DEST, status) ?    \
                          0 : RECIPROCAL_INEXACT,                           \
                          IS_DENORMAL(DEST, BITS));                         \
                                                                            \
        if (get_enabled_exceptions(env, c)) {                               \
            DEST = ((FLOAT_SNAN ## BITS(status) >> 6) << 6) | c;            \
        }                                                                   \
    } while (0)

void helper_msa_frsqrt_df(CPUMIPSState *env, uint32_t df, uint32_t wd,
                          uint32_t ws)
{
    wr_t wx, *pwx = &wx;
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    uint32_t i;

    clear_msacsr_cause(env);

    switch (df) {
    case DF_WORD:
        for (i = 0; i < DF_ELEMENTS(DF_WORD); i++) {
            MSA_FLOAT_RECIPROCAL(pwx->w[i], float32_sqrt(pws->w[i],
                    &env->active_tc.msa_fp_status), 32);
        }
        break;
    case DF_DOUBLE:
        for (i = 0; i < DF_ELEMENTS(DF_DOUBLE); i++) {
            MSA_FLOAT_RECIPROCAL(pwx->d[i], float64_sqrt(pws->d[i],
                    &env->active_tc.msa_fp_status), 64);
        }
        break;
    default:
        assert(0);
    }

    check_msacsr_cause(env, GETPC());

    msa_move_v(pwd, pwx);
}

void helper_msa_frcp_df(CPUMIPSState *env, uint32_t df, uint32_t wd,
                        uint32_t ws)
{
    wr_t wx, *pwx = &wx;
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    uint32_t i;

    clear_msacsr_cause(env);

    switch (df) {
    case DF_WORD:
        for (i = 0; i < DF_ELEMENTS(DF_WORD); i++) {
            MSA_FLOAT_RECIPROCAL(pwx->w[i], pws->w[i], 32);
        }
        break;
    case DF_DOUBLE:
        for (i = 0; i < DF_ELEMENTS(DF_DOUBLE); i++) {
            MSA_FLOAT_RECIPROCAL(pwx->d[i], pws->d[i], 64);
        }
        break;
    default:
        assert(0);
    }

    check_msacsr_cause(env, GETPC());

    msa_move_v(pwd, pwx);
}

void helper_msa_frint_df(CPUMIPSState *env, uint32_t df, uint32_t wd,
                         uint32_t ws)
{
    wr_t wx, *pwx = &wx;
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    uint32_t i;

    clear_msacsr_cause(env);

    switch (df) {
    case DF_WORD:
        for (i = 0; i < DF_ELEMENTS(DF_WORD); i++) {
            MSA_FLOAT_UNOP(pwx->w[i], round_to_int, pws->w[i], 32);
        }
        break;
    case DF_DOUBLE:
        for (i = 0; i < DF_ELEMENTS(DF_DOUBLE); i++) {
            MSA_FLOAT_UNOP(pwx->d[i], round_to_int, pws->d[i], 64);
        }
        break;
    default:
        assert(0);
    }

    check_msacsr_cause(env, GETPC());

    msa_move_v(pwd, pwx);
}

#define MSA_FLOAT_LOGB(DEST, ARG, BITS)                                     \
    do {                                                                    \
        float_status *status = &env->active_tc.msa_fp_status;               \
        int c;                                                              \
                                                                            \
        set_float_exception_flags(0, status);                               \
        set_float_rounding_mode(float_round_down, status);                  \
        DEST = float ## BITS ## _ ## log2(ARG, status);                     \
        DEST = float ## BITS ## _ ## round_to_int(DEST, status);            \
        set_float_rounding_mode(ieee_rm[(env->active_tc.msacsr &            \
                                         MSACSR_RM_MASK) >> MSACSR_RM],     \
                                status);                                    \
                                                                            \
        set_float_exception_flags(get_float_exception_flags(status) &       \
                                  (~float_flag_inexact),                    \
                                  status);                                  \
                                                                            \
        c = update_msacsr(env, 0, IS_DENORMAL(DEST, BITS));                 \
                                                                            \
        if (get_enabled_exceptions(env, c)) {                               \
            DEST = ((FLOAT_SNAN ## BITS(status) >> 6) << 6) | c;            \
        }                                                                   \
    } while (0)

void helper_msa_flog2_df(CPUMIPSState *env, uint32_t df, uint32_t wd,
                         uint32_t ws)
{
    wr_t wx, *pwx = &wx;
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    uint32_t i;

    clear_msacsr_cause(env);

    switch (df) {
    case DF_WORD:
        for (i = 0; i < DF_ELEMENTS(DF_WORD); i++) {
            MSA_FLOAT_LOGB(pwx->w[i], pws->w[i], 32);
        }
        break;
    case DF_DOUBLE:
        for (i = 0; i < DF_ELEMENTS(DF_DOUBLE); i++) {
            MSA_FLOAT_LOGB(pwx->d[i], pws->d[i], 64);
        }
        break;
    default:
        assert(0);
    }

    check_msacsr_cause(env, GETPC());

    msa_move_v(pwd, pwx);
}

void helper_msa_fexupl_df(CPUMIPSState *env, uint32_t df, uint32_t wd,
                          uint32_t ws)
{
    wr_t wx, *pwx = &wx;
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    uint32_t i;

    clear_msacsr_cause(env);

    switch (df) {
    case DF_WORD:
        for (i = 0; i < DF_ELEMENTS(DF_WORD); i++) {
            /*
             * Half precision floats come in two formats: standard
             * IEEE and "ARM" format.  The latter gains extra exponent
             * range by omitting the NaN/Inf encodings.
             */
            bool ieee = true;

            MSA_FLOAT_BINOP(pwx->w[i], from_float16, Lh(pws, i), ieee, 32);
        }
        break;
    case DF_DOUBLE:
        for (i = 0; i < DF_ELEMENTS(DF_DOUBLE); i++) {
            MSA_FLOAT_UNOP(pwx->d[i], from_float32, Lw(pws, i), 64);
        }
        break;
    default:
        assert(0);
    }

    check_msacsr_cause(env, GETPC());
    msa_move_v(pwd, pwx);
}

void helper_msa_fexupr_df(CPUMIPSState *env, uint32_t df, uint32_t wd,
                          uint32_t ws)
{
    wr_t wx, *pwx = &wx;
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    uint32_t i;

    clear_msacsr_cause(env);

    switch (df) {
    case DF_WORD:
        for (i = 0; i < DF_ELEMENTS(DF_WORD); i++) {
            /*
             * Half precision floats come in two formats: standard
             * IEEE and "ARM" format.  The latter gains extra exponent
             * range by omitting the NaN/Inf encodings.
             */
            bool ieee = true;

            MSA_FLOAT_BINOP(pwx->w[i], from_float16, Rh(pws, i), ieee, 32);
        }
        break;
    case DF_DOUBLE:
        for (i = 0; i < DF_ELEMENTS(DF_DOUBLE); i++) {
            MSA_FLOAT_UNOP(pwx->d[i], from_float32, Rw(pws, i), 64);
        }
        break;
    default:
        assert(0);
    }

    check_msacsr_cause(env, GETPC());
    msa_move_v(pwd, pwx);
}

void helper_msa_ffql_df(CPUMIPSState *env, uint32_t df, uint32_t wd,
                        uint32_t ws)
{
    wr_t wx, *pwx = &wx;
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    uint32_t i;

    switch (df) {
    case DF_WORD:
        for (i = 0; i < DF_ELEMENTS(DF_WORD); i++) {
            MSA_FLOAT_UNOP(pwx->w[i], from_q16, Lh(pws, i), 32);
        }
        break;
    case DF_DOUBLE:
        for (i = 0; i < DF_ELEMENTS(DF_DOUBLE); i++) {
            MSA_FLOAT_UNOP(pwx->d[i], from_q32, Lw(pws, i), 64);
        }
        break;
    default:
        assert(0);
    }

    msa_move_v(pwd, pwx);
}

void helper_msa_ffqr_df(CPUMIPSState *env, uint32_t df, uint32_t wd,
                        uint32_t ws)
{
    wr_t wx, *pwx = &wx;
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    uint32_t i;

    switch (df) {
    case DF_WORD:
        for (i = 0; i < DF_ELEMENTS(DF_WORD); i++) {
            MSA_FLOAT_UNOP(pwx->w[i], from_q16, Rh(pws, i), 32);
        }
        break;
    case DF_DOUBLE:
        for (i = 0; i < DF_ELEMENTS(DF_DOUBLE); i++) {
            MSA_FLOAT_UNOP(pwx->d[i], from_q32, Rw(pws, i), 64);
        }
        break;
    default:
        assert(0);
    }

    msa_move_v(pwd, pwx);
}

void helper_msa_ftint_s_df(CPUMIPSState *env, uint32_t df, uint32_t wd,
                           uint32_t ws)
{
    wr_t wx, *pwx = &wx;
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    uint32_t i;

    clear_msacsr_cause(env);

    switch (df) {
    case DF_WORD:
        for (i = 0; i < DF_ELEMENTS(DF_WORD); i++) {
            MSA_FLOAT_UNOP0(pwx->w[i], to_int32, pws->w[i], 32);
        }
        break;
    case DF_DOUBLE:
        for (i = 0; i < DF_ELEMENTS(DF_DOUBLE); i++) {
            MSA_FLOAT_UNOP0(pwx->d[i], to_int64, pws->d[i], 64);
        }
        break;
    default:
        assert(0);
    }

    check_msacsr_cause(env, GETPC());

    msa_move_v(pwd, pwx);
}

void helper_msa_ftint_u_df(CPUMIPSState *env, uint32_t df, uint32_t wd,
                           uint32_t ws)
{
    wr_t wx, *pwx = &wx;
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    uint32_t i;

    clear_msacsr_cause(env);

    switch (df) {
    case DF_WORD:
        for (i = 0; i < DF_ELEMENTS(DF_WORD); i++) {
            MSA_FLOAT_UNOP0(pwx->w[i], to_uint32, pws->w[i], 32);
        }
        break;
    case DF_DOUBLE:
        for (i = 0; i < DF_ELEMENTS(DF_DOUBLE); i++) {
            MSA_FLOAT_UNOP0(pwx->d[i], to_uint64, pws->d[i], 64);
        }
        break;
    default:
        assert(0);
    }

    check_msacsr_cause(env, GETPC());

    msa_move_v(pwd, pwx);
}

#define float32_from_int32 int32_to_float32
#define float32_from_uint32 uint32_to_float32

#define float64_from_int64 int64_to_float64
#define float64_from_uint64 uint64_to_float64

void helper_msa_ffint_s_df(CPUMIPSState *env, uint32_t df, uint32_t wd,
                           uint32_t ws)
{
    wr_t wx, *pwx = &wx;
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    uint32_t i;

    clear_msacsr_cause(env);

    switch (df) {
    case DF_WORD:
        for (i = 0; i < DF_ELEMENTS(DF_WORD); i++) {
            MSA_FLOAT_UNOP(pwx->w[i], from_int32, pws->w[i], 32);
        }
        break;
    case DF_DOUBLE:
        for (i = 0; i < DF_ELEMENTS(DF_DOUBLE); i++) {
            MSA_FLOAT_UNOP(pwx->d[i], from_int64, pws->d[i], 64);
        }
        break;
    default:
        assert(0);
    }

    check_msacsr_cause(env, GETPC());

    msa_move_v(pwd, pwx);
}

void helper_msa_ffint_u_df(CPUMIPSState *env, uint32_t df, uint32_t wd,
                           uint32_t ws)
{
    wr_t wx, *pwx = &wx;
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    wr_t *pws = &(env->active_fpu.fpr[ws].wr);
    uint32_t i;

    clear_msacsr_cause(env);

    switch (df) {
    case DF_WORD:
        for (i = 0; i < DF_ELEMENTS(DF_WORD); i++) {
            MSA_FLOAT_UNOP(pwx->w[i], from_uint32, pws->w[i], 32);
        }
        break;
    case DF_DOUBLE:
        for (i = 0; i < DF_ELEMENTS(DF_DOUBLE); i++) {
            MSA_FLOAT_UNOP(pwx->d[i], from_uint64, pws->d[i], 64);
        }
        break;
    default:
        assert(0);
    }

    check_msacsr_cause(env, GETPC());

    msa_move_v(pwd, pwx);
}

/* Data format min and max values */
#define DF_BITS(df) (1 << ((df) + 3))

/* Element-by-element access macros */
#define DF_ELEMENTS(df) (MSA_WRLEN / DF_BITS(df))

#if !defined(CONFIG_USER_ONLY)
#define MEMOP_IDX(DF)                                                   \
    MemOpIdx oi = make_memop_idx(MO_TE | DF | MO_UNALN,                 \
                                 cpu_mmu_index(env, false));
#else
#define MEMOP_IDX(DF)
#endif

#if TARGET_BIG_ENDIAN
static inline uint64_t bswap16x4(uint64_t x)
{
    uint64_t m = 0x00ff00ff00ff00ffull;
    return ((x & m) << 8) | ((x >> 8) & m);
}

static inline uint64_t bswap32x2(uint64_t x)
{
    return ror64(bswap64(x), 32);
}
#endif

void helper_msa_ld_b(CPUMIPSState *env, uint32_t wd,
                     target_ulong addr)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    uintptr_t ra = GETPC();
    uint64_t d0, d1;

    /* Load 8 bytes at a time.  Vector element ordering makes this LE.  */
    d0 = cpu_ldq_le_data_ra(env, addr + 0, ra);
    d1 = cpu_ldq_le_data_ra(env, addr + 8, ra);
    pwd->d[0] = d0;
    pwd->d[1] = d1;
}

void helper_msa_ld_h(CPUMIPSState *env, uint32_t wd,
                     target_ulong addr)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    uintptr_t ra = GETPC();
    uint64_t d0, d1;

    /*
     * Load 8 bytes at a time.  Use little-endian load, then for
     * big-endian target, we must then swap the four halfwords.
     */
    d0 = cpu_ldq_le_data_ra(env, addr + 0, ra);
    d1 = cpu_ldq_le_data_ra(env, addr + 8, ra);
#if TARGET_BIG_ENDIAN
    d0 = bswap16x4(d0);
    d1 = bswap16x4(d1);
#endif
    pwd->d[0] = d0;
    pwd->d[1] = d1;
}

void helper_msa_ld_w(CPUMIPSState *env, uint32_t wd,
                     target_ulong addr)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    uintptr_t ra = GETPC();
    uint64_t d0, d1;

    /*
     * Load 8 bytes at a time.  Use little-endian load, then for
     * big-endian target, we must then bswap the two words.
     */
    d0 = cpu_ldq_le_data_ra(env, addr + 0, ra);
    d1 = cpu_ldq_le_data_ra(env, addr + 8, ra);
#if TARGET_BIG_ENDIAN
    d0 = bswap32x2(d0);
    d1 = bswap32x2(d1);
#endif
    pwd->d[0] = d0;
    pwd->d[1] = d1;
}

void helper_msa_ld_d(CPUMIPSState *env, uint32_t wd,
                     target_ulong addr)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    uintptr_t ra = GETPC();
    uint64_t d0, d1;

    d0 = cpu_ldq_data_ra(env, addr + 0, ra);
    d1 = cpu_ldq_data_ra(env, addr + 8, ra);
    pwd->d[0] = d0;
    pwd->d[1] = d1;
}

#define MSA_PAGESPAN(x) \
        ((((x) & ~TARGET_PAGE_MASK) + MSA_WRLEN / 8 - 1) >= TARGET_PAGE_SIZE)

static inline void ensure_writable_pages(CPUMIPSState *env,
                                         target_ulong addr,
                                         int mmu_idx,
                                         uintptr_t retaddr)
{
    /* FIXME: Probe the actual accesses (pass and use a size) */
    if (unlikely(MSA_PAGESPAN(addr))) {
        /* first page */
        probe_write(env, addr, 0, mmu_idx, retaddr);
        /* second page */
        addr = (addr & TARGET_PAGE_MASK) + TARGET_PAGE_SIZE;
        probe_write(env, addr, 0, mmu_idx, retaddr);
    }
}

void helper_msa_st_b(CPUMIPSState *env, uint32_t wd,
                     target_ulong addr)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    int mmu_idx = cpu_mmu_index(env, false);
    uintptr_t ra = GETPC();

    ensure_writable_pages(env, addr, mmu_idx, ra);

    /* Store 8 bytes at a time.  Vector element ordering makes this LE.  */
    cpu_stq_le_data_ra(env, addr + 0, pwd->d[0], ra);
    cpu_stq_le_data_ra(env, addr + 8, pwd->d[1], ra);
}

void helper_msa_st_h(CPUMIPSState *env, uint32_t wd,
                     target_ulong addr)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    int mmu_idx = cpu_mmu_index(env, false);
    uintptr_t ra = GETPC();
    uint64_t d0, d1;

    ensure_writable_pages(env, addr, mmu_idx, ra);

    /* Store 8 bytes at a time.  See helper_msa_ld_h. */
    d0 = pwd->d[0];
    d1 = pwd->d[1];
#if TARGET_BIG_ENDIAN
    d0 = bswap16x4(d0);
    d1 = bswap16x4(d1);
#endif
    cpu_stq_le_data_ra(env, addr + 0, d0, ra);
    cpu_stq_le_data_ra(env, addr + 8, d1, ra);
}

void helper_msa_st_w(CPUMIPSState *env, uint32_t wd,
                     target_ulong addr)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    int mmu_idx = cpu_mmu_index(env, false);
    uintptr_t ra = GETPC();
    uint64_t d0, d1;

    ensure_writable_pages(env, addr, mmu_idx, ra);

    /* Store 8 bytes at a time.  See helper_msa_ld_w. */
    d0 = pwd->d[0];
    d1 = pwd->d[1];
#if TARGET_BIG_ENDIAN
    d0 = bswap32x2(d0);
    d1 = bswap32x2(d1);
#endif
    cpu_stq_le_data_ra(env, addr + 0, d0, ra);
    cpu_stq_le_data_ra(env, addr + 8, d1, ra);
}

void helper_msa_st_d(CPUMIPSState *env, uint32_t wd,
                     target_ulong addr)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    int mmu_idx = cpu_mmu_index(env, false);
    uintptr_t ra = GETPC();

    ensure_writable_pages(env, addr, mmu_idx, GETPC());

    cpu_stq_data_ra(env, addr + 0, pwd->d[0], ra);
    cpu_stq_data_ra(env, addr + 8, pwd->d[1], ra);
}

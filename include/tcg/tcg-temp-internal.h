/*
 * TCG internals related to TCG temp allocation
 *
 * Copyright (c) 2008 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef TCG_TEMP_INTERNAL_H
#define TCG_TEMP_INTERNAL_H

/*
 * Allocation and freeing of EBB temps is reserved to TCG internals
 */

void tcg_temp_free_internal(TCGTemp *);

static inline void tcg_temp_free_i32(TCGv_i32 arg)
{
    tcg_temp_free_internal(tcgv_i32_temp(arg));
}

static inline void tcg_temp_free_i64(TCGv_i64 arg)
{
    tcg_temp_free_internal(tcgv_i64_temp(arg));
}

static inline void tcg_temp_free_i128(TCGv_i128 arg)
{
    tcg_temp_free_internal(tcgv_i128_temp(arg));
}

static inline void tcg_temp_free_ptr(TCGv_ptr arg)
{
    tcg_temp_free_internal(tcgv_ptr_temp(arg));
}

static inline void tcg_temp_free_vec(TCGv_vec arg)
{
    tcg_temp_free_internal(tcgv_vec_temp(arg));
}

static inline TCGv_i32 tcg_temp_ebb_new_i32(void)
{
    TCGTemp *t = tcg_temp_new_internal(TCG_TYPE_I32, TEMP_EBB);
    return temp_tcgv_i32(t);
}

static inline TCGv_i64 tcg_temp_ebb_new_i64(void)
{
    TCGTemp *t = tcg_temp_new_internal(TCG_TYPE_I64, TEMP_EBB);
    return temp_tcgv_i64(t);
}

static inline TCGv_i128 tcg_temp_ebb_new_i128(void)
{
    TCGTemp *t = tcg_temp_new_internal(TCG_TYPE_I128, TEMP_EBB);
    return temp_tcgv_i128(t);
}

static inline TCGv_ptr tcg_temp_ebb_new_ptr(void)
{
    TCGTemp *t = tcg_temp_new_internal(TCG_TYPE_PTR, TEMP_EBB);
    return temp_tcgv_ptr(t);
}

#endif /* TCG_TEMP_FREE_H */

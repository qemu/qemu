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

void tcg_temp_free_i32(TCGv_i32 arg);
void tcg_temp_free_i64(TCGv_i64 arg);
void tcg_temp_free_i128(TCGv_i128 arg);
void tcg_temp_free_ptr(TCGv_ptr arg);
void tcg_temp_free_vec(TCGv_vec arg);

TCGv_i32 tcg_temp_ebb_new_i32(void);
TCGv_i64 tcg_temp_ebb_new_i64(void);
TCGv_ptr tcg_temp_ebb_new_ptr(void);
TCGv_i128 tcg_temp_ebb_new_i128(void);

/* Forget all freed EBB temps, so that new allocations produce new temps. */
static inline void tcg_temp_ebb_reset_freed(TCGContext *s)
{
    memset(s->free_temps, 0, sizeof(s->free_temps));
}

#endif /* TCG_TEMP_FREE_H */

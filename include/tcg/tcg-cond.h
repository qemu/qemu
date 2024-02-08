/*
 * Tiny Code Generator for QEMU
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

#ifndef TCG_COND_H
#define TCG_COND_H

/*
 * Conditions.  Note that these are laid out for easy manipulation by
 * the functions below:
 *    bit 0 is used for inverting;
 *    bit 1 is used for conditions that need swapping (signed/unsigned).
 *    bit 2 is used with bit 1 for swapping.
 *    bit 3 is used for unsigned conditions.
 */
typedef enum {
    /* non-signed */
    TCG_COND_NEVER  = 0 | 0 | 0 | 0,
    TCG_COND_ALWAYS = 0 | 0 | 0 | 1,

    /* equality */
    TCG_COND_EQ     = 8 | 0 | 0 | 0,
    TCG_COND_NE     = 8 | 0 | 0 | 1,

    /* "test" i.e. and then compare vs 0 */
    TCG_COND_TSTEQ  = 8 | 4 | 0 | 0,
    TCG_COND_TSTNE  = 8 | 4 | 0 | 1,

    /* signed */
    TCG_COND_LT     = 0 | 0 | 2 | 0,
    TCG_COND_GE     = 0 | 0 | 2 | 1,
    TCG_COND_GT     = 0 | 4 | 2 | 0,
    TCG_COND_LE     = 0 | 4 | 2 | 1,

    /* unsigned */
    TCG_COND_LTU    = 8 | 0 | 2 | 0,
    TCG_COND_GEU    = 8 | 0 | 2 | 1,
    TCG_COND_GTU    = 8 | 4 | 2 | 0,
    TCG_COND_LEU    = 8 | 4 | 2 | 1,
} TCGCond;

/* Invert the sense of the comparison.  */
static inline TCGCond tcg_invert_cond(TCGCond c)
{
    return (TCGCond)(c ^ 1);
}

/* Swap the operands in a comparison.  */
static inline TCGCond tcg_swap_cond(TCGCond c)
{
    return (TCGCond)(c ^ ((c & 2) << 1));
}

/* Must a comparison be considered signed?  */
static inline bool is_signed_cond(TCGCond c)
{
    return (c & (8 | 2)) == 2;
}

/* Must a comparison be considered unsigned?  */
static inline bool is_unsigned_cond(TCGCond c)
{
    return (c & (8 | 2)) == (8 | 2);
}

/* Must a comparison be considered a test?  */
static inline bool is_tst_cond(TCGCond c)
{
    return (c | 1) == TCG_COND_TSTNE;
}

/* Create an "unsigned" version of a "signed" comparison.  */
static inline TCGCond tcg_unsigned_cond(TCGCond c)
{
    return is_signed_cond(c) ? (TCGCond)(c + 8) : c;
}

/* Create a "signed" version of an "unsigned" comparison.  */
static inline TCGCond tcg_signed_cond(TCGCond c)
{
    return is_unsigned_cond(c) ? (TCGCond)(c - 8) : c;
}

/* Create the eq/ne version of a tsteq/tstne comparison.  */
static inline TCGCond tcg_tst_eqne_cond(TCGCond c)
{
    return is_tst_cond(c) ? (TCGCond)(c - 4) : c;
}

/* Create the lt/ge version of a tstne/tsteq comparison of the sign.  */
static inline TCGCond tcg_tst_ltge_cond(TCGCond c)
{
    return is_tst_cond(c) ? (TCGCond)(c ^ 0xf) : c;
}

/*
 * Create a "high" version of a double-word comparison.
 * This removes equality from a LTE or GTE comparison.
 */
static inline TCGCond tcg_high_cond(TCGCond c)
{
    switch (c) {
    case TCG_COND_GE:
    case TCG_COND_LE:
    case TCG_COND_GEU:
    case TCG_COND_LEU:
        return (TCGCond)(c ^ (4 | 1));
    default:
        return c;
    }
}

#endif /* TCG_COND_H */

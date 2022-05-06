/*
 * Internal declarations for Tiny Code Generator for QEMU
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

#ifndef TCG_INTERNAL_H
#define TCG_INTERNAL_H

#define TCG_HIGHWATER 1024

typedef struct TCGHelperInfo {
    void *func;
    const char *name;
    unsigned flags;
    unsigned typemask;
} TCGHelperInfo;

extern TCGContext tcg_init_ctx;
extern TCGContext **tcg_ctxs;
extern unsigned int tcg_cur_ctxs;
extern unsigned int tcg_max_ctxs;

void tcg_region_init(size_t tb_size, int splitwx, unsigned max_cpus);
bool tcg_region_alloc(TCGContext *s);
void tcg_region_initial_alloc(TCGContext *s);
void tcg_region_prologue_set(TCGContext *s);

static inline void *tcg_call_func(TCGOp *op)
{
    return (void *)(uintptr_t)op->args[TCGOP_CALLO(op) + TCGOP_CALLI(op)];
}

static inline const TCGHelperInfo *tcg_call_info(TCGOp *op)
{
    return (void *)(uintptr_t)op->args[TCGOP_CALLO(op) + TCGOP_CALLI(op) + 1];
}

static inline unsigned tcg_call_flags(TCGOp *op)
{
    return tcg_call_info(op)->flags;
}

#endif /* TCG_INTERNAL_H */

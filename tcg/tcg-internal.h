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

#ifdef CONFIG_TCG_INTERPRETER
#include <ffi.h>
#endif

#define TCG_HIGHWATER 1024

/*
 * Describe the calling convention of a given argument type.
 */
typedef enum {
    TCG_CALL_RET_NORMAL,         /* by registers */
    TCG_CALL_RET_BY_REF,         /* for i128, by reference */
    TCG_CALL_RET_BY_VEC,         /* for i128, by vector register */
} TCGCallReturnKind;

typedef enum {
    TCG_CALL_ARG_NORMAL,         /* by registers (continuing onto stack) */
    TCG_CALL_ARG_EVEN,           /* like normal, but skipping odd slots */
    TCG_CALL_ARG_EXTEND,         /* for i32, as a sign/zero-extended i64 */
    TCG_CALL_ARG_EXTEND_U,       /*      ... as a zero-extended i64 */
    TCG_CALL_ARG_EXTEND_S,       /*      ... as a sign-extended i64 */
    TCG_CALL_ARG_BY_REF,         /* for i128, by reference, first */
    TCG_CALL_ARG_BY_REF_N,       /*       ... by reference, subsequent */
} TCGCallArgumentKind;

typedef struct TCGCallArgumentLoc {
    TCGCallArgumentKind kind    : 8;
    unsigned arg_slot           : 8;
    unsigned ref_slot           : 8;
    unsigned arg_idx            : 4;
    unsigned tmp_subindex       : 2;
} TCGCallArgumentLoc;

/* Avoid "unsigned < 0 is always false" Werror, when iarg_regs is empty. */
#define REG_P(L) \
    ((int)(L)->arg_slot < (int)ARRAY_SIZE(tcg_target_call_iarg_regs))

typedef struct TCGHelperInfo {
    void *func;
    const char *name;
#ifdef CONFIG_TCG_INTERPRETER
    ffi_cif *cif;
#endif
    unsigned typemask           : 32;
    unsigned flags              : 8;
    unsigned nr_in              : 8;
    unsigned nr_out             : 8;
    TCGCallReturnKind out_kind  : 8;

    /* Maximum physical arguments are constrained by TCG_TYPE_I128. */
    TCGCallArgumentLoc in[MAX_CALL_IARGS * (128 / TCG_TARGET_REG_BITS)];
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

#if TCG_TARGET_REG_BITS == 32
static inline TCGv_i32 TCGV_LOW(TCGv_i64 t)
{
    return temp_tcgv_i32(tcgv_i64_temp(t) + HOST_BIG_ENDIAN);
}
static inline TCGv_i32 TCGV_HIGH(TCGv_i64 t)
{
    return temp_tcgv_i32(tcgv_i64_temp(t) + !HOST_BIG_ENDIAN);
}
#else
extern TCGv_i32 TCGV_LOW(TCGv_i64) QEMU_ERROR("32-bit code path is reachable");
extern TCGv_i32 TCGV_HIGH(TCGv_i64) QEMU_ERROR("32-bit code path is reachable");
#endif

static inline TCGv_i64 TCGV128_LOW(TCGv_i128 t)
{
    /* For 32-bit, offset by 2, which may then have TCGV_{LOW,HIGH} applied. */
    int o = HOST_BIG_ENDIAN ? 64 / TCG_TARGET_REG_BITS : 0;
    return temp_tcgv_i64(tcgv_i128_temp(t) + o);
}

static inline TCGv_i64 TCGV128_HIGH(TCGv_i128 t)
{
    int o = HOST_BIG_ENDIAN ? 0 : 64 / TCG_TARGET_REG_BITS;
    return temp_tcgv_i64(tcgv_i128_temp(t) + o);
}

#endif /* TCG_INTERNAL_H */

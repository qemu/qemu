/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * LoongArch translation routines.
 *
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 */

#ifndef TARGET_LOONGARCH_TRANSLATE_H
#define TARGET_LOONGARCH_TRANSLATE_H

#include "exec/translator.h"

#define TRANS(NAME, FUNC, ...) \
    static bool trans_##NAME(DisasContext *ctx, arg_##NAME * a) \
    { return FUNC(ctx, a, __VA_ARGS__); }

/*
 * If an operation is being performed on less than TARGET_LONG_BITS,
 * it may require the inputs to be sign- or zero-extended; which will
 * depend on the exact operation being performed.
 */
typedef enum {
    EXT_NONE,
    EXT_SIGN,
    EXT_ZERO,
} DisasExtend;

typedef struct DisasContext {
    DisasContextBase base;
    target_ulong page_start;
    uint32_t opcode;
    uint16_t mem_idx;
    uint16_t plv;
    int vl;   /* Vector length */
    TCGv zero;
} DisasContext;

void generate_exception(DisasContext *ctx, int excp);

extern TCGv cpu_gpr[32], cpu_pc;
extern TCGv_i32 cpu_fscr0;
extern TCGv_i64 cpu_fpr[32];

#endif

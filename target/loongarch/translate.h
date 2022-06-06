/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * LoongArch translation routines.
 *
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 */

#ifndef TARGET_LOONGARCH_TRANSLATE_H
#define TARGET_LOONGARCH_TRANSLATE_H

#include "exec/translator.h"

typedef struct DisasContext {
    DisasContextBase base;
    target_ulong page_start;
    uint32_t opcode;
    int mem_idx;
} DisasContext;

void generate_exception(DisasContext *ctx, int excp);

extern TCGv cpu_gpr[32], cpu_pc;
extern TCGv_i32 cpu_fscr0;
extern TCGv_i64 cpu_fpr[32];

#endif

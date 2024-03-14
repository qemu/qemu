/*
 * TCG Helper Information Structure
 *
 * Copyright (c) 2023 Linaro Ltd
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef TCG_HELPER_INFO_H
#define TCG_HELPER_INFO_H

#ifdef CONFIG_TCG_INTERPRETER
#include <ffi.h>
#endif
#include "tcg-target-reg-bits.h"

#define MAX_CALL_IARGS  7

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

struct TCGHelperInfo {
    void *func;
    const char *name;

    /* Used with g_once_init_enter. */
#ifdef CONFIG_TCG_INTERPRETER
    ffi_cif *cif;
#else
    uintptr_t init;
#endif

    unsigned typemask           : 32;
    unsigned flags              : 8;
    unsigned nr_in              : 8;
    unsigned nr_out             : 8;
    TCGCallReturnKind out_kind  : 8;

    /* Maximum physical arguments are constrained by TCG_TYPE_I128. */
    TCGCallArgumentLoc in[MAX_CALL_IARGS * (128 / TCG_TARGET_REG_BITS)];
};

#endif /* TCG_HELPER_INFO_H */

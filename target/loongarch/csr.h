/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2025 Loongson Technology Corporation Limited
 */

#ifndef TARGET_LOONGARCH_CSR_H
#define TARGET_LOONGARCH_CSR_H

#include "cpu-csr.h"

typedef void (*GenCSRFunc)(void);
enum {
    CSRFL_READONLY = (1 << 0),
    CSRFL_EXITTB   = (1 << 1),
    CSRFL_IO       = (1 << 2),
};

typedef struct {
    int offset;
    int flags;
    GenCSRFunc readfn;
    GenCSRFunc writefn;
} CSRInfo;

#endif /* TARGET_LOONGARCH_CSR_H */

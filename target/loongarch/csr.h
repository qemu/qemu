/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2025 Loongson Technology Corporation Limited
 */

#ifndef TARGET_LOONGARCH_CSR_H
#define TARGET_LOONGARCH_CSR_H

#include "cpu-csr.h"

#define CSR_OFFSET(id)                  offsetof(CPULoongArchState, id)
#define CPU_CSR_OFFSET(id, vm_level)    CSR_OFFSET(id)

typedef void (*GenCSRFunc)(void);
enum {
    CSRFL_READONLY = (1 << 0),
    CSRFL_EXITTB   = (1 << 1),
    CSRFL_IO       = (1 << 2),
    CSRFL_UNUSED   = (1 << 3),
    CSRFL_BASIC    = (1 << 4),
};

typedef struct {
    const char *name;
    int offset;
    int flags;
    GenCSRFunc readfn;
    GenCSRFunc writefn;
} CSRInfo;

CSRInfo *get_csr(unsigned int csr_num);
bool set_csr_flag(unsigned int csr_num, int flag);
static inline unsigned int get_csr_offset(const CSRInfo *csr, int vm_level)
{
    return csr->offset;
}
#endif /* TARGET_LOONGARCH_CSR_H */

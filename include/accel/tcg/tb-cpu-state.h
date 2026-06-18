/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Definition of TCGTBCPUState.
 */

#ifndef EXEC_TB_CPU_STATE_H
#define EXEC_TB_CPU_STATE_H

#ifndef CONFIG_TCG
#error Can only include this header with TCG
#endif

#include "exec/vaddr.h"

typedef struct TCGTBCPUState {
    vaddr pc;
    uint32_t flags;
    uint32_t cflags;
    uint64_t cs_base;
} TCGTBCPUState;

#endif

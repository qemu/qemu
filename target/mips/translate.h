/*
 *  MIPS translation routines.
 *
 *  Copyright (c) 2004-2005 Jocelyn Mayer
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef TARGET_MIPS_TRANSLATE_H
#define TARGET_MIPS_TRANSLATE_H

#include "exec/translator.h"

typedef struct DisasContext {
    DisasContextBase base;
    target_ulong saved_pc;
    target_ulong page_start;
    uint32_t opcode;
    uint64_t insn_flags;
    int32_t CP0_Config1;
    int32_t CP0_Config2;
    int32_t CP0_Config3;
    int32_t CP0_Config5;
    /* Routine used to access memory */
    int mem_idx;
    MemOp default_tcg_memop_mask;
    uint32_t hflags, saved_hflags;
    target_ulong btarget;
    bool ulri;
    int kscrexist;
    bool rxi;
    int ie;
    bool bi;
    bool bp;
    uint64_t PAMask;
    bool mvh;
    bool eva;
    bool sc;
    int CP0_LLAddr_shift;
    bool ps;
    bool vp;
    bool cmgcr;
    bool mrp;
    bool nan2008;
    bool abs2008;
    bool saar;
    bool mi;
    int gi;
} DisasContext;

#endif

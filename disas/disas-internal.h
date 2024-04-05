/*
 * Definitions used internally in the disassembly code
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef DISAS_INTERNAL_H
#define DISAS_INTERNAL_H

#include "disas/dis-asm.h"

typedef struct CPUDebug {
    struct disassemble_info info;
    CPUState *cpu;
} CPUDebug;

void disas_initialize_debug(CPUDebug *s);
void disas_initialize_debug_target(CPUDebug *s, CPUState *cpu);
int disas_gstring_printf(FILE *stream, const char *fmt, ...)
    G_GNUC_PRINTF(2, 3);

int print_insn_od_host(bfd_vma pc, disassemble_info *info);
int print_insn_od_target(bfd_vma pc, disassemble_info *info);

#endif

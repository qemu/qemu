/*
 * TranslationBlock internal declarations (target specific)
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef ACCEL_TCG_TB_INTERNAL_TARGET_H
#define ACCEL_TCG_TB_INTERNAL_TARGET_H

void tb_check_watchpoint(CPUState *cpu, uintptr_t retaddr);

#endif

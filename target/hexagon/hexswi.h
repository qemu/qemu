/*
 * Copyright(c) 2025 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HEXSWI_H
#define HEXSWI_H


#include "cpu.h"

void hexagon_cpu_do_interrupt(CPUState *cpu);
void register_trap_exception(CPUHexagonState *env, int type, int imm,
                             target_ulong PC);

#endif /* HEXSWI_H */

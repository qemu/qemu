/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HEXSWI_H
#define HEXSWI_H


#include "cpu.h"

void hexagon_cpu_do_interrupt(CPUState *cpu);
void register_trap_exception(CPUHexagonState *env, int type, int imm,
                             uint32_t PC);

#endif /* HEXSWI_H */

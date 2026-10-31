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
void hexagon_vmrte(CPUHexagonState *env);

#define HEX_GEVB_HW_ERROR      (HEX_EVENT_PRECISE << 2)
#define HEX_GEVB_HW_TRAP0      (HEX_EVENT_TRAP0 << 2)
#define HEX_GEVB_HW_FPTRAP     (HEX_EVENT_FPTRAP << 2)
#define HEX_GEVB_HW_DEBUG      (HEX_EVENT_DEBUG << 2)

#endif /* HEXSWI_H */

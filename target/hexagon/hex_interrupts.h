/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HEX_INTERRUPTS_H
#define HEX_INTERRUPTS_H

bool hex_check_interrupts(CPUHexagonState *env);
void hex_clear_interrupts(CPUHexagonState *env, uint32_t mask, uint32_t type);
void hex_raise_interrupts(CPUHexagonState *env, uint32_t mask, uint32_t type);
void hex_interrupt_update(CPUHexagonState *env);

#endif

/*
 * gdbstub internals
 *
 * Copyright (c) 2022 Linaro Ltd
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef _INTERNALS_H_
#define _INTERNALS_H_

#include "exec/cpu-common.h"

bool gdb_supports_guest_debug(void);
int gdb_breakpoint_insert(CPUState *cs, int type, vaddr addr, vaddr len);
int gdb_breakpoint_remove(CPUState *cs, int type, vaddr addr, vaddr len);
void gdb_breakpoint_remove_all(CPUState *cs);

#endif /* _INTERNALS_H_ */

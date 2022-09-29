/*
 * gdbstub internals
 *
 * Copyright (c) 2022 Linaro Ltd
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef _INTERNALS_H_
#define _INTERNALS_H_

bool gdb_supports_guest_debug(void);
int gdb_breakpoint_insert(CPUState *cs, int type, hwaddr addr, hwaddr len);
int gdb_breakpoint_remove(CPUState *cs, int type, hwaddr addr, hwaddr len);
void gdb_breakpoint_remove_all(CPUState *cs);

#endif /* _INTERNALS_H_ */

/*
 *  qemu user cpu loop
 *
 *  Copyright (c) 2003-2008 Fabrice Bellard
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef USER_CPU_LOOP_H
#define USER_CPU_LOOP_H

#include "exec/log.h"
#include "special-errno.h"

void target_exception_dump(CPUArchState *env, const char *fmt, int code);
#define EXCP_DUMP(env, fmt, code) \
    target_exception_dump(env, fmt, code)

typedef struct target_pt_regs target_pt_regs;

void target_cpu_copy_regs(CPUArchState *env, target_pt_regs *regs);

#endif

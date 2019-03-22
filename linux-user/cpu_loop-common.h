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

#ifndef CPU_LOOP_COMMON_H
#define CPU_LOOP_COMMON_H

#include "exec/log.h"

#define EXCP_DUMP(env, fmt, ...)                                        \
do {                                                                    \
    CPUState *cs = env_cpu(env);                                        \
    fprintf(stderr, fmt , ## __VA_ARGS__);                              \
    cpu_dump_state(cs, stderr, 0);                                      \
    if (qemu_log_separate()) {                                          \
        qemu_log(fmt, ## __VA_ARGS__);                                  \
        log_cpu_state(cs, 0);                                           \
    }                                                                   \
} while (0)

void target_cpu_copy_regs(CPUArchState *env, struct target_pt_regs *regs);
#endif

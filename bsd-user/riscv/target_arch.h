/*
 * RISC-V specific prototypes
 *
 * Copyright (c) 2019 Mark Corbin <mark.corbin@embecsom.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef TARGET_ARCH_H
#define TARGET_ARCH_H

#include "qemu.h"

void target_cpu_set_tls(CPURISCVState *env, target_ulong newtls);

#endif /* TARGET_ARCH_H */

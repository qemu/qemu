/*
 *  SH4 emulation
 *
 *  Copyright (c) 2005 Samuel Tardieu
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
#ifndef _EXEC_SH4_H
#define _EXEC_SH4_H

#include "config.h"
#include "dyngen-exec.h"

register struct CPUSH4State *env asm(AREG0);

#include "cpu.h"
#include "exec-all.h"

#ifndef CONFIG_USER_ONLY
#include "softmmu_exec.h"
#endif

#endif				/* _EXEC_SH4_H */

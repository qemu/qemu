/*
 *  ARM micro operations
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *  Copyright (c) 2005-2007 CodeSourcery, LLC
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
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include "exec.h"

/* memory access */

#define MEMSUFFIX _raw
#include "op_mem.h"

#if !defined(CONFIG_USER_ONLY)
#define MEMSUFFIX _user
#include "op_mem.h"
#define MEMSUFFIX _kernel
#include "op_mem.h"
#endif

/* iwMMXt support */
#include "op_iwmmxt.c"

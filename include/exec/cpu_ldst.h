/*
 *  Software MMU support
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
 *
 */

/*
 * Generate inline load/store functions for all MMU modes (typically
 * at least _user and _kernel) as well as _data versions, for all data
 * sizes.
 *
 * Used by target op helpers.
 *
 * MMU mode suffixes are defined in target cpu.h.
 */
#ifndef CPU_LDST_H
#define CPU_LDST_H

#if !defined(CONFIG_USER_ONLY)
#include "exec/softmmu_exec.h"
#endif

#endif /* CPU_LDST_H */

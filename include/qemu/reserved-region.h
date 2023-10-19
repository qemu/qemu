/*
 * QEMU ReservedRegion helpers
 *
 * Copyright (c) 2023 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef QEMU_RESERVED_REGION_H
#define QEMU_RESERVED_REGION_H

#include "exec/memory.h"

/*
 * Insert a new region into a sorted list of reserved regions. In case
 * there is overlap with existing regions, the new added region has
 * higher priority and replaces the overlapped segment.
 */
GList *resv_region_list_insert(GList *list, ReservedRegion *reg);

#endif

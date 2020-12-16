/*
 * Copy-on-read filter block driver
 *
 * The filter driver performs Copy-On-Read (COR) operations
 *
 * Copyright (c) 2018-2020 Virtuozzo International GmbH.
 *
 * Author:
 *   Andrey Shinkevich <andrey.shinkevich@virtuozzo.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef BLOCK_COPY_ON_READ
#define BLOCK_COPY_ON_READ

#include "block/block_int.h"

void bdrv_cor_filter_drop(BlockDriverState *cor_filter_bs);

#endif /* BLOCK_COPY_ON_READ */

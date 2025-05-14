/*
 * copy-before-write filter driver
 *
 * The driver performs Copy-Before-Write (CBW) operation: it is injected above
 * some node, and before each write it copies _old_ data to the target node.
 *
 * Copyright (c) 2018-2021 Virtuozzo International GmbH.
 *
 * Author:
 *  Sementsov-Ogievskiy Vladimir <vsementsov@virtuozzo.com>
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

#ifndef COPY_BEFORE_WRITE_H
#define COPY_BEFORE_WRITE_H

#include "block/block_int.h"
#include "block/block-copy.h"

/*
 * Global state (GS) API. These functions run under the BQL.
 *
 * See include/block/block-global-state.h for more information about
 * the GS API.
 */

BlockDriverState *bdrv_cbw_append(BlockDriverState *source,
                                  BlockDriverState *target,
                                  const char *filter_node_name,
                                  bool discard_source,
                                  uint64_t min_cluster_size,
                                  BlockCopyState **bcs,
                                  OnCbwError on_cbw_error,
                                  Error **errp);
void bdrv_cbw_drop(BlockDriverState *bs);

#endif /* COPY_BEFORE_WRITE_H */
